#pragma once

#include <ostd/Color.hpp>
#include <ostd/Geometry.hpp>
#include <ostd/Utils.hpp>
#include <ostd/IOHandlers.hpp>
#include <ogfx/WindowBase.hpp>
#include <ogfx/BasicRenderer.hpp>
#include <ogfx/RawTextInput.hpp>
#include <ogfx/WindowBaseOutputHandler.hpp>
#include "../assembler/Assembler.hpp"


namespace ogfx
{
	namespace gui
	{
		class Button : public ostd::Rectangle
		{
			public: class EventListener : public ostd::BaseObject
			{
				public: enum class eEventType { None = 0, MousePressed, KeyPressed, KeyReleased };
				public:
					EventListener(Button& _parent);
					virtual void handleSignal(ostd::tSignal& signal) override;
					inline virtual void onSignalHandled(ostd::tSignal& signal) {  }
					inline Button& getParent(void) { return parent; }

				private:
					Button& parent;
					eEventType m_lastEvent { eEventType::None };
			};
			public: class Theme
			{
				public:
					ostd::Color textColor { 0, 0, 0, 0 };
					ostd::Color borderColor { 0, 0, 0, 0 };
					ostd::Color backgroundColor { 0, 0, 0, 0 };

					ostd::Color textColor_Hover { 0, 0, 0, 0 };
					ostd::Color borderColor_Hover { 0, 0, 0, 0 };
					ostd::Color backgroundColor_Hover { 0, 0, 0, 0 };

					ostd::Color textColor_Pressed { 0, 0, 0, 0 };
					ostd::Color borderColor_Pressed { 0, 0, 0, 0 };
					ostd::Color backgroundColor_Pressed { 0, 0, 0, 0 };

					int32_t fontSize { 0 };
			};
			public: struct tDefaultThemes
			{
				inline static const Theme DebugTheme {
					{ 220, 	220, 	255 },	//Text Color
					{ 10, 	20, 	120 },	//Border Color
					{ 0, 	0, 		22 },	//Background Color

					{ 220, 	220, 	255 },	//Text Color Hover
					{ 10, 	20, 	120 },	//Border Color Hover
					{ 0, 	0, 		22 },	//Background Color Hover

					{ 220, 	220, 	255 },	//Text Color Pressed
					{ 10, 	20, 	120 },	//Border Color Pressed
					{ 0, 	0, 		22 },	//Background Color Pressed

					20						//Font Size
				};
				inline static const Theme DefaultTheme {
					{ 120, 	120, 	180 },	//Text Color
					{ 10, 	20, 	120 },	//Border Color
					{ 0, 	2, 		10 },	//Background Color

					{ 120, 	120, 	210 },	//Text Color Hover
					{ 10, 	20, 	180 },	//Border Color Hover
					{ 0, 	2, 		50 },	//Background Color Hover

					{ 120, 	120, 	120 },	//Text Color Pressed
					{ 10, 	20, 	60 },	//Border Color Pressed
					{ 0, 	2, 		0 },	//Background Color Pressed

					20						//Font Size
				};
			};
			public: enum eActionEventType { None = 0, Pressed };
			public: class ActionEventData : public ostd::BaseObject
			{
				public:
					inline ActionEventData(Button& _sender, const ostd::String& _senderName, eActionEventType _eventType, ostd::BaseObject& _userData) :
																																								sender(_sender),
																																								senderName(_senderName),
																																								eventType(_eventType),
																																								userData(_userData)
					{
						setTypeName("ogfx::gui::Button::ActionEventData");
						validate();
					}

				public:
					Button& sender;
					ostd::String senderName { "" };
					eActionEventType eventType { eActionEventType::None };
					ostd::BaseObject& userData { ostd::BaseObject::InvalidRef() };
			};

			public:
				inline Button(void) { create({ 0.0f, 0.0f }, { 200.0f, 30.0f }, "UnnamedButton"); }
				inline Button(const ostd::Vec2& position, const ostd::Vec2& size, const ostd::String& name) { create(position, size, name); }
				Button& create(const ostd::Vec2& position, const ostd::Vec2& size, const ostd::String& name);

				virtual void render(ogfx::BasicRenderer2D& gfx);
				virtual void update(void);
				virtual void fixedUpdate(void);

				virtual inline void onRender(ogfx::BasicRenderer2D& gfx) {  }
				virtual inline void onUpdate(void) {  }
				virtual inline void onFixedUpdate(void) {  }

