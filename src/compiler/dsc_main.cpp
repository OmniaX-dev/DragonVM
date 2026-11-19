// dsc: the DScript compiler. DScript -> dasm -> (the assembler, linked in) -> DRX or raw binary,
// with the compiled libraries it imports (#import) linked in; or a library (--lib: a .dlib).

#include "Preprocessor.hpp"
#include "Parser.hpp"
#include "CodeGen.hpp"
#include "../assembler/Assembler.hpp"
#include "../tools/DRX.hpp"
#include "../tools/DLIB.hpp"
#include "../tools/DynamicLinking.hpp"
#include <set>
#include <functional>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <ostd/utils/Hash.hpp>

using namespace dscript;

struct Options
{
	std::string input;					// the first source file
	std::vector<std::string> inputs;	// all of them (more than one: only for libraries)
	std::string output;
	std::string dasmOutput;
	std::vector<std::string> includeDirs;
	std::vector<std::string> libraryDirs;
	bool library { false };
	bool shared { false };				// --lib --shared: a shared library (.drl)
	// How a program links each library it imports (other/info/dynamic-linking.md): static (copied
	// into it) unless --dynamic (all of them), --dynamic=<names> or #import("name", dynamic);
	// --static=<names> and #import("name", static) keep one static.
	bool dynamicAll { false };
	bool wholeLibraries { false };		// --whole-libraries: static libraries linked whole (not only what is used)
	std::set<std::string> dynamicNames, staticNames;
	std::vector<std::pair<std::string, std::string>> defines;
	bool raw { false };
	bool rom { false };
	std::string load;
	std::string fixed;
	std::string header;
	bool debug { false };
	bool noInline { false };
	bool optimize { false };
	std::string disassemblyDir;
	bool verbose { false };
	std::vector<std::string> dasmArgs;	// -Wa,<options>: handed to the assembler
};

