#include "Assembler.hpp"
#include <ostd/Utils.hpp>
#include <ostd/IOHandlers.hpp>

ostd::ConsoleOutputHandler out;


struct tCommandLineArgs
{
	ostd::String source_file_path = "";
	ostd::String dest_file_path = "";
	bool save_disassembly = false;
	bool verbose = false;
	ostd::String disassembly_file_path = "";
};

int main(int argc, char** argv)
{
	tCommandLineArgs args;
	if (argc < 4)
	{
		out.fg("red").p("Error: too few arguments.").nl();
		out.fg("red").p("  Usage: ./dasm <source> -o <destination> [...options...]").reset().nl();
		return 1;
	}
	else
	{
		args.source_file_path = argv[1];
		for (int32_t i = 2; i < argc; i++)
		{
			ostd::String edit(argv[i]);
			if (edit == "-o")
			{
				if (i == argc - 1)
					break; //TODO: Warning
				i++;
				args.dest_file_path = argv[i];
			}
			else if (edit == "--save-disassembly")
			{
				if (i == argc - 1)
					break; //TODO: Warning
				i++;
				args.disassembly_file_path = argv[i];
				args.save_disassembly = true;
			}
			else if (edit == "--verbose")
				args.verbose = true;
		}
	}

	dragon::code::Assembler::assembleToFile(args.source_file_path, args.dest_file_path);
	if (args.verbose)
		dragon::code::Assembler::tempPrint();

	if (args.save_disassembly)
		dragon::code::Assembler::saveDisassemblyToFile(args.disassembly_file_path);

	return 0;
}