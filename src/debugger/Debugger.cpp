#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"
#include "DisassemblyLoader.hpp"
#include <ostd/Defines.hpp>
#include <ostd/Console.hpp>

namespace dragon
{
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

	ostd::String Debugger::Utils::findSymbol(const DisassemblyList& labels, uint16_t address)
	{
		for (auto& label : labels)
		{
			if (label.addr == address)
				return label.code;
		}
		return "";
	}

	uint16_t Debugger::Utils::findSymbol(const DisassemblyList& labels, const ostd::String& symbol)
	{
		for (auto& label : labels)
		{
			if (label.code == symbol)
				return label.addr;
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
		out.fg(ostd::ConsoleColors::Magenta).p(" #/> ").fg(ostd::ConsoleColors::White);
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
		int32_t cw = ostd::Utils::getConsoleWidth();
		ostd::String str = ostd::Utils::duplicateChar('#', cw);
		out.bg(ostd::ConsoleColors::Yellow).fg(ostd::ConsoleColors::Black).p(str.cpp_str()).reset().nl();
	}

	void Debugger::Display::printDiff(void)
	{
		out.clear();
		
		ostd::String str;
		str.add("|===============|================PREV================|================CURR================|=====|====REG====|====REG====|");
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
		str.add("| SubRoutine:   |*%PREV_SUB_ROUTINE%*****************|**%CURR_SUB_ROUTINE%****************| R6  |*%PREV_R7%*|*%CURR_R7%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("|    IP:        |*%PREV_IP%**************************|**%CURR_IP%*************************| R7  |*%PREV_R8%*|*%CURR_R8%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("|    SP:        |*%PREV_SP%**************************|**%CURR_SP%*************************| R8  |*%PREV_R9%*|*%CURR_R9%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("|    FP:        |*%PREV_FP%**************************|**%CURR_FP%*************************| R9  |*%PREV_R10%|*%CURR_R10%|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|=====|===========|===========|");
		str.add("\n");
		str.add("|    RV:        |*%PREV_RV%**************************|**%CURR_RV%*************************|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|");
		str.add("\n");
		str.add("|    PP:        |*%PREV_PP%**************************|**%CURR_PP%*************************|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|");
		str.add("\n");
		str.add("|    FL:        |*%PREV_FL%**************************|**%CURR_FL%*************************|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|");
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


		
		ostd::RegexRichString rgxstr(str);
		rgxstr.fg("InstAddr|Code|StackFrame|DBG BRK|INT Handler|BIOS Mode|SubRoutine", "Magenta");
		rgxstr.fg("IP|SP|FP|RV|PP|FL|ACC", "Cyan");
		rgxstr.fg("R10|R2|R3|R4|R5|R6|R7|R8|R9|R1", "BrightGreen");
		rgxstr.fg("PREV", "Red");
		rgxstr.fg("CURR", "Green");
		out.pStyled(rgxstr);

		out.nl().nl().fg(ostd::ConsoleColors::Yellow).p("Pres <Enter> to continue execution...").nl().reset();
		std::cin.get();
	}

	void Debugger::Display::printTrackedAddresses(void)
	{
		out.clear();

		const dragon::DragonRuntime::tMachineDebugInfo& minfo = dragon::DragonRuntime::getMachineInfoDiff();
		if (minfo.trackedAddresses.size() == 0) return;
		for (int32_t i = 0; i < minfo.trackedAddresses.size(); i++)
		{
			auto addr = minfo.trackedAddresses[i];
			ostd::String symbol = Utils::findSymbol(debugger.data, addr);
			if (symbol == "")
				symbol = Utils::findSymbol(debugger.labels, addr);
			symbol.fixedLength(30);
			out.p(symbol);
			out.p(ostd::Utils::getHexStr(addr, true, 2)).p("    ");
			out.p(ostd::Utils::getHexStr(minfo.previousInstructionTrackedValues[i], true, 1)).p("    ");
			out.p(ostd::Utils::getHexStr(minfo.currentInstructionTrackedValues[i], true, 1)).p("    ");
			out.nl();
		}

		out.nl().nl().fg(ostd::ConsoleColors::Yellow).p("Pres <Enter> to continue execution...").nl().reset();
		std::cin.get();
	}

	void Debugger::Display::printStack(void)
	{
		out.clear();

		const dragon::DragonRuntime::tMachineDebugInfo& minfo = dragon::DragonRuntime::getMachineInfoDiff();
		uint16_t stack_ptr = DragonRuntime::cpu.readRegister(data::Registers::SP);
		std::cout << "      " << ostd::Utils::getHexStr(stack_ptr, true, 2) << "      \n";
		ostd::Utils::printByteStream(*DragonRuntime::ram.getByteStream(), 0xFF80, 16, 8, out, stack_ptr, 1, "Stack");

		out.nl();

		for (auto& frame : dragon::DragonRuntime::cpu.m_debug_stackFrameStrings)
		{
			ostd::RegexRichString rgxrstr(frame);
			rgxrstr.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "Red"); //Number Constants
			rgxrstr.fg("(?<!\\w)(r[1-9]|r10|fl|pp|rv|fp|sp|ip|acc)(?!\\w)", "BrightGreen", true); //Registers
			rgxrstr.fg("(?<!\\w)(args|argc|StackFrame|New FP)(?!\\w)", "Magenta", true); //Other

			out.reset().nl().fg(ostd::ConsoleColors::BrightBlue).p("=======================").nl().reset();
			out.pStyled(rgxrstr);
		}
		out.reset().nl().fg(ostd::ConsoleColors::BrightBlue).p("=======================").nl().reset();

		out.nl().nl().fg(ostd::ConsoleColors::Yellow).p("Pres <Enter> to continue execution...").nl().reset();
		std::cin.get();
	}

	void Debugger::Display::printCallStack(void)
	{
		out.clear();

		const dragon::DragonRuntime::tMachineDebugInfo& minfo = dragon::DragonRuntime::getMachineInfoDiff();
		
		auto& callStack = minfo.callStack;
		int32_t depth = -1;
		for (auto& call : callStack)
		{
			if (call.info.new_trim().toLower().startsWith("ret"))
				depth--;
			else
				depth++;
			if (depth < 0) depth = 0;
			out.p(ostd::String().addLeftPadding(depth * 4));
			out.p(call.info.new_fixedLength(10)).p(" ");
			ostd::String subroutine_name = Utils::findSymbol(debugger.labels, call.addr);
			out.p(ostd::Utils::getHexStr(call.addr, true, 2));
			if (call.info.new_trim().toLower() == "call imm" && subroutine_name != "")
				out.p(" (").p(subroutine_name).p(")");
			out.nl();
		}

		out.nl().nl().fg(ostd::ConsoleColors::Yellow).p("Pres <Enter> to continue execution...").nl().reset();
		std::cin.get();
	}

	ostd::String Debugger::Display::changeScreen(void)
	{
		if (debugger.command == "diff")
		{
			printDiff();
			printStep();
			printPrompt();
			return getCommandInput();
		}
		else if (debugger.command == "tracker")
		{
			printTrackedAddresses();
			printStep();
			printPrompt();
			return getCommandInput();
		}
		else if (debugger.command == "stack")
		{
			printStack();
			printStep();
			printPrompt();
			return getCommandInput();
		}
		else if (debugger.command == "call")
		{
			printCallStack();
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
			out.fg("red").p("Error, too few arguments.").reset().nl();
			return 128;
		}
		else
		{
			debugger.args.machine_config_path = argv[1];
			for (int32_t i = 2; i < argc; i++)
			{
				ostd::String edit(argv[i]);
				if (edit == "--verbose-load")
					debugger.args.verbose_load = true;
				else if (edit == "--step-exec")
					debugger.args.step_exec = true;
				else if (edit == "--track-step-diff-off")
					debugger.args.track_step_diff = false;
				else if (edit == "--hide-vdisplay")
					debugger.args.hide_virtual_display = true;
				else if (edit == "--auto-start")
					debugger.args.auto_start_debug = true;
				else if (edit == "--force-load")
				{
					if ((argc - 1) - i < 2)
						break; //TODO: Warning
					i++;
					debugger.args.force_load_file = argv[i];
					i++;
					edit = argv[i];
					if (!edit.isNumeric())
						continue; //TODO: Error
					debugger.args.force_load_mem_offset = (uint16_t)edit.toInt();
					debugger.args.force_load = true;
				}
			}
		}
		return 0;
	}

	int32_t Debugger::initRuntime(void)
	{
		int32_t init_state = dragon::DragonRuntime::initMachine(debugger.args.machine_config_path,
																debugger.args.verbose_load,
																debugger.args.track_step_diff,
																debugger.args.hide_virtual_display,
																debugger.args.track_call_stack,
																true); //CPU Debug Mode Enabled
		if (init_state != 0) return init_state; //TODO: Error

		if (debugger.args.force_load)
			dragon::DragonRuntime::forceLoad(debugger.args.force_load_file, debugger.args.force_load_mem_offset);

		dragon::DisassemblyLoader::loadDirectory(debugger.disassemblyDirectory);
		debugger.code = dragon::DisassemblyLoader::getCodeTable();
		debugger.labels = dragon::DisassemblyLoader::getLabelTable();
		debugger.data = dragon::DisassemblyLoader::getDataTable();

		return 0;
	}

	ostd::String Debugger::getCommandInput(void)
	{
		// ostd::String cmd;
		// ostd::KeyboardController keyboard;
		
		// ostd::eKeys key = ostd::eKeys::NoKeyPressed;
		// int32_t commandHistoryIndex = ZERO(commandHistory.size() - 1);
		// while (key != ostd::eKeys::Enter)
		// {
		// 	key = keyboard.waitForKeyPress();
		// 	if (key == ostd::eKeys::Up)
		// 	{
		// 		if (commandHistory.size() == 0)
		// 			continue;
		// 		Utils::clearConsoleLine();
		// 		Display::printPrompt();
		// 		out.p(commandHistory[commandHistoryIndex]);
		// 		commandHistoryIndex = ZERO(commandHistoryIndex - 1);
		// 	}
		// 	else if (key == ostd::eKeys::Down)
		// 	{
		// 		if (commandHistory.size() == 0)
		// 			continue;
		// 		Utils::clearConsoleLine();
		// 		Display::printPrompt();
		// 		out.p(commandHistory[commandHistoryIndex]);
		// 		commandHistoryIndex = CAP(commandHistoryIndex + 1, commandHistory.size() - 1);
		// 	}
		// 	else if (key == ostd::eKeys::Enter)
		// 	{
		// 		cmd = keyboard.getInputString();
		// 		commandHistory.push_back(cmd);
		// 	}
		// }

		// return cmd;

		ostd::String cmd;
		std::getline(std::cin, cmd.cpp_str_ref());
		return cmd;
	}

}