#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"

int main(int argc, char** argv)
{
	//Loading commandline arguments
	int32_t rValue = dragon::Debugger::loadArguments(argc, argv);
	if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER)
		return 0;
	if (rValue != 0) return rValue;

	//Initializing the runtime
	rValue = dragon::Debugger::initRuntime();
	if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER)
		return 0;
	if (rValue != 0) return rValue;

	//Running top-level prompt
	rValue = dragon::Debugger::topLevelPrompt();
	if (rValue == dragon::DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER)
		return 0;
	if (rValue != 0) return rValue;

	//Executing the runtime
	return dragon::Debugger::executeRuntime();
}