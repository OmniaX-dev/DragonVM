#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"
#include "DisassemblyLoader.hpp"
#include <ostd/Defines.hpp>
#include <ostd/Console.hpp>

namespace dragon
{
	//Close Event Listener
	Debugger::tCloseEventListener Debugger::closeEventListener;

	void Debugger::tCloseEventListener::init(void)
	{
		validate();
		enableSignals();
		connectSignal(ostd::tBuiltinSignals::WindowClosed);
	}

	void Debugger::tCloseEventListener::handleSignal(ostd::tSignal& signal)
	{
		m_mainWindowClosed = signal.ID == ostd::tBuiltinSignals::WindowClosed;
	}




	//Debugger::Utils
	DisassemblyList Debugger::Utils::findCodeRegion(const DisassemblyList& code, uint16_t address, uint16_t codeRegionMargin)
	{
		if (code.size() <= (codeRegionMargin * 2) + 1) return code;
		std::vector<dragon::code::Assembler::tDisassemblyLine> codeRegion;
		uint16_t start = 0;
		uint16_t end = (codeRegionMargin * 2);
		for (int32_t i = 0; i < code.size(); i++) 
		{
			if (code[i].addr != address) continue;
			if (i + 1 <= codeRegionMargin) break;
			if (code.size() - (i + 1) < codeRegionMargin)
			{
				end = code.size() - 1;
				start = end - ((codeRegionMargin * 2) + 1);
				break;
			}
			start = i - codeRegionMargin;
			end = i + codeRegionMargin;
			break;
		}
		for (int16_t i = start; i <= end; i++)
			codeRegion.push_back(code[i]);
		return codeRegion;
	}

	ostd::String Debugger::Utils::findSymbol(const DisassemblyList& list, uint16_t address, uint16_t* outSize)
	{
		for (auto& line : list)
		{
			if (line.addr == address)
			{
				if (outSize != nullptr)
					*outSize = line.size;
				return line.code;
			}
		}
		return "";
	}

	uint16_t Debugger::Utils::findSymbol(const DisassemblyList& list, const ostd::String& symbol, uint16_t* outSize)
	{
		for (auto& line : list)
		{
			if (line.code == symbol)
			{
				if (outSize != nullptr)
					*outSize = line.size;
				return line.addr;
			}
		}
		return 0x0000;
	}

	bool Debugger::Utils::isValidLabelNameChar(char c)
	{
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_');
	}

	void Debugger::Utils::clearConsoleLine(void)
	{
		for (int32_t i = 0; i < ostd::Utils::getConsoleWidth(); i++)
			out.p("\b");
		for (int32_t i = 0; i < ostd::Utils::getConsoleWidth(); i++)
			out.p(" ");
		for (int32_t i = 0; i < ostd::Utils::getConsoleWidth(); i++)
			out.p("\b");
	}

	bool Debugger::Utils::isEscapeKeyPressed(bool blocking)
	{
		ostd::KeyboardController kbc;
		kbc.disableCommandBuffer();
		kbc.disableOutput();
		if (blocking)
		{
			ostd::eKeys key = ostd::eKeys::NoKeyPressed;
			while ((key = kbc.waitForKeyPress()) != ostd::eKeys::Escape)
			{
				ostd::Utils::sleep(120);
			}
			return true;
		}
		return kbc.getPressedKey() == ostd::eKeys::Escape;
	}

	ostd::ConsoleOutputHandler& Debugger::Utils::printFullLine(char c, const ostd::ConsoleColors::tConsoleColor& foreground)
	{
		int32_t cw = ostd::Utils::getConsoleWidth();
		ostd::String str = ostd::Utils::duplicateChar(c, cw);
		out.fg(foreground).p(str).reset().nl();
		return out;
	}

	ostd::ConsoleOutputHandler& Debugger::Utils::printFullLine(char c, const ostd::ConsoleColors::tConsoleColor& foreground, const ostd::ConsoleColors::tConsoleColor& background)
	{
		int32_t cw = ostd::Utils::getConsoleWidth();
		ostd::String str = ostd::Utils::duplicateChar(c, cw);
		out.bg(background).fg(foreground).p(str).reset().nl();
		return out;
	}




	//Debugger::Display
	void Debugger::Display::colorizeInstructionBody(const ostd::String& instBody, bool currentLine, const DisassemblyList& labelList)
	{
		ostd::RegexRichString rgxrstr(instBody);
		rgxrstr.fg("\\{|\\}|\\+|\\*|\\-|\\/|\\(|\\)|\\[|\\]", "Red"); //Operators
		rgxrstr.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "BrightYellow"); //Number Constants
		rgxrstr.fg("(?<!\\w)(r[1-9]|r10|fl|pp|rv|fp|sp|ip|acc)(?!\\w)", "BrightGreen", true); //Registers
		rgxrstr.col("\\$\\w+", "Cyan", "Black"); //Labels
		ostd::String instEdit = rgxrstr.getRawString();
		for (auto& label : labelList)
		{
			int32_t index = -1;
			ostd::String labelEdit = label.code;
			labelEdit.trim();
			while ((index = instEdit.indexOf(labelEdit, index + 1)) != -1)
			{
				if (index + labelEdit.len() < instEdit.len() && Utils::isValidLabelNameChar(instEdit.at(index + labelEdit.len())))
					continue;
				ostd::String instStr = instEdit;
				instStr.cpp_str_ref().replace(index, labelEdit.len(), labelEdit.cpp_str() + "[@@ style foreground:brightgray](" + ostd::Utils::getHexStr(label.addr, true, 2).cpp_str() + ")[@@/]");
				instEdit = instStr;
			}
		}
		rgxrstr.setRawString(instEdit);
		out.p("\t").pStyled(rgxrstr);
		if (currentLine)
			out.p("   ").bg(ostd::ConsoleColors::Yellow).fg(ostd::ConsoleColors::Black).p(" <-- ");
		out.nl();
		out.reset().reset();
	}

	void Debugger::Display::colorCodeInstructions(const ostd::String& inst, bool currentLine, const DisassemblyList& labelList)
	{
		ostd::String instEditor = inst;
		ostd::String instBody = "";
		ostd::String instHead = inst;
		instEditor.trim();
		if (instEditor.contains(" "))
		{
			instHead = instEditor.new_substr(0, instEditor.indexOf(" "));
			instBody = instEditor.new_substr(instEditor.indexOf(" "));
		}
		if (currentLine)
		{
			out.bg(ostd::ConsoleColors::Yellow).fg(ostd::ConsoleColors::Black);
		}
		else if (instHead == "call" || instHead == "ret" || instHead == "int" || instHead == "rti")
			out.bg(ostd::ConsoleColors::BrightRed).fg(ostd::ConsoleColors::White);
		else if (instHead == "hlt" || instHead == "debug_break")
			out.fg(ostd::ConsoleColors::Red);
		else if (instHead == "nop")
			out.fg(ostd::ConsoleColors::Gray);
		else
			out.fg(ostd::ConsoleColors::Blue);
		out.p(instHead.cpp_str());
		out.reset().reset();
		colorizeInstructionBody(instBody, currentLine, labelList);
	}

	void Debugger::Display::printPrompt(void)
	{
		out.fg(ostd::ConsoleColors::Magenta).p(" (ddb) #/> ").fg(ostd::ConsoleColors::White).flush();
	}

	void Debugger::Display::printStep(void)
	{
		out.clear();
		int32_t codeRegionSpan = 15;
		auto codeRegion = Utils::findCodeRegion(debugger.code, debugger.currentAddress, codeRegionSpan);
		for (int32_t i = 0; i < codeRegion.size(); i++)
		{
			auto& _da = codeRegion[i];
			bool currentLine = _da.addr == debugger.currentAddress;
			ostd::String label = Utils::findSymbol(debugger.labels, _da.addr);
			bool specialSection = _da.code.startsWith("[") &&_da.code.endsWith("]");
			label.fixedLength(debugger.labelLineLength);
			out.fg(ostd::ConsoleColors::Gray).p(label.cpp_str()).p("  ");
			if (currentLine)
			{
				out.fg(ostd::ConsoleColors::Black).bg(ostd::ConsoleColors::Yellow).p(ostd::Utils::getHexStr(_da.addr, true, 2).cpp_str()).p("  ").reset();;
			}
			else
			{
				if (specialSection)
					out.fg(ostd::ConsoleColors::Cyan);
				else if (_da.code == "debug_break")
					out.fg(ostd::ConsoleColors::Red);
				else
					out.fg(ostd::ConsoleColors::BrightGray);
				out.p(ostd::Utils::getHexStr(_da.addr, true, 2).cpp_str()).p("  ");
			}
			if (specialSection)
				out.fg(ostd::ConsoleColors::Cyan).p(_da.code.cpp_str()).nl();
				colorCodeInstructions(_da.code, currentLine, debugger.labels);
			out.reset();
		}
		Utils::printFullLine('#', ostd::ConsoleColors::Black, ostd::ConsoleColors::Yellow);
	}

	void Debugger::Display::printDiff(void)
	{
		out.clear();
		
		ostd::String str;
		str.add("|===============|================PREV================|================CURR================|=====|===PREV====|===CURR====|");
		str.add("\n");
		str.add("| InstAddr:     |*%PREV_INST_ADDR%*******************|*%CURR_INST_ADDR%*******************| R1  |*%PREV_R1%*|*%CURR_R1%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("| Code:         |                ----                |*%CURR_CODE%************************| R2  |*%PREV_R2%*|*%CURR_R2%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("| StackFrame:   |*%PREV_STACK_FRAME%*****************|*%CURR_STACK_FRAME%*****************| R3  |*%PREV_R3%*|*%CURR_R3%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("| DBG BRK:      |*%PREV_DBG_BRK%*********************|*%CURR_DBG_BRK%*********************| R4  |*%PREV_R4%*|*%CURR_R4%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("| INT Handler:  |*%PREV_INT_HANDLER%*****************|*%CURR_INT_HANDLER%*****************| R5  |*%PREV_R5%*|*%CURR_R5%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("| BIOS Mode:    |*%PREV_BIOS_MODE%*******************|**%CURR_BIOS_MODE%******************| R6  |*%PREV_R6%*|*%CURR_R6%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("| SubRoutine:   |*%PREV_SUB_ROUTINE%*****************|**%CURR_SUB_ROUTINE%****************| R7  |*%PREV_R7%*|*%CURR_R7%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("|    IP:        |*%PREV_IP%**************************|**%CURR_IP%*************************| R8  |*%PREV_R8%*|*%CURR_R8%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("|    SP:        |*%PREV_SP%**************************|**%CURR_SP%*************************| R9  |*%PREV_R9%*|*%CURR_R9%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("|    FP:        |*%PREV_FP%**************************|**%CURR_FP%*************************| R10 |*%PREV_R10%|*%CURR_R10%|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|=====|===========|===========|");
		str.add("\n");
		str.add("|    RV:        |*%PREV_RV%**************************|**%CURR_RV%*************************| S1  |*%PREV_S1%*|*%CURR_S1*%|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("|    PP:        |*%PREV_PP%**************************|**%CURR_PP%*************************| S2  |*%PREV_S2%*|*%CURR_S2%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("|    FL:        |*%PREV_FL%**************************|**%CURR_FL%*************************| S3  |*%PREV_S3%*|*%CURR_S3%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|=====|===========|===========|");
		str.add("\n");
		str.add("|    ACC:       |*%PREV_ACC%*************************|**%CURR_ACC%************************|");
		str.add("\n");
		str.add("|===============|====================================|====================================|");


		str.replaceAll("*", "");
		int32_t item_len = 36;
		const dragon::DragonRuntime::tMachineDebugInfo& minfo = dragon::DragonRuntime::getMachineInfoDiff();
		ostd::String tmp = " ", tmpStyle = "";

		//Instruction Address
		{
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionAddress, true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_INST_ADDR%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionAddress, true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionAddress != minfo.previousInstructionAddress)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_INST_ADDR%", tmpStyle);
		}

		//Code
		{
			tmp = " ", tmpStyle = "";
			ostd::String prevCode = " (";
			prevCode.add(minfo.previousInstructionOpCode).add(") ");
			for (int32_t i = 0; i < minfo.previousInstructionFootprintSize; i++)
				prevCode.add(ostd::Utils::getHexStr(minfo.previousInstructionFootprint[i], false, 1)).add(" ");
			tmp.add(prevCode);
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Black,background:BrightGray]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_CODE%", tmpStyle);

			// tmp = " ";
			// ostd::String currCode = " (";
			// currCode.add(minfo.currentInstructionOpCode).add(") ");
			// for (int32_t i = 0; i < minfo.currentInstructionFootprintSize; i++)
			// 	currCode.add(ostd::Utils::getHexStr(minfo.currentInstructionFootprint[i], false, 1)).add(" ");
			// tmp.add(currCode);
			// tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			// if (currCode != prevCode)
			// 	tmpStyle = "[@@style foreground:Black,background:Yellow]";
			// else
			// 	tmpStyle = "[@@style foreground:Blue]";
			// tmpStyle.add(tmp).add("[@@/]");
			// str.replaceAll("%CURR_CODE%", tmpStyle);
		}

		//Stack Frame
		{
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionStackFrameSize, true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_STACK_FRAME%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionStackFrameSize, true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionStackFrameSize != minfo.previousInstructionStackFrameSize)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_STACK_FRAME%", tmpStyle);
		}

		//Debug Break
		{
			tmp = " ", tmpStyle = "";
			tmp.add(STR_BOOL(minfo.previousInstructionDebugBreak));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_DBG_BRK%", tmpStyle);

			tmp = " ";
			tmp.add(STR_BOOL(minfo.currentInstructionDebugBreak));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionDebugBreak != minfo.previousInstructionDebugBreak)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_DBG_BRK%", tmpStyle);
		}

		//INT Handler
		{
			tmp = " ", tmpStyle = "";
			tmp.add(STR_BOOL(minfo.previousInstructionInterruptHandler));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_INT_HANDLER%", tmpStyle);

			tmp = " ";
			tmp.add(STR_BOOL(minfo.currentInstructionInterruptHandler));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionInterruptHandler != minfo.previousInstructionInterruptHandler)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_INT_HANDLER%", tmpStyle);
		}
		
		//Bios Mode
		{
			tmp = " ", tmpStyle = "";
			tmp.add(STR_BOOL(minfo.previousInstructionBiosMode));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_BIOS_MODE%", tmpStyle);

			tmp = " ";
			tmp.add(STR_BOOL(minfo.currentInstructionBiosMode));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionBiosMode != minfo.previousInstructionBiosMode)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_BIOS_MODE%", tmpStyle);
		}
		
		//SubRoutine Info
		{
			tmp = " ", tmpStyle = "";
			tmp.add(minfo.previousSubRoutineCounter).add(" (");
			tmp.add(STR_BOOL(minfo.previousIsInSubRoutine)).add(")");
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			ostd::String old_tmp = tmp;
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_SUB_ROUTINE%", tmpStyle);

			tmp = " ";
			tmp.add(minfo.currentSubRoutineCounter).add(" (");
			tmp.add(STR_BOOL(minfo.currentIsInSubRoutine)).add(")");
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (old_tmp != tmp)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_SUB_ROUTINE%", tmpStyle);
		}

		//System Registers
		{
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::IP], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_IP%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::IP], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::IP] != minfo.previousInstructionRegisters[dragon::data::Registers::IP])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_IP%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::SP], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_SP%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::SP], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::SP] != minfo.previousInstructionRegisters[dragon::data::Registers::SP])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_SP%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::FP], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_FP%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::FP], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::FP] != minfo.previousInstructionRegisters[dragon::data::Registers::FP])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_FP%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::RV], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_RV%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::RV], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::RV] != minfo.previousInstructionRegisters[dragon::data::Registers::RV])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_RV%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::PP], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_PP%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::PP], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::PP] != minfo.previousInstructionRegisters[dragon::data::Registers::PP])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_PP%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::FL], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_FL%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::FL], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::FL] != minfo.previousInstructionRegisters[dragon::data::Registers::FL])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_FL%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::ACC], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_ACC%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::ACC], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::ACC] != minfo.previousInstructionRegisters[dragon::data::Registers::ACC])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_ACC%", tmpStyle);
		}

		item_len = 11;
		//General Registers
		{
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R1], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_R1%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R1], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R1] != minfo.previousInstructionRegisters[dragon::data::Registers::R1])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_R1%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R2], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_R2%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R2], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R2] != minfo.previousInstructionRegisters[dragon::data::Registers::R2])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_R2%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R3], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_R3%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R3], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R3] != minfo.previousInstructionRegisters[dragon::data::Registers::R3])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_R3%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R4], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_R4%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R4], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R4] != minfo.previousInstructionRegisters[dragon::data::Registers::R4])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_R4%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R5], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_R5%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R5], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R5] != minfo.previousInstructionRegisters[dragon::data::Registers::R5])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_R5%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R6], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_R6%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R6], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R6] != minfo.previousInstructionRegisters[dragon::data::Registers::R6])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_R6%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R7], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_R7%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R7], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R7] != minfo.previousInstructionRegisters[dragon::data::Registers::R7])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_R7%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R8], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_R8%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R8], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R8] != minfo.previousInstructionRegisters[dragon::data::Registers::R8])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_R8%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R9], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_R9%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R9], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R9] != minfo.previousInstructionRegisters[dragon::data::Registers::R9])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_R9%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R10], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_R10%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R10], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R10] != minfo.previousInstructionRegisters[dragon::data::Registers::R10])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_R10%", tmpStyle);
		}

		//Special Registers
		{
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::S1], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_S1%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::S1], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::S1] != minfo.previousInstructionRegisters[dragon::data::Registers::S1])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_S1%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::S2], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_S2%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::S2], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::S2] != minfo.previousInstructionRegisters[dragon::data::Registers::S2])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_S2%", tmpStyle);



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::S3], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%PREV_S3%", tmpStyle);

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::S3], true, 2));
			tmp.addPadding(item_len, ' ', ostd::String::ePaddingBehavior::AllowOddExtraLeft);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::S3] != minfo.previousInstructionRegisters[dragon::data::Registers::S3])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll("%CURR_S3%", tmpStyle);
		}
		
		ostd::RegexRichString rgxstr(str);
		rgxstr.fg("InstAddr|Code|StackFrame|DBG BRK|INT Handler|BIOS Mode|SubRoutine", "Magenta");
		rgxstr.fg("IP|SP|FP|RV|PP|FL|ACC", "Cyan");
		rgxstr.fg("R10|R2|R3|R4|R5|R6|R7|R8|R9|R1", "BrightGreen");
		rgxstr.fg("S1|S2|S3", "BrightRed");
		rgxstr.fg("PREV", "Red");
		rgxstr.fg("CURR", "Green");
		out.pStyled(rgxstr);

		out.nl().nl().fg(ostd::ConsoleColors::Yellow).p("Pres <Escape> to go back...").nl().reset();
		Utils::isEscapeKeyPressed(true);
	}

	void Debugger::Display::printTrackedAddresses(void)
	{
		out.clear();
		out.fg(ostd::ConsoleColors::Yellow).p("Tracked Data: ").reset().nl();
		const int32_t symbol_len = 24;
		const int32_t size_len = 8;
		const int32_t addr_len = 10;
		const int32_t map_len = 8;
		const int32_t prev_len = 35;
		const dragon::DragonRuntime::tMachineDebugInfo& minfo = dragon::DragonRuntime::getMachineInfoDiff();
		if (minfo.trackedAddresses.size() == 0) return;
		int32_t cw = ostd::Utils::getConsoleWidth();
		ostd::String header = "|Symbol";
		header.addRightPadding(symbol_len);
		header.add("|Bytes");
		header.addRightPadding(header.len() + size_len - 6);
		header.add("|Addr");
		header.addRightPadding(header.len() + addr_len - 5);
		header.add("|Map");
		header.addRightPadding(header.len() + map_len - 4);
		header.add("|Previous");
		header.addRightPadding(header.len() + prev_len - 9);
		header.add("|Current");
		header.fixedLength(cw - 1);
		header.addChar('|');
		ostd::String border = ostd::Utils::duplicateChar('=', cw);
		out.fg(ostd::ConsoleColors::Blue).p(border).nl();
		ostd::RegexRichString rgx(header);
		rgx.fg("\\|", "blue");
		rgx.fg("Symbol|Bytes|Addr|Map|Previous|Current", "yellow");
		out.pStyled(rgx).reset().nl();
		ostd::String h_sep = ostd::Utils::duplicateChar('=', cw);
		h_sep.put(0, '|');
		h_sep.put(symbol_len, '|');
		h_sep.put(symbol_len + size_len, '|');
		h_sep.put(symbol_len + size_len + addr_len, '|');
		h_sep.put(symbol_len + size_len + addr_len + map_len, '|');
		h_sep.put(symbol_len + size_len + addr_len + map_len + prev_len, '|');
		h_sep.put(cw - 1, '|');
		out.fg(ostd::ConsoleColors::Blue).p(h_sep).reset().nl();
		for (int32_t i = 0; i < minfo.trackedAddresses.size(); i++)
		{
			uint16_t data_size = 1;
			auto addr = minfo.trackedAddresses[i];
			ostd::String symbol = Utils::findSymbol(debugger.data, addr, &data_size);
			if (symbol == "")
				symbol = Utils::findSymbol(debugger.labels, addr, &data_size);
			bool no_symbol = false;
			if (symbol == "")
			{
				symbol = "<no-symbol>";
				no_symbol = true;
			}
			symbol.fixedLength(symbol_len - 1);
			if (no_symbol)
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::Gray).p(symbol).reset();
			else
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::Magenta).p(symbol).reset();

			ostd::String tmp = "";
			tmp.add(data_size);
			tmp.fixedLength(size_len - 1);
			if (no_symbol)
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::Cyan).p(tmp).reset();
			else
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::BrightCyan).p(tmp).reset();

			tmp = "";
			tmp.add(ostd::Utils::getHexStr(addr, true, 2));
			tmp.fixedLength(addr_len - 1);
			if (no_symbol)
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::Gray).p(tmp).reset();
			else
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::BrightGray).p(tmp).reset();

			tmp = "";
			if (addr >= (uint16_t)DragonRuntime::cpu.readRegister(data::Registers::SP))
				tmp.add("STACK");
			else
				tmp.add(DragonRuntime::memMap.getMemoryRegionName(addr));
			tmp.fixedLength(map_len - 1);
			out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::BrightYellow).p(tmp).reset();

			tmp = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionTrackedValues[i], false, 1));
			for (int32_t j = 1; j < data_size; j++)
				tmp.add(".").add(ostd::Utils::getHexStr(minfo.previousInstructionTrackedValues[i + j], false, 1));
			tmp.fixedLength(prev_len - 1);
			if (no_symbol)
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::Red).p(tmp).reset();
			else
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::BrightRed).p(tmp).reset();

			tmp = "";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionTrackedValues[i], false, 1));
			uint32_t tmp_i = i;
			for (int32_t j = 1; j < data_size; j++, i++)
				tmp.add(".").add(ostd::Utils::getHexStr(minfo.currentInstructionTrackedValues[i + 1], false, 1));
			tmp.fixedLength(prev_len - 1);
			bool data_different = false;
			for (int32_t j = 0; j < data_size; j++)
			{
				if (minfo.currentInstructionTrackedValues[tmp_i + j] != minfo.previousInstructionTrackedValues[tmp_i + j])
				{
					data_different = true;
					break;
				}
			}
			if (no_symbol)
			{
				if (data_different)
					out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::BrightGray).p(tmp).reset();
				else
					out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::Green).p(tmp).reset();
			}
			else
			{
				if (data_different)
					out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::White).p(tmp).reset();
				else
					out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::BrightGreen).p(tmp).reset();
			}
			tmp = "|";
			tmp.addLeftPadding(cw - symbol_len - size_len - addr_len - map_len - prev_len -  prev_len);
			out.fg(ostd::ConsoleColors::Blue).p(tmp).reset();
		}
		out.nl();
		out.fg(ostd::ConsoleColors::Blue).p(border).reset().nl();

		out.nl().nl().fg(ostd::ConsoleColors::Yellow).p("Pres <Escape> to go back...").nl().reset();
		Utils::isEscapeKeyPressed(true);
	}

	void Debugger::Display::printStack(uint16_t nrows)
	{
		if (nrows == 0) nrows = 8;
		uint16_t ncols = 16;
		uint16_t startAddr = (0xFFFF - (ncols * nrows) + 1);
		out.clear();

		out.nl();
		out.fg(ostd::ConsoleColors::Yellow).p("Stack frames: ").reset().nl();
		Utils::printFullLine('=', ostd::ConsoleColors::BrightBlue);
		for (auto& frame : dragon::DragonRuntime::cpu.m_debug_stackFrameStrings)
		{
			ostd::RegexRichString rgxrstr(frame);
			rgxrstr.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "Red"); //Number Constants
			rgxrstr.fg("(?<!\\w)(r[1-9]|r10|fl|pp|rv|fp|sp|ip|acc)(?!\\w)", "BrightGreen", true); //Registers
			rgxrstr.fg("(?<!\\w)(args|argc|StackFrame|New FP)(?!\\w)", "Magenta", true); //Other

			out.pStyled(rgxrstr);
			out.nl();
			Utils::printFullLine('=', ostd::ConsoleColors::BrightBlue);
		}
		out.nl();

		out.fg(ostd::ConsoleColors::Yellow).p("Stack view: ").reset().nl();
		uint16_t stack_ptr = DragonRuntime::cpu.readRegister(data::Registers::SP);
		ostd::Utils::printByteStream(*DragonRuntime::ram.getByteStream(), startAddr, ncols, nrows, out, stack_ptr, 2, "Stack");

		out.nl().nl().fg(ostd::ConsoleColors::Yellow).p("Pres <Escape> to go back...").nl().reset();
		Utils::isEscapeKeyPressed(true);
	}

	void Debugger::Display::printCallStack(void)
	{
		out.clear();
		out.fg(ostd::ConsoleColors::Yellow).p("Subroutine/Interrupt call stack: ").reset().nl();
		Utils::printFullLine('=', ostd::ConsoleColors::Yellow);

		const dragon::DragonRuntime::tMachineDebugInfo& minfo = dragon::DragonRuntime::getMachineInfoDiff();
		
		auto& callStack = minfo.callStack;
		int32_t level = -1;
		ostd::String ch_ang_u = "";
		ch_ang_u.addChar((unsigned char)218);
		ostd::String ch_ang_l = "|";
		ostd::String ch_ang_d = "";
		ch_ang_d.addChar((unsigned char)192);
		for (auto& call : callStack)
		{
			ostd::String call_info = call.info.new_trim().toLower();
			ostd::String subroutine_addr = ostd::Utils::getHexStr(call.addr, true, 2);
			ostd::String subroutine_name = Utils::findSymbol(debugger.labels, call.addr);
			ostd::String call_str = "";
			bool dec_lvl = false;
			if (call_info == "int")
			{
				call_str = ch_ang_u + "int " + ostd::Utils::getHexStr(call.addr, true, 1);
				level++;
			}
			else if (call_info == "ret")
			{
				call_str = ch_ang_d + "ret";
				dec_lvl = true;
			}
			else if (call_info == "ret int")
			{
				call_str = ch_ang_d + "rti";
				dec_lvl = true;
			}
			else
			{
				if (call_info == "call reg")
					subroutine_addr = "*" + subroutine_addr;
				if (subroutine_name != "")
					call_str = ch_ang_u + subroutine_name + " (" + subroutine_addr + ")";
				else
					call_str = ch_ang_u + subroutine_addr;
				level++;
			}

			ostd::String line_str = "";
			for (int32_t i = 0; i < level; i++)
				line_str.add("|   ");
			line_str.add(call_str);

			ostd::RegexRichString rgx(line_str);
			rgx.fg("\\(|\\)|\\*", "Cyan"); //Operators
			rgx.fg("(?<![a-zA-Z\\_\\$\\.])int|rti(?! [a-zA-Z\\_\\$\\.])", "Blue");
			rgx.fg("(?<![a-zA-Z\\_\\$\\.])ret(?! [a-zA-Z\\_\\$\\.])", "Red");
			rgx.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "BrightYellow"); //Number Constants
			rgx.fg(ostd::String("[\\").add(ch_ang_u).add("\\|\\").add(ch_ang_d).add("]"), "darkgray");
			rgx.fg("\\$\\w+", "Green"); //Labels
			
			out.fg(ostd::ConsoleColors::BrightGray).p("|").reset();
			out.fg(ostd::ConsoleColors::Magenta).p(ostd::Utils::getHexStr(call.inst_addr, true, 2));
			out.fg(ostd::ConsoleColors::BrightGray).p("| > ").reset();
			out.pStyled(rgx).nl().reset();

			if (dec_lvl)
				level--;
		}

		Utils::printFullLine('=', ostd::ConsoleColors::Yellow);
		out.nl().nl().fg(ostd::ConsoleColors::Yellow).p("Pres <Escape> to go back...").nl().reset();
		Utils::isEscapeKeyPressed(true);
	}

	void Debugger::Display::printHelp(void)
	{
		out.clear();
		int32_t commandLength = 34;
		Utils::printFullLine('#', ostd::ConsoleColors::Black, ostd::ConsoleColors::Red);

		out.nl().fg(ostd::ConsoleColors::Yellow).p("List of available commands:").reset().nl();
		ostd::String tmpCommand = "(d)iff-view";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to show the state of the machine, comparing the current cycle with the last.").reset().nl();
		tmpCommand = "(q)uit";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to close the program and exit the debugger. (<Escape> key can also be used.)").reset().nl();
		tmpCommand = "(h)elp";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to show this dialog.").reset().nl();
		tmpCommand = "(t)racker";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to show manually tracked values.").reset().nl();
		tmpCommand = "(s)tack [rows=8]";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to show the stack and a description of every stack-frame saved.").reset().nl();
		tmpCommand = "(ca)ll-stack";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to show information about all subroutine/interrupt calls.").reset().nl();
		tmpCommand = "(c)ontinue";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used exit step-execution mode and resume the normal execution of the program.").reset().nl();
		tmpCommand = "(p)rint [word] <addr/symbol>";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to print values from memory. Valid [size] values are: byte/word/dword/qword.").reset().nl();
		
		out.nl().fg(ostd::ConsoleColors::Cyan).p("The letters in parenthesis can be used as the short version of the command.").nl();
		out.p("Square brackets represent optional parameters. If they have an '=' and a value, that is the default if not specified.").reset().nl();

		out.nl();
		Utils::printFullLine('#', ostd::ConsoleColors::Black, ostd::ConsoleColors::Red);

		out.nl().nl().fg(ostd::ConsoleColors::Yellow).p("Pres <Escape> to go back...").nl().reset();
		Utils::isEscapeKeyPressed(true);
	}

	ostd::String Debugger::Display::changeScreen(void)
	{
		if (debugger.command == "diff-view" || debugger.command == "d")
		{
			printDiff();
			printStep();
			printPrompt();
			return getCommandInput();
		}
		else if (debugger.command == "tracker" || debugger.command == "t")
		{
			printTrackedAddresses();
			printStep();
			printPrompt();
			return getCommandInput();
		}
		else if (debugger.command.startsWith("stack") || debugger.command.startsWith("s"))
		{
			uint16_t default_stack_rows = 8;
			ostd::String params = debugger.command.new_trim();
			if (params == "stack" || params == "s")
				printStack(default_stack_rows);
			else if (params.contains(" "))
			{
				params.substr(params.indexOf(" ") + 1).trim();
				if (!params.isNumeric()) return "";
				default_stack_rows = (uint16_t)params.toInt();
				if (default_stack_rows > 0xFFFF / 16) default_stack_rows = 8;
				printStack(default_stack_rows);
			}
			else return "";
			printStep();
			printPrompt();
			return getCommandInput();
		}
		else if (debugger.command == "call-stack" || debugger.command == "ca")
		{
			printCallStack();
			printStep();
			printPrompt();
			return getCommandInput();
		}
		else if (debugger.command == "help" || debugger.command == "h")
		{
			printHelp();
			printStep();
			printPrompt();
			return getCommandInput();
		}
		return "";
	}




	//Debugger
	void Debugger::processErrors(void)
	{
		if (!dragon::DragonRuntime::hasError()) return;
		while (dragon::data::ErrorHandler::hasError())
		{
			auto err = dragon::data::ErrorHandler::popError();
			out.nl().fg(ostd::ConsoleColors::Red).p("Error ").p(ostd::Utils::getHexStr(err.code, true, 8).cpp_str()).p(": ").p(err.text.cpp_str()).nl();
		}
		debugger.args.step_exec = true;
	}

	int32_t Debugger::loadArguments(int argc, char** argv)
	{
		if (argc < 2)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("Use the --help option for more info.").reset().nl();
			return DragonRuntime::RETURN_VAL_TOO_FEW_ARGUMENTS;
		}
		else
		{
			debugger.args.machine_config_path = argv[1];
			if (debugger.args.machine_config_path == "--help")
			{
				print_application_help();
				return DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER;
			}
			for (int32_t i = 2; i < argc; i++)
			{
				ostd::String edit(argv[i]);
				if (edit == "--verbose-load")
					debugger.args.verbose_load = true;
				else if (edit == "--step-exec")
					debugger.args.step_exec = true;
				else if (edit == "--track-step-diff-off")
					debugger.args.track_step_diff = false;
				else if (edit == "--auto-track-data-off")
					debugger.args.auto_track_all_data_symbols = false;
				else if (edit == "--hide-vdisplay")
					debugger.args.hide_virtual_display = true;
				else if (edit == "--auto-start")
					debugger.args.auto_start_debug = true;
				else if (edit == "--force-load")
				{
					if ((argc - 1) - i < 2)
						return DragonRuntime::RETURN_VAL_MISSING_PARAM;
					i++;
					debugger.args.force_load_file = argv[i];
					i++;
					edit = argv[i];
					if (!edit.isNumeric())
						return DragonRuntime::RETURN_VAL_PARAMETER_NOT_NUMERIC;
					debugger.args.force_load_mem_offset = (uint16_t)edit.toInt();
					debugger.args.force_load = true;
				}
				else if (edit == "--help")
				{
					print_application_help();
					return DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER;
				}
			}
		}
		return DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
	}

	int32_t Debugger::initRuntime(void)
	{
		int32_t init_state = dragon::DragonRuntime::initMachine(debugger.args.machine_config_path,
																debugger.args.verbose_load,
																debugger.args.track_step_diff,
																debugger.args.hide_virtual_display,
																debugger.args.track_call_stack,
																true); //CPU Debug Mode Enabled
		closeEventListener.init();
		if (init_state != 0) return init_state; //TODO: Error

		if (debugger.args.force_load)
			dragon::DragonRuntime::forceLoad(debugger.args.force_load_file, debugger.args.force_load_mem_offset);

		dragon::DisassemblyLoader::loadDirectory(debugger.disassemblyDirectory);
		debugger.code = dragon::DisassemblyLoader::getCodeTable();
		debugger.labels = dragon::DisassemblyLoader::getLabelTable();
		debugger.data = dragon::DisassemblyLoader::getDataTable();

		return DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
	}

	ostd::String Debugger::getCommandInput(void)
	{
		ostd::String cmd;
		ostd::KeyboardController kbc;
		ostd::eKeys key = ostd::eKeys::NoKeyPressed;

		while ((key = kbc.getPressedKey()) != ostd::eKeys::Enter)
		{
			if (key == ostd::eKeys::Escape)
				return InputCommandQuit;
		}
		return kbc.getInputString();

		// ostd::String cmd;
		// std::getline(std::cin, cmd.cpp_str_ref());
		// return cmd;
	}

	int32_t Debugger::topLevelPrompt(void)
	{
		if (!data().args.auto_start_debug)
		{
			bool exit_loop = false;
			bool done_with_auto_data_track = false;
			DragonRuntime::vDisplay.hide();
			while (!exit_loop)
			{
				if (!done_with_auto_data_track)
				{
					data().command = "track ";
					for (auto& sym : debugger.data)
						data().command.add(sym.code).add(" ");
					done_with_auto_data_track = true;
				}
				else
				{
					Display::printPrompt();
					data().command = getCommandInput();
				}
				data().command.trim().toLower();
				if (data().command == "r" || data().command == "run")
				{
					output().p("Starting program...").nl();
					exit_loop = true;
				}
				else if (data().command == "q" || data().command == "quit" || data().command == InputCommandQuit)
				{
					output().nl().p("Exiting debugger...").nl();
					return DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER;
				}
				else if (data().command.startsWith("t ") || data().command.startsWith("track "))
				{
					data().command.substr(data().command.indexOf(" ") + 1).trim();
					exec_watch_command();
				}
				else if (data().command == "h" || data().command == "help")
				{
					print_top_level_prompt_help();
				}
				else if (data().command == "s" || data().command == "step")
				{
					data().args.step_exec = !data().args.step_exec;
					output().p("Step execution = ").p(STR_BOOL(data().args.step_exec)).nl();
				}
				else if (data().command == "display")
				{
					data().args.hide_virtual_display = !data().args.hide_virtual_display;
					output().p("Virtual Display Hidden = ").p(STR_BOOL(data().args.hide_virtual_display)).nl();
				}
			}
		}

		if (!data().args.hide_virtual_display)
			DragonRuntime::vDisplay.show();

		data().currentAddress = DragonRuntime::cpu.readRegister(data::Registers::IP);
		return DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
	}

	int32_t Debugger::executeRuntime(void)
	{
		int32_t rValue = 0;
		bool userQuit = false;
		output().clear().fg(ostd::ConsoleColors::Green).p("Program running...").nl();
		if (!data().args.step_exec)
			output().fg(ostd::ConsoleColors::Yellow).p("Press <Escape> to enter in step-execution mode...").reset().nl();
		while (!userQuit)
		{
			ostd::SignalHandler::refresh();
			if (closeEventListener.hasHappened())
				userQuit = true;
			data().command.clr();
			if (!userQuit && data().args.step_exec)
				rValue = step_execution(userQuit, true);
			else if (!userQuit)
				rValue = normal_runtime(userQuit);
			data().currentAddress = DragonRuntime::cpu.readRegister(data::Registers::IP);
		}
		output().nl().fg(ostd::ConsoleColors::Yellow).p("Execution terminated.").nl().nl().reset();
		return rValue;
	}

	int32_t Debugger::step_execution(bool& outUserQuit, bool exec_first_step)
	{
		if (exec_first_step && !DragonRuntime::cpu.isHalted())
			DragonRuntime::runStep(data().trackedAddresses);
		Display::printStep();
		processErrors();
		if (DragonRuntime::cpu.isInDebugBreakPoint())
			output().fg(ostd::ConsoleColors::Red).p("Reached Debug Break Point.").reset().nl();
		Display::printPrompt();
		data().command = getCommandInput();
		data().command.trim().toLower();
		while (data().command != "")
		{
			if (data().command == "q" || data().command == "quit" || data().command == InputCommandQuit)
			{
				output().nl();
				outUserQuit = true;
				data().command = "";
			}
			else if (data().command == "c" || data().command == "continue")
			{
				data().args.step_exec = false;
				data().command = "";
				output().clear().fg(ostd::ConsoleColors::Green).p("Program running...").nl();
				output().fg(ostd::ConsoleColors::Yellow).p("Press <Escape> to enter in step-execution mode...").reset().nl();
			}
			else if (data().command.startsWith("p ") || data().command.startsWith("print "))
			{
				data().command.substr(data().command.indexOf(" ") + 1).trim();
				const uint8_t TYPE_STRING = 0;
				const uint8_t TYPE_BYTE = 1;
				const uint8_t TYPE_WORD = 2;
				const uint8_t TYPE_DWORD = 4;
				const uint8_t TYPE_QWORD = 8;
				uint8_t type = TYPE_WORD;
				if (data().command.contains(" "))
				{
					ostd::String size_str = data().command.new_substr(0, data().command.indexOf(" ")).trim();
					data().command.substr(data().command.indexOf(" ") + 1).trim();
					if (size_str == "byte") type = TYPE_BYTE;
					else if (size_str == "word") type = TYPE_WORD;
					else if (size_str == "dword") type = TYPE_DWORD;
					else if (size_str == "qword") type = TYPE_QWORD;
					else if (size_str == "string") type = TYPE_STRING;
				}
				if (data().command.isNumeric())
				{
					if (type == TYPE_STRING)
						type = TYPE_WORD;
					uint16_t addr = data().command.toInt();
					uint16_t end_addr = addr;
					ostd::String tmp = "";
					tmp.add("*(").add(ostd::Utils::getHexStr(addr, true, 2));
					if (type != TYPE_BYTE)
					{
						end_addr = addr + type - 1;
						if (end_addr < addr)
							end_addr = addr;
						else
							tmp.add("-").add(ostd::Utils::getHexStr(end_addr, true, 2));
					}
					tmp.add(")");
					ostd::RegexRichString rgx(tmp);
					rgx.fg("\\(|\\)|-", "darkgray");
					rgx.fg("\\*", "red");
					rgx.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "cyan"); //Number Constants
					output().pStyled(rgx);
					output().fg(ostd::ConsoleColors::White).p(" = ");
					output().fg(ostd::ConsoleColors::Gray).p("[");
					for (uint16_t a = addr; a <= end_addr; a++)
					{
						uint8_t value = DragonRuntime::memMap.read8(a);
						output().fg(ostd::ConsoleColors::BrightRed).p(ostd::Utils::getHexStr(value, true, 1));
						if (a < end_addr)
							output().p(" ");
					}
					output().fg(ostd::ConsoleColors::Gray).p("]");
					output().reset().nl();
				}
				else if (data().command.startsWith("$"))
				{
					uint16_t size = 0;
					uint16_t addr = Utils::findSymbol(debugger.data, data().command, &size);
					if (addr == 0)
						addr = Utils::findSymbol(debugger.labels, data().command, &size);
					if (addr == 0)
						output().fg(ostd::ConsoleColors::Red).p("Unknown symbol for <print> command.").reset().nl();
					else
					{
						ostd::String tmp = "";
						tmp.add("*(").add(ostd::Utils::getHexStr(addr, true, 2));
						if (size > 1)
							tmp.add("-").add(ostd::Utils::getHexStr((uint16_t)(addr + size - 1), true, 2));
						tmp.add(")");
						ostd::RegexRichString rgx(tmp);
						rgx.fg("\\(|\\)|-", "darkgray");
						rgx.fg("\\*", "red");
						rgx.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "cyan"); //Number Constants
						output().pStyled(rgx);
						output().fg(ostd::ConsoleColors::White).p(" = ");
						output().fg(ostd::ConsoleColors::Gray).p("[");
						if (type == TYPE_STRING)
							output().fg(ostd::ConsoleColors::BrightRed).p("\"");
						for (uint16_t a = addr; a < addr + size; a++)
						{
							uint8_t value = DragonRuntime::memMap.read8(a);
							if (type == TYPE_STRING)
							{
								output().fg(ostd::ConsoleColors::BrightRed).pChar((char)value);
								continue;
							}
							output().fg(ostd::ConsoleColors::BrightRed).p(ostd::Utils::getHexStr(value, true, 1));
							if (a < addr + size - 1)
								output().p(" ");
						}
						if (type == TYPE_STRING)
							output().fg(ostd::ConsoleColors::BrightRed).p("\"");
						output().fg(ostd::ConsoleColors::Gray).p("]");
						output().reset().nl();
					}
				}
				else
				{
					output().fg(ostd::ConsoleColors::Red).p("Invalid value for <print> command.").reset().nl();
				}
				Display::printPrompt();
				data().command = getCommandInput();
			}
			else
				data().command = Display::changeScreen();
		}
		return DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
	}

	int32_t Debugger::normal_runtime(bool& outUserQuit)
	{
		bool result = DragonRuntime::runStep(data().trackedAddresses);
		bool hasError = DragonRuntime::hasError();
		bool enableStepExec = !result || hasError || Utils::isEscapeKeyPressed() || DragonRuntime::cpu.isInDebugBreakPoint();
		data().args.step_exec = enableStepExec;
		if (enableStepExec)
			return step_execution(outUserQuit, false);
		return DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
	}

	void Debugger::exec_watch_command(void)
	{
		auto tokens = data().command.tokenize();
		for (auto& addr : tokens)
		{
			data().command = addr;
			if (data().command.isNumeric())
			{
				data().trackedAddresses.push_back((uint16_t)data().command.toInt());
			}
			else if (data().command.startsWith("$"))
			{
				uint16_t nbytes = 1;
				uint16_t addr = Utils::findSymbol(data().data, data().command, &nbytes);
				if (addr > 0)
				{
					for (int32_t i = 0; i < nbytes; i++)
						data().trackedAddresses.push_back(addr + i);
				}
				else
				{
					addr = Utils::findSymbol(data().labels, data().command, &nbytes);
					if (addr > 0)
					{
						for (int32_t i = 0; i < nbytes; i++)
							data().trackedAddresses.push_back(addr + i);
					}
				}
			}
			else if (data().command.contains("[") && data().command.endsWith("]"))
			{
				uint16_t nbytes = 1;
				ostd::String str_nbytes = data().command.new_substr(data().command.indexOf("[") + 1).trim();
				str_nbytes.substr(0, str_nbytes.len() - 1);
				data().command.substr(0, data().command.indexOf("[")).trim();
				if (str_nbytes.isNumeric())
					nbytes = str_nbytes.toInt();
				if (nbytes < 1) nbytes = 1;
				if (data().command.isNumeric())
				{
					uint16_t addr = data().command.toInt();
					for (int32_t i = 0; i < nbytes; i++)
						data().trackedAddresses.push_back(addr + i);
				}
			}
		}
		output().p("Tracking ").p((int)data().trackedAddresses.size()).p(" addresses.").nl();
	}

	void Debugger::print_top_level_prompt_help(void)
	{
		int32_t commandLength = 40;
		Utils::printFullLine('#', ostd::ConsoleColors::Black, ostd::ConsoleColors::Red);

		out.nl().fg(ostd::ConsoleColors::Yellow).p("List of available commands:").reset().nl();
		ostd::String tmpCommand = "(r)un";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to start the runtime.").reset().nl();
		tmpCommand = "(q)uit";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to exit the debugger. (<Escape> key can also be used here.)").reset().nl();
		tmpCommand = "(h)elp";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to show this dialog.").reset().nl();
		tmpCommand = "(s)tep";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to enable/disable step-execution for the runtime.").reset().nl();
		tmpCommand = "(t)rack <address[bytes=1]/symbol>";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to track specific addresses during execution.").reset().nl();
		tmpCommand = "display";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to enable/disable the Virtual Display for the runtime.").reset().nl();
		
		out.nl().fg(ostd::ConsoleColors::Cyan).p("The letters in parenthesis can be used as the short version of the command.").nl();
		out.p("Square brackets represent optional parameters. If they have an '=' and a value, that is the default if not specified.").reset().nl();

		out.nl();
		Utils::printFullLine('#', ostd::ConsoleColors::Black, ostd::ConsoleColors::Red);
	}

	void Debugger::print_application_help(void)
	{
		int32_t commandLength = 46;

		out.nl().fg(ostd::ConsoleColors::Yellow).p("List of available parameters:").reset().nl();
		ostd::String tmpCommand = "--verbose-load";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to show more information while loading the virtual machine.").reset().nl();
		tmpCommand = "--step-exec";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Launches the debugger directly in step-execution mode.").reset().nl();
		tmpCommand = "--track-step-diff-off";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Disables the tracking of most machine data used for differential-view.").reset().nl();
		tmpCommand = "--auto-track-data-off";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Disables the automatic tracking of all data-symbols.").reset().nl();
		tmpCommand = "--hide-vdisplay";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Hides the Virtual Display during the execution of the program.").reset().nl();
		tmpCommand = "--auto-start";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Launches the runtime directly, skipping the top-level prompt of the debugger.").reset().nl();
		tmpCommand = "--force-load <binary-file> <ram-offset>";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Injects the specified binary into RAM at the specified offset.").reset().nl();
		tmpCommand = "--help";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Displays this help message.").reset().nl();
		
		out.nl().fg(ostd::ConsoleColors::Magenta).p("Usage: ./ddb <machine-config-file> [...options...]").reset().nl();
		out.nl();
	}
}