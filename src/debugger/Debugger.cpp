#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"
#include "DisassemblyLoader.hpp"
#include <ostd/Defines.hpp>

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

	uint16_t Debugger::Utils::findSymbol(const DisassemblyList& labels, const ostd::StringEditor& symbol)
	{
		for (auto& label : labels)
		{
			if (label.code == symbol.str())
				return label.addr;
		}
		return 0x0000;
	}

	bool Debugger::Utils::isValidLabelNameChar(char c)
	{
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_');
	}

	ostd::StringEditor Debugger::Utils::fillString(const ostd::StringEditor& str, char fill, int32_t totalLength)
	{
		int32_t fillLen = totalLength - str.len();
		if (fillLen < 1) return str;
		ostd::StringEditor newStr = str;
		newStr.add(ostd::Utils::duplicateChar(fill, fillLen));
		return newStr;
	}




	//Debugger::Display
	void Debugger::Display::colorizeInstructionBody(const ostd::String& instBody, bool currentLine, const DisassemblyList& labelList)
	{
		ostd::RegexRichString rgxrstr(instBody);
		rgxrstr.fg("\\{|\\}|\\+|\\*|\\-|\\/|\\(|\\)|\\[|\\]", "Red"); //Operators
		rgxrstr.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "BrightYellow"); //Number Constants
		rgxrstr.fg("(?<!\\w)(r[1-9]|r10|fl|pp|rv|fp|sp|ip|acc)(?!\\w)", "BrightGreen", true); //Registers
		rgxrstr.col("\\$\\w+", "Cyan", "Black"); //Labels
		ostd::StringEditor instEdit = rgxrstr.getRawString();
		for (auto& label : labelList)
		{
			int32_t index = -1;
			ostd::StringEditor labelEdit = label.code;
			labelEdit.trim();
			while ((index = instEdit.indexOf(labelEdit.str(), index + 1)) != -1)
			{
				if (index + labelEdit.str().length() < instEdit.len() && Utils::isValidLabelNameChar(instEdit.at(index + labelEdit.str().length())))
					continue;
				ostd::String instStr = instEdit.str();
				instStr.replace(index, labelEdit.str().length(), labelEdit.str() + "[@@ style foreground:brightgray](" + ostd::Utils::getHexStr(label.addr, true, 2) + ")[@@/]");
				instEdit = instStr;
			}
		}
		rgxrstr.setRawString(instEdit);
		out.p("\t").pStyled(rgxrstr);
		if (currentLine)
			out.p("   ").col(ostd::legacy::ConsoleCol::OnYellow).col(ostd::legacy::ConsoleCol::Black).p(" <-- ");
		out.nl();
		out.reset().resetColors();
	}

	void Debugger::Display::colorCodeInstructions(const ostd::String& inst, bool currentLine, const DisassemblyList& labelList)
	{
		ostd::StringEditor instEditor = inst;
		ostd::StringEditor instBody = "";
		ostd::StringEditor instHead = inst;
		instEditor.trim();
		if (instEditor.contains(" "))
		{
			instHead = instEditor.substr(0, instEditor.indexOf(" "));
			instBody = instEditor.substr(instEditor.indexOf(" "));
		}
		if (currentLine)
		{
			out.col(ostd::legacy::ConsoleCol::OnYellow).col(ostd::legacy::ConsoleCol::Black);
		}
		else if (instHead.str() == "call" || instHead.str() == "ret" || instHead.str() == "int" || instHead.str() == "rti")
			out.col(ostd::legacy::ConsoleCol::OnBrightRed).col(ostd::legacy::ConsoleCol::White);
		else if (instHead.str() == "hlt" || instHead.str() == "debug_break")
			out.col(ostd::legacy::ConsoleCol::Red);
		else if (instHead.str() == "nop")
			out.col(ostd::legacy::ConsoleCol::Gray);
		else
			out.col(ostd::legacy::ConsoleCol::Blue);
		out.p(instHead);
		out.reset().resetColors();
		colorizeInstructionBody(instBody.str(), currentLine, labelList);
	}

	void Debugger::Display::printPrompt(void)
	{
		out.col(ostd::legacy::ConsoleCol::Magenta).p(" #/> ").col(ostd::legacy::ConsoleCol::White);
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
			bool specialSection = _da.code.starts_with("[") &&_da.code.ends_with("]");
			if (label.length() < debugger.labelLineLength)
			{
				label += ostd::Utils::duplicateChar(' ', debugger.labelLineLength - label.length());
			}
			else if (label.length() > debugger.labelLineLength)
			{
				ostd::StringEditor edit(label);
				edit = edit.substr(0, debugger.labelLineLength - 3);
				edit.add("...");
				label = edit.str();
			}
			out.col(ostd::legacy::ConsoleCol::Gray).p(label).p("  ");
			if (currentLine)
			{
				out.col(ostd::legacy::ConsoleCol::Black).col(ostd::legacy::ConsoleCol::OnYellow).p(ostd::Utils::getHexStr(_da.addr, true, 2)).p("  ").reset();;
			}
			else
			{
				if (specialSection)
					out.col(ostd::legacy::ConsoleCol::Cyan);
				else
					out.col(ostd::legacy::ConsoleCol::BrightGray);
				out.p(ostd::Utils::getHexStr(_da.addr, true, 2)).p("  ");
			}
			if (specialSection)
				out.col(ostd::legacy::ConsoleCol::Cyan).p(_da.code).nl();
			else 
				colorCodeInstructions(_da.code, currentLine, debugger.labels);
			out.reset();
		}
		int32_t cw = ostd::Utils::getConsoleWidth();
		ostd::String str = ostd::Utils::duplicateChar('#', cw);
		out.col(ostd::legacy::ConsoleCol::OnYellow).col(ostd::legacy::ConsoleCol::Black).p(str).reset().nl();
	}

	void Debugger::Display::printDiff(void)
	{
		out.clear();
		
		ostd::StringEditor str;
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
		str.add("|    IP:        |*%PREV_IP%**************************|**%CURR_IP%*************************| R7  |*%PREV_R7%*|*%CURR_R7%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("|    SP:        |*%PREV_SP%**************************|**%CURR_SP%*************************| R8  |*%PREV_R8%*|*%CURR_R8%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("|    FP:        |*%PREV_FP%**************************|**%CURR_FP%*************************| R9  |*%PREV_R9%*|*%CURR_R9%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|-----|-----------|-----------|");
		str.add("\n");
		str.add("|    RV:        |*%PREV_RV%**************************|**%CURR_RV%*************************| R10 |*%PREV_R10%|*%CURR_R10%|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|=====|===========|===========|");
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
		ostd::StringEditor tmp = " ", tmpStyle = "";

		//Instruction Address
		{
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionAddress, true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_INST_ADDR%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionAddress, true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionAddress != minfo.previousInstructionAddress)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_INST_ADDR%", tmpStyle.str());
		}

		//Code
		{
			tmp = " ", tmpStyle = "";
			ostd::StringEditor prevCode = " (";
			prevCode.add(minfo.previousInstructionOpCode).add(") ");
			for (int32_t i = 0; i < minfo.previousInstructionFootprintSize; i++)
				prevCode.add(ostd::Utils::getHexStr(minfo.previousInstructionFootprint[i], false, 1)).add(" ");
			tmp.add(prevCode.str());
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Black,background:BrightGray]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_CODE%", tmpStyle.str());

			// tmp = " ";
			// ostd::StringEditor currCode = " (";
			// currCode.add(minfo.currentInstructionOpCode).add(") ");
			// for (int32_t i = 0; i < minfo.currentInstructionFootprintSize; i++)
			// 	currCode.add(ostd::Utils::getHexStr(minfo.currentInstructionFootprint[i], false, 1)).add(" ");
			// tmp.add(currCode.str());
			// tmp = fillString(tmp, ' ', item_len);
			// if (currCode.str() != prevCode.str())
			// 	tmpStyle = "[@@style foreground:Black,background:Yellow]";
			// else
			// 	tmpStyle = "[@@style foreground:Blue]";
			// tmpStyle.add(tmp.str()).add("[@@/]");
			// str.replaceAll("%CURR_CODE%", tmpStyle.str());
		}

		//Stack Frame
		{
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionStackFrameSize, true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_STACK_FRAME%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionStackFrameSize, true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionStackFrameSize != minfo.previousInstructionStackFrameSize)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_STACK_FRAME%", tmpStyle.str());
		}

		//Debug Break
		{
			tmp = " ", tmpStyle = "";
			tmp.add(STR_BOOL(minfo.previousInstructionDebugBreak));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_DBG_BRK%", tmpStyle.str());

			tmp = " ";
			tmp.add(STR_BOOL(minfo.currentInstructionDebugBreak));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionDebugBreak != minfo.previousInstructionDebugBreak)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_DBG_BRK%", tmpStyle.str());
		}

		//INT Handler
		{
			tmp = " ", tmpStyle = "";
			tmp.add(STR_BOOL(minfo.previousInstructionInterruptHandler));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_INT_HANDLER%", tmpStyle.str());

			tmp = " ";
			tmp.add(STR_BOOL(minfo.currentInstructionInterruptHandler));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionInterruptHandler != minfo.previousInstructionInterruptHandler)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_INT_HANDLER%", tmpStyle.str());
		}
		
		//Bios Mode
		{
			tmp = " ", tmpStyle = "";
			tmp.add(STR_BOOL(minfo.previousInstructionBiosMode));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_BIOS_MODE%", tmpStyle.str());

			tmp = " ";
			tmp.add(STR_BOOL(minfo.currentInstructionBiosMode));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionBiosMode != minfo.previousInstructionBiosMode)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_BIOS_MODE%", tmpStyle.str());
		}

		//System Registers
		{
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::IP], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_IP%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::IP], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::IP] != minfo.previousInstructionRegisters[dragon::data::Registers::IP])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_IP%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::SP], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_SP%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::SP], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::SP] != minfo.previousInstructionRegisters[dragon::data::Registers::SP])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_SP%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::FP], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_FP%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::FP], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::FP] != minfo.previousInstructionRegisters[dragon::data::Registers::FP])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_FP%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::RV], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_RV%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::RV], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::RV] != minfo.previousInstructionRegisters[dragon::data::Registers::RV])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_RV%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::PP], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_PP%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::PP], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::PP] != minfo.previousInstructionRegisters[dragon::data::Registers::PP])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_PP%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::FL], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_FL%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::FL], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::FL] != minfo.previousInstructionRegisters[dragon::data::Registers::FL])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_FL%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::ACC], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_ACC%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::ACC], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::ACC] != minfo.previousInstructionRegisters[dragon::data::Registers::ACC])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_ACC%", tmpStyle.str());
		}

		item_len = 11;
		//General Registers
		{
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R1], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_R1%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R1], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R1] != minfo.previousInstructionRegisters[dragon::data::Registers::R1])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_R1%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R2], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_R2%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R2], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R2] != minfo.previousInstructionRegisters[dragon::data::Registers::R2])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_R2%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R3], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_R3%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R3], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R3] != minfo.previousInstructionRegisters[dragon::data::Registers::R3])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_R3%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R4], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_R4%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R4], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R4] != minfo.previousInstructionRegisters[dragon::data::Registers::R4])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_R4%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R5], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_R5%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R5], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R5] != minfo.previousInstructionRegisters[dragon::data::Registers::R5])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_R5%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R6], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_R6%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R6], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R6] != minfo.previousInstructionRegisters[dragon::data::Registers::R6])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_R6%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R7], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_R7%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R7], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R7] != minfo.previousInstructionRegisters[dragon::data::Registers::R7])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_R7%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R8], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_R8%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R8], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R8] != minfo.previousInstructionRegisters[dragon::data::Registers::R8])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_R8%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R9], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_R9%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R9], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R9] != minfo.previousInstructionRegisters[dragon::data::Registers::R9])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_R9%", tmpStyle.str());



			
			tmp = " ", tmpStyle = "";
			tmp.add(ostd::Utils::getHexStr(minfo.previousInstructionRegisters[dragon::data::Registers::R10], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%PREV_R10%", tmpStyle.str());

			tmp = " ";
			tmp.add(ostd::Utils::getHexStr(minfo.currentInstructionRegisters[dragon::data::Registers::R10], true, 2));
			tmp = Utils::fillString(tmp, ' ', item_len);
			if (minfo.currentInstructionRegisters[dragon::data::Registers::R10] != minfo.previousInstructionRegisters[dragon::data::Registers::R10])
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp.str()).add("[@@/]");
			str.replaceAll("%CURR_R10%", tmpStyle.str());
		}


		
		ostd::RegexRichString rgxstr(str);
		rgxstr.fg("InstAddr|Code|StackFrame|DBG BRK|INT Handler|BIOS Mode", "Magenta");
		rgxstr.fg("IP|SP|FP|RV|PP|FL|ACC", "Cyan");
		rgxstr.fg("R10|R2|R3|R4|R5|R6|R7|R8|R9|R1", "BrightGreen");
		rgxstr.fg("PREV", "Red");
		rgxstr.fg("CURR", "Green");
		out.pStyled(rgxstr);

		out.nl().nl().col(ostd::legacy::ConsoleCol::Yellow).p("Pres <Enter> to continue execution...").nl().reset();
		std::cin.get();
	}

	void Debugger::Display::printTrackedAddresses(const std::vector<uint16_t>& trackedAddresses)
	{

	}

	ostd::StringEditor Debugger::Display::changeScreen(void)
	{
		if (debugger.command.str() == "diff")
		{
			printDiff();
			printStep();
			return getCommandInput();
		}
		else if (debugger.command.str() == "tracker")
		{
			printTrackedAddresses(debugger.trackedAddresses);
			printStep();
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
			out.nl().col(ostd::legacy::ConsoleCol::Red).p("Error ").p(ostd::Utils::getHexStr(err.code, true, 8)).p(": ").p(err.text).nl();
		}
		debugger.args.step_exec = true;
	}

	int32_t Debugger::loadArguments(int argc, char** argv)
	{
		if (argc < 2)
		{
			out.col("red").p("Error, too few arguments.").resetColors().nl();
			return 128;
		}
		else
		{
			debugger.args.machine_config_path = argv[1];
			for (int32_t i = 2; i < argc; i++)
			{
				ostd::StringEditor edit(argv[i]);
				if (edit.str() == "--verbose-load")
					debugger.args.verbose_load = true;
				else if (edit.str() == "--step-exec")
					debugger.args.step_exec = true;
				else if (edit.str() == "--track-step-diff")
					debugger.args.track_step_diff = true;
				else if (edit.str() == "--hide-vdisplay")
					debugger.args.hide_virtual_display = true;
				else if (edit.str() == "--auto-start")
					debugger.args.auto_start_debug = true;
				else if (edit.str() == "--force-load")
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
		int32_t init_state = dragon::DragonRuntime::initMachine(debugger.args.machine_config_path, debugger.args.verbose_load, debugger.args.track_step_diff, debugger.args.hide_virtual_display);
		if (init_state != 0) return init_state; //TODO: Error

		if (debugger.args.force_load)
			dragon::DragonRuntime::forceLoad(debugger.args.force_load_file, debugger.args.force_load_mem_offset);

		dragon::DisassemblyLoader::loadDirectory(debugger.disassemblyDirectory.str());
		debugger.code = dragon::DisassemblyLoader::getCodeTable();
		debugger.labels = dragon::DisassemblyLoader::getLabelTable();
		debugger.data = dragon::DisassemblyLoader::getDataTable();

		return 0;
	}

	ostd::StringEditor Debugger::getCommandInput(void)
	{
		ostd::String cmd;
		std::getline(std::cin, cmd);
		return cmd;
	}

}