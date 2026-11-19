#pragma once

// Dynamic linking: programs that use shared libraries (DRX2) and the shared libraries (DRL).
// The full description, for writing a loader (the DragonOS kernel's), is other/info/dynamic-linking.md.
//
// DRL (dsc --lib --shared): a library compiled to be loaded once and used by every process.
//   Its image (assembled at 0) is "text": code, string literals, constant arrays, and a template
//   of its writable globals (the "rw" part, [rwStart, rwStart + rwSize)). The text is shared:
//   loaded once, relocated once, mapped read-only into every process that uses the library.
//   Each process gets its own copy of the rw part (from the template); the library's code finds
//   that copy through a per-process table of "biases" (one word per library, indexed by the
//   library's slot): the address of global g is table[slot] + offset(g), where table[slot] =
//   (the process's copy of the rw part) - rwStart. The table's address is read from one system
//   word (LibTable) that the loader / kernel points at the running process's table.
//
// DRX2 (dsc --dynamic): a program that uses shared libraries instead of carrying them: a DRX
//   (image at 0 + relocations) plus the libraries it needs (name + ABI) and its imports. A
//   program is never shared: its imports are resolved for the process it runs in.
//
// Both: big endian, like the machine; strings are offsets into a string table of 0-terminated
// names (offset 0 is the empty string).

#include "DRX.hpp"
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <functional>
#include <algorithm>

namespace dragon
{
	namespace dyn
	{
		inline constexpr uint32_t ProgramMagic = 0x44525832;	// "DRX2"
		inline constexpr uint32_t LibraryMagic = 0x44524C31;	// "DRL1"
		inline constexpr uint32_t ProgramHeaderSize = 0x40;
		inline constexpr uint32_t LibraryHeaderSize = 0x60;
		inline constexpr uint16_t NoLibrary = 0xFFFF;

		// What an import word gets.
		enum class ImportKind : uint16_t
		{
			Absolute = 0,	// the symbol's address: in the exporter's text, or (a writable global) in this process's copy
			Init = 1,		// (programs) the library's initialization routine (its text + init)
			SlotOf = 2,		// (shared code) 4 * the exporter's slot: the offset of its entry in the table
			OffsetOf = 3,	// (shared code) the symbol's offset in the exporter's image (added to its bias)
			SelfSlot = 4,	// (shared code) 4 * this library's own slot
			LibTable = 5,	// (shared code) the address of the system word that holds the running process's table
		};

		inline const char* kindName(ImportKind k)
		{
			switch (k)
			{
				case ImportKind::Absolute: return "absolute";
				case ImportKind::Init: return "init";
				case ImportKind::SlotOf: return "slot-of";
				case ImportKind::OffsetOf: return "offset-of";
				case ImportKind::SelfSlot: return "self-slot";
				case ImportKind::LibTable: return "lib-table";
			}
			return "?";
		}

		struct LibraryRef { std::string name; uint32_t abi { 0 }; };
		// library: the index in the program's libraries / the DRL's dependencies (NoLibrary: none).
		struct Import { uint32_t offset { 0 }; uint16_t library { NoLibrary }; ImportKind kind { ImportKind::Absolute }; std::string symbol; };
		struct Export { std::string symbol; uint32_t offset { 0 }; };

		struct Program
		{
			std::vector<uint8_t> image;
			uint32_t entry { 0 };
			std::vector<uint32_t> relocations;		// words that get the program's base
			uint64_t id { 0 };
			std::vector<LibraryRef> libraries;		// in initialization order (dependencies first)
			std::vector<Import> imports;
		};

		struct Library
		{
			std::string name;
			uint32_t abi { 0 };
			uint64_t id { 0 };
			std::vector<uint8_t> image;
			uint32_t init { 0 };
			uint32_t rwStart { 0 }, rwSize { 0 };
			std::vector<uint32_t> textRelocations;		// words outside rw: + text base (once, shared)
			std::vector<uint32_t> dataTextRelocations;	// words inside rw that point into the text: + text base (per process)
			std::vector<uint32_t> dataDataRelocations;	// words inside rw that point into rw: + bias (per process)
			std::vector<LibraryRef> dependencies;
			std::vector<Import> imports;
			std::vector<Export> exports;				// sorted by name