				void setText(const ostd::String& text);
				void appendText(const ostd::String& text);
				void setTheme(Theme theme);

				inline void setEventListener(EventListener& evtl) { m_eventListener = &evtl; }
				inline void setName(const ostd::String& name) { m_name = name; }

				inline EventListener* getEventListener(void) const { return m_eventListener; }
				inline ostd::String getText(void) const { return m_text; }
				inline Theme& getTheme(void) { return m_theme; }
				inline ostd::String getName(void) const { return m_name; }
				inline bool isMouseInside(void) const { return m_mouseInside; }
				inline bool isPressed(void) const { return m_pressed; }

			private:
				EventListener* m_eventListener { nullptr };
				ogfx::BasicRenderer2D* m_gfx { nullptr };

			protected:
				ostd::String m_name { "" };
				ostd::String m_text { "" };
				Theme m_theme;
				bool m_mouseInside { false };
				bool m_pressed { false };

			public:
				inline static const uint32_t actionEventSignalID { ostd::SignalHandler::newCustomSignal(12400) };
		};
	}
}

namespace dragon
{
	typedef std::vector<dragon::code::Assembler::tDisassemblyLine> DisassemblyList;

	class DebuggerNew : public ogfx::WindowBase
	{
		public: struct tCommandLineArgs
		{
			inline tCommandLineArgs(void) {  }
			ostd::String machine_config_path = "";
			bool verbose_load = false;
			bool force_load = false;
			bool step_exec = false;
			bool track_step_diff = true;
			bool auto_start_debug = false;
			bool hide_virtual_display = true;
			bool track_call_stack = true;
			bool auto_track_all_data_symbols = true;
			ostd::String force_load_file = "";
			uint16_t force_load_mem_offset = 0x00;
		};
		public: struct tDebuggerData
		{
			inline tDebuggerData(void) {  }
			tCommandLineArgs args;
			DisassemblyList code;
			DisassemblyList labels;
			DisassemblyList data;
			std::vector<uint16_t> trackedAddresses;
			ostd::String command;
			int32_t labelLineLength { 40 };
			uint16_t currentAddress { 0 };
			bool userQuit { false };
			ostd::String disassemblyDirectory { "disassembly" };
			std::vector<uint16_t> manualBreakPoints;
		};
		public:
			//Utils
			DisassemblyList findCodeRegion(const DisassemblyList& code, uint16_t address, uint16_t codeRegionMargin);
			ostd::String findSymbol(const DisassemblyList& labels, uint16_t address, uint16_t* outSize = nullptr);
			uint16_t findSymbol(const DisassemblyList& labels, const ostd::String& symbol, uint16_t* outSize = nullptr);
			bool isValidLabelNameChar(char c);
			void removeBreakPoint(uint16_t addr);
			bool isBreakPoint(uint16_t addr);
			void addBreakPoint(uint16_t addr);

			//Debugger
			void processErrors(void);
			int32_t loadArguments(int argc, char** argv);
			int32_t initRuntime(void);

			//Display
			void colorizeInstructionBody(const ostd::String& instBody, bool currentLine, const DisassemblyList& labelList);
			void colorCodeInstructions(const ostd::String& inst, bool currentLine, const DisassemblyList& labelList);
			void printStep(void);

			//General
			inline DebuggerNew(void) : m_sigHandler(m_textInput, *this), m_btnSigHandler(m_testBtn) {  }
			void onInitialize(void) override;
			void handleSignal(ostd::tSignal& signal) override;
			void onRender(void) override;
			void onFixedUpdate(void) override;
			void onUpdate(void) override;

		private:
			tDebuggerData debugger;
			ogfx::gui::RawTextInput m_textInput;
			ogfx::BasicRenderer2D m_gfx;
			ogfx::gui::RawTextInputEventListener m_sigHandler;
			ogfx::gui::RawTextInputNumberCharacterFilter m_numCharFilter;

			ogfx::gui::Button m_testBtn;
			ogfx::gui::Button::EventListener m_btnSigHandler;

			ogfx::WindowBaseOutputHandler m_wout;
			ostd::IPoint m_consoleSize { 300, 50 };
			ostd::Vec2 m_consolePosition { 650, 8 };
			// std::vector<code::Assembler::tDisassemblyLine> m_codeTable;
			// int32_t m_codeRandomIndex { 0 };

	};

	uint32_t __debugger_entry_point(void);
}
