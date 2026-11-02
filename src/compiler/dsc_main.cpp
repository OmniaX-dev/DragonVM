// dsc: the DScript compiler. DScript -> dasm -> (the assembler, linked in) -> DRX or raw binary.

#include "Preprocessor.hpp"
#include "Parser.hpp"
#include "CodeGen.hpp"
#include "../assembler/Assembler.hpp"
#include "../tools/DRX.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

using namespace dscript;

struct Options
{
	std::string input;
	std::string output;
	std::string dasmOutput;
	std::vector<std::string> includeDirs;
	std::vector<std::pair<std::string, std::string>> defines;
	bool raw { false };
	std::string load;
	std::string fixed;
	std::string header;
	bool debug { false };
	std::string disassemblyDir;
	bool verbose { false };
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
		"  --verbose                 Prints the program size and relocations\n\n"
		"Output formats:\n"
		"  (default)                 DRX: relocatable executable, loaded anywhere by a loader (an OS):\n"
		"                            the program is called and returns when main returns (RV = main's value)\n"
		"  --raw                     Bare metal binary at a fixed address; ends with hlt after main\n"
		"    --load <address>        .load: the address the binary runs at (default 0x01010000, the RAM)\n"
		"    --fixed <size>,<fill>   .fixed: pads the binary to exactly <size> bytes\n"
		"    --header <name>         .header: adds a header (e.g. KERNEL0_BOOT)\n"
		"    --save-disassembly <dir>  Saves the debugger disassembly (as dasm does)\n"
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
		else if (a == "--verbose") o.verbose = true;
		else if (a == "--raw") o.raw = true;
		else if (a == "--load") o.load = l_value(a);
		else if (a == "--fixed") o.fixed = l_value(a);
		else if (a == "--header") o.header = l_value(a);
		else if (a == "--save-disassembly") o.disassemblyDir = l_value(a);
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
		for (const auto& d : o.defines)
			pp.define(d.first, d.second);
		std::vector<Token> tokens = pp.processFile(o.input);
		Parser parser(tokens);
		Node* program = parser.parseProgram();
		CodeGenOptions cg;
		cg.raw = o.raw;
		if (o.raw)
		{
			cg.directives.push_back(".load " + (o.load.empty() ? std::string("0x01010000") : o.load));
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
	dragon::code::Assembler::cpuExtensions = { "extmov", "extalu" };
	dragon::code::Assembler::debugMode = o.debug;
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
		if (o.verbose)
			std::cout << o.output << ": " << bin.size() << " bytes (raw)\n";
		return 0;
	}

	// Relocatable: assembled at two addresses, the 32-bit words that differ by exactly the
	// difference are addresses.
	const uint32_t delta = 0x01000000;
	std::vector<uint8_t> a = assemble(lines, directory, 0);
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
