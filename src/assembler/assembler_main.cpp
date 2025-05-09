#include "Assembler.hpp"

int main(int argc, char** argv)
{
	int32_t rValue = dragon::code::Assembler::Application::loadArguments(argc, argv);
	if (rValue == dragon::code::Assembler::Application::RETURN_VAL_CLOSE_PROGRAM)
		return dragon::code::Assembler::Application::RETURN_VAL_EXIT_SUCCESS;
	if (rValue != dragon::code::Assembler::Application::RETURN_VAL_EXIT_SUCCESS)
		return rValue;
	auto& args = dragon::code::Assembler::Application::args;
	dragon::code::Assembler::saveExports = args.save_exports;
	dragon::code::Assembler::debugMode = args.debug_mode;
	dragon::code::Assembler::cpuExtensions = args.cpu_extensions;
	dragon::code::Assembler::assembleToFile(args.source_file_path, args.dest_file_path);
	dragon::code::Assembler::printProgramInfo(args.verbose_level);
	if (args.save_disassembly)
		dragon::code::Assembler::saveDisassemblyToFile(args.disassembly_file_path);
	return dragon::code::Assembler::Application::RETURN_VAL_EXIT_SUCCESS;
}