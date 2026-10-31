#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"
#include <ostd/io/Memory.hpp>
#include <ostd/string/String.hpp>
#include <ostd/string/TextStyleParser.hpp>

namespace dragon
{
	//Debugger::Display
	void Debugger::Display::colorizeInstructionBody(ostd::OutputHandlerBase& out, const String& instBody, const DisassemblyList& labelList)
	{
		ostd::RegexRichString rgxrstr(instBody);
		rgxrstr.fg("\\{|\\}|\\+|\\*|\\-|\\/|\\(|\\)|\\[|\\]", "Red"); //Operators
		rgxrstr.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "BrightYellow"); //Number Constants
		rgxrstr.fg("(?<!\\w)(r[1-9]|r10|fl|pp|rv|fp|sp|ip|acc)(?!\\w)", "BrightGreen", true); //Registers
		rgxrstr.fg("\\%low", "Magenta");
		rgxrstr.col("\\$\\w+", "Cyan", "Black"); //Labels
		String instEdit = rgxrstr.getRawString();
		for (auto& label : labelList)
		{
			i32 index = -1;
			String labelEdit = label.code;
			labelEdit.trim();
			while ((index = instEdit.indexOf(labelEdit, index + 1)) != -1)
			{
				if (index + labelEdit.len() < instEdit.len() && Utils::isValidLabelNameChar(instEdit.at(index + labelEdit.len())))
					continue;
				String instStr = instEdit;
				instStr.cpp_str_ref().replace(index, labelEdit.len(), labelEdit.cpp_str() + "[@@ style foreground:brightgray](" + String::getHexStr(label.addr, true, 2).cpp_str() + ")[@@/]");
				instEdit = instStr;
			}
		}
		rgxrstr.setRawString(instEdit);
		out.p("\t").pStyled(rgxrstr);
		out.reset();
	}

	void Debugger::Display::colorCodeInstructions(ostd::OutputHandlerBase& out, const String& inst, const DisassemblyList& labelList)
	{
		String instEditor = inst;
		String instBody = "";
		String instHead = inst;
		instEditor.trim();
		if (instEditor.contains(" "))
		{
			instHead = instEditor.new_substr(0, instEditor.indexOf(" "));
			instBody = instEditor.new_substr(instEditor.indexOf(" "));
		}
		if (instHead == "call" || instHead == "ret" || instHead == "int" || instHead == "rti")
			out.bg(ostd::ConsoleColors::BrightRed).fg(ostd::ConsoleColors::White);
		else if (instHead == "hlt" || instHead == "debug_break")
			out.fg(ostd::ConsoleColors::Red);
		else if (instHead == "nop")
			out.fg(ostd::ConsoleColors::Gray);
		else if(instHead.isNumeric())
			out.fg(ostd::ConsoleColors::Magenta);
		else
			out.fg(ostd::ConsoleColors::Blue);
		out.p(instHead.cpp_str());
		out.reset();
		colorizeInstructionBody(out, instBody, labelList);
	}

	void Debugger::Display::printDisassemblyLine(ostd::OutputHandlerBase& out, const Debugger& dbg, const code::Assembler::tDisassemblyLine& _da)
	{
		// Two columns of gutter, for the breakpoint / current line markers.
		out.p("  ");
		String label = Utils::findSymbol(dbg.data().labels, _da.addr);
		bool specialSection = _da.code.startsWith("[") &&_da.code.endsWith("]");
		label.fixedLength(dbg.data().labelLineLength);
		out.fg(ostd::ConsoleColors::Gray).p(label.cpp_str()).p("  ");
		if (specialSection)
			out.fg(ostd::ConsoleColors::Cyan);
		else if (_da.code == "debug_break")
			out.fg(ostd::ConsoleColors::Red);
		else
			out.fg(ostd::ConsoleColors::BrightGray);
		out.p(String::getHexStr(_da.addr, true, 2).cpp_str()).p("  ");
		if (specialSection)
			out.fg(ostd::ConsoleColors::Cyan).p(_da.code.cpp_str());
		else
			colorCodeInstructions(out, _da.code, dbg.data().labels);
		out.reset();
	}

	void Debugger::Display::printDiff(ostd::OutputHandlerBase& out, const Debugger& dbg)
	{
		String str;
		str.add("|===============|================PREV================|================CURR================|=====|===PREV====|===CURR====|");
		str.add("\n");
		str.add("| InstAddr:     |*%PREV_INST_ADDR%*******************|*%CURR_INST_ADDR%*******************| R1  |*%PREV_R1%*|*%CURR_R1%*|");
		str.add("\n");
		str.add("| Code:         |*%PREV_CODE%************************|*%CURR_CODE%************************| R2  |*%PREV_R2%*|*%CURR_R2%*|");
		str.add("\n");
		str.add("| StackFrame:   |*%PREV_STACK_FRAME%*****************|*%CURR_STACK_FRAME%*****************| R3  |*%PREV_R3%*|*%CURR_R3%*|");
		str.add("\n");
		str.add("| DBG BRK:      |*%PREV_DBG_BRK%*********************|*%CURR_DBG_BRK%*********************| R4  |*%PREV_R4%*|*%CURR_R4%*|");
		str.add("\n");
		str.add("| INT Handler:  |*%PREV_INT_HANDLER%*****************|*%CURR_INT_HANDLER%*****************| R5  |*%PREV_R5%*|*%CURR_R5%*|");
		str.add("\n");
		str.add("| BIOS Mode:    |*%PREV_BIOS_MODE%*******************|**%CURR_BIOS_MODE%******************| R6  |*%PREV_R6%*|*%CURR_R6%*|");
		str.add("\n");
		str.add("| SubRoutine:   |*%PREV_SUB_ROUTINE%*****************|**%CURR_SUB_ROUTINE%****************| R7  |*%PREV_R7%*|*%CURR_R7%*|");
		str.add("\n");
		str.add("|    IP:        |*%PREV_IP%**************************|**%CURR_IP%*************************| R8  |*%PREV_R8%*|*%CURR_R8%*|");
		str.add("\n");
		str.add("|    SP:        |*%PREV_SP%**************************|**%CURR_SP%*************************| R9  |*%PREV_R9%*|*%CURR_R9%*|");
		str.add("\n");
		str.add("|    FP:        |*%PREV_FP%**************************|**%CURR_FP%*************************| R10 |*%PREV_R10%|*%CURR_R10%|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|=====|===========|===========|");
		str.add("\n");
		str.add("|    RV:        |*%PREV_RV%**************************|**%CURR_RV%*************************| S1  |*%PREV_S1%*|*%CURR_S1%*|");
		str.add("\n");
		str.add("|    PP:        |*%PREV_PP%**************************|**%CURR_PP%*************************| S2  |*%PREV_S2%*|*%CURR_S2%*|");
		str.add("\n");
		str.add("|    FL:        |*%PREV_FL%**************************|**%CURR_FL%*************************| OF  |*%PREV_OF%*|*%CURR_OF%*|");
		str.add("\n");
		str.add("|---------------|------------------------------------|------------------------------------|=====|===========|===========|");
		str.add("\n");
		str.add("|    ACC:       |*%PREV_ACC%*************************|**%CURR_ACC%************************|");
		str.add("\n");
		str.add("|===============|====================================|====================================|");


		str.replaceAll("*", "");
		const Debugger::tMachineInfo& prev = dbg.getPreviousInfo();
		const Debugger::tMachineInfo& curr = dbg.getCurrentInfo();
		// One PREV / CURR pair: the current value is highlighted when it changed.
		auto l_pair = [&str](const String& prevKey, const String& prevValue, const String& currKey, const String& currValue, i32 item_len) {
			String tmp = " ";
			tmp.add(prevValue);
			tmp.addPadding(item_len, ' ', String::ePaddingBehavior::AllowOddExtraLeft);
			String tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp).add("[@@/]");
			str.replaceAll(prevKey, tmpStyle);

			String tmp2 = " ";
			tmp2.add(currValue);
			tmp2.addPadding(item_len, ' ', String::ePaddingBehavior::AllowOddExtraLeft);
			if (currValue != prevValue)
				tmpStyle = "[@@style foreground:Black,background:BrightRed]";
			else
				tmpStyle = "[@@style foreground:Blue]";
			tmpStyle.add(tmp2).add("[@@/]");
			str.replaceAll(currKey, tmpStyle);
		};
		auto l_hex = [](i16 value) { return String::getHexStr(cast<u16>(value), true, 2); };
		auto l_code = [](const Debugger::tMachineInfo& info) {
			String code = " (";
			code.add(info.opCode).add(") ");
			for (i32 i = 0; i < info.footprintSize; i++)
				code.add(String::getHexStr(info.footprint[i], false, 1)).add(" ");
			return code;
		};
		i32 item_len = 36;

		l_pair("%PREV_INST_ADDR%", l_hex(prev.instructionAddress), "%CURR_INST_ADDR%", l_hex(curr.instructionAddress), item_len);
		l_pair("%PREV_CODE%", l_code(prev), "%CURR_CODE%", l_code(curr), item_len);
		l_pair("%PREV_STACK_FRAME%", l_hex(prev.stackFrameSize), "%CURR_STACK_FRAME%", l_hex(curr.stackFrameSize), item_len);
		l_pair("%PREV_DBG_BRK%", STR_BOOL(prev.debugBreak), "%CURR_DBG_BRK%", STR_BOOL(curr.debugBreak), item_len);
		l_pair("%PREV_INT_HANDLER%", String("").add(prev.interruptHandlerCount), "%CURR_INT_HANDLER%", String("").add(curr.interruptHandlerCount), item_len);
		l_pair("%PREV_BIOS_MODE%", STR_BOOL(prev.biosMode), "%CURR_BIOS_MODE%", STR_BOOL(curr.biosMode), item_len);
		l_pair("%PREV_SUB_ROUTINE%", String("").add(prev.subroutineCounter).add(" (").add(STR_BOOL(prev.inSubroutine)).add(")"),
			   "%CURR_SUB_ROUTINE%", String("").add(curr.subroutineCounter).add(" (").add(STR_BOOL(curr.inSubroutine)).add(")"), item_len);

		//System Registers
		l_pair("%PREV_IP%", l_hex(prev.registers[data::Registers::IP]), "%CURR_IP%", l_hex(curr.registers[data::Registers::IP]), item_len);
		l_pair("%PREV_SP%", l_hex(prev.registers[data::Registers::SP]), "%CURR_SP%", l_hex(curr.registers[data::Registers::SP]), item_len);
		l_pair("%PREV_FP%", l_hex(prev.registers[data::Registers::FP]), "%CURR_FP%", l_hex(curr.registers[data::Registers::FP]), item_len);
		l_pair("%PREV_RV%", l_hex(prev.registers[data::Registers::RV]), "%CURR_RV%", l_hex(curr.registers[data::Registers::RV]), item_len);
		l_pair("%PREV_PP%", l_hex(prev.registers[data::Registers::PP]), "%CURR_PP%", l_hex(curr.registers[data::Registers::PP]), item_len);
		l_pair("%PREV_FL%", l_hex(prev.registers[data::Registers::FL]), "%CURR_FL%", l_hex(curr.registers[data::Registers::FL]), item_len);
		l_pair("%PREV_ACC%", l_hex(prev.registers[data::Registers::ACC]), "%CURR_ACC%", l_hex(curr.registers[data::Registers::ACC]), item_len);

		item_len = 11;
		//General Registers
		const std::pair<const char*, u8> general[] = {
			{ "R1", data::Registers::R1 }, { "R2", data::Registers::R2 }, { "R3", data::Registers::R3 },
			{ "R4", data::Registers::R4 }, { "R5", data::Registers::R5 }, { "R6", data::Registers::R6 },
			{ "R7", data::Registers::R7 }, { "R8", data::Registers::R8 }, { "R9", data::Registers::R9 },
			{ "R10", data::Registers::R10 },
			//Special Registers
			{ "S1", data::Registers::S1 }, { "S2", data::Registers::S2 }, { "OF", data::Registers::OFFSET }
		};
		for (const auto& reg : general)
		{
			const String name = reg.first;
			l_pair(String("%PREV_").add(name).add("%"), l_hex(prev.registers[reg.second]),
				   String("%CURR_").add(name).add("%"), l_hex(curr.registers[reg.second]), item_len);
		}

		ostd::RegexRichString rgxstr(str);
		rgxstr.fg("InstAddr|Code|StackFrame|DBG BRK|INT Handler|BIOS Mode|SubRoutine", "Magenta");
		rgxstr.fg("IP|SP|FP|RV|PP|FL|ACC", "Cyan");
		rgxstr.fg("R10|R2|R3|R4|R5|R6|R7|R8|R9|R1", "BrightGreen");
		rgxstr.fg("S1|S2|OF", "BrightRed");
		rgxstr.fg("PREV", "Red");
		rgxstr.fg("CURR", "Green");
		out.pStyled(rgxstr);
		out.nl();
		out.fg(ostd::ConsoleColors::BrightGray).p(" Cycle ").fg(ostd::ConsoleColors::BrightYellow).p(prev.cycle);
		out.fg(ostd::ConsoleColors::BrightGray).p(" -> ").fg(ostd::ConsoleColors::BrightYellow).p(curr.cycle);
		out.fg(ostd::ConsoleColors::BrightGray).p("   Flags: ").fg(ostd::ConsoleColors::Cyan).p(String::getBinStr(cast<u16>(curr.registers[data::Registers::FL]), true, 2));
		out.fg(ostd::ConsoleColors::BrightGray).p("   Interrupts: ").fg(ostd::ConsoleColors::Cyan).p(STR_BOOL(DragonRuntime::cpu.readFlag(data::Flags::InterruptsEnabled)));
		out.fg(ostd::ConsoleColors::BrightGray).p("   Halted: ").fg(curr.halted ? ostd::ConsoleColors::Red : ostd::ConsoleColors::Cyan).p(STR_BOOL(curr.halted)).reset().nl();
	}

	void Debugger::Display::printTrackedAddresses(ostd::OutputHandlerBase& out, const Debugger& dbg, i32 width)
	{
		out.fg(ostd::ConsoleColors::Yellow).p("Tracked Data: ").reset().nl();
		const i32 symbol_len = 24;
		const i32 size_len = 8;
		const i32 addr_len = 10;
		const i32 map_len = 8;
		const i32 prev_len = 35;
		const Debugger::tMachineInfo& prev = dbg.getPreviousInfo();
		const Debugger::tMachineInfo& curr = dbg.getCurrentInfo();
		if (dbg.data().tracked.size() == 0)
		{
			out.fg(ostd::ConsoleColors::BrightGray).p("Nothing is tracked (use the 'track' or 'watch' command).").reset().nl();
			return;
		}
		i32 cw = std::max(width, symbol_len + size_len + addr_len + map_len + prev_len * 2 + 2);
		String header = "|Symbol";
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
		String border = String::duplicateChar('=', cw);
		out.fg(ostd::ConsoleColors::Blue).p(border).nl();
		ostd::RegexRichString rgx(header);
		rgx.fg("\\|", "blue");
		rgx.fg("Symbol|Bytes|Addr|Map|Previous|Current", "yellow");
		out.pStyled(rgx).reset().nl();
		String h_sep = String::duplicateChar('=', cw);
		h_sep.put(0, '|');
		h_sep.put(symbol_len, '|');
		h_sep.put(symbol_len + size_len, '|');
		h_sep.put(symbol_len + size_len + addr_len, '|');
		h_sep.put(symbol_len + size_len + addr_len + map_len, '|');
		h_sep.put(symbol_len + size_len + addr_len + map_len + prev_len, '|');
		h_sep.put(cw - 1, '|');
		out.fg(ostd::ConsoleColors::Blue).p(h_sep).reset().nl();
		size_t valueIndex = 0;
		for (const auto& item : dbg.data().tracked)
		{
			const u16 addr = item.address;
			const u16 data_size = item.size;
			String symbol = item.label;
			if (symbol == "")
				symbol = Utils::findSymbol(dbg.data().data, addr);
			if (symbol == "")
				symbol = Utils::findSymbol(dbg.data().labels, addr);
			bool no_symbol = false;
			if (symbol == "")
			{
				symbol = "<no-symbol>";
				no_symbol = true;
			}
			if (item.breakOnChange)
				symbol = String("(w) ").add(symbol);
			symbol.fixedLength(symbol_len - 1);
			if (no_symbol)
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::Gray).p(symbol).reset();
			else
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::Magenta).p(symbol).reset();

			String tmp = "";
			tmp.add(data_size);
			tmp.fixedLength(size_len - 1);
			if (no_symbol)
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::Cyan).p(tmp).reset();
			else
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::BrightCyan).p(tmp).reset();

			tmp = "";
			tmp.add(String::getHexStr(addr, true, 2));
			tmp.fixedLength(addr_len - 1);
			if (no_symbol)
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::Gray).p(tmp).reset();
			else
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::BrightGray).p(tmp).reset();

			tmp = "";
			if (addr >= (u16)DragonRuntime::cpu.readRegister(data::Registers::SP))
				tmp.add("STACK");
			else
				tmp.add(DragonRuntime::memMap.getMemoryRegionName(addr));
			tmp.fixedLength(map_len - 1);
			out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::BrightYellow).p(tmp).reset();

			auto l_value = [&](const stdvec<u8>& values) {
				String value = "";
				for (u16 j = 0; j < data_size; j++)
				{
					if (j > 0)
						value.add(".");
					value.add(valueIndex + j < values.size() ? String::getHexStr(values[valueIndex + j], false, 1) : String("??"));
				}
				value.fixedLength(prev_len - 1);
				return value;
			};
			bool data_different = false;
			for (u16 j = 0; j < data_size; j++)
			{
				if (valueIndex + j >= prev.trackedValues.size() || valueIndex + j >= curr.trackedValues.size())
					continue;
				if (curr.trackedValues[valueIndex + j] != prev.trackedValues[valueIndex + j])
				{
					data_different = true;
					break;
				}
			}
			tmp = l_value(prev.trackedValues);
			if (no_symbol)
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::Red).p(tmp).reset();
			else
				out.fg(ostd::ConsoleColors::Blue).p("|").fg(ostd::ConsoleColors::BrightRed).p(tmp).reset();

			tmp = l_value(curr.trackedValues);
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
			tmp.addLeftPadding(std::max(1, cw - symbol_len - size_len - addr_len - map_len - prev_len -  prev_len));
			out.fg(ostd::ConsoleColors::Blue).p(tmp).reset().nl();
			valueIndex += data_size;
		}
		out.fg(ostd::ConsoleColors::Blue).p(border).reset().nl();
	}

	stdvec<Debugger::tStackFrame> Debugger::Utils::getStackFrames(void)
	{
		// The frames are found by following the saved frame pointers (see VirtualCPU::pushStackFrame()):
		// above FP are the saved stack frame size, FP, IP, ACC, PP, R10..R1 and the arguments count.
		auto& mem = DragonRuntime::memMap;
		stdvec<tStackFrame> frames;
		u16 fp = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::FP));
		while (fp != 0 && fp < 0xFFFE - 32 && frames.size() < 64)
		{
			tStackFrame frame;
			frame.fp = fp;
			frame.frameSize = mem.peek16(cast<u16>(fp + 2));
			frame.savedFP = mem.peek16(cast<u16>(fp + 4));
			frame.savedIP = mem.peek16(cast<u16>(fp + 6));
			frame.argCount = mem.peek16(cast<u16>(fp + 32));
			frames.push_back(frame);
			if (frame.savedFP <= fp)
				break;
			fp = frame.savedFP;
		}
		return frames;
	}

	void Debugger::Display::printStackFrames(ostd::OutputHandlerBase& out, const Debugger& dbg, i32 width)
	{
		auto& mem = DragonRuntime::memMap;
		out.fg(ostd::ConsoleColors::Yellow).p("Stack frames (innermost first): ").reset().nl();
		Utils::printFullLine(out, '=', ostd::ConsoleColors::BrightBlue, width);
		const auto frames = Utils::getStackFrames();
		i32 depth = 0;
		for (const auto& f : frames)
		{
			const u16 fp = f.fp;
			auto l_word = [&](u16 offset) { return mem.peek16(cast<u16>(fp + offset)); };
			const u16 argStartAddr = (f.argCount == 0 ? 0 : cast<u16>(fp + 32 + f.argCount * 2));
			String frame = "";
			frame.add("#").add(depth).add("  args: ").add(String::getHexStr(argStartAddr, true, 2)).add(", argc: ").add(f.argCount);
			for (u16 i = 0; i < f.argCount && i < 8; i++)
				frame.add(i == 0 ? "  [" : " ").add(String::getHexStr(l_word(cast<u16>(32 + (f.argCount - i) * 2)), true, 2)).add(i + 1 == std::min<u16>(f.argCount, 8) ? "]" : "");
			frame.add("\n");
			for (u16 r = 1; r <= 10; r++)
				frame.add(r == 1 ? "R1: " : String(" R").add(r).add(": ")).add(String::getHexStr(l_word(cast<u16>(32 - r * 2)), true, 2));
			frame.add("\n");
			frame.add("PP: ").add(String::getHexStr(l_word(10), true, 2));
			frame.add(" ACC: ").add(String::getHexStr(l_word(8), true, 2));
			frame.add(" IP: ").add(String::getHexStr(f.savedIP, true, 2));
			String returnSymbol = Utils::findNearestSymbol(dbg.data().labels, f.savedIP);
			if (returnSymbol != "")
				frame.add(" (").add(returnSymbol).add(")");
			frame.add(" FP: ").add(String::getHexStr(f.savedFP, true, 2));
			frame.add("\n");
			frame.add("StackFrame: ").add(f.frameSize).add(", New FP: ").add(String::getHexStr(fp, true, 2));

			ostd::RegexRichString rgxrstr(frame);
			rgxrstr.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "Red"); //Number Constants
			rgxrstr.fg("(?<!\\w)(r[1-9]|r10|fl|pp|rv|fp|sp|ip|acc)(?!\\w)", "BrightGreen", true); //Registers
			rgxrstr.fg("(?<!\\w)(args|argc|StackFrame|New FP)(?!\\w)", "Magenta", true); //Other
			rgxrstr.fg("\\$[\\w\\.]+", "Cyan"); //Labels
			rgxrstr.fg("#[0-9]+", "Yellow");

			out.pStyled(rgxrstr);
			out.nl();
			Utils::printFullLine(out, '=', ostd::ConsoleColors::BrightBlue, width);
			depth++;
		}
		if (frames.empty())
		{
			out.fg(ostd::ConsoleColors::BrightGray).p("No stack frames.").reset().nl();
			Utils::printFullLine(out, '=', ostd::ConsoleColors::BrightBlue, width);
		}
	}

	void Debugger::Display::printStack(ostd::OutputHandlerBase& out, const Debugger& dbg, u16 nrows, i32 width)
	{
		if (nrows == 0) nrows = 8;
		u16 ncols = 16;
		u16 startAddr = (0xFFFF - (ncols * nrows) + 1);
		printStackFrames(out, dbg, width);
		out.nl();
		out.fg(ostd::ConsoleColors::Yellow).p("Stack view: ").reset().nl();
		u16 stack_ptr = DragonRuntime::cpu.readRegister(data::Registers::SP);
		const auto& image = dbg.getBusImage();
		ostd::Memory::printByteStream(std::span<const i8>(reinterpret_cast<const i8*>(image.data()), image.size()), startAddr, ncols, nrows, out, stack_ptr, 2, "Stack");
	}

	void Debugger::Display::printCallStack(ostd::OutputHandlerBase& out, const Debugger& dbg, i32 width)
	{
		out.fg(ostd::ConsoleColors::Yellow).p("Subroutine/Interrupt call stack: ").reset().nl();
		Utils::printFullLine(out, '=', ostd::ConsoleColors::Yellow, width);

		auto& callStack = dbg.getCallTrace();
		const size_t count = dbg.getVisibleCallTraceSize();
		// Only the last calls: the trace can be very long.
		const size_t MaxShown = 2000;
		const size_t first = (count > MaxShown ? count - MaxShown : 0);
		if (first > 0)
			out.fg(ostd::ConsoleColors::BrightGray).p("(").p(cast<u64>(first)).p(" older calls not shown)").reset().nl();
		String ch_ang_u = "┌";
		String ch_ang_d = "└";
		for (size_t c = first; c < count; c++)
		{
			const auto& call = callStack[c];
			// The depth before the call is the indentation (calls: after - 1, returns: after).
			String call_info = call.info.new_trim().toLower();
			String subroutine_addr = String::getHexStr(call.addr, true, 2);
			String subroutine_name = Utils::findSymbol(dbg.data().labels, call.addr);
			String call_str = "";
			i32 level = call.depth;
			if (call_info == "int")
			{
				call_str = ch_ang_u + "int " + String::getHexStr(call.addr, true, 1);
				level--;
			}
			else if (call_info == "hw int")
			{
				call_str = ch_ang_u + "hwi " + String::getHexStr(call.addr, true, 1) + " (" + data::InterruptCodes::getInterruptName(call.addr) + ")";
				level--;
			}
			else if (call_info == "ret")
			{
				call_str = ch_ang_d + "ret";
			}
			else if (call_info == "ret int")
			{
				call_str = ch_ang_d + "rti";
			}
			else
			{
				if (call_info == "call reg")
					subroutine_addr = "*" + subroutine_addr;
				if (subroutine_name != "")
					call_str = ch_ang_u + "call " + subroutine_name + " (" + subroutine_addr + ")";
				else
					call_str = ch_ang_u + "call " + subroutine_addr;
				level--;
			}

			String line_str = "";
			for (i32 i = 0; i < level; i++)
				line_str.add("|   ");
			line_str.add(call_str);

			ostd::RegexRichString rgx(line_str);
			rgx.fg("\\(|\\)|\\*", "Cyan"); //Operators
			rgx.fg("(?<![a-zA-Z\\_\\$\\.])int|rti(?! [a-zA-Z\\_\\$\\.])", "Blue");
			rgx.fg("(?<![a-zA-Z\\_\\$\\.])hwi(?! [a-zA-Z\\_\\$\\.])", "Magenta");
			rgx.fg("(?<![a-zA-Z\\_\\$\\.])call|ret(?! [a-zA-Z\\_\\$\\.])", "Red");
			rgx.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "BrightYellow"); //Number Constants
			rgx.fg(String("[\\").add(ch_ang_u).add("\\|\\").add(ch_ang_d).add("]"), "darkgray");
			rgx.fg("\\$\\w+", "Green"); //Labels

			out.fg(ostd::ConsoleColors::BrightGray).p("|").reset();
			if (call.interrupts_disabled)
				out.fg(ostd::ConsoleColors::BrightGray);
			else
				out.fg(ostd::ConsoleColors::Magenta);
			out.p(String::getHexStr(call.inst_addr, true, 2));
			out.fg(ostd::ConsoleColors::BrightGray).p("| > ").reset();
			out.pStyled(rgx).nl().reset();
		}

		Utils::printFullLine(out, '=', ostd::ConsoleColors::Yellow, width);
	}

	void Debugger::Display::printBreakpoints(ostd::OutputHandlerBase& out, const Debugger& dbg)
	{
		out.fg(ostd::ConsoleColors::Yellow).p("Breakpoints:").reset().nl();
		if (dbg.data().breakpoints.empty())
			out.fg(ostd::ConsoleColors::BrightGray).p("  none (click the left margin of the disassembly, or use 'break <addr|$label>')").reset().nl();
		for (const auto& [addr, bp] : dbg.data().breakpoints)
		{
			if (!bp.enabled)
				out.fg(ostd::ConsoleColors::BrightGray).p("  o ");
			else
				out.fg(bp.debugBreak ? ostd::ConsoleColors::Yellow : ostd::ConsoleColors::Red).p("  * ");
			out.fg(ostd::ConsoleColors::BrightYellow).p(String::getHexStr(addr, true, 2));
			if (bp.debugBreak)
				out.fg(ostd::ConsoleColors::BrightGray).p(bp.enabled ? "  (debug_break)" : "  (debug_break, disabled)");
			String symbol = Utils::findNearestSymbol(dbg.data().labels, addr);
			if (symbol != "")
				out.fg(ostd::ConsoleColors::Cyan).p("  ").p(symbol);
			u16 instructionSize = 1;
			String code = Utils::findSymbol(dbg.data().code, addr, &instructionSize);
			if (code != "")
				out.fg(ostd::ConsoleColors::BrightGray).p("  ").p(code.new_trim());
			out.reset().nl();
		}
		out.nl().fg(ostd::ConsoleColors::Yellow).p("Watched data (the machine stops when they change):").reset().nl();
		bool any = false;
		for (const auto& item : dbg.data().tracked)
		{
			if (!item.breakOnChange)
				continue;
			any = true;
			out.fg(ostd::ConsoleColors::Magenta).p("  w ").fg(ostd::ConsoleColors::BrightYellow).p(String::getHexStr(item.address, true, 2));
			out.fg(ostd::ConsoleColors::BrightGray).p(" [").p(item.size).p("]  ").fg(ostd::ConsoleColors::Cyan).p(item.label).reset().nl();
		}
		if (!any)
			out.fg(ostd::ConsoleColors::BrightGray).p("  none (use 'watch <addr[bytes]|$symbol>')").reset().nl();
	}

	void Debugger::Display::printHistory(ostd::OutputHandlerBase& out, const Debugger& dbg, i32 rows)
	{
		const History& history = dbg.history();
		if (!dbg.isHistoryEnabled())
		{
			out.fg(ostd::ConsoleColors::Yellow).p("History recording is disabled ('history on' to enable it).").reset().nl();
			return;
		}
		out.fg(ostd::ConsoleColors::Yellow).p("History: ").reset();
		out.fg(ostd::ConsoleColors::BrightGray).p("position ").fg(ostd::ConsoleColors::BrightYellow).p(history.getPosition());
		out.fg(ostd::ConsoleColors::BrightGray).p(" of ").fg(ostd::ConsoleColors::BrightYellow).p(history.getLastPosition());
		out.fg(ostd::ConsoleColors::BrightGray).p("  (cycles ").fg(ostd::ConsoleColors::BrightYellow).p(history.getCycleAt(history.getFirstPosition()));
		out.fg(ostd::ConsoleColors::BrightGray).p(" - ").fg(ostd::ConsoleColors::BrightYellow).p(history.getCycleAt(history.getLastPosition()));
		out.fg(ostd::ConsoleColors::BrightGray).p(")  memory: ").fg(ostd::ConsoleColors::BrightYellow);
		out.p(cast<f64>(history.getMemoryUsage()) / (1024.0 * 1024.0), 1).p(" / ").p(cast<f64>(history.getMemoryLimit()) / (1024.0 * 1024.0), 0).p(" MiB").reset().nl();
		out.fg(ostd::ConsoleColors::BrightGray).p("State: ").p(history.getStateSize()).p(" bytes in ").p(cast<i32>(history.getSegments().size())).p(" segments: ");
		for (const auto& segment : history.getSegments())
			out.fg(ostd::ConsoleColors::Cyan).p(segment.name).fg(ostd::ConsoleColors::BrightGray).p("(").p(segment.size).p(") ");
		out.reset().nl().nl();

		out.fg(ostd::ConsoleColors::Yellow).p("Entries around the current position (").fg(ostd::ConsoleColors::BrightGreen).p(">").fg(ostd::ConsoleColors::Yellow).p("):").reset().nl();
		const u64 position = history.getPosition();
		const u64 half = cast<u64>(std::max(rows / 2, 4));
		const u64 from = std::max(history.getFirstPosition() + 1, position > half ? position - half : 1);
		const u64 to = std::min(history.getLastPosition(), from + half * 2);
		for (u64 p = from; p <= to; p++)
		{
			const History::CycleDiff* entry = history.getEntryTo(p);
			if (entry == nullptr)
				continue;
			out.fg(p == position ? ostd::ConsoleColors::BrightGreen : ostd::ConsoleColors::BrightGray).p(p == position ? " > " : "   ");
			String pos = String("").add(p);
			pos.addLeftPadding(10);
			out.fg(ostd::ConsoleColors::BrightGray).p(pos).p("  cycle ");
			String cycle = String("").add(entry->cycle);
			cycle.addRightPadding(10);
			out.fg(ostd::ConsoleColors::BrightYellow).p(cycle);
			switch (entry->kind)
			{
				case History::eEntryKind::Cycle: out.fg(ostd::ConsoleColors::Blue).p(" cycle    "); break;
				case History::eEntryKind::Edit: out.fg(ostd::ConsoleColors::Magenta).p(" edit     "); break;
				case History::eEntryKind::External: out.fg(ostd::ConsoleColors::Cyan).p(" external "); break;
			}
			out.fg(ostd::ConsoleColors::BrightGray).p(" ").p(cast<u64>(entry->runs.size())).p(" bytes");
			if (entry->fileWrites)
				out.fg(ostd::ConsoleColors::Red).p("  disk writes: ").p(cast<u64>(entry->fileWrites->size()));
			out.reset().nl();
		}
	}

	void Debugger::Display::printHelp(ostd::OutputHandlerBase& out)
	{
		i32 commandLength = 34;
		out.fg(ostd::ConsoleColors::Yellow).p("List of available commands:").reset().nl();
		auto l_cmd = [&](const String& command, const String& description) {
			String tmpCommand = command;
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p(description).reset().nl();
		};
		l_cmd("<Enter> / step [n] / si [n]", "Executes one (or n) instructions. (F10)");
		l_cmd("(n)ext", "Steps over a call / int instruction. (F11)");
		l_cmd("finish / out", "Runs until the current subroutine returns. (Shift+F11)");
		l_cmd("(c)ontinue / (r)un", "Resumes the execution. (F5)");
		l_cmd("pause", "Pauses the execution. (F6)");
		l_cmd("until <addr/symbol>", "Runs until the instruction at the address.");
		l_cmd("back [n] / sb [n]", "Steps back in time one (or n) instructions. (F9)");
		l_cmd("rc / rcontinue", "Runs backwards until a breakpoint / watch. (Shift+F5)");
		l_cmd("goto <cycle>", "Moves to a cycle of the recorded history.");
		l_cmd("history [on/off/clear]", "Shows the history, or turns recording on / off.");
		l_cmd("(b)reak <addr/symbol>", "Toggles a breakpoint (enables / disables a debug_break).");
		l_cmd("bl / breakpoints", "Lists the breakpoints and watches.");
		l_cmd("bc / clear", "Removes every breakpoint (disables the debug_break ones).");
		l_cmd("(p)rint [word] <addr/symbol>", "Prints values from memory. Valid [size] values are: byte/word/dword/qword/string.");
		l_cmd("(t)rack <address[bytes]/symbol>", "Tracks addresses during execution ('t' alone: the tracker).");
		l_cmd("(w)atch <address[bytes]/symbol>", "Tracks addresses, and stops when they change.");
		l_cmd("untrack <addr/symbol/all>", "Stops tracking.");
		l_cmd("set <register> <value>", "Writes a register (r1..r10, ip, sp, fp, rv, pp, fl, acc, s1, s2, of).");
		l_cmd("x / mem <addr/symbol>", "Shows the memory at the address.");
		l_cmd("(d)iff-view", "Shows the state of the machine, comparing the current cycle with the last.");
		l_cmd("(s)tack [rows=8]", "Shows the stack and a description of every stack-frame.");
		l_cmd("(ca)ll-stack", "Shows information about all subroutine/interrupt calls.");
		l_cmd("display", "Shows / hides the Virtual Display.");
		l_cmd("detach / attach <screen|console>", "Moves the screen or the console to its own window, and back.");
		l_cmd("speed <hz/max/default>", "Changes the speed the machine runs at.");
		l_cmd("(q)uit", "Closes the debugger.");
		l_cmd("(h)elp", "Shows this list.");

		out.nl().fg(ostd::ConsoleColors::Cyan).p("The letters in parenthesis can be used as the short version of the command.").nl();
		out.p("Square brackets represent optional parameters. If they have an '=' and a value, that is the default if not specified.").nl();
		out.p("Numbers can be decimal, hexadecimal (0x...) or binary (0b...).").reset().nl();
	}

	void Debugger::Display::printApplicationHelp(ostd::OutputHandlerBase& out)
	{
		i32 commandLength = 46;

		out.nl().fg(ostd::ConsoleColors::Yellow).p("List of available parameters:").reset().nl();
		auto l_param = [&](const String& param, const String& description) {
			String tmpCommand = param;
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p(description).reset().nl();
		};
		l_param("--verbose-load", "Used to show more information while loading the virtual machine.");
		l_param("--auto-start", "Starts running the machine right away (instead of paused at the first instruction).");
		l_param("--step-exec", "Starts paused at the first instruction (the default).");
		l_param("--history-off", "Doesn't record the history (no stepping back). Alias: --track-step-diff-off");
		l_param("--history-limit <MiB>", "Memory used by the history at most (the oldest cycles are forgotten). Default: 512.");
		l_param("--auto-track-data-off", "Disables the automatic tracking of all data-symbols.");
		l_param("--hide-vdisplay", "Hides the Virtual Display.");
		l_param("--max-speed", "Runs the machine as fast as possible, instead of at its clock speed.");
		l_param("--exec \"<cmd>; <cmd>; ...\"", "Runs console commands at the start (each one once the machine stopped).");
		l_param("--force-load <binary-file> <ram-offset>", "Injects the specified binary into RAM at the specified offset.");
		l_param("--help", "Displays this help message.");

		out.nl().fg(ostd::ConsoleColors::Magenta).p("Usage: ./ddb <machine-config-file> [...options...]").reset().nl();
		out.nl();
	}
}