			inline bool inRw(uint32_t offset) const { return offset >= rwStart && offset < rwStart + rwSize; }
			// The export's offset, or -1.
			inline int64_t find(const std::string& symbol) const
			{
				auto it = std::lower_bound(exports.begin(), exports.end(), symbol, [](const Export& e, const std::string& s) { return e.symbol < s; });
				return (it != exports.end() && it->symbol == symbol) ? (int64_t)it->offset : -1;
			}
		};

		// The ABI of a library: what programs compiled against it depend on (its interface: subs,
		// structs, constants, inline subs). FNV-1a, 32 bits, never 0.
		inline uint32_t abiOf(const std::string& interface)
		{
			uint32_t h = 0x811C9DC5u;
			for (unsigned char c : interface)
			{
				h ^= c;
				h *= 0x01000193u;
			}
			return h == 0 ? 1 : h;
		}

		// The id of a library build (for the debugger), like a DRX's.
		inline uint64_t computeId(const Library& lib)
		{
			drx::Program p;
			p.image = lib.image;
			p.entry = lib.init;
			p.relocations = lib.textRelocations;
			p.relocations.insert(p.relocations.end(), lib.dataTextRelocations.begin(), lib.dataTextRelocations.end());
			p.relocations.insert(p.relocations.end(), lib.dataDataRelocations.begin(), lib.dataDataRelocations.end());
			for (const auto& i : lib.imports)
				p.relocations.push_back(i.offset ^ ((uint32_t)i.kind << 28));
			return drx::computeId(p);
		}

		namespace detail
		{
			using drx::readU32;
			using drx::writeU32;
			inline uint16_t readU16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
			inline void writeU16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

			struct Strings
			{
				std::vector<uint8_t> bytes { 0 };
				std::map<std::string, uint32_t> index;
				uint32_t add(const std::string& s)
				{
					if (s.empty())
						return 0;
					auto it = index.find(s);
					if (it != index.end())
						return it->second;
					const uint32_t at = (uint32_t)bytes.size();
					bytes.insert(bytes.end(), s.begin(), s.end());
					bytes.push_back(0);
					index[s] = at;
					return at;
				}
			};

			struct Out
			{
				std::vector<uint8_t> file;
				explicit Out(uint32_t headerSize) : file(headerSize, 0) {}
				void at(uint32_t pos, uint32_t v) { writeU32(&file[pos], v); }
				uint32_t here(void) const { return (uint32_t)file.size(); }
				void u32(uint32_t v) { file.resize(file.size() + 4); writeU32(&file[file.size() - 4], v); }
				void u16(uint16_t v) { file.resize(file.size() + 2); writeU16(&file[file.size() - 2], v); }
				// A table of words: its count and offset in the header.
				void words(uint32_t countAt, const std::vector<uint32_t>& v) { at(countAt, (uint32_t)v.size()); at(countAt + 4, here()); for (uint32_t w : v) u32(w); }
			};

			// Reads a string of the table (false if it isn't a valid one).
			inline bool string(const std::vector<uint8_t>& file, uint32_t table, uint32_t size, uint32_t offset, std::string& out)
			{
				if (offset >= size)
					return false;
				out.clear();
				for (uint32_t i = table + offset; i < table + size; i++)
				{
					if (file[i] == 0)
						return true;
					out += (char)file[i];
				}
				return false;
			}
		}

		inline bool isProgram(const std::vector<uint8_t>& file) { return file.size() >= ProgramHeaderSize && drx::readU32(file.data()) == ProgramMagic; }
		inline bool isLibrary(const std::vector<uint8_t>& file) { return file.size() >= LibraryHeaderSize && drx::readU32(file.data()) == LibraryMagic; }

