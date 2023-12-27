#include <ostd/Utils.hpp>

#include "DragonRuntime.hpp"

ostd::ConsoleOutputHandler out;

struct tCommandLineArgs
{
	ostd::String machine_config_path = "";
	bool basic_debug = false;
	bool step_exec = false;
	bool verbose_load = false;
	int32_t cycle_limit = 0;
	bool force_load = false;
	ostd::String force_load_file = "";
	uint16_t force_load_mem_offset = 0x00;
};

int main(int argc, char** argv)
{
	tCommandLineArgs args;
	if (argc < 2)
	{
		out.fg(ostd::ConsoleColors::Red).p("Error, too few arguments.").reset().nl();
		return 128;
	}
	else
	{
		args.machine_config_path = argv[1];
		for (int32_t i = 2; i < argc; i++)
		{
			ostd::StringEditor edit(argv[i]);
			if (edit.str() == "--debug")
				args.basic_debug = true;
			else if (edit.str() == "--step")
				args.step_exec = true;
			else if (edit.str() == "--verbose-load")
				args.verbose_load = true;
			else if (edit.str() == "--limit-cycles")
			{
				if (i == argc - 1)
					break; //TODO: Warning
				i++;
				edit = argv[i];
				if (!edit.isNumeric())
					continue; //TODO: Error
				args.cycle_limit = (int32_t)edit.toInt();
			}
			else if (edit.str() == "--force-load")
			{
				if ((argc - 1) - i < 2)
					break; //TODO: Warning
				i++;
				args.force_load_file = argv[i];
				i++;
				edit = argv[i];
				if (!edit.isNumeric())
					continue; //TODO: Error
				args.force_load_mem_offset = (uint16_t)edit.toInt();
				args.force_load = true;
			}
		}
	}

	int32_t init_state = dragon::DragonRuntime::initMachine(args.machine_config_path, args.verbose_load);
	if (init_state != 0) return init_state; //TODO: Error

	if (args.force_load)
		dragon::DragonRuntime::forceLoad(args.force_load_file, args.force_load_mem_offset);

	dragon::DragonRuntime::runMachine(args.cycle_limit, args.basic_debug, args.step_exec);

	return 0;
}
