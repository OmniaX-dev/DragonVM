// dsc: the DScript compiler. DScript -> dasm -> (the assembler, linked in) -> DRX or raw binary,
// with the compiled libraries it imports (#import) linked in; or a library (--lib: a .dlib).

#include "Preprocessor.hpp"
#include "Parser.hpp"
#include "CodeGen.hpp"
#include "../assembler/Assembler.hpp"
#include "../tools/DRX.hpp"
#include "../tools/DLIB.hpp"
#include <set>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <ostd/utils/Hash.hpp>

using namespace dscript;

struct Options
{
	std::string input;
	std::string output;
	std::string dasmOutput;
	std::vector<std::string> includeDirs;
	std::vector<std::string> libraryDirs;
	bool library { false };
	std::vector<std::pair<std::string, std::string>> defines;
	bool raw { false };
	bool rom { false };
	std::string load;
	std::string fixed;
	std::string header;
	bool debug { false };
	bool noInline { false };
	std::string disassemblyDir;
	bool verbose { false };
	std::vector<std::string> dasmArgs;	// -Wa,<options>: handed to the assembler
};

static void printHelp(void)
{
	std::cout <<
		"dsc - the DScript compiler\n\n"
		"Usage: dsc <source.ds> [options]\n\n"
		"  -o <file>                 Output file (default: the source name with .drx, or .bin with --raw)\n"
		"  -I <dir>                  Adds an include directory (#include(<file>) and fallback for #include(file))\n"
		"  -L <dir>                  Adds a library directory (#import(<name>): name.dlib; fallback for #import(path))\n"
		"  --lib                     Compiles a library (.dlib) instead of a program: no main; programs use it with\n"
		"                            #import(<name>), and it is linked into them\n"
		"  -D <NAME>[=<value>]       Defines NAME (like #define)\n"
		"  --emit-dasm <file>        Also writes the generated dasm source\n"
		"  --debug, -g               Keeps debug_break / debug_* instructions of low blocks\n"
		"  --no-inline               Compiles inline subs as normal subs (each call is a real call: easier to\n"
		"                            follow in the disassembly and the debugger)\n"
		"  --verbose                 Prints the program size and relocations\n"
		"  -Wa,<option>[,<option>...]  Passes options to the assembler (see dasm --help), e.g. -Wa,-D for dasm's\n"
		"                            debug mode, -Wa,--save-disassembly,<file>, -Wa,--verbose-2. Not allowed: -o and\n"
		"                            --disable-extmov / --disable-extalu (the generated code needs both)\n\n"
		"Output formats:\n"
		"  (default)                 DRX: relocatable executable, loaded anywhere by a loader (an OS):\n"
		"                            the program is called and returns when main returns (RV = main's value)\n"
		"  --raw                     Bare metal binary at a fixed address; ends with hlt after main\n"
		"    --load <address>        .load: the address the binary runs at (default 0x01010000, the RAM)\n"
		"    --fixed <size>,<fill>   .fixed: pads the binary to exactly <size> bytes\n"
		"    --header <name>         .header: adds a header (e.g. KERNEL0_BOOT)\n"
		"    --save-disassembly <dir/file>  Saves the debugger disassembly, with the source lines (in a directory:\n"
		"                            named like dasm's, for ddb)\n"
		"  --rom                     A ROM image (e.g. the BIOS): --raw, loaded at 0x00000000 unless --load is\n"
		"                            given, and everything that would write into the program is an error\n"
		"                            (global variables: use const and RAM instead). Defines __ROM__\n"
		"  --help                    This help\n";
}