		// ============================== DRX2 ==============================
		// 0x00 'DRX2'        0x04 header size (0x40)   0x08 image size   0x0C entry
		// 0x10 reloc count   0x14 reloc offset         0x18 id (hi)      0x1C id (lo)
		// 0x20 library count 0x24 library table        (8 bytes each: name, abi)
		// 0x28 import count  0x2C import table         (12 bytes each: offset, library (u16), kind (u16), symbol)
		// 0x30 strings       0x34 strings size         0x38, 0x3C: 0
		// Then: the image, the relocations, the libraries, the imports, the strings.
		inline std::vector<uint8_t> write(const Program& p)
		{
			detail::Strings strings;
			detail::Out o(ProgramHeaderSize);
			o.at(0x00, ProgramMagic);
			o.at(0x04, ProgramHeaderSize);
			o.at(0x08, (uint32_t)p.image.size());
			o.at(0x0C, p.entry);
			o.at(0x18, (uint32_t)(p.id >> 32));
			o.at(0x1C, (uint32_t)p.id);
			o.file.insert(o.file.end(), p.image.begin(), p.image.end());
			o.words(0x10, p.relocations);
			o.at(0x20, (uint32_t)p.libraries.size());
			o.at(0x24, o.here());
			for (const auto& l : p.libraries) { o.u32(strings.add(l.name)); o.u32(l.abi); }
			o.at(0x28, (uint32_t)p.imports.size());
			o.at(0x2C, o.here());
			for (const auto& i : p.imports) { o.u32(i.offset); o.u16(i.library); o.u16((uint16_t)i.kind); o.u32(strings.add(i.symbol)); }
			o.at(0x30, o.here());
			o.at(0x34, (uint32_t)strings.bytes.size());
			o.file.insert(o.file.end(), strings.bytes.begin(), strings.bytes.end());
			return o.file;
		}

		inline bool read(const std::vector<uint8_t>& file, Program& p, std::string& message)
		{
			using detail::readU32;
			if (!isProgram(file)) { message = "not a DRX2 file"; return false; }
			auto l_fits = [&](uint32_t offset, uint64_t size) { return (uint64_t)offset + size <= file.size(); };
			const uint32_t headerSize = readU32(&file[0x04]), imageSize = readU32(&file[0x08]);
			const uint32_t relocCount = readU32(&file[0x10]), relocAt = readU32(&file[0x14]);
			const uint32_t libCount = readU32(&file[0x20]), libAt = readU32(&file[0x24]);
			const uint32_t impCount = readU32(&file[0x28]), impAt = readU32(&file[0x2C]);
			const uint32_t strAt = readU32(&file[0x30]), strSize = readU32(&file[0x34]);
			if (headerSize < ProgramHeaderSize || !l_fits(headerSize, imageSize) || !l_fits(relocAt, (uint64_t)relocCount * 4)
				|| !l_fits(libAt, (uint64_t)libCount * 8) || !l_fits(impAt, (uint64_t)impCount * 12) || !l_fits(strAt, strSize) || strSize == 0)
			{
				message = "truncated DRX2 file";
				return false;
			}
			p = Program();
			p.image.assign(file.begin() + headerSize, file.begin() + headerSize + imageSize);
			p.entry = readU32(&file[0x0C]);
			p.id = ((uint64_t)readU32(&file[0x18]) << 32) | readU32(&file[0x1C]);
			for (uint32_t i = 0; i < relocCount; i++)
			{
				const uint32_t r = readU32(&file[relocAt + i * 4]);
				if ((uint64_t)r + 4 > imageSize) { message = "relocation outside of the image"; return false; }
				p.relocations.push_back(r);
			}
			for (uint32_t i = 0; i < libCount; i++)
			{
				LibraryRef l;
				if (!detail::string(file, strAt, strSize, readU32(&file[libAt + i * 8]), l.name) || l.name.empty()) { message = "bad library name"; return false; }
				l.abi = readU32(&file[libAt + i * 8 + 4]);
				p.libraries.push_back(l);
			}
			for (uint32_t i = 0; i < impCount; i++)
			{
				const uint8_t* e = &file[impAt + i * 12];
				Import imp;
				imp.offset = readU32(e);
				imp.library = detail::readU16(e + 4);
				imp.kind = (ImportKind)detail::readU16(e + 6);
				if (!detail::string(file, strAt, strSize, readU32(e + 8), imp.symbol)) { message = "bad import name"; return false; }
				if ((uint64_t)imp.offset + 4 > imageSize || imp.library >= libCount) { message = "bad import"; return false; }
				p.imports.push_back(imp);
			}
			return true;
		}

		// ============================== DRL ==============================
		// 0x00 'DRL1'               0x04 header size (0x60)    0x08 image size     0x0C init
		// 0x10 rw start             0x14 rw size               0x18 id (hi)        0x1C id (lo)
		// 0x20 abi                  0x24 name (string)
		// 0x28 text relocs (count)  0x2C (offset)              (words: + text base, once)
		// 0x30 data->text relocs    0x34                       (words in rw: + text base, per process)
		// 0x38 data->data relocs    0x3C                       (words in rw: + bias, per process)
		// 0x40 dependencies         0x44                       (8 bytes each: name, abi)
		// 0x48 imports              0x4C                       (12 bytes each: offset, dependency (u16), kind (u16), symbol)
		// 0x50 exports              0x54                       (8 bytes each: symbol, offset; sorted by name)
		// 0x58 strings              0x5C strings size
		// Then: the image, the tables (in that order), the strings.
		inline std::vector<uint8_t> write(const Library& l)
		{
			detail::Strings strings;
			detail::Out o(LibraryHeaderSize);
			o.at(0x00, LibraryMagic);
			o.at(0x04, LibraryHeaderSize);
			o.at(0x08, (uint32_t)l.image.size());
			o.at(0x0C, l.init);
			o.at(0x10, l.rwStart);
			o.at(0x14, l.rwSize);
			o.at(0x18, (uint32_t)(l.id >> 32));
			o.at(0x1C, (uint32_t)l.id);
			o.at(0x20, l.abi);
			o.at(0x24, strings.add(l.name));
			o.file.insert(o.file.end(), l.image.begin(), l.image.end());
			o.words(0x28, l.textRelocations);
			o.words(0x30, l.dataTextRelocations);
			o.words(0x38, l.dataDataRelocations);
			o.at(0x40, (uint32_t)l.dependencies.size());
			o.at(0x44, o.here());
			for (const auto& d : l.dependencies) { o.u32(strings.add(d.name)); o.u32(d.abi); }
			o.at(0x48, (uint32_t)l.imports.size());
			o.at(0x4C, o.here());
			for (const auto& i : l.imports) { o.u32(i.offset); o.u16(i.library); o.u16((uint16_t)i.kind); o.u32(strings.add(i.symbol)); }
			o.at(0x50, (uint32_t)l.exports.size());
			o.at(0x54, o.here());
			for (const auto& e : l.exports) { o.u32(strings.add(e.symbol)); o.u32(e.offset); }
			o.at(0x58, o.here());
			o.at(0x5C, (uint32_t)strings.bytes.size());
			o.file.insert(o.file.end(), strings.bytes.begin(), strings.bytes.end());
			return o.file;
		}