static void printHelp(void)
{
	std::cout <<
		"dsc - the DScript compiler\n\n"
		"Usage: dsc <source.ds> [options]\n"
		"       dsc --lib <source.ds>... -o <file.dlib> [options]\n\n"
		"  -o <file>                 Output file (default: the source name with .drx, or .bin with --raw)\n"
		"  -I <dir>                  Adds an include directory (#include(\"file\"): after the including file's\n"
		"                            directory)\n"
		"  -L <dir>                  Adds a library directory (#import(\"name\"): name.dlib, after the importing\n"
		"                            file's directory)\n"
		"  --lib                     Compiles a library (.dlib) instead of a program: no main; programs use it with\n"
		"                            #import(\"name\"), and it is linked into them. Can take several source files\n"
		"                            (compiled as one, in order; -o is then required)\n"
		"  --shared                  With --lib: also a shared library: <name>.dlib (what programs compile\n"
		"                            against, and link statically) and <name>.drl (what their loader loads when\n"
		"                            they link it dynamically: one copy of its code for every process, its\n"
		"                            writable globals per process). See other/info/dynamic-linking.md\n"
		"  --dynamic                 Links every imported library dynamically (its .drl, loaded with the program:\n"
		"                            a DRX2) instead of copying it into the program\n"
		"  --dynamic=<name>[,...]    Links these libraries dynamically (the others: static)\n"
		"  --static=<name>[,...]     Links these libraries statically (also with --dynamic). In the source:\n"
		"                            #import(\"name\", dynamic) / #import(\"name\", static). A dynamic library's\n"
		"                            dependencies are dynamic too (the ones not chosen: automatically)\n"
		"  --whole-libraries         Links the static libraries whole (by default only the subs and data the\n"
		"                            program reaches from what it uses are linked)\n"
		"  -D <NAME>[=<value>]       Defines NAME (like #define)\n"
		"  --emit-dasm <file>        Also writes the generated dasm source\n"
		"  --debug, -g               Keeps debug_break / debug_* instructions of low blocks\n"
		"  --no-inline               Compiles inline subs as normal subs (each call is a real call: easier to\n"
		"                            follow in the disassembly and the debugger)\n"
		"  -O                        Optimizes: scalar locals and parameters whose address is never taken are kept in\n"
		"                            registers, and copies between registers are removed (-O0: off, the default)\n"
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
		"  --save-disassembly <dir/file>  Saves the debugger disassembly, with the source lines (in a directory:\n"
		"                            named like dasm's, for ddb). A DRX's is at offset 0, with the program's id:\n"
		"                            ddb moves it to where the program is loaded (drx::load tells it)\n"
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
		else if (a == "--shared") o.shared = true;
		else if (a == "--dynamic") o.dynamicAll = true;
		else if (a == "--whole-libraries") o.wholeLibraries = true;
		else if (a.rfind("--dynamic=", 0) == 0 || a.rfind("--static=", 0) == 0)
		{
			const bool dyn = a[2] == 'd';
			std::string list = a.substr(dyn ? 10 : 9);
			size_t start = 0;
			while (start <= list.size())
			{
				size_t comma = list.find(',', start);
				if (comma == std::string::npos)
					comma = list.size();
				if (comma > start)
					(dyn ? o.dynamicNames : o.staticNames).insert(list.substr(start, comma - start));
				start = comma + 1;
			}
		}
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
		else if (a == "-O" || a == "-O1" || a == "-O2") o.optimize = true;
		else if (a == "-O0") o.optimize = false;
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
		else
			o.inputs.push_back(a);
	}
	if (o.inputs.empty())
	{
		std::cerr << "dsc: no source file (see --help)\n";
		return 2;
	}
	o.input = o.inputs[0];
	if (o.inputs.size() > 1 && !o.library)
	{
		std::cerr << "dsc: only one source file is compiled, unless it's a library (--lib) (found " << o.inputs[1] << ")\n";
		return 2;
	}
	if (o.inputs.size() > 1 && o.output.empty())
	{
		std::cerr << "dsc: a library made of several source files needs -o\n";
		return 2;
	}
	if (!o.raw && (!o.load.empty() || !o.fixed.empty() || !o.header.empty()))
	{
		std::cerr << "dsc: --load, --fixed and --header need --raw\n";
		return 2;
	}
	if (!o.header.empty() && dragon::code::Assembler::headerBytes(o.header).empty())
	{
		std::cerr << "dsc: unknown --header " << o.header << " (known: KERNEL0_BOOT)\n";
		return 2;
	}
	if (o.shared && !o.library)
	{
		std::cerr << "dsc: --shared is for libraries (--lib --shared); a program that uses shared libraries is --dynamic\n";
		return 2;
	}
	for (const auto& n : o.dynamicNames)
	{
		if (o.staticNames.count(n) > 0)
		{
			std::cerr << "dsc: " << n << " is both --dynamic and --static\n";
			return 2;
		}
	}
	if ((o.dynamicAll || !o.dynamicNames.empty() || !o.staticNames.empty()) && o.library)
	{
		std::cerr << "dsc: --dynamic / --static are for programs: a program decides how it links its libraries\n";
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
// --fixed <size>,<fill>
static void parseFixed(const std::string& fixed, size_t& size, uint8_t& fill)
{
	const size_t comma = fixed.find(',');
	size = (size_t)std::strtoul(fixed.substr(0, comma).c_str(), nullptr, 0);
	fill = (uint8_t)(comma == std::string::npos ? 0 : std::strtoul(fixed.substr(comma + 1).c_str(), nullptr, 0));
}

static void dasmAfterAssembly(const Options& o, uint32_t librariesSize)
{
	auto& args = dragon::code::Assembler::Application::args;
	if (o.dasmArgs.empty())
		return;
	if (!o.fixed.empty())
	{
		// With libraries, the assembler doesn't get .fixed (see the link step).
		size_t size = 0;
		uint8_t fill = 0;
		parseFixed(o.fixed, size, fill);
		dragon::code::Assembler::setFixedInfo((u32)size, fill);
	}
	dragon::code::Assembler::setLibrariesInfo(librariesSize);
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
	bool relocatable, Unit& out, const Options& o, bool program,
	const std::vector<Preprocessor::ImportedLibrary>& libraries = {})
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
	{
		// What the link step will append (each library aligned).
		size_t end = a.size();
		for (const auto& l : libraries)
			end = ((end + 15) & ~(size_t)15) + l.library.image.size();
		dasmAfterAssembly(o, (uint32_t)(end - a.size()));	// (a DRX's disassembly and symbols are at offset 0: add the load address)
	}
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
	std::vector<DisassemblyLine> head, code, data, labels, lines, sources, subs;

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
			else if (e.code == "{ SUBS }") part = 5;
			else (part == 0 ? code : part == 1 ? data : part == 2 ? labels : part == 3 ? lines : part == 4 ? sources : subs).push_back(e);
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
		if (!subs.empty())
		{
			out.push_back({ 0xFFFFFF04, "{ SUBS }" });
			out.insert(out.end(), subs.begin(), subs.end());
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
		parts.subs.insert(parts.subs.end(), own.subs.begin(), own.subs.end());	// (by label: moved with the labels)
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

// The subs (for the debugger's breakpoints by name): a { SUBS } part after the rest.
static void addSubTable(Unit& unit, const std::vector<std::string>& subs)
{
	if (subs.empty() || unit.disassembly.empty())
		return;
	unit.disassembly.push_back({ 0xFFFFFF04, "{ SUBS }" });
	for (const auto& s : subs)
		unit.disassembly.push_back({ 0, String(s), 0 });
}

static void saveDisassembly(const std::string& file, const std::vector<DisassemblyLine>& entries)
{
	std::filesystem::path path(file);
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path());
	if (entries.empty() || !dragon::code::Assembler::saveDisassemblyEntries(String(file), entries))
		std::cerr << "dsc: cannot save the disassembly to " << file << "\n";
}

// ================================== Unused code ===================================
// A library's pieces (DLIB::pieces): where its image can be cut. 0 (the entry jump), every data
// symbol that isn't inside another one (a global, a string, a constant array), and the code
// labels the code generator says are safe (CodeGen::pieceStarts: __ds_lib_init, and the subs
// that follow a ret).
static std::vector<uint32_t> libraryPieces(const Unit& unit, const std::vector<std::string>& starts)
{
	std::set<uint32_t> cuts { 0 };
	DisassemblyParts parts;
	parts.add(unit.disassembly);
	std::vector<std::pair<uint32_t, uint32_t>> data;
	for (const auto& e : parts.data)
		data.push_back({ e.addr, e.size });
	std::sort(data.begin(), data.end());
	uint32_t end = 0;
	for (const auto& d : data)
	{
		if (d.first >= end)
			cuts.insert(d.first);
		end = std::max(end, d.first + d.second);
	}
	std::map<std::string, uint32_t> address(unit.symbols.begin(), unit.symbols.end());
	for (const auto& s : starts)
	{
		auto it = address.find(s);
		if (it != address.end())
			cuts.insert(it->second);
	}
	std::vector<uint32_t> out;
	for (uint32_t c : cuts)
	{
		if (c < unit.image.size())
			out.push_back(c);
	}
	return out;
}

static size_t pieceOf(const std::vector<uint32_t>& pieces, uint32_t offset)
{
	return (size_t)(std::upper_bound(pieces.begin(), pieces.end(), offset) - pieces.begin()) - 1;
}

// The library with only its kept pieces (in their order): the relocations (their places and the
// addresses in them), imports, exports, init and disassembly moved to the new offsets. remap
// gets old offset -> new offset (for kept pieces). false: an address points into a piece that
// was left out (the reachability missed something: a bug).
static bool compactLibrary(const dragon::dlib::Library& lib, const std::vector<char>& kept, dragon::dlib::Library& out,
	std::function<uint32_t(uint32_t)>& remap, std::string& message)
{
	const std::vector<uint32_t>& pieces = lib.pieces;
	std::vector<int64_t> delta(pieces.size(), 0);
	out = lib;
	out.image.clear();
	for (size_t p = 0; p < pieces.size(); p++)
	{
		if (!kept[p])
			continue;
		const uint32_t start = pieces[p], end = (p + 1 < pieces.size()) ? pieces[p + 1] : (uint32_t)lib.image.size();
		delta[p] = (int64_t)out.image.size() - start;
		out.image.insert(out.image.end(), lib.image.begin() + start, lib.image.begin() + end);
	}
	remap = [pieces, delta](uint32_t off) { return (uint32_t)((int64_t)off + delta[pieceOf(pieces, off)]); };
	auto l_kept = [&](uint32_t off) { return off < lib.image.size() && kept[pieceOf(pieces, off)]; };
	// The disassembly: code entries (by their old address) for rewriting the addresses in them.
	std::vector<std::pair<uint32_t, size_t>> codeEntries;
	{
		int part = 0;
		for (size_t i = 0; i < lib.debug.size(); i++)
		{
			const std::string& t = lib.debug[i].text;
			if (t == "{ DATA }") part = 1;
			else if (t == "{ LABELS }") part = 2;
			else if (t == "{ LINES }") part = 3;
			else if (t == "{ SOURCES }") part = 4;
			else if (t == "{ SUBS }") part = 5;
			else if (part == 0)
				codeEntries.push_back({ lib.debug[i].address, i });
		}
		std::sort(codeEntries.begin(), codeEntries.end());
	}
	std::vector<std::string> texts;
	for (const auto& e : lib.debug)
		texts.push_back(e.text);
	out.relocations.clear();
	for (uint32_t r : lib.relocations)
	{
		if (!l_kept(r))
			continue;
		const uint32_t value = dragon::drx::readU32(&lib.image[r]);
		if (!l_kept(value))
		{
			message = "an address (at 0x" + hexWord(r).substr(2) + ") points into a piece that was left out";
			return false;
		}
		const uint32_t moved = remap(value);
		dragon::drx::writeU32(&out.image[remap(r)], moved);
		out.relocations.push_back(remap(r));
		auto it = std::upper_bound(codeEntries.begin(), codeEntries.end(), std::make_pair(r, (size_t)-1));
		if (moved != value && it != codeEntries.begin())
		{
			std::string& text = texts[(--it)->second];
			const size_t at = text.find(hexWord(value));
			if (at != std::string::npos)
				text.replace(at, 10, hexWord(moved));
		}
	}
	out.imports.clear();
	for (const auto& i : lib.imports)
	{
		if (l_kept(i.offset))
			out.imports.push_back({ remap(i.offset), i.symbol });
	}
	out.exports.clear();
	for (const auto& e : lib.exports)
	{
		if (l_kept(e.offset))
			out.exports.push_back({ e.symbol, remap(e.offset) });
	}
	out.init = remap(lib.init);
	// The disassembly: what is left, at its new addresses (sources and subs are no addresses;
	// a sub left out disappears from the sub table).
	out.debug.clear();
	std::set<std::string> labels;
	int part = 0;
	for (size_t i = 0; i < lib.debug.size(); i++)
	{
		dragon::dlib::DebugEntry e = lib.debug[i];
		e.text = texts[i];
		if (e.text == "{ DATA }" || e.text == "{ LABELS }" || e.text == "{ LINES }" || e.text == "{ SOURCES }" || e.text == "{ SUBS }")
		{
			part = (e.text == "{ DATA }") ? 1 : (e.text == "{ LABELS }") ? 2 : (e.text == "{ LINES }") ? 3 : (e.text == "{ SOURCES }") ? 4 : 5;
			out.debug.push_back(e);
			continue;
		}
		if (part <= 3)
		{
			if (!l_kept(e.address))
				continue;
			e.address = remap(e.address);
			if (part == 2)
				labels.insert(e.text.size() > 1 && e.text[0] == '$' ? e.text.substr(1) : e.text);
		}
		else if (part == 5)
		{
			const size_t tab = e.text.find('\t');
			const size_t tab2 = (tab == std::string::npos) ? std::string::npos : e.text.find('\t', tab + 1);
			if (tab2 != std::string::npos && labels.count(e.text.substr(tab + 1, tab2 - tab - 1)) == 0)
				continue;
		}
		out.debug.push_back(e);
	}
	out.pieces.clear();
	return true;
}

// ================================== Dynamic linking ===================================
// (other/info/dynamic-linking.md)
typedef std::map<std::string, std::pair<size_t, uint32_t>> SymbolMap;

static std::string libraryName(const std::string& path) { return std::filesystem::path(path).stem().string(); }

// Saves the disassembly where the options say (-Wa,--save-disassembly and --save-disassembly).
static void saveDisassemblies(const Options& o, const std::vector<DisassemblyLine>& entries)
{
	if (dragon::code::Assembler::Application::args.save_disassembly && !o.dasmArgs.empty())
		saveDisassembly(dragon::code::Assembler::Application::args.disassembly_file_path.cpp_str(), entries);
	if (o.disassemblyDir.empty())
		return;
	std::string file = o.disassemblyDir;
	if (file.back() == '/' || std::filesystem::is_directory(file))
	{
		std::filesystem::create_directories(file);
		file = (std::filesystem::path(file) / (ostd::Hash::md5(String(o.output)).cpp_str() + ".dds")).string();
	}
	saveDisassembly(file, entries);
}

// The disassembly of a unit that is linked by its loader: the import words shown as $symbol
// (assembled: the image as assembled, with the made-up addresses), its id (the debugger moves
// it to where it is loaded, as a DRX's).
static std::vector<DisassemblyLine> loadedDisassembly(const Unit& unit, const std::vector<uint8_t>& assembled, uint64_t id)
{
	std::vector<DisassemblyLine> entries = unit.disassembly;
	if (entries.empty())
		return entries;
	DisassemblyParts parts;
	parts.add(entries);
	for (const auto& i : unit.imports)
		parts.patch(i.offset, hexWord(dragon::drx::readU32(&assembled[i.offset])), "$" + i.symbol, 0);
	entries = parts.entries();
	auto at = entries.begin();
	while (at != entries.end() && (at->code.startsWith("{ NAME } ") || at->code.startsWith("{ VERSION } ")))
		at++;
	entries.insert(at, { 0, String("{ DRX } ").add(dragon::drx::idString(id)), 0 });
	return entries;
}

static bool hasPrefix(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// dsc --lib --shared: a DRL. The code generator (CodeGenOptions::shared) reaches the writable
// globals through the process's table; here the made-up symbols it uses become what the loader
// fills in (imports of kinds SelfSlot, LibTable, SlotOf, OffsetOf) or are resolved now
// ($__dsrw__<g>: g's offset in the image), and the relocations are sorted by where the word is
// (text: shared, rw: per process) and what it points to.
static int linkShared(const Options& o, const std::vector<std::string>& dasm, const std::set<std::string>& importable, const std::string& directory,
	const SymbolMap& symbols, const std::vector<Preprocessor::ImportedLibrary>& libraries, const std::vector<std::string>& exported,
	const std::string& interface, const std::vector<std::string>& subTable)
{
	using namespace dragon::dyn;
	// The made-up symbols of the generated code: imports too.
	std::set<std::string> names = importable;
	for (const auto& line : dasm)
	{
		for (size_t i = line.find("$__ds"); i != std::string::npos; i = line.find("$__ds", i + 1))
		{
			size_t j = i + 1;
			while (j < line.size() && (std::isalnum((unsigned char)line[j]) || line[j] == '_'))
				j++;
			const std::string name = line.substr(i + 1, j - i - 1);
			if (hasPrefix(name, "__dsrw__") || hasPrefix(name, "__dsslot__") || hasPrefix(name, "__dsoff__") || name == "__ds_slot" || name == "__ds_libtable")
				names.insert(name);
		}
	}
	Unit unit;
	if (!assembleUnit(dasm, names, directory, 0, true, unit, o, true))
		return 1;
	addSubTable(unit, subTable);
	const std::vector<uint8_t> assembled = unit.image;
	std::map<std::string, uint32_t> address(unit.symbols.begin(), unit.symbols.end());
	if (address.count("__ds_rw_start") == 0 || address.count("__ds_rw_end") == 0)
	{
		std::cerr << "dsc: internal error: no rw markers in a shared library\n";
		return 1;
	}
	Library lib;
	lib.name = libraryName(o.output);
	lib.abi = abiOf(interface);
	lib.image = unit.image;
	// (After the start marker's byte: a library without writable globals has an empty rw part.)
	lib.rwStart = address["__ds_rw_start"] + 1;
	lib.rwSize = address["__ds_rw_end"] - lib.rwStart;
	auto l_symbolAt = [&](uint32_t a) -> std::string {
		std::string best = "?";
		for (const auto& s : unit.symbols)
		{
			if (s.second == a && s.first != "__ds_rw_end")
				return s.first;
		}
		return best;
	};
	auto l_library = [&](const std::string& symbol, uint16_t& out) -> bool {
		auto it = symbols.find(symbol);
		if (it == symbols.end())
		{
			std::cerr << "dsc: " << o.input << " uses " << symbol << ", which none of the imported libraries has (a library missing?)\n";
			return false;
		}
		out = (uint16_t)it->second.first;
		return true;
	};
	for (const auto& i : unit.imports)
	{
		Import imp;
		imp.offset = i.offset;
		const std::string& name = i.symbol;
		if (hasPrefix(name, "__dsrw__"))
		{
			const std::string own = name.substr(8);
			auto it = address.find(own);
			if (it == address.end() || !lib.inRw(it->second))
			{
				std::cerr << "dsc: internal error: " << own << " is not in the library's writable data\n";
				return 1;
			}
			dragon::drx::writeU32(&lib.image[i.offset], it->second);
			continue;
		}
		if (name == "__ds_slot")
			imp.kind = ImportKind::SelfSlot;
		else if (name == "__ds_libtable")
			imp.kind = ImportKind::LibTable;
		else if (hasPrefix(name, "__dsslot__") || hasPrefix(name, "__dsoff__"))
		{
			imp.kind = hasPrefix(name, "__dsslot__") ? ImportKind::SlotOf : ImportKind::OffsetOf;
			imp.symbol = name.substr(hasPrefix(name, "__dsslot__") ? 10 : 9);
			if (!l_library(imp.symbol, imp.library))
				return 1;
		}
		else
		{
			imp.kind = ImportKind::Absolute;
			imp.symbol = name;
			if (!l_library(name, imp.library))
				return 1;
		}
		if (lib.inRw(i.offset) && imp.kind != ImportKind::Absolute)
		{
			std::cerr << "dsc: internal error: a " << kindName(imp.kind) << " import in the writable data\n";
			return 1;
		}
		lib.imports.push_back(imp);
	}
	for (uint32_t r : unit.relocations)
	{
		const uint32_t target = dragon::drx::readU32(&lib.image[r]);
		const bool toRw = lib.inRw(target);
		if (lib.inRw(r))
			(toRw ? lib.dataDataRelocations : lib.dataTextRelocations).push_back(r);
		else if (toRw)
		{
			std::cerr << "dsc: " << o.input << ": the shared code uses the address of the writable global " << l_symbolAt(target)
					  << " directly (at offset 0x" << std::hex << r << std::dec << "): in a shared library every process has its own copy of it. "
					  << "A low block or a constant array that names a global? (use the variable in DScript, or make the data const)\n";
			return 1;
		}
		else
			lib.textRelocations.push_back(r);
	}
	const std::set<std::string> wanted(exported.begin(), exported.end());
	for (const auto& sym : unit.symbols)
	{
		if (sym.first == "__ds_lib_init")
			lib.init = sym.second;
		else if (wanted.count(sym.first) > 0)
			lib.exports.push_back({ sym.first, sym.second });
	}
	std::sort(lib.exports.begin(), lib.exports.end(), [](const auto& x, const auto& y) { return x.symbol < y.symbol; });
	for (const auto& l : libraries)
		lib.dependencies.push_back({ libraryName(l.path), abiOf(l.library.interface) });
	lib.id = computeId(lib);
	saveDisassemblies(o, loadedDisassembly(unit, assembled, lib.id));
	if (!writeFile(o.output, write(lib)))
	{
		std::cerr << "dsc: cannot write " << o.output << "\n";
		return 1;
	}
	if (o.verbose)
		std::cout << o.output << ": shared library " << lib.name << ", " << lib.image.size() << " bytes (" << lib.rwSize << " writable), "
				  << lib.exports.size() << " symbols, " << lib.textRelocations.size() << " + " << lib.dataTextRelocations.size() + lib.dataDataRelocations.size()
				  << " relocations, " << lib.imports.size() << " imports, ABI " << dragon::drx::idString(lib.abi).substr(10) << ", id "
				  << dragon::drx::idString(lib.id) << "\n";
	return 0;
}

// Compiles and links one output (o.output).
static int build(const Options& o)
{
	std::vector<std::string> dasm;
	std::vector<std::string> subTable;	// the disassembly's { SUBS } (CodeGen::subTable)
	std::vector<std::string> pieceStarts;	// CodeGen::pieceStarts (a library's pieces)
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
		std::vector<Token> tokens = pp.processFiles(o.inputs);
		Parser parser(tokens);
		Node* program = parser.parseProgram();
		CodeGenOptions cg;
		cg.raw = o.raw;
		cg.rom = o.rom;
		cg.noInline = o.noInline;
		cg.optimize = o.optimize;
		cg.noAsserts = pp.isDefined("NDEBUG");
		if (o.raw)
		{
			const std::string defaultLoad = (o.rom ? "0x00000000" : "0x01010000");
			cg.directives.push_back(".load " + (o.load.empty() ? defaultLoad : o.load));
			// With libraries, the padding is done after they are linked.
			if (!o.fixed.empty() && pp.imports().empty())
				cg.directives.push_back(".fixed " + o.fixed);
			// The header (--header) is added by the link step, in front of everything: the
			// addresses (the program's and the libraries') are those of the code after it.
		}
		cg.library = o.library;
		cg.shared = o.shared;
		cg.libraries = o.library ? 0 : (int)pp.imports().size();
		CodeGen codegen(cg);
		dasm = codegen.generate(program);
		subTable = codegen.subTable();
		pieceStarts = codegen.pieceStarts();
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
	dragon::code::Assembler::cpuExtensions = { "extmov", "extalu", "extfpu" };
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

	if (o.shared)
		return linkShared(o, dasm, importable, directory, symbols, libraries, exported, interface, subTable);

	if (o.library)
	{
		Unit unit;
		if (!assembleUnit(dasm, importable, directory, 0, true, unit, o, true))
			return 1;
		addSubTable(unit, subTable);
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
		// The sources are recorded relative to the library, so that they are found wherever
		// the library and its sources are, and from whatever directory it is imported.
		std::error_code ec;
		const std::filesystem::path libDir = std::filesystem::weakly_canonical(std::filesystem::absolute(o.output), ec).parent_path();
		std::map<std::string, std::string> relative;
		for (auto& l : interfaceLines)
		{
			if (l.file.empty() || l.file[0] == '<')
				continue;
			auto it = relative.find(l.file);
			if (it == relative.end())
			{
				const std::string rel = std::filesystem::weakly_canonical(std::filesystem::absolute(l.file), ec).lexically_relative(libDir).string();
				it = relative.emplace(l.file, rel.empty() ? l.file : rel).first;
			}
			l.file = it->second;
		}
		lib.lines = interfaceLines;
		for (const auto& e : unit.disassembly)
		{
			if (!e.code.startsWith("{ NAME } ") && !e.code.startsWith("{ VERSION } "))
				lib.debug.push_back({ e.addr, e.size, e.code.cpp_str() });
		}
		lib.pieces = libraryPieces(unit, pieceStarts);
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

	// How each library is linked: static (copied after the program) or dynamic (its .drl, loaded
	// with the program: a DRX2). A dynamic library's dependencies are dynamic too: its .drl uses
	// their shared copies, so a static copy in the program would be a second one (a second set of
	// globals).
	std::vector<bool> dynamicLib(libraries.size(), false), chosenStatic(libraries.size(), false);
	for (size_t k = 0; k < libraries.size(); k++)
	{
		const std::string name = libraryName(libraries[k].path);
		const std::string& mode = libraries[k].mode;
		if (o.staticNames.count(name) > 0)
			chosenStatic[k] = true;
		else if (o.dynamicNames.count(name) > 0)
			dynamicLib[k] = true;
		else if (mode == "static")
			chosenStatic[k] = true;
		else
			dynamicLib[k] = (mode == "dynamic" || o.dynamicAll);
	}
	for (size_t k = libraries.size(); k-- > 0; )		// (dependencies come first: they are seen after)
	{
		if (!dynamicLib[k])
			continue;
		for (size_t d : libraries[k].deps)
		{
			if (chosenStatic[d])
			{
				std::cerr << "dsc: " << libraryName(libraries[d].path) << " is linked statically, but " << libraryName(libraries[k].path)
						  << ", linked dynamically, needs it: its shared library uses the shared " << libraryName(libraries[d].path)
						  << " (a static copy would be a second one, with its own globals). Link " << libraryName(libraries[d].path)
						  << " dynamically too, or " << libraryName(libraries[k].path) << " statically\n";
				return 1;
			}
			dynamicLib[d] = true;
		}
	}
	std::vector<Preprocessor::ImportedLibrary> staticLibs;	// (in the program, in this order)
	std::vector<uint32_t> dynamicIndex(libraries.size(), 0);	// index in the DRX2's library table
	dragon::dyn::Program dynamic;
	for (size_t k = 0; k < libraries.size(); k++)
	{
		if (dynamicLib[k])
		{
			dynamicIndex[k] = (uint32_t)dynamic.libraries.size();
			dynamic.libraries.push_back({ libraryName(libraries[k].path), dragon::dyn::abiOf(libraries[k].library.interface) });
		}
		else
			staticLibs.push_back(libraries[k]);
	}
	if (o.raw && !dynamic.libraries.empty())
	{
		std::cerr << "dsc: a raw program (--raw, --rom) can't link libraries dynamically (" << dynamic.libraries[0].name << "): it has no loader\n";
		return 1;
	}

	// The program, then each static library (aligned), connected: base is where the image runs
	// (raw: the load address; DRX: 0, everything relocated by the loader).
	uint32_t base = 0;
	if (o.raw)
		base = (uint32_t)std::strtoul((o.load.empty() ? (o.rom ? "0x00000000" : "0x01010000") : o.load).c_str(), nullptr, 0);
	Unit unit;
	if (!assembleUnit(dasm, importable, directory, 0, !o.raw, unit, o, true, staticLibs))
		return 1;
	addSubTable(unit, subTable);

	// Unused code: of each static library with pieces (DLIB::pieces), only the pieces reachable
	// from what the program uses: its imports, every library's initialization (__ds_start calls
	// them), then whatever a kept piece's relocations point to (same library) and its imports
	// name (other static libraries). The libraries without pieces are linked whole.
	std::vector<Preprocessor::ImportedLibrary> linked = libraries;
	if (!o.wholeLibraries)
	{
		std::vector<std::vector<char>> kept(libraries.size());
		std::vector<std::pair<size_t, size_t>> work;
		auto l_reach = [&](const std::string& symbol) {
			auto it = symbols.find(symbol);
			if (it == symbols.end() || dynamicLib[it->second.first] || libraries[it->second.first].library.pieces.empty())
				return;
			const auto& pieces = libraries[it->second.first].library.pieces;
			if (it->second.second < libraries[it->second.first].library.image.size())
				work.push_back({ it->second.first, pieceOf(pieces, it->second.second) });
		};
		for (size_t k = 0; k < libraries.size(); k++)
		{
			if (dynamicLib[k])
				continue;
			const auto& lib = libraries[k].library;
			kept[k].assign(lib.pieces.size(), 0);
			l_reach("__dlib_init_" + std::to_string(k));
			if (lib.pieces.empty())
			{
				for (const auto& i : lib.imports)		// (linked whole: everything it uses is reached)
					l_reach(i.symbol);
			}
		}
		for (const auto& i : unit.imports)
			l_reach(i.symbol);
		while (!work.empty())
		{
			const auto [k, p] = work.back();
			work.pop_back();
			if (kept[k][p])
				continue;
			kept[k][p] = 1;
			const auto& lib = libraries[k].library;
			const uint32_t start = lib.pieces[p], end = (p + 1 < lib.pieces.size()) ? lib.pieces[p + 1] : (uint32_t)lib.image.size();
			for (uint32_t r : lib.relocations)
			{
				if (r < start || r >= end)
					continue;
				const uint32_t target = dragon::drx::readU32(&lib.image[r]);
				if (target < lib.image.size())
					work.push_back({ k, pieceOf(lib.pieces, target) });
			}
			for (const auto& i : lib.imports)
			{
				if (i.offset >= start && i.offset < end)
					l_reach(i.symbol);
			}
		}
		for (size_t k = 0; k < libraries.size(); k++)
		{
			const auto& lib = libraries[k].library;
			if (dynamicLib[k] || lib.pieces.empty())
				continue;
			std::function<uint32_t(uint32_t)> remap;
			std::string message;
			if (!compactLibrary(lib, kept[k], linked[k].library, remap, message))
			{
				std::cerr << "dsc: internal error while leaving out the unused code of " << libraries[k].path << ": " << message << " (--whole-libraries avoids it)\n";
				return 1;
			}
			// Its symbols, at their new offsets (the ones left out: never used).
			for (auto& sym : symbols)
			{
				if (sym.second.first != k)
					continue;
				const uint32_t off = sym.second.second;
				sym.second.second = (off < lib.image.size() && kept[k][pieceOf(lib.pieces, off)]) ? remap(off) : UINT32_MAX;
			}
			if (o.verbose)
				std::cout << libraryName(libraries[k].path) << ": " << linked[k].library.image.size() << " of " << lib.image.size() << " bytes linked\n";
		}
		staticLibs.clear();
		for (size_t k = 0; k < libraries.size(); k++)
		{
			if (!dynamicLib[k])
				staticLibs.push_back(linked[k]);
		}
	}
	std::vector<uint8_t> image = unit.image;
	std::vector<uint32_t> relocations = unit.relocations;
	std::vector<uint32_t> offsets(libraries.size(), 0), staticOffsets;
	for (size_t k = 0; k < libraries.size(); k++)
	{
		if (dynamicLib[k])
			continue;
		image.resize((image.size() + 15) & ~(size_t)15, 0);
		offsets[k] = (uint32_t)image.size();
		staticOffsets.push_back(offsets[k]);
		image.insert(image.end(), linked[k].library.image.begin(), linked[k].library.image.end());
	}
	auto l_add = [&](uint32_t at, uint32_t value) { dragon::drx::writeU32(&image[at], dragon::drx::readU32(&image[at]) + value); };
	auto l_resolve = [&](const std::string& symbol, const std::string& user, uint32_t at) -> bool {
		auto it = symbols.find(symbol);
		if (it == symbols.end())
		{
			std::cerr << "dsc: " << user << " uses " << symbol << ", which none of the imported libraries has (a library missing?)\n";
			return false;
		}
		const size_t k = it->second.first;
		if (dynamicLib[k])
		{
			// Filled in by the loader (__dlib_init_<n>: the library's initialization).
			const bool init = symbol.rfind("__dlib_init_", 0) == 0;
			dynamic.imports.push_back({ at, (uint16_t)dynamicIndex[k], init ? dragon::dyn::ImportKind::Init : dragon::dyn::ImportKind::Absolute, init ? "" : symbol });
			dragon::drx::writeU32(&image[at], 0);
			return true;
		}
		if (it->second.second == UINT32_MAX)
		{
			std::cerr << "dsc: internal error: " << symbol << " was left out of " << libraries[k].path << " (--whole-libraries avoids it)\n";
			return false;
		}
		dragon::drx::writeU32(&image[at], base + offsets[k] + it->second.second);
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
		if (dynamicLib[k])
			continue;
		const auto& lib = linked[k].library;
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
	// A DRX: its id (in the header and in the disassembly, which is at offset 0: the debugger
	// moves it to where the program is loaded).
	dragon::drx::Program program;
	if (!o.raw)
	{
		program.image = image;
		program.entry = 0;
		program.relocations = relocations;
		for (const auto& i : dynamic.imports)	// (a DRX2: what the loader fills in is part of the build)
			program.relocations.push_back(i.offset ^ ((uint32_t)i.library << 24) ^ ((uint32_t)i.kind << 28));
		program.id = dragon::drx::computeId(program);
		program.relocations = relocations;
	}
	auto l_disassembly = [&]() {
		std::vector<DisassemblyLine> entries = linkedDisassembly(unit, base, staticLibs, staticOffsets);
		if (program.id != 0)
		{
			auto at = entries.begin();
			while (at != entries.end() && (at->code.startsWith("{ NAME } ") || at->code.startsWith("{ VERSION } ")))
				at++;
			entries.insert(at, { 0, String("{ DRX } ").add(dragon::drx::idString(program.id)), 0 });
		}
		return entries;
	};
	// The disassembly for the debugger (-Wa,--save-disassembly, --save-disassembly).
	auto l_saveDisassembly = [&]() { saveDisassemblies(o, l_disassembly()); };

	if (o.raw)
	{
		if (!o.fixed.empty() && !libraries.empty())
		{
			size_t size = 0;
			uint8_t fill = 0;
			parseFixed(o.fixed, size, fill);
			if (image.size() > size)
			{
				std::cerr << "dsc: the program and its libraries (" << image.size() << " bytes) are bigger than --fixed " << size << "\n";
				return 1;
			}
			image.resize(size, fill);
		}
		if (!o.header.empty())
		{
			const ostd::ByteStream header = dragon::code::Assembler::headerBytes(o.header);
			image.insert(image.begin(), header.begin(), header.end());
		}
		if (!writeFile(o.output, image))
		{
			std::cerr << "dsc: cannot write " << o.output << "\n";
			return 1;
		}
		l_saveDisassembly();
		if (o.verbose)
			std::cout << o.output << ": " << image.size() << " bytes (raw" << (libraries.empty() ? "" : ", " + std::to_string(libraries.size()) + " libraries") << ")\n";
		return 0;
	}

	if (image.size() >= 0x01000000)
	{
		std::cerr << "dsc: the program is too big (16 MiB at most)\n";
		return 1;
	}
	// Without dynamic libraries: a DRX (DRX1, as always); with: a DRX2.
	std::vector<uint8_t> file;
	if (dynamic.libraries.empty())
		file = dragon::drx::write(program);
	else
	{
		dynamic.image = image;
		dynamic.entry = 0;
		dynamic.relocations = relocations;
		dynamic.id = program.id;
		file = dragon::dyn::write(dynamic);
	}
	if (!writeFile(o.output, file))
	{
		std::cerr << "dsc: cannot write " << o.output << "\n";
		return 1;
	}
	l_saveDisassembly();
	if (o.verbose)
	{
		std::cout << o.output << ": " << image.size() << " bytes, " << relocations.size() << " relocations";
		if (!staticLibs.empty())
			std::cout << ", " << staticLibs.size() << " static libraries";
		if (!dynamic.libraries.empty())
			std::cout << ", " << dynamic.libraries.size() << " shared libraries (DRX2, " << dynamic.imports.size() << " imports)";
		std::cout << ", id " << dragon::drx::idString(program.id) << "\n";
	}
	return 0;
}

int main(int argc, char** argv)
{
	Options o;
	int r = parseArgs(argc, argv, o);
	if (r != 0)
		return r == 1 ? 0 : r;
	if (!o.shared)
		return build(o);
	// --lib --shared: both the .dlib (static) and the .drl (shared), from the same sources.
	std::filesystem::path stem(o.output);
	if (stem.extension() == ".dlib" || stem.extension() == ".drl")
		stem.replace_extension();
	Options staticLib = o;
	staticLib.shared = false;
	staticLib.output = stem.string() + ".dlib";
	staticLib.disassemblyDir.clear();		// (the .dlib keeps its disassembly in itself)
	r = build(staticLib);
	if (r != 0)
		return r;
	Options sharedLib = o;
	sharedLib.output = stem.string() + ".drl";
	return build(sharedLib);
}
