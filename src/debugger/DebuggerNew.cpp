#include "DebuggerNew.hpp"
#include <SDL2/SDL_keycode.h>
#include <cstdint>
#include <ogfx/BasicRenderer.hpp>
#include <ogfx/WindowBase.hpp>
#include <ostd/Defines.hpp>
#include <ostd/Geometry.hpp>
#include <ostd/IOHandlers.hpp>
#include <ostd/Random.hpp>
#include <ostd/String.hpp>
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

		float w = getWindowWidth();
		float h = 40.0f;
		m_textInput.create({ 0.0f, (float)(getWindowHeight() - h) }, { w, h }, "MainInputTXT");
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
		printStep();
		// for (int32_t i = m_codeRandomIndex; i < m_codeRandomIndex + m_consoleSize.y; i++)
		// {
		// 	if (i >= m_codeTable.size()) break;
		// 	auto code = m_codeTable[i];
		// 	m_wout.tab().fg({ 100, 100, 100, 255 }).p(ostd::Utils::getHexStr(code.addr, true, 2));
		// 	m_wout.clear().tab().tab();
		// 	// colorCodeInstructions(code.code, m_wout);
		// }

		// m_textInput.render(m_gfx);
		// m_testBtn.render(m_gfx);
	}

	void DebuggerNew::onFixedUpdate(void)
	{
		m_textInput.fixedUpdate();
		m_testBtn.fixedUpdate();
	}

	void DebuggerNew::onUpdate(void)
	{
		m_textInput.update();
		m_testBtn.update();
	}




	uint32_t __debugger_entry_point(void)
	{
		DebuggerNew window;
		window.initialize(1600, 1000, "DragonVM Live Debugger");
		window.setClearColor({ 0, 2	, 15 });

		while (window.isRunning())
		{
			window.update();
		}
		return 0;
	}
}
