// dsc: the DScript compiler. DScript -> dasm -> (the assembler, linked in) -> DRX or raw binary.

#include "Preprocessor.hpp"
#include "Parser.hpp"
#include "CodeGen.hpp"
#include "../assembler/Assembler.hpp"
#include "../tools/DRX.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>

using namespace dscript;

struct Options
{
	std::string input;
	std::string output;
	std::string dasmOutput;
	std::vector<std::string> includeDirs;
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
		"    --save-disassembly <dir>  Saves the debugger disassembly (as dasm does)\n"
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
	if (o.output.empty())
		o.output = std::filesystem::path(o.input).replace_extension(o.raw ? ".bin" : ".drx").string();
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
	if (args.save_disassembly)
	{
		std::filesystem::path file(args.disassembly_file_path.cpp_str());
		if (file.has_parent_path())
			std::filesystem::create_directories(file.parent_path());
		if (!dragon::code::Assembler::saveDisassemblyToFile(args.disassembly_file_path, std::filesystem::path(o.output).filename().string()))
			std::cerr << "dsc: cannot save the disassembly to " << args.disassembly_file_path.cpp_str() << "\n";
	}
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

int main(int argc, char** argv)
{
	Options o;
	int r = parseArgs(argc, argv, o);
	if (r != 0)
		return r == 1 ? 0 : r;

	std::vector<std::string> dasm;
	try
	{
		Preprocessor pp(o.includeDirs);
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
			if (!o.fixed.empty())
				cg.directives.push_back(".fixed " + o.fixed);
			if (!o.header.empty())
				cg.directives.push_back(".header " + o.header);
		}
		CodeGen codegen(cg);
		dasm = codegen.generate(program);
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

	if (o.raw)
	{
		std::vector<uint8_t> bin = assemble(lines, directory, 0);
		if (bin.empty())
		{
			std::cerr << "dsc: the assembler failed\n";
			return 1;
		}
		if (!writeFile(o.output, bin))
		{
			std::cerr << "dsc: cannot write " << o.output << "\n";
			return 1;
		}
		if (!o.disassemblyDir.empty())
			dragon::code::Assembler::saveDisassemblyToFile(o.disassemblyDir, std::filesystem::path(o.output).filename().string());
		dasmAfterAssembly(o);
		if (o.verbose)
			std::cout << o.output << ": " << bin.size() << " bytes (raw)\n";
		return 0;
	}

	// Relocatable: assembled at two addresses, the 32-bit words that differ by exactly the
	// difference are addresses.
	const uint32_t delta = 0x01000000;
	std::vector<uint8_t> a = assemble(lines, directory, 0);
	dasmAfterAssembly(o);	// (a DRX's disassembly and symbols are at offset 0: add the load address)
	std::vector<uint8_t> b = assemble(lines, directory, delta);
	if (a.empty() || a.size() != b.size())
	{
		std::cerr << "dsc: the assembler failed\n";
		return 1;
	}
	if (a.size() >= delta)
	{
		std::cerr << "dsc: the program is too big (16 MiB at most)\n";
		return 1;
	}
	dragon::drx::Program program;
	program.image = a;
	program.entry = 0;
	for (size_t i = 0; i < a.size(); i++)
	{
		if (a[i] == b[i])
			continue;
		if (i + 4 > a.size() || dragon::drx::readU32(&a[i]) + delta != dragon::drx::readU32(&b[i]))
		{
			std::cerr << "dsc: an address is used in a field that is not 32 bits wide, at offset 0x" << std::hex << i << std::dec
					  << " (in a low block?): it cannot be relocated\n";
			return 1;
		}
		program.relocations.push_back((uint32_t)i);
		i += 3;
	}
	if (!writeFile(o.output, dragon::drx::write(program)))
	{
		std::cerr << "dsc: cannot write " << o.output << "\n";
		return 1;
	}
	if (o.verbose)
		std::cout << o.output << ": " << a.size() << " bytes, " << program.relocations.size() << " relocations\n";
	return 0;
}