static int parseArgs(int argc, char** argv, Options& o)
{
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto l_value = [&](const std::string& name) -> std::string {
			if (i + 1 >= argc)
			{
				std::cerr << "dsc: missing value for " << name << "\n";
				std::exit(2);
			}
			return argv[++i];
		};
		if (a == "--help" || a == "-h") { printHelp(); return 1; }
		else if (a == "-o") o.output = l_value(a);
		else if (a == "-I") o.includeDirs.push_back(l_value(a));
		else if (a == "-L") o.libraryDirs.push_back(l_value(a));
		else if (a == "--lib") o.library = true;
		else if (a == "-D")
		{
			std::string d = l_value(a);
			size_t eq = d.find('=');
			if (eq == std::string::npos)
				o.defines.push_back({ d, "" });
			else
				o.defines.push_back({ d.substr(0, eq), d.substr(eq + 1) });
		}
		else if (a == "--emit-dasm") o.dasmOutput = l_value(a);
		else if (a == "--debug" || a == "-g") o.debug = true;
		else if (a == "--no-inline") o.noInline = true;
		else if (a == "--verbose") o.verbose = true;
		else if (a == "--raw") o.raw = true;
		else if (a == "--rom") { o.rom = true; o.raw = true; }
		else if (a == "--load") o.load = l_value(a);
		else if (a == "--fixed") o.fixed = l_value(a);
		else if (a == "--header") o.header = l_value(a);
		else if (a == "--save-disassembly") o.disassemblyDir = l_value(a);
		else if (a.rfind("-Wa,", 0) == 0)
		{
			std::string rest = a.substr(4);
			size_t start = 0;
			while (start <= rest.size())
			{
				size_t comma = rest.find(',', start);
				if (comma == std::string::npos)
					comma = rest.size();
				if (comma > start)
					o.dasmArgs.push_back(rest.substr(start, comma - start));
				start = comma + 1;
			}
		}
		else if (!a.empty() && a[0] == '-')
		{
			std::cerr << "dsc: unknown option: " << a << " (see --help)\n";
			return 2;
		}
		else if (o.input.empty())
			o.input = a;
		else
		{
			std::cerr << "dsc: only one source file is compiled (found " << a << ")\n";
			return 2;
		}
	}
	if (o.input.empty())
	{
		std::cerr << "dsc: no source file (see --help)\n";
		return 2;
	}
	if (!o.raw && (!o.load.empty() || !o.fixed.empty() || !o.header.empty() || !o.disassemblyDir.empty()))
	{
		std::cerr << "dsc: --load, --fixed, --header and --save-disassembly need --raw\n";
		return 2;
	}
	if (o.library && o.raw)
	{
		std::cerr << "dsc: a library (--lib) is relocatable: --raw and --rom are for programs\n";
		return 2;
	}
	if (o.output.empty())
		o.output = std::filesystem::path(o.input).replace_extension(o.library ? ".dlib" : o.raw ? ".bin" : ".drx").string();
	return 0;
}

// Checks the -Wa options and loads them with dasm's own argument parser (it ignores unknown
// options, so they are checked here first).
static bool loadDasmArgs(const Options& o)
{
	if (o.dasmArgs.empty())
		return true;
	static const std::vector<std::string> flags = { "-D", "--debug", "--verbose", "--verbose-2", "--verbose-full" };
	static const std::vector<std::string> withValue = { "--save-disassembly", "--save-final-stage", "-I", "--include-path" };
	auto l_has = [](const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); };
	for (size_t i = 0; i < o.dasmArgs.size(); i++)
	{
		const std::string& a = o.dasmArgs[i];
		if (l_has(withValue, a))
		{
			if (i + 1 >= o.dasmArgs.size())
			{
				std::cerr << "dsc: -Wa: missing value for " << a << " (-Wa," << a << ",<value>)\n";
				return false;
			}
			i++;
		}
		else if (a == "-o")
		{
			std::cerr << "dsc: -Wa: use dsc's own -o\n";
			return false;
		}
		else if (a == "--disable-extmov" || a == "--disable-extalu")
		{
			std::cerr << "dsc: -Wa: " << a << " is not allowed (the generated code needs extmov and extalu)\n";
			return false;
		}
		else if (!l_has(flags, a))
		{
			std::cerr << "dsc: -Wa: unknown or unsupported assembler option: " << a << "\n";
			return false;
		}
	}
	// dasm's parser: dasm <source> -o <output> <options...> (the output names the default disassembly file).
	std::vector<std::string> argvStrings = { "dasm", o.input, "-o", o.output };
	argvStrings.insert(argvStrings.end(), o.dasmArgs.begin(), o.dasmArgs.end());
	std::vector<char*> argv;
	for (auto& s : argvStrings)
		argv.push_back(s.data());
	const i32 r = dragon::code::Assembler::Application::loadArguments((int)argv.size(), argv.data());
	if (r == dragon::code::Assembler::Application::RETURN_VAL_INVALID_PARAM)
	{
		std::cerr << "dsc: -Wa: invalid value (an include directory that does not exist, or a file that cannot be created)\n";
		return false;
	}
	if (r != dragon::code::Assembler::Application::RETURN_VAL_EXIT_SUCCESS)
	{
		std::cerr << "dsc: -Wa: the assembler rejected the options (code " << r << ")\n";
		return false;
	}
	return true;
}

// After the program is assembled (at the address it runs at): what the -Wa options ask for.
static void dasmAfterAssembly(const Options& o)
{
	auto& args = dragon::code::Assembler::Application::args;
	if (o.dasmArgs.empty())
		return;
	if (args.verbose_level != -1)
		dragon::code::Assembler::printProgramInfo(args.verbose_level);
	// (-Wa,--save-disassembly: after the libraries are linked, with their code.)
}

static bool writeFile(const std::string& path, const std::vector<uint8_t>& data)
{
	std::ofstream file(path, std::ios::binary);
	if (!file)
		return false;
	file.write((const char*)data.data(), (std::streamsize)data.size());
	return (bool)file;
}

static std::vector<uint8_t> assemble(const std::vector<String>& lines, const std::string& directory, uint32_t base)
{
	dragon::code::Assembler::defaultLoadAddress = base;
	ostd::ByteStream code = dragon::code::Assembler::assembleFromLines(lines, directory);
	std::vector<uint8_t> out(code.size());
	for (size_t i = 0; i < code.size(); i++)
		out[i] = (uint8_t)code[i];
	return out;
}

// ======================================== Linking ========================================
// A piece of a program (the program itself, or a library): its image, the 32-bit addresses in
// it (relocations: of its own symbols; imports: of the other pieces' symbols), its symbols.
struct Unit
{
	std::vector<uint8_t> image;
	std::vector<uint32_t> relocations;
	std::vector<dragon::dlib::Import> imports;
	std::vector<std::pair<std::string, u32>> symbols;
	std::vector<dragon::code::Assembler::tDisassemblyLine> disassembly;	// for the debugger (of the assembly at base)
};

