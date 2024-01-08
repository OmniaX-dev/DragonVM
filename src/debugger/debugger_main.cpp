#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"

void exec_watch_command(void)
{
	using namespace dragon;

	auto tokens = Debugger::data().command.tokenize();
	for (auto& addr : tokens)
	{
		Debugger::data().command = addr;
		if (Debugger::data().command.isNumeric())
			Debugger::data().trackedAddresses.push_back((uint16_t)Debugger::data().command.toInt());
		else if (Debugger::data().command.startsWith("$"))
		{
			uint8_t nbytes = 1;
			if (Debugger::data().command.contains("[") && Debugger::data().command.endsWith("]"))
			{
				ostd::String str_nbytes = Debugger::data().command.new_substr(Debugger::data().command.indexOf("[") + 1).trim();
				str_nbytes.substr(0, str_nbytes.len() - 1);
				std::cout << str_nbytes << "\n";
				std::cout << Debugger::data().command << "\n";
				Debugger::data().command.substr(0, Debugger::data().command.indexOf("[")).trim();
				std::cout << Debugger::data().command << "\n";
				if (str_nbytes.isNumeric())
					nbytes = str_nbytes.toInt();
				if (nbytes < 1) nbytes = 1;
			}
			uint16_t addr = Debugger::Utils::findSymbol(Debugger::data().data, Debugger::data().command);
			if (addr > 0)
			{
				for (int32_t i = 0; i < nbytes; i++)
					Debugger::data().trackedAddresses.push_back(addr + i);
			}
			else
			{
				addr = Debugger::Utils::findSymbol(Debugger::data().labels, Debugger::data().command);
				if (addr > 0)
				{
					for (int32_t i = 0; i < nbytes; i++)
						Debugger::data().trackedAddresses.push_back(addr + i);
				}
			}
		}
	}
	std::cout << (int)Debugger::data().trackedAddresses.size() << "\n";
}

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
			if (Debugger::data().command == "r" || Debugger::data().command == "run")
				break;
			else if (Debugger::data().command == "q" || Debugger::data().command == "quit")
				return rValue;
			else if (Debugger::data().command.startsWith("watch "))
			{
				Debugger::data().command.substr(6).trim();
				exec_watch_command();
			}
			else if (Debugger::data().command == "step")
			{
				Debugger::data().args.step_exec = true;
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
			Debugger::output().nl().fg(ostd::ConsoleColors::Yellow).p("Execution Finished. Pres <Enter> to exit...").nl().reset();
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
		while (Debugger::data().command != "")
		{
			if (Debugger::data().command == "q" || Debugger::data().command == "quit")
			{
				userQuit = true;
				Debugger::data().command = "";
			}
			else if (Debugger::data().command == "c" || Debugger::data().command == "continue")
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