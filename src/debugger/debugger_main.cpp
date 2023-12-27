#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"


int main(int argc, char** argv)
{
	using namespace dragon;
	int32_t rValue = Debugger::loadArguments(argc, argv);
	if (rValue != 0) return rValue;
	rValue = Debugger::initRuntime();
	if (rValue != 0) return rValue;

	if (!Debugger::data().args.auto_start_debug)
	{
		while (true)
		{
			Debugger::Display::printPrompt();
			Debugger::data().command = Debugger::getCommandInput();
			Debugger::data().command.trim().toLower();
			if (Debugger::data().command.str() == "r" || Debugger::data().command.str() == "run")
				break;
			else if (Debugger::data().command.str() == "q" || Debugger::data().command.str() == "quit")
				return rValue;
			else if (Debugger::data().command.startsWith("watch "))
			{
				Debugger::data().command = Debugger::data().command.substr(6);
				Debugger::data().command.trim();
				auto tokens = Debugger::data().command.tokenize();
				for (auto& addr : tokens)
				{
					Debugger::data().command = addr;
					if (Debugger::data().command.isNumeric())
						Debugger::data().trackedAddresses.push_back((uint16_t)Debugger::data().command.toInt());
					else if (Debugger::data().command.startsWith("$"))
					{
						uint16_t addr = Debugger::Utils::findSymbol(Debugger::data().data, Debugger::data().command);
						if (addr > 0)
							Debugger::data().trackedAddresses.push_back(addr);
						else
						{
							addr = Debugger::Utils::findSymbol(Debugger::data().labels, Debugger::data().command);
							if (addr > 0)
								Debugger::data().trackedAddresses.push_back(addr);
						}
					}
				}
			}
		}
	}

	constexpr int32_t labelLineLen = 20;
	Debugger::data().currentAddress = dragon::DragonRuntime::cpu.readRegister(dragon::data::Registers::IP);

	bool userQuit = false;
	while (true)
	{
		Debugger::data().command.clr();
		bool result = false;
		bool hasError = false;
		if (!userQuit)
		{
			result = dragon::DragonRuntime::runStep(Debugger::data().trackedAddresses);
			hasError = dragon::DragonRuntime::hasError();
			Debugger::Display::printStep();
			Debugger::processErrors();
		}
		if (!result || userQuit)
		{
			Debugger::output().nl().col(ostd::legacy::ConsoleCol::Yellow).p("Execution Finished. Pres <Enter> to exit...").nl().reset();
			std::cin.get();
			break;
		}
		Debugger::Display::printPrompt();
		if (Debugger::data().args.step_exec || dragon::DragonRuntime::cpu.isInDebugBreakPoint())
		{
			Debugger::data().command = Debugger::getCommandInput();
			if (dragon::DragonRuntime::cpu.isInDebugBreakPoint())
				Debugger::data().args.step_exec = true;
		}
		Debugger::data().command.trim().toLower();
		while (Debugger::data().command.str() != "")
		{
			if (Debugger::data().command.str() == "q" || Debugger::data().command.str() == "quit")
			{
				userQuit = true;
				Debugger::data().command = "";
			}
			else if (Debugger::data().command.str() == "c" || Debugger::data().command.str() == "continue")
			{
				Debugger::data().args.step_exec = false;
				Debugger::data().command = "";
			}
			else
				Debugger::data().command = Debugger::Display::changeScreen();
		}
		Debugger::data().currentAddress = dragon::DragonRuntime::cpu.readRegister(dragon::data::Registers::IP);
	}

	return rValue;
}