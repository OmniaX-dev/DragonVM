#include "Assembler.hpp"

namespace dragon
{
	namespace code
	{
		int32_t Assembler::Application::loadArguments(int argc, char** argv)
		{
			if (argc < 2)
			{
				out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
				out.fg(ostd::ConsoleColors::Red).p("Use the --help option for more info.").reset().nl();
				return RETURN_VAL_TOO_FEW_ARGUMENTS;
			}
			else
			{
				args.source_file_path = argv[1];
				if (args.source_file_path == "--help")
				{
					print_application_help();
					return RETURN_VAL_CLOSE_PROGRAM;
				}
				for (int32_t i = 2; i < argc; i++)
				{
					ostd::String edit(argv[i]);
					if (edit == "-o")
					{
						if (i == argc - 1)
							return RETURN_VAL_MISSING_PARAM;
						i++;
						args.dest_file_path = argv[i];
					}
					else if (edit == "--save-disassembly")
					{
						if (i == argc - 1)
							return RETURN_VAL_MISSING_PARAM;
						i++;
						args.disassembly_file_path = argv[i];
						args.save_disassembly = true;
					}
					else if (edit == "--help")
					{
						print_application_help();
						return RETURN_VAL_CLOSE_PROGRAM;
					}
					else if (edit == "--extmov")
						args.cpu_extensions.push_back("extmov");
					else if (edit == "--extalu")
						args.cpu_extensions.push_back("extalu");
					else if (edit == "--verbose")
						args.verbose = true;
					else if (edit == "--save-exports")
						args.save_exports = true;
				}
			}
			return RETURN_VAL_EXIT_SUCCESS;
		}

		void Assembler::Application::print_application_help(void)
		{
			int32_t commandLength = 46;

			out.nl().fg(ostd::ConsoleColors::Yellow).p("List of available parameters:").reset().nl();
			ostd::String tmpCommand = "--save-disassembly <destination-directory>";
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Saves debug information in the destination directory.").reset().nl();
			tmpCommand = "--verbose";
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Shows more information about the assembled program.").reset().nl();
			tmpCommand = "-o <destination-binary-file>";
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to specify the output binary file.").reset().nl();
			tmpCommand = "--save-exports";
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to save any specified exports in the code.").reset().nl();
			tmpCommand = "--extmov";
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Enables mnemonics for the <extmov> CPU extension.").reset().nl();
			tmpCommand = "--extalu";
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Enables mnemonics for the <extalu> CPU extension.").reset().nl();
			tmpCommand = "--help";
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Displays this help message.").reset().nl();
			
			out.nl().fg(ostd::ConsoleColors::Magenta).p("Usage: ./dasm <source> -o <destination> [...options...]").reset().nl();
			out.nl();
		}
	}
}