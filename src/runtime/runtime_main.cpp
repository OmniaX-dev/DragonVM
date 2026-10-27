#include <ostd/utils/Defines.hpp>
#include "DragonRuntime.hpp"
#include "RuntimeWindow.hpp"

int main(int argc, char** argv)
{
	//Loading commandline arguments
	dragon::DragonRuntime::tCommandLineArgs args;
	i32 rValue = dragon::DragonRuntime::loadArguments(argc, argv, args);
	if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_RUNTIME)
		return 0;
	if (rValue != 0) return rValue;

	//Initializing the runtime
	dragon::DragonRuntime::tRuntimeInitInfo initInfo;
	initInfo.configFilePath = args.machine_config_path;
	initInfo.verboseLoad = args.verbose_load;
	rValue = dragon::DragonRuntime::initMachine(initInfo);
	if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_RUNTIME)
		return 0;
	if (rValue != 0) return rValue;

	//Opening the virtual display
	auto& out = dragon::DragonRuntime::output();
	if (initInfo.verboseLoad)
		out.fg(ostd::ConsoleColors::Magenta).p("Initializing virtual display:").nl();
	dragon::RuntimeWindow window;
	window.create("DragonVM", "font.bmp");
	if (initInfo.verboseLoad)
		out.fg(ostd::ConsoleColors::BrightYellow).p("    Done. (").p(window.getWindowWidth()).p("x").p(window.getWindowHeight()).p(")").reset().nl().nl();

	//Executing the runtime
	if (args.force_load)
		dragon::DragonRuntime::forceLoad(args.force_load_file, args.force_load_mem_offset);
	window.runMachine();
	return dragon::DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
}
