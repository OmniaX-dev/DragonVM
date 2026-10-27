#include <ostd/utils/Defines.hpp>
#include "Debugger.hpp"
#include "DebuggerWindow.hpp"
#include "../runtime/DragonRuntime.hpp"

int main(int argc, char** argv)
{
	dragon::Debugger debugger;

	//Loading commandline arguments
	i32 rValue = debugger.loadArguments(argc, argv);
	if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER)
		return 0;
	if (rValue != 0) return rValue;

	//Opening the debugger (the machine's screen is one of its widgets)
	dragon::DebuggerWindow window(debugger);
	window.initialize(1880, 1020, "DragonVM Debugger");
	window.enableResizable();

	//Initializing the runtime
	rValue = debugger.initRuntime(window.getDisplay());
	if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER)
		return 0;
	if (rValue != 0) return rValue;

	//Running the debugger
	window.start();
	window.mainLoop();
	return dragon::DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
}
