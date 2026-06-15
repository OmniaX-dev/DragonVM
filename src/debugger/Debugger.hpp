#pragma once

#include <ostd/io/IOHandlers.hpp>
#include "../assembler/Assembler.hpp"

namespace dragon
{
	typedef std::vector<dragon::code::Assembler::tDisassemblyLine> DisassemblyList;

	class Debugger
	{
		public: struct tCommandLineArgs
		{
			inline tCommandLineArgs(void) {  }
			String machine_config_path = "";
			bool verbose_load = false;
			bool force_load = false;
			bool step_exec = false;
			bool track_step_diff = true;
			bool auto_start_debug = false;
			bool hide_virtual_display = true;
			bool track_call_stack = true;
			bool auto_track_all_data_symbols = true;
			String force_load_file = "";
			u16 force_load_mem_offset = 0x00;
		};
		public: struct tDebuggerData
		{
			inline tDebuggerData(void) {  }
			tCommandLineArgs args;
			DisassemblyList code;
			DisassemblyList labels;
			DisassemblyList data;
			std::vector<u16> trackedAddresses;
			String command;
			i32 labelLineLength { 40 };
			u16 currentAddress { 0 };
			bool userQuit { false };
			String disassemblyDirectory { "disassembly" };
			std::vector<u16> manualBreakPoints;
		};
		struct tCloseEventListener : public ostd::BaseObject
		{
			void init(void);
			void handleSignal(ostd::Signal& signal);
			inline bool hasHappened(void) const { return m_mainWindowClosed; }

			private:
				bool m_mainWindowClosed { false };
		};
		public: class Utils
		{
			public:
				static DisassemblyList findCodeRegion(const DisassemblyList& code, u16 address, u16 codeRegionMargin);
				static String findSymbol(const DisassemblyList& labels, u16 address, u16* outSize = nullptr);
				static u16 findSymbol(const DisassemblyList& labels, const String& symbol, u16* outSize = nullptr);
				static bool isValidLabelNameChar(char c);
				static void clearConsoleLine(void);
				static bool isEscapeKeyPressed(bool blocking = false);
				static ostd::ConsoleOutputHandler& printFullLine(char c, const ostd::ConsoleColors::tConsoleColor& foreground);
				static ostd::ConsoleOutputHandler& printFullLine(char c, const ostd::ConsoleColors::tConsoleColor& foreground, const ostd::ConsoleColors::tConsoleColor& background);
				static void removeBreakPoint(u16 addr);
				static bool isBreakPoint(u16 addr);
				static void addBreakPoint(u16 addr);
		};
		public: class Display
		{
			public:
				static void colorizeInstructionBody(const String& instBody, bool currentLine, const DisassemblyList& labelList);
				static void colorCodeInstructions(const String& inst, bool currentLine, const DisassemblyList& labelList);
				static void printPrompt(void);
				static void printStep(void);
				static void printDiff(void);
				static void printTrackedAddresses(void);
				static void printStack(u16 nrows);
				static void printCallStack(void);
				static void printHelp(void);
				static String changeScreen(void);
		};
		public:
			static void processErrors(void);
			static i32 loadArguments(int argc, char** argv);
			static i32 initRuntime(void);
			static String getCommandInput(void);
			static inline tDebuggerData& data(void) { return debugger; }
			static inline ostd::ConsoleOutputHandler& output(void) { return out; }
			static i32 topLevelPrompt(void);
			static i32 executeRuntime(void);

		private:
			static i32 step_execution(bool& outUserQuit, bool exec_first_step = true);
			static i32 normal_runtime(bool& outUserQuit);
			static void exec_watch_command(void);
			static void print_top_level_prompt_help(void);
			static void print_application_help(void);

		private:
			inline static tDebuggerData debugger;
			inline static ostd::ConsoleOutputHandler out;
			static tCloseEventListener closeEventListener;

		public:
			inline static const String InputCommandQuit = "//quit//";
	};
}
