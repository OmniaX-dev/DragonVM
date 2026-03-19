#include "DebuggerNew.hpp"
#include <SDL2/SDL_keycode.h>
#include <cstdint>
#include <ogfx/BasicRenderer.hpp>
#include <ogfx/WindowBase.hpp>
#include <ostd/math/Geometry.hpp>
#include <ostd/io/IOHandlers.hpp>
#include <ostd/math/Random.hpp>
#include <ostd/string/String.hpp>
#include <ostd/utils/Utils.hpp>
#include "DisassemblyLoader.hpp"
#include "../runtime/DragonRuntime.hpp"

namespace ogfx
{
	namespace gui
	{
		Button::EventListener::EventListener(Button& _parent) : parent(_parent)
		{
			connectSignal(ostd::tBuiltinSignals::KeyPressed);
			connectSignal(ostd::tBuiltinSignals::KeyReleased);
			connectSignal(ostd::tBuiltinSignals::MouseMoved);
			connectSignal(ostd::tBuiltinSignals::MousePressed);
			connectSignal(ostd::tBuiltinSignals::MouseReleased);
			connectSignal(ostd::tBuiltinSignals::OnGuiEvent);
			connectSignal(ostd::tBuiltinSignals::WindowResized);
		}

		void Button::EventListener::handleSignal(ostd::tSignal& signal)
		{
			if (signal.ID == ostd::tBuiltinSignals::KeyPressed)
			{
				if (m_lastEvent != eEventType::KeyPressed)
				{
					m_lastEvent = eEventType::KeyPressed;
				}
				auto& data = (ogfx::KeyEventData&)signal.userData;
				if (data.keyCode == SDLK_BACKSPACE)
				{
				}
				else if (data.keyCode == SDLK_LEFT)
				{
				}
				else if (data.keyCode == SDLK_RIGHT)
				{
				}
				else if (data.keyCode == SDLK_RETURN)
				{
				}
				else if (data.keyCode == SDLK_TAB)
				{
				}
			}
			else if (signal.ID == ostd::tBuiltinSignals::MouseMoved)
			{
				auto& data = (ogfx::MouseEventData&)signal.userData;
				if (parent.contains((float)data.position_x, (float)data.position_y))
					parent.m_mouseInside = true;
				else
					parent.m_mouseInside = false;
			}
			else if (signal.ID == ostd::tBuiltinSignals::MousePressed)
			{
				auto& data = (ogfx::MouseEventData&)signal.userData;
				if (data.button == ogfx::MouseEventData::eButton::Left && parent.m_gfx != nullptr && parent.m_mouseInside)
				{
					ostd::String text = parent.m_text;
					ostd::Vec2 relativePosition = { (float)data.position_x, (float)data.position_y };
					relativePosition -= (parent.getPosition());
					parent.m_pressed = true;
				}
			}
			else if (signal.ID == ostd::tBuiltinSignals::MouseReleased)
			{
				auto& data = (ogfx::MouseEventData&)signal.userData;
				if (data.button == ogfx::MouseEventData::eButton::Left && parent.m_gfx != nullptr)
				{
					if (parent.m_pressed)
					{
						ActionEventData aed(parent, parent.getName(), eActionEventType::Pressed, ostd::BaseObject::InvalidRef());
						ostd::SignalHandler::emitSignal(Button::actionEventSignalID, ostd::tSignalPriority::RealTime, aed);
					}
					parent.m_pressed = false;
				}
			}
			onSignalHandled(signal);
		}





		Button& Button::create(const ostd::Vec2& position, const ostd::Vec2& size, const ostd::String& name)
		{
			setPosition(position);
			setSize(size);
			m_name = name;
			m_eventListener = new EventListener(*this); //TODO: Delete -- Memory Leak
			m_theme = tDefaultThemes::DefaultTheme;
			return *this;
		}

		void Button::render(ogfx::BasicRenderer2D& gfx)
		{
			m_gfx = &gfx;
			ostd::Color backgroundColor = m_theme.backgroundColor;
			ostd::Color borderColor = m_theme.borderColor;
			ostd::Color textColor = m_theme.textColor;
			if (m_pressed)
			{
				backgroundColor = m_theme.backgroundColor_Pressed;
				borderColor = m_theme.borderColor_Pressed;
				textColor = m_theme.textColor_Pressed;
			}
			else if (m_mouseInside)
			{
				backgroundColor = m_theme.backgroundColor_Hover;
				borderColor = m_theme.borderColor_Hover;
				textColor = m_theme.textColor_Hover;
			}
			gfx.outlinedRect(*this, backgroundColor, borderColor, 2);
			if (m_text.len() > 0)
			{
				ostd::IPoint strSize = gfx.getStringSize(m_text, m_theme.fontSize);
				ostd::Vec2 txtPos = getPosition() + ostd::Vec2 { (getw() / 2.0f) - (strSize.x / 2.0f), (geth() / 2.0f) - (strSize.y / 2.0f) };
				gfx.drawString(m_text, txtPos, textColor, m_theme.fontSize);
			}
			onRender(gfx);
		}

		void Button::update(void)
		{
			onUpdate();
		}

		void Button::fixedUpdate(void)
		{
			onFixedUpdate();
		}

		void Button::setText(const ostd::String& text)
		{
			m_text = text;
		}

		void Button::appendText(const ostd::String& text)
		{
			m_text.add(text);
		}

		void Button::setTheme(Theme theme)
		{
			m_theme = theme;
		}
	}
}




namespace dragon
{
	//DebuggerNew::Utils
	DisassemblyList DebuggerNew::findCodeRegion(const DisassemblyList& code, uint16_t address, uint16_t codeRegionMargin)
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

	ostd::String DebuggerNew::findSymbol(const DisassemblyList& list, uint16_t address, uint16_t* outSize)
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

	uint16_t DebuggerNew::findSymbol(const DisassemblyList& list, const ostd::String& symbol, uint16_t* outSize)
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

	bool DebuggerNew::isValidLabelNameChar(char c)
	{
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_');
	}

	void DebuggerNew::removeBreakPoint(uint16_t addr)
	{
		if (debugger.manualBreakPoints.size() == 0)
			return;
		int32_t i = 0;
		for ( ; i < debugger.manualBreakPoints.size(); i++)
		{
			if (debugger.manualBreakPoints[i] == addr)
				break;
		}
		if (i >= debugger.manualBreakPoints.size())
			return;
		debugger.manualBreakPoints.erase(debugger.manualBreakPoints.begin() + i);
	}

	bool DebuggerNew::isBreakPoint(uint16_t addr)
	{
		for (const auto& b : debugger.manualBreakPoints)
			if (b == addr) return true;
		return false;
	}

	void DebuggerNew::addBreakPoint(uint16_t addr)
	{
		debugger.manualBreakPoints.push_back(addr);
	}




	//Debugger
	void DebuggerNew::processErrors(void)
	{
		if (!dragon::DragonRuntime::hasError()) return;
		while (dragon::data::ErrorHandler::hasError())
		{
			auto err = dragon::data::ErrorHandler::popError();
			out.nl().fg(ostd::ConsoleColors::Red).p("Error ").p(ostd::Utils::getHexStr(err.code, true, 8).cpp_str()).p(": ").p(err.text.cpp_str()).nl();
		}
		debugger.args.step_exec = true;
	}

	int32_t DebuggerNew::loadArguments(int argc, char** argv)
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
				// print_application_help();
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
					// print_application_help();
					return DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER;
				}
			}
		}
		return DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
	}

	int32_t DebuggerNew::initRuntime(void)
	{
		int32_t init_state = dragon::DragonRuntime::initMachine(debugger.args.machine_config_path,
																debugger.args.verbose_load,
																debugger.args.track_step_diff,
																debugger.args.hide_virtual_display,
																debugger.args.track_call_stack,
																true); //CPU Debug Mode Enabled
		// closeEventListener.init();
		if (init_state != 0) return init_state; //TODO: Error

		if (debugger.args.force_load)
			dragon::DragonRuntime::forceLoad(debugger.args.force_load_file, debugger.args.force_load_mem_offset);

		dragon::DisassemblyLoader::loadDirectory(debugger.disassemblyDirectory);
		debugger.code = dragon::DisassemblyLoader::getCodeTable();
		debugger.labels = dragon::DisassemblyLoader::getLabelTable();
		debugger.data = dragon::DisassemblyLoader::getDataTable();

		return DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
	}

	ostd::String DebuggerNew::getCommandInput(void)
	{
		// ostd::String cmd;
		// ostd::KeyboardController kbc;
		// ostd::eKeys key = ostd::eKeys::NoKeyPressed;

		// while ((key = kbc.getPressedKey()) != ostd::eKeys::Enter)
		// {
		// 	if (key == ostd::eKeys::Escape)
		// 		return InputCommandQuit;
		// }
		// return kbc.getInputString();

		return "";
	}

	int32_t DebuggerNew::executeRuntime(void)
	{
		int32_t rValue = 0;
		bool userQuit = false;
		output().clear().fg(ostd::ConsoleColors::Green).p("Program running...").nl();
		if (!data().args.step_exec)
			output().fg(ostd::ConsoleColors::Yellow).p("Press <Escape> to enter in step-execution mode...").reset().nl();
		while (!userQuit)
		{
			ostd::SignalHandler::refresh();
			// if (closeEventListener.hasHappened())
			// 	userQuit = true;
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

	int32_t DebuggerNew::step_execution(bool& outUserQuit, bool exec_first_step)
	{
		if (exec_first_step && !DragonRuntime::cpu.isHalted())
			DragonRuntime::runStep(data().trackedAddresses);
		// Display::printStep();
		processErrors();
		if (DragonRuntime::cpu.isInDebugBreakPoint())
			output().fg(ostd::ConsoleColors::Red).p("Reached Debug Break Point.").reset().nl();
		// Display::printPrompt();
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
					uint16_t addr = findSymbol(debugger.data, data().command, &size);
					if (addr == 0)
						addr = findSymbol(debugger.labels, data().command, &size);
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
				// Display::printPrompt();
				data().command = getCommandInput();
			}
			else if (data().command.startsWith("b ") || data().command.startsWith("break "))
			{//0x2C1D
				data().command.substr(data().command.indexOf(" ") + 1).trim();
				uint16_t addr = 0;
				bool valid = false;
				if (data().command.isNumeric())
				{
					addr = (uint16_t)data().command.toInt();
					valid = true;
				}
				else if (data().command.startsWith("$"))
				{
					addr = findSymbol(debugger.labels, data().command);
					if (addr == 0x0000 || addr == 0xFFFF)
						output().fg(ostd::ConsoleColors::Red).p("Invalid symbol: ").p(data().command).reset().nl();
					else
						valid = true;
				}
				else
				{
					output().fg(ostd::ConsoleColors::Red).p("Invalid value for <break> command.").reset().nl();
				}
				if (valid)
				{
					if (isBreakPoint(addr))
					{
						removeBreakPoint(addr);
						output().fg(ostd::ConsoleColors::Yellow).p("Breakpoint removed at address: ").p(ostd::Utils::getHexStr(addr, true, 2)).reset().nl();
					}
					else
					{
						addBreakPoint(addr);
						output().fg(ostd::ConsoleColors::Yellow).p("Breakpoint set at address: ").p(ostd::Utils::getHexStr(addr, true, 2)).reset().nl();
					}
				}
				// Display::printPrompt();
				data().command = getCommandInput();
			}
			else
				data().command = "";//Display::changeScreen();
		}
		return DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
	}

	int32_t DebuggerNew::normal_runtime(bool& outUserQuit)
	{
		bool result = DragonRuntime::runStep(data().trackedAddresses);
		if (isBreakPoint((uint16_t)DragonRuntime::cpu.readRegister(data::Registers::IP)))
		{
			data().args.step_exec = true;
			return step_execution(outUserQuit, false);
		}
		bool hasError = DragonRuntime::hasError();
		bool enableStepExec = !result || hasError || DragonRuntime::cpu.isInDebugBreakPoint();
		data().args.step_exec = enableStepExec;
		if (enableStepExec)
			return step_execution(outUserQuit, false);
		return DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
	}




	//Display
	void DebuggerNew::colorizeInstructionBody(const ostd::String& instBody, bool currentLine, const DisassemblyList& labelList)
	{
		ostd::RegexRichString rgxrstr(instBody);
		rgxrstr.fg("\\{|\\}|\\+|\\*|\\-|\\/|\\(|\\)|\\[|\\]", "Red"); //Operators
		rgxrstr.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "Blue"); //Number Constants
		rgxrstr.fg("(?<!\\w)(r[1-9]|r10|fl|pp|rv|fp|sp|ip|acc)(?!\\w)", "BrightGreen", true); //Registers
		rgxrstr.fg("\\%low", "Magenta");
		rgxrstr.col("\\$\\w+", "Cyan", "Black"); //Labels
		ostd::String instEdit = rgxrstr.getRawString();
		for (auto& label : labelList)
		{
			int32_t index = -1;
			ostd::String labelEdit = label.code;
			labelEdit.trim();
			while ((index = instEdit.indexOf(labelEdit, index + 1)) != -1)
			{
				if (index + labelEdit.len() < instEdit.len() && isValidLabelNameChar(instEdit.at(index + labelEdit.len())))
					continue;
				ostd::String instStr = instEdit;
				instStr.cpp_str_ref().replace(index, labelEdit.len(), labelEdit.cpp_str() + "[@@ style foreground:brightgray](" + ostd::Utils::getHexStr(label.addr, true, 2).cpp_str() + ")[@@/]");
				instEdit = instStr;
			}
		}
		rgxrstr.setRawString(instEdit);
		m_wout.tab().pStyled(rgxrstr);
		if (currentLine)
			m_wout.p("   ").bg(ostd::ConsoleColors::Yellow).fg(ostd::ConsoleColors::Red).p(" <-- ");
		m_wout.nl();
		m_wout.reset();
	}

	void DebuggerNew::colorCodeInstructions(const ostd::String& inst, bool currentLine, const DisassemblyList& labelList)
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
			m_wout.bg(ostd::ConsoleColors::Yellow).fg(ostd::ConsoleColors::Red);
		}
		else if (instHead == "call" || instHead == "ret" || instHead == "int" || instHead == "rti")
			m_wout.bg(ostd::ConsoleColors::BrightRed).fg(ostd::ConsoleColors::White);
		else if (instHead == "hlt" || instHead == "debug_break")
			m_wout.fg(ostd::ConsoleColors::Red);
		else if (instHead == "nop")
			m_wout.fg(ostd::ConsoleColors::Gray);
		else if(instHead.isNumeric())
			m_wout.fg(ostd::ConsoleColors::Magenta);
		else
			m_wout.fg(ostd::ConsoleColors::Yellow);
		m_wout.p(instHead.cpp_str());
		m_wout.reset();
		colorizeInstructionBody(instBody, currentLine, labelList);
	}

	void DebuggerNew::printStep(void)
	{
		m_wout.clear();
		int32_t codeRegionSpan = (m_consoleSize.y / 2) - 1;
		auto codeRegion = findCodeRegion(debugger.code, debugger.currentAddress, codeRegionSpan);
		for (int32_t i = 0; i < codeRegion.size(); i++)
		{
			auto& _da = codeRegion[i];
			bool currentLine = _da.addr == debugger.currentAddress;
			ostd::String label = findSymbol(debugger.labels, _da.addr);
			bool specialSection = _da.code.startsWith("[") &&_da.code.endsWith("]");
			label.fixedLength(debugger.labelLineLength);
			m_wout.fg(ostd::ConsoleColors::Gray).p(label.cpp_str()).p("  ");
			if (currentLine)
			{
				m_wout.fg(ostd::ConsoleColors::Black).bg(ostd::ConsoleColors::Yellow).p(ostd::Utils::getHexStr(_da.addr, true, 2).cpp_str()).p("  ").reset();;
			}
			else
			{
				if (specialSection)
					m_wout.fg(ostd::ConsoleColors::Cyan);
				else if (_da.code == "debug_break")
					m_wout.fg(ostd::ConsoleColors::Red);
				else
					m_wout.fg(ostd::ConsoleColors::BrightGray);
				m_wout.p(ostd::Utils::getHexStr(_da.addr, true, 2).cpp_str()).p("  ");
			}
			if (specialSection)
				m_wout.fg(ostd::ConsoleColors::Cyan).p(_da.code.cpp_str()).nl();
			colorCodeInstructions(_da.code, currentLine, debugger.labels);
			m_wout.reset();
		}
	}




	//General
	void DebuggerNew::onInitialize(void)
	{
		enableSignals();
		connectSignal(ostd::tBuiltinSignals::KeyReleased);
		connectSignal(ogfx::gui::RawTextInput::actionEventSignalID);
		connectSignal(ogfx::gui::Button::actionEventSignalID);
		enableMouseDragEvent(false);

		// DisassemblyLoader::loadDirectory("disassembly");
		// m_codeTable = DisassemblyLoader::getCodeTable();
		// m_codeRandomIndex = ostd::Random::geti32(0, m_codeTable.size() - m_consoleSize.y - 1);

		m_gfx.init(*this);
		m_gfx.setFont("res/Courier Prime.ttf");

		float w = m_consolePosition.x - 12;
		float h = 40.0f;
		m_textInput.create({ 0.0f, (float)(getWindowHeight() - h) }, { w, h }, "CmdTxt");
		m_textInput.setEventListener(m_sigHandler);
		m_textInput.getTheme().extraPaddingTop = 3;

		m_testBtn.create({ 100.0f, 100.0f }, { 120.0f, 90.0f }, "TestBTN");
		m_testBtn.setText("BTN");
		m_testBtn.setEventListener(m_btnSigHandler);

		m_wout.attachWindow(*this);
		m_wout.setMonospaceFont("res/UbuntuMono-R.ttf");
		m_wout.setFontSize(22);
		m_wout.setConsoleMaxCharacters(m_consoleSize);
		m_wout.setConsolePosition(m_consolePosition);
		m_wout.setWrapMode(ogfx::WindowBaseOutputHandler::eWrapMode::TripleDots);
		m_wout.setDefaultForegroundColor({ 180, 180, 180, 255 });

		std::cout << STR_BOOL(ostd::Utils::loadByteStreamFromFile("./bios.bin", m_test)) << "\n";
 	}

	void DebuggerNew::handleSignal(ostd::tSignal& signal)
	{
		if (signal.ID == ostd::tBuiltinSignals::KeyReleased)
		{
			auto& evtData = (ogfx::KeyEventData&)signal.userData;
			if (evtData.keyCode == SDLK_ESCAPE)
				close();
			// else if (evtData.keyCode == SDLK_SPACE)
			// 	m_codeRandomIndex = ostd::Random::geti32(0, m_codeTable.size() - m_consoleSize.y - 1);
		}
		else if (signal.ID == ogfx::gui::RawTextInput::actionEventSignalID)
		{
			auto& data = (ogfx::gui::RawTextInput::ActionEventData&)signal.userData;
			if (data.senderName != "MainInputTXT")
				return;
			if (data.eventType == ogfx::gui::RawTextInput::eActionEventType::Enter)
			{
				out.fg(ostd::ConsoleColors::Green).p(data.sender.getText()).reset().nl();
				data.sender.setText("");
			}
			else if (data.eventType == ogfx::gui::RawTextInput::eActionEventType::Tab)
			{
				out.fg(ostd::ConsoleColors::Red).p("TAB").reset().nl();
				data.sender.appendText("TAB");
			}
		}
		else if (signal.ID == ogfx::gui::Button::actionEventSignalID)
		{
			auto& data = (ogfx::gui::Button::ActionEventData&)signal.userData;
			if (data.senderName != "TestBTN")
				return;
			if (data.eventType == ogfx::gui::Button::eActionEventType::Pressed)
			{
				out.fg(ostd::ConsoleColors::Green).p(data.sender.getText()).reset().nl();
			}
		}
	}

	void DebuggerNew::onRender(void)
	{
		m_gfx.outlinedRect(m_wout.getConsoleBounds(), { 0, 0, 20, 255 }, { 255, 255, 255, 200 }, 2);

		m_wout.beginFrame();
		ostd::Utils::printByteStream(m_test, 0, 16, 16, m_wout, 8, 4, "HELLO");
		// printStep();

		m_textInput.render(m_gfx);
		// m_testBtn.render(m_gfx);
	}

	void DebuggerNew::onFixedUpdate(double frameTime_s)
	{
		m_textInput.fixedUpdate();
		m_testBtn.fixedUpdate();
		// std::cout << getFPS() << "\n";
	}

	void DebuggerNew::onUpdate(void)
	{
		m_textInput.update();
		m_testBtn.update();
	}
}