		inline bool read(const std::vector<uint8_t>& file, Library& l, std::string& message)
		{
			using detail::readU32;
			if (!isLibrary(file)) { message = "not a DRL file"; return false; }
			auto l_fits = [&](uint32_t offset, uint64_t size) { return (uint64_t)offset + size <= file.size(); };
			const uint32_t headerSize = readU32(&file[0x04]), imageSize = readU32(&file[0x08]);
			const uint32_t strAt = readU32(&file[0x58]), strSize = readU32(&file[0x5C]);
			if (headerSize < LibraryHeaderSize || !l_fits(headerSize, imageSize) || !l_fits(strAt, strSize) || strSize == 0)
			{
				message = "truncated DRL file";
				return false;
			}
			l = Library();
			l.image.assign(file.begin() + headerSize, file.begin() + headerSize + imageSize);
			l.init = readU32(&file[0x0C]);
			l.rwStart = readU32(&file[0x10]);
			l.rwSize = readU32(&file[0x14]);
			l.id = ((uint64_t)readU32(&file[0x18]) << 32) | readU32(&file[0x1C]);
			l.abi = readU32(&file[0x20]);
			if (!detail::string(file, strAt, strSize, readU32(&file[0x24]), l.name) || l.name.empty()) { message = "bad library name"; return false; }
			if ((uint64_t)l.rwStart + l.rwSize > imageSize || l.init >= std::max<uint32_t>(imageSize, 1)) { message = "bad rw part or init"; return false; }
			auto l_words = [&](uint32_t countAt, std::vector<uint32_t>& out) -> bool {
				const uint32_t count = readU32(&file[countAt]), at = readU32(&file[countAt + 4]);
				if (!l_fits(at, (uint64_t)count * 4)) return false;
				for (uint32_t i = 0; i < count; i++)
				{
					const uint32_t r = readU32(&file[at + i * 4]);
					if ((uint64_t)r + 4 > imageSize) return false;
					out.push_back(r);
				}
				return true;
			};
			if (!l_words(0x28, l.textRelocations) || !l_words(0x30, l.dataTextRelocations) || !l_words(0x38, l.dataDataRelocations))
			{
				message = "bad relocation table";
				return false;
			}
			const uint32_t depCount = readU32(&file[0x40]), depAt = readU32(&file[0x44]);
			const uint32_t impCount = readU32(&file[0x48]), impAt = readU32(&file[0x4C]);
			const uint32_t expCount = readU32(&file[0x50]), expAt = readU32(&file[0x54]);
			if (!l_fits(depAt, (uint64_t)depCount * 8) || !l_fits(impAt, (uint64_t)impCount * 12) || !l_fits(expAt, (uint64_t)expCount * 8))
			{
				message = "truncated DRL file";
				return false;
			}
			for (uint32_t i = 0; i < depCount; i++)
			{
				LibraryRef d;
				if (!detail::string(file, strAt, strSize, readU32(&file[depAt + i * 8]), d.name) || d.name.empty()) { message = "bad dependency name"; return false; }
				d.abi = readU32(&file[depAt + i * 8 + 4]);
				l.dependencies.push_back(d);
			}
			for (uint32_t i = 0; i < impCount; i++)
			{
				const uint8_t* e = &file[impAt + i * 12];
				Import imp;
				imp.offset = readU32(e);
				imp.library = detail::readU16(e + 4);
				imp.kind = (ImportKind)detail::readU16(e + 6);
				if (!detail::string(file, strAt, strSize, readU32(e + 8), imp.symbol)) { message = "bad import name"; return false; }
				const bool needsLib = imp.kind == ImportKind::Absolute || imp.kind == ImportKind::SlotOf || imp.kind == ImportKind::OffsetOf;
				if ((uint64_t)imp.offset + 4 > imageSize || (needsLib && imp.library >= depCount) || (uint16_t)imp.kind > (uint16_t)ImportKind::LibTable
					|| imp.kind == ImportKind::Init)
				{
					message = "bad import";
					return false;
				}
				l.imports.push_back(imp);
			}
			for (uint32_t i = 0; i < expCount; i++)
			{
				Export e;
				if (!detail::string(file, strAt, strSize, readU32(&file[expAt + i * 8]), e.symbol)) { message = "bad export name"; return false; }
				e.offset = readU32(&file[expAt + i * 8 + 4]);
				if (e.offset >= imageSize) { message = "export outside of the image"; return false; }
				l.exports.push_back(e);
			}
			if (!std::is_sorted(l.exports.begin(), l.exports.end(), [](const Export& a, const Export& b) { return a.symbol < b.symbol; }))
			{
				message = "exports not sorted";
				return false;
			}
			return true;
		}

		// ============================== A loader (drun's) ==============================
		// Loads a DRX2 and the shared libraries it needs into a machine's memory, for one process.
		// It does what a kernel's loader does (other/info/dynamic-linking.md), in one go:
		//   1. every library (dependencies first): its text placed (page aligned), relocated, its
		//      text imports resolved, written; its slot is its index;
		//   2. the process: the table (one bias per slot) and the LibTable word pointing at it;
		//      each library's rw copy (from its template), with its data relocations and imports;
		//   3. the program: relocated to its base, its imports resolved.
		class Linker
		{
			public:
				struct Loaded
				{
					Library lib;
					uint32_t text { 0 };	// where its image is
					uint32_t data { 0 };	// this process's copy of its rw part (0: none)
					std::vector<uint32_t> deps;	// its dependencies: indices in libraries()
					inline uint32_t bias(void) const { return data - lib.rwStart; }
				};

				std::vector<std::string> directories;	// where <name>.drl files are looked for
				// Writes bytes to the machine's memory (false: not there).
				std::function<bool(uint32_t address, const std::vector<uint8_t>& bytes)> write;
				// The debugger's DRX listener (a library's text loaded at base, with its id).
				std::function<void(uint32_t base, uint32_t size, uint64_t id)> notify;
				// Prints what is done (--verbose-link).
				std::function<void(const std::string&)> log;

				// Links `program` for base (where its image will be: the caller writes it). next: the
				// first free address after it (libraries and the per-process parts go there). The
				// relocated, linked image is returned in out.
				bool link(const Program& program, uint32_t base, uint32_t next, std::vector<uint8_t>& out, std::string& message)
				{
					m_next = alignPage(next);
					std::vector<uint32_t> needed;
					for (const auto& ref : program.libraries)
					{
						int32_t index = load(ref, message);
						if (index < 0)
							return false;
						needed.push_back((uint32_t)index);
					}
					// The process: the LibTable word, then the table (a word per slot).
					m_libTable = m_next;
					const uint32_t table = m_libTable + 4;
					m_next = alignPage(table + 4 * (uint32_t)m_libs.size());
					std::vector<uint8_t> tableBytes(4 + 4 * m_libs.size(), 0);
					drx::writeU32(&tableBytes[0], table);
					for (auto& l : m_libs)
					{
						if (l.lib.rwSize == 0)
							continue;
						l.data = m_next;
						m_next = alignPage(m_next + l.lib.rwSize);
					}
					for (size_t s = 0; s < m_libs.size(); s++)
						drx::writeU32(&tableBytes[4 + 4 * s], m_libs[s].lib.rwSize == 0 ? 0 : m_libs[s].bias());
					if (!put(m_libTable, tableBytes, message))
						return false;
					// The texts, now that the LibTable word's address is known.
					for (size_t s = 0; s < m_libs.size(); s++)
					{
						if (!placeText((uint32_t)s, message))
							return false;
					}
					for (size_t s = 0; s < m_libs.size(); s++)
					{
						if (!copyData((uint32_t)s, message))
							return false;
					}
					// The program.
					out = program.image;
					for (uint32_t r : program.relocations)
						drx::writeU32(&out[r], drx::readU32(&out[r]) + base);
					for (const auto& imp : program.imports)
					{
						const Loaded& from = m_libs[needed[imp.library]];
						uint32_t value = 0;
						if (imp.kind == ImportKind::Init)
							value = from.text + from.lib.init;
						else if (imp.kind == ImportKind::Absolute)
						{
							if (!absolute(from, imp.symbol, value, message))
								return false;
						}
						else
						{
							message = "a program import of kind " + std::string(kindName(imp.kind));
							return false;
						}
						drx::writeU32(&out[imp.offset], value);
					}
					return true;
				}

				inline const std::vector<Loaded>& libraries(void) const { return m_libs; }
				inline uint32_t libTableWord(void) const { return m_libTable; }
				inline uint32_t end(void) const { return m_next; }

			private:
				static uint32_t alignPage(uint32_t a) { return (a + 0xFFF) & ~0xFFFu; }

				bool put(uint32_t address, const std::vector<uint8_t>& bytes, std::string& message)
				{
					if (!write || !write(address, bytes))
					{
						message = "no memory at " + drx::idString(address).substr(10);
						return false;
					}
					return true;
				}

				// The library (and its dependencies, first) known: its index (= its slot), or -1.
				int32_t load(const LibraryRef& ref, std::string& message)
				{
					for (size_t i = 0; i < m_libs.size(); i++)
					{
						if (m_libs[i].lib.name != ref.name)
							continue;
						if (m_libs[i].lib.abi != ref.abi)
						{
							message = ref.name + ": ABI mismatch (compiled against another version of the library)";
							return -1;
						}
						return (int32_t)i;
					}
					for (const auto& n : m_loading)
					{
						if (n == ref.name)
						{
							message = ref.name + ": the libraries depend on each other";
							return -1;
						}
					}
					std::vector<uint8_t> file;
					std::string path;
					for (const auto& dir : directories)
					{
						path = dir + "/" + ref.name + ".drl";
						if (readFile(path, file))
							break;
						file.clear();
					}
					if (file.empty())
					{
						message = ref.name + ".drl: not found";
						return -1;
					}
					Loaded l;
					if (!read(file, l.lib, message))
					{
						message = path + ": " + message;
						return -1;
					}
					if (l.lib.abi != ref.abi)
					{
						message = ref.name + ": ABI mismatch (compiled against another version of the library)";
						return -1;
					}
					m_loading.push_back(ref.name);
					for (const auto& d : l.lib.dependencies)
					{
						const int32_t index = load(d, message);
						if (index < 0)
							return -1;
						l.deps.push_back((uint32_t)index);
					}
					m_loading.pop_back();
					l.text = m_next;
					m_next = alignPage(m_next + (uint32_t)l.lib.image.size());
					m_libs.push_back(std::move(l));
					if (log)
						log(ref.name + ": slot " + std::to_string(m_libs.size() - 1) + ", text at " + drx::idString(m_libs.back().text).substr(10));
					return (int32_t)m_libs.size() - 1;
				}

