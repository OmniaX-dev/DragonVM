// #include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"
#include "DebuggerNew.hpp"
#include <ostd/Signals.hpp>

int main(int argc, char** argv)
{
	// //Loading commandline arguments
	// int32_t rValue = dragon::Debugger::loadArguments(argc, argv);
	// if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER)
	// 	return 0;
	// if (rValue != 0) return rValue;

	// //Initializing the runtime
	// rValue = dragon::Debugger::initRuntime();
	// if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER)
	// 	return 0;
	// if (rValue != 0) return rValue;

	// //Running top-level prompt
	// rValue = dragon::Debugger::topLevelPrompt();
	// if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER)
	// 	return 0;
	// if (rValue != 0) return rValue;

	// //Executing the runtime
	// return dragon::Debugger::executeRuntime();


	ostd::SignalHandler::init();

	dragon::DebuggerNew debuggerInstance;
	debuggerInstance.initialize(2000, 1090, "DragonVM Live Debugger");
	debuggerInstance.setClearColor({ 5, 0, 0 });

	//Loading commandline arguments
	int32_t rValue = debuggerInstance.loadArguments(argc, argv);
	if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER)
		return 0;
	if (rValue != 0) return rValue;

	//Initializing the runtime
	rValue = debuggerInstance.initRuntime();
	if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER)
		return 0;
	if (rValue != 0) return rValue;

	while (debuggerInstance.isRunning())
	{
		debuggerInstance.update();
	}

	return 0;
}