// Assembles dasm at base. The symbols of `importable` that it uses but doesn't define are
// written as made-up addresses (FakeBase + 16 * n): assembled again with them moved by
// FakeShift, the words that move are where they are used. relocatable: assembled again at
// base + RelocDelta, the words that move by that are its own addresses.
static bool assembleUnit(const std::vector<std::string>& dasm, const std::set<std::string>& importable, const std::string& directory, uint32_t base,
	bool relocatable, Unit& out, const Options& o, bool program)
{
	const uint32_t RelocDelta = 0x01000000, FakeBase = 0xF0000000, FakeShift = 0x02000000;
	std::set<std::string> defined;
	bool inData = false;
	for (const auto& line : dasm)
	{
		std::string t = line;
		t.erase(0, t.find_first_not_of(" \t"));
		while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r'))
			t.pop_back();
		if (t.rfind(".data", 0) == 0) inData = true;
		else if (t.rfind(".code", 0) == 0) inData = false;
		else if (inData && !t.empty() && t[0] == '$')
			defined.insert(t.substr(1, t.find_first_of(" :\t") == std::string::npos ? std::string::npos : t.find_first_of(" :\t") - 1));
		else if (!inData && !t.empty() && t.back() == ':' && t.find(' ') == std::string::npos && t.find(',') == std::string::npos)
			defined.insert(t.substr(0, t.size() - 1));
	}
	std::vector<std::string> used;
	std::map<std::string, size_t> usedIndex;
	auto l_isName = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
	auto l_lines = [&](uint32_t shift) {
		std::vector<String> lines;
		bool data = false;
		for (const auto& line : dasm)
		{
			const size_t first = line.find_first_not_of(" \t");
			if (first != std::string::npos && line.compare(first, 5, ".data") == 0) data = true;
			else if (first != std::string::npos && line.compare(first, 5, ".code") == 0) data = false;
			if (line.find('$') == std::string::npos || importable.empty())
			{
				lines.push_back(String(line));
				continue;
			}
			std::string r;
			for (size_t i = 0; i < line.size(); )
			{
				if (line[i] != '$' || (data && i == first))
				{
					r += line[i++];
					continue;
				}
				size_t j = i + 1;
				while (j < line.size() && l_isName(line[j]))
					j++;
				const std::string name = line.substr(i + 1, j - i - 1);
				if (name.empty() || importable.count(name) == 0 || defined.count(name) > 0)
				{
					r += line.substr(i, j - i);
					i = j;
					continue;
				}
				auto it = usedIndex.find(name);
				if (it == usedIndex.end())
				{
					it = usedIndex.insert({ name, used.size() }).first;
					used.push_back(name);
				}
				const uint32_t value = FakeBase + 16 * (uint32_t)it->second + shift;
				char buf[40];
				if (data)
					std::snprintf(buf, sizeof(buf), "0x%02X, 0x%02X, 0x%02X, 0x%02X", value >> 24, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
				else
					std::snprintf(buf, sizeof(buf), "0x%08X", value);
				r += buf;
				i = j;
			}
			lines.push_back(String(r));
		}
		return lines;
	};
	// The first assembly is the one at base, with the made-up addresses (the disassembly and
	// the symbols are from it).
	const std::vector<String> lines = l_lines(0);
	std::vector<uint8_t> a = assemble(lines, directory, base);
	out.symbols = dragon::code::Assembler::symbolAddresses();
	out.disassembly = dragon::code::Assembler::disassemblyEntries(String(program ? std::filesystem::path(o.output).filename().string() : ""));
	if (program)
		dasmAfterAssembly(o);	// (a DRX's disassembly and symbols are at offset 0: add the load address)
	std::vector<uint8_t> moved, shifted;
	if (relocatable)
		moved = assemble(lines, directory, base + RelocDelta);
	if (!used.empty())
		shifted = assemble(l_lines(FakeShift), directory, base);
	if (a.empty() || (relocatable && moved.size() != a.size()) || (!used.empty() && shifted.size() != a.size()))
	{
		std::cerr << "dsc: the assembler failed\n";
		return false;
	}
	if (relocatable && a.size() >= RelocDelta)
	{
		std::cerr << "dsc: the program is too big (16 MiB at most)\n";
		return false;
	}
	for (size_t i = 0; i < a.size(); i++)
	{
		const bool reloc = relocatable && a[i] != moved[i];
		const bool import = !used.empty() && a[i] != shifted[i];
		if (!reloc && !import)
			continue;
		const uint32_t word = (i + 4 <= a.size()) ? dragon::drx::readU32(&a[i]) : 0;
		if (reloc && i + 4 <= a.size() && word + RelocDelta == dragon::drx::readU32(&moved[i]))
			out.relocations.push_back((uint32_t)i);
		else if (import && i + 4 <= a.size() && word + FakeShift == dragon::drx::readU32(&shifted[i]) && word >= FakeBase
				 && (word - FakeBase) % 16 == 0 && (word - FakeBase) / 16 < used.size())
			out.imports.push_back({ (uint32_t)i, used[(word - FakeBase) / 16] });
		else
		{
			std::cerr << "dsc: an address is used in a field that is not 32 bits wide, at offset 0x" << std::hex << i << std::dec
					  << " (in a low block?): it cannot be relocated\n";
			return false;
		}
		i += 3;
	}
	out.image = a;
	return true;
}

// ===================================== Disassembly ======================================
typedef dragon::code::Assembler::tDisassemblyLine DisassemblyLine;

// The parts of a disassembly (see Assembler::saveDisassemblyToFile).
struct DisassemblyParts
{
	std::vector<DisassemblyLine> head, code, data, labels, lines, sources;

	void add(const std::vector<DisassemblyLine>& entries)
	{
		int part = 0;
		for (const auto& e : entries)
		{
			if (e.code.startsWith("{ NAME } ") || e.code.startsWith("{ VERSION } ")) head.push_back(e);
			else if (e.code == "{ DATA }") part = 1;
			else if (e.code == "{ LABELS }") part = 2;
			else if (e.code == "{ LINES }") part = 3;
			else if (e.code == "{ SOURCES }") part = 4;
			else (part == 0 ? code : part == 1 ? data : part == 2 ? labels : part == 3 ? lines : sources).push_back(e);
		}
	}

	std::vector<DisassemblyLine> entries(void) const
	{
		std::vector<DisassemblyLine> out = head;
		out.insert(out.end(), code.begin(), code.end());
		out.push_back({ 0xFFFFFF00, "{ DATA }" });
		out.insert(out.end(), data.begin(), data.end());
		out.push_back({ 0xFFFFFF01, "{ LABELS }" });
		out.insert(out.end(), labels.begin(), labels.end());
		if (!lines.empty())
		{
			out.push_back({ 0xFFFFFF02, "{ LINES }" });
			out.insert(out.end(), lines.begin(), lines.end());
			out.push_back({ 0xFFFFFF03, "{ SOURCES }" });
			out.insert(out.end(), sources.begin(), sources.end());
		}
		return out;
	}

	// The instruction at `address` (the last one that starts there or before) shows `from` as `to`.
	void patch(uint32_t address, const std::string& from, const std::string& to, size_t first)
	{
		auto it = std::upper_bound(code.begin() + (std::ptrdiff_t)first, code.end(), address,
			[](uint32_t a, const DisassemblyLine& l) { return a < l.addr; });
		if (it == code.begin() + (std::ptrdiff_t)first)
			return;
		--it;
		const std::string text = it->code.cpp_str();
		const size_t at = text.find(from);
		if (at != std::string::npos)
			it->code = String(text.substr(0, at) + to + text.substr(at + from.size()));
	}
};

static std::string hexWord(uint32_t v)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "0x%08X", v);
	return buf;
}

// The program's disassembly (of its last assembly) with the code of the libraries linked
// after it: at their addresses, with their source files, and the addresses in the text as
// they are once linked (the symbols of other libraries by their names).
static std::vector<DisassemblyLine> linkedDisassembly(const Unit& unit, uint32_t base,
	const std::vector<Preprocessor::ImportedLibrary>& libraries, const std::vector<uint32_t>& offsets)
{
	const std::vector<DisassemblyLine>& entries = unit.disassembly;
	if (entries.empty() || libraries.empty())
		return entries;
	DisassemblyParts parts;
	parts.add(entries);
	for (const auto& i : unit.imports)
		parts.patch(base + i.offset, hexWord(dragon::drx::readU32(&unit.image[i.offset])), "$" + i.symbol, 0);
	int sourceCount = 0;
	std::map<std::string, uint32_t> sourceIndex;	// path -> index (a file is there once)
	auto l_path = [](const DisassemblyLine& e) { const std::string t = e.code.cpp_str(); return t.substr(0, t.find('\n')); };
	for (const auto& e : parts.sources)
	{
		sourceCount = std::max(sourceCount, (int)e.addr + 1);
		sourceIndex[l_path(e)] = e.addr;
	}
	for (size_t k = 0; k < libraries.size(); k++)
	{
		const auto& lib = libraries[k].library;
		const uint32_t at = base + offsets[k];
		DisassemblyParts own;
		std::vector<DisassemblyLine> libEntries;
		for (const auto& e : lib.debug)
			libEntries.push_back({ e.address, String(e.text), e.size });
		own.add(libEntries);
		for (auto& e : own.code) e.addr += at;
		for (auto& e : own.data) e.addr += at;
		for (auto& e : own.labels) e.addr += at;
		// Its source files: those already there keep their index, the others get new ones.
		std::map<uint32_t, uint32_t> renumber;
		std::vector<DisassemblyLine> newSources;
		for (auto e : own.sources)
		{
			auto it = sourceIndex.find(l_path(e));
			if (it != sourceIndex.end())
				renumber[e.addr] = it->second;
			else
			{
				renumber[e.addr] = (uint32_t)sourceCount;
				sourceIndex[l_path(e)] = (uint32_t)sourceCount;
				e.addr = (uint32_t)sourceCount++;
				newSources.push_back(e);
			}
		}
		own.sources = newSources;
		for (auto& e : own.lines)
		{
			e.addr += at;
			e.size = (u16)renumber[e.size];
		}
		const size_t first = parts.code.size();
		parts.code.insert(parts.code.end(), own.code.begin(), own.code.end());
		parts.data.insert(parts.data.end(), own.data.begin(), own.data.end());
		parts.labels.insert(parts.labels.end(), own.labels.begin(), own.labels.end());
		parts.lines.insert(parts.lines.end(), own.lines.begin(), own.lines.end());
		parts.sources.insert(parts.sources.end(), own.sources.begin(), own.sources.end());
		for (uint32_t r : lib.relocations)
		{
			const uint32_t old = dragon::drx::readU32(&lib.image[r]);
			parts.patch(at + r, hexWord(old), hexWord(old + at), first);
		}
		for (const auto& i : lib.imports)
			parts.patch(at + i.offset, hexWord(dragon::drx::readU32(&lib.image[i.offset])), "$" + i.symbol, first);
	}
	return parts.entries();
}

static void saveDisassembly(const std::string& file, const std::vector<DisassemblyLine>& entries)
{
	std::filesystem::path path(file);
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path());
	if (entries.empty() || !dragon::code::Assembler::saveDisassemblyEntries(String(file), entries))
		std::cerr << "dsc: cannot save the disassembly to " << file << "\n";
}

int main(int argc, char** argv)
{
	Options o;
	int r = parseArgs(argc, argv, o);
	if (r != 0)
		return r == 1 ? 0 : r;

	std::vector<std::string> dasm;
	std::vector<Preprocessor::ImportedLibrary> libraries;
	std::vector<std::string> dependencies, exported;
	std::string interface;
	std::vector<dragon::dlib::LineInfo> interfaceLines;
	try
	{
		Preprocessor pp(o.includeDirs);
		pp.setLibraryDirs(o.libraryDirs);
		pp.define("__DSCRIPT__", "1");
		if (o.raw)
			pp.define("__RAW__", "1");
		if (o.rom)
			pp.define("__ROM__", "1");
		for (const auto& d : o.defines)
			pp.define(d.first, d.second);
		std::vector<Token> tokens = pp.processFile(o.input);
		Parser parser(tokens);
		Node* program = parser.parseProgram();
		CodeGenOptions cg;
		cg.raw = o.raw;
		cg.rom = o.rom;
		cg.noInline = o.noInline;
		cg.noAsserts = pp.isDefined("NDEBUG");
		if (o.raw)
		{
			const std::string defaultLoad = (o.rom ? "0x00000000" : "0x01010000");
			cg.directives.push_back(".load " + (o.load.empty() ? defaultLoad : o.load));
			// With libraries, the padding is done after they are linked.
			if (!o.fixed.empty() && pp.imports().empty())
				cg.directives.push_back(".fixed " + o.fixed);
			if (!o.header.empty())
				cg.directives.push_back(".header " + o.header);
		}
		cg.library = o.library;
		cg.libraries = o.library ? 0 : (int)pp.imports().size();
		CodeGen codegen(cg);
		dasm = codegen.generate(program);
		libraries = pp.imports();
		if (o.library)
		{
			dependencies = pp.importArguments();
			exported = codegen.exports();
			interface = codegen.interface(interfaceLines);
		}
	}
	catch (const CompileError& e)
	{
		std::cerr << posString(e.pos) << ": error: " << e.message << "\n";
		return 1;
	}

	if (!o.dasmOutput.empty())
	{
		std::ofstream f(o.dasmOutput);
		for (const auto& line : dasm)
			f << line << "\n";
	}

	// The assembler (linked in).
	std::vector<String> lines;
	for (const auto& line : dasm)
		lines.push_back(String(line));
	if (!loadDasmArgs(o))
		return 2;
	dragon::code::Assembler::cpuExtensions = { "extmov", "extalu" };
	dragon::code::Assembler::debugMode = o.debug || dragon::code::Assembler::Application::args.debug_mode;
	dragon::code::Assembler::saveExports = false;
	const std::string directory = std::filesystem::path(o.input).parent_path().string();

	// What the libraries offer: name -> (library, offset in its image); __dlib_init_<n>: the
	// initialization of library n.
	std::map<std::string, std::pair<size_t, uint32_t>> symbols;
	for (size_t k = 0; k < libraries.size(); k++)
	{
		const auto& lib = libraries[k].library;
		symbols["__dlib_init_" + std::to_string(k)] = { k, lib.init };
		for (const auto& e : lib.exports)
		{
			auto it = symbols.find(e.symbol);
			if (it != symbols.end())
			{
				std::cerr << "dsc: " << e.symbol << " is in two libraries: " << libraries[it->second.first].path << " and " << libraries[k].path << "\n";
				return 1;
			}
			symbols[e.symbol] = { k, e.offset };
		}
	}
	std::set<std::string> importable;
	for (const auto& sym : symbols)
		importable.insert(sym.first);

	if (o.library)
	{
		Unit unit;
		if (!assembleUnit(dasm, importable, directory, 0, true, unit, o, true))
			return 1;
		dragon::dlib::Library lib;
		lib.image = unit.image;
		lib.relocations = unit.relocations;
		lib.imports = unit.imports;
		const std::set<std::string> wanted(exported.begin(), exported.end());
		for (const auto& sym : unit.symbols)
		{
			if (sym.first == "__ds_lib_init")
				lib.init = sym.second;
			else if (wanted.count(sym.first) > 0)
				lib.exports.push_back({ sym.first, sym.second });
		}
		std::sort(lib.exports.begin(), lib.exports.end(), [](const auto& x, const auto& y) { return x.symbol < y.symbol; });
		lib.dependencies = dependencies;
		lib.interface = interface;
		lib.lines = interfaceLines;
		for (const auto& e : unit.disassembly)
		{
			if (!e.code.startsWith("{ NAME } ") && !e.code.startsWith("{ VERSION } "))
				lib.debug.push_back({ e.addr, e.size, e.code.cpp_str() });
		}
		if (dragon::code::Assembler::Application::args.save_disassembly && !o.dasmArgs.empty())
			saveDisassembly(dragon::code::Assembler::Application::args.disassembly_file_path.cpp_str(), unit.disassembly);
		if (!writeFile(o.output, dragon::dlib::write(lib)))
		{
			std::cerr << "dsc: cannot write " << o.output << "\n";
			return 1;
		}
		if (o.verbose)
			std::cout << o.output << ": library, " << lib.image.size() << " bytes, " << lib.exports.size() << " symbols, "
					  << lib.relocations.size() << " relocations, " << lib.imports.size() << " imports\n";
		return 0;
	}

	// The program, then each library (aligned), connected: base is where the image runs (raw:
	// the load address; DRX: 0, everything relocated by the loader).
	uint32_t base = 0;
	if (o.raw)
		base = (uint32_t)std::strtoul((o.load.empty() ? (o.rom ? "0x00000000" : "0x01010000") : o.load).c_str(), nullptr, 0);
	Unit unit;
	if (!assembleUnit(dasm, importable, directory, 0, !o.raw, unit, o, true))
		return 1;
	std::vector<uint8_t> image = unit.image;
	std::vector<uint32_t> relocations = unit.relocations;
	std::vector<uint32_t> offsets;
	for (const auto& l : libraries)
	{
		image.resize((image.size() + 15) & ~(size_t)15, 0);
		offsets.push_back((uint32_t)image.size());
		image.insert(image.end(), l.library.image.begin(), l.library.image.end());
	}
	auto l_add = [&](uint32_t at, uint32_t value) { dragon::drx::writeU32(&image[at], dragon::drx::readU32(&image[at]) + value); };
	auto l_resolve = [&](const std::string& symbol, const std::string& user, uint32_t at) -> bool {
		auto it = symbols.find(symbol);
		if (it == symbols.end())
		{
			std::cerr << "dsc: " << user << " uses " << symbol << ", which none of the imported libraries has (a library missing?)\n";
			return false;
		}
		dragon::drx::writeU32(&image[at], base + offsets[it->second.first] + it->second.second);
		if (!o.raw)
			relocations.push_back(at);
		return true;
	};
	for (const auto& i : unit.imports)
	{
		if (!l_resolve(i.symbol, o.input, i.offset))
			return 1;
	}
	for (size_t k = 0; k < libraries.size(); k++)
	{
		const auto& lib = libraries[k].library;
		for (uint32_t r : lib.relocations)
		{
			l_add(offsets[k] + r, base + offsets[k]);
			if (!o.raw)
				relocations.push_back(offsets[k] + r);
		}
		for (const auto& i : lib.imports)
		{
			if (!l_resolve(i.symbol, libraries[k].path, offsets[k] + i.offset))
				return 1;
		}
	}
	std::sort(relocations.begin(), relocations.end());
	// The disassembly for the debugger (-Wa,--save-disassembly; raw: --save-disassembly).
	if (dragon::code::Assembler::Application::args.save_disassembly && !o.dasmArgs.empty())
		saveDisassembly(dragon::code::Assembler::Application::args.disassembly_file_path.cpp_str(), linkedDisassembly(unit, base, libraries, offsets));

	if (o.raw)
	{
		if (!o.fixed.empty() && !libraries.empty())
		{
			const size_t comma = o.fixed.find(',');
			const size_t size = (size_t)std::strtoul(o.fixed.substr(0, comma).c_str(), nullptr, 0);
			const uint8_t fill = (uint8_t)(comma == std::string::npos ? 0 : std::strtoul(o.fixed.substr(comma + 1).c_str(), nullptr, 0));
			if (image.size() > size)
			{
				std::cerr << "dsc: the program and its libraries (" << image.size() << " bytes) are bigger than --fixed " << size << "\n";
				return 1;
			}
			image.resize(size, fill);
		}
		if (!writeFile(o.output, image))
		{
			std::cerr << "dsc: cannot write " << o.output << "\n";
			return 1;
		}
		if (!o.disassemblyDir.empty())
		{
			// A directory (as for ddb: one file per program, named like dasm's), or a file.
			std::string file = o.disassemblyDir;
			if (file.back() == '/' || std::filesystem::is_directory(file))
			{
				std::filesystem::create_directories(file);
				file = (std::filesystem::path(file) / (ostd::Hash::md5(String(o.output)).cpp_str() + ".dds")).string();
			}
			saveDisassembly(file, linkedDisassembly(unit, base, libraries, offsets));
		}
		if (o.verbose)
			std::cout << o.output << ": " << image.size() << " bytes (raw" << (libraries.empty() ? "" : ", " + std::to_string(libraries.size()) + " libraries") << ")\n";
		return 0;
	}

	if (image.size() >= 0x01000000)
	{
		std::cerr << "dsc: the program is too big (16 MiB at most)\n";
		return 1;
	}
	dragon::drx::Program program;
	program.image = image;
	program.entry = 0;
	program.relocations = relocations;
	if (!writeFile(o.output, dragon::drx::write(program)))
	{
		std::cerr << "dsc: cannot write " << o.output << "\n";
		return 1;
	}
	if (o.verbose)
		std::cout << o.output << ": " << image.size() << " bytes, " << relocations.size() << " relocations"
				  << (libraries.empty() ? "" : ", " + std::to_string(libraries.size()) + " libraries") << "\n";
	return 0;
}