				// An Absolute import: in the exporter's text, or in this process's copy of its rw part.
				bool absolute(const Loaded& from, const std::string& symbol, uint32_t& value, std::string& message)
				{
					const int64_t offset = from.lib.find(symbol);
					if (offset < 0)
					{
						message = from.lib.name + " has no " + symbol;
						return false;
					}
					value = (from.lib.inRw((uint32_t)offset) ? from.bias() : from.text) + (uint32_t)offset;
					return true;
				}

				bool placeText(uint32_t slot, std::string& message)
				{
					Loaded& l = m_libs[slot];
					std::vector<uint8_t> image = l.lib.image;
					for (uint32_t r : l.lib.textRelocations)
						drx::writeU32(&image[r], drx::readU32(&image[r]) + l.text);
					for (const auto& imp : l.lib.imports)
					{
						if (l.lib.inRw(imp.offset))
							continue;		// per process (copyData)
						uint32_t value = 0;
						if (!textImport(l, slot, imp, value, message))
							return false;
						drx::writeU32(&image[imp.offset], value);
					}
					if (!put(l.text, image, message))
						return false;
					if (notify)
						notify(l.text, (uint32_t)image.size(), l.lib.id);
					return true;
				}

				bool textImport(const Loaded& l, uint32_t slot, const Import& imp, uint32_t& value, std::string& message)
				{
					switch (imp.kind)
					{
						case ImportKind::SelfSlot: value = 4 * slot; return true;
						case ImportKind::LibTable: value = m_libTable; return true;
						default: break;
					}
					const Loaded& from = m_libs[l.deps[imp.library]];
					const int64_t offset = from.lib.find(imp.symbol);
					if (offset < 0)
					{
						message = l.lib.name + ": " + from.lib.name + " has no " + imp.symbol;
						return false;
					}
					switch (imp.kind)
					{
						case ImportKind::SlotOf: value = 4 * l.deps[imp.library]; return true;
						case ImportKind::OffsetOf: value = (uint32_t)offset; return true;
						case ImportKind::Absolute:
							if (from.lib.inRw((uint32_t)offset))
							{
								message = l.lib.name + ": its shared code uses the address of " + from.lib.name + "'s global " + imp.symbol + " (different in every process)";
								return false;
							}
							value = from.text + (uint32_t)offset;
							return true;
						default:
							message = l.lib.name + ": an import of kind " + kindName(imp.kind);
							return false;
					}
				}

				bool copyData(uint32_t slot, std::string& message)
				{
					Loaded& l = m_libs[slot];
					if (l.lib.rwSize == 0)
						return true;
					std::vector<uint8_t> data(l.lib.image.begin() + l.lib.rwStart, l.lib.image.begin() + l.lib.rwStart + l.lib.rwSize);
					auto l_add = [&](uint32_t r, uint32_t v) { drx::writeU32(&data[r - l.lib.rwStart], drx::readU32(&data[r - l.lib.rwStart]) + v); };
					for (uint32_t r : l.lib.dataTextRelocations)
						l_add(r, l.text);
					for (uint32_t r : l.lib.dataDataRelocations)
						l_add(r, l.bias());
					for (const auto& imp : l.lib.imports)
					{
						if (!l.lib.inRw(imp.offset))
							continue;
						if (imp.kind != ImportKind::Absolute)
						{
							message = l.lib.name + ": an import of kind " + kindName(imp.kind) + " in its writable data";
							return false;
						}
						uint32_t value = 0;
						if (!absolute(m_libs[l.deps[imp.library]], imp.symbol, value, message))
							return false;
						drx::writeU32(&data[imp.offset - l.lib.rwStart], value);
					}
					return put(l.data, data, message);
				}

				static bool readFile(const std::string& path, std::vector<uint8_t>& out);

				std::vector<Loaded> m_libs;
				std::vector<std::string> m_loading;
				uint32_t m_next { 0 };
				uint32_t m_libTable { 0 };
		};
	}
}

#include <fstream>
inline bool dragon::dyn::Linker::readFile(const std::string& path, std::vector<uint8_t>& out)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return true;
}
