#include <ostd/Utils.hpp>
#include "DragonRuntime.hpp"

int main(int argc, char** argv)
{
	//Loading commandline arguments
	dragon::DragonRuntime::tCommandLineArgs args;
	int32_t rValue = dragon::DragonRuntime::loadArguments(argc, argv, args);
	if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_RUNTIME)
		return 0;
	if (rValue != 0) return rValue;

	//Initializing the runtime
	rValue = dragon::DragonRuntime::initMachine(args.machine_config_path, args.verbose_load);
	if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_RUNTIME)
		return 0;
	if (rValue != 0) return rValue;

	//Executing the runtime
	if (args.force_load)
		dragon::DragonRuntime::forceLoad(args.force_load_file, args.force_load_mem_offset);
	dragon::DragonRuntime::runMachine(args.cycle_limit, args.basic_debug, args.step_exec);
	return dragon::DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
}
