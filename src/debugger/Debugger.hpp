#pragma once

#include <ostd/Utils.hpp>
#include <ostd/IOHandlers.hpp>
#include "../assembler/Assembler.hpp"

namespace dragon
{
	typedef std::vector<dragon::code::Assembler::tDisassemblyLine> DisassemblyList;

	class Debugger
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
			int32_t labelLineLength { 20 };
			uint16_t currentAddress { 0 };
			bool userQuit { false };
			ostd::String disassemblyDirectory { "disassembly" };
		};
		struct tCloseEventListener : public ostd::BaseObject
		{
			void init(void);
			void handleSignal(ostd::tSignal& signal);
			inline bool hasHappened(void) const { return m_mainWindowClosed; }

			private:
				bool m_mainWindowClosed { false };
		};
		public: class Utils
		{
			public:
				static DisassemblyList findCodeRegion(const DisassemblyList& code, uint16_t address, uint16_t codeRegionMargin);
				static ostd::String findSymbol(const DisassemblyList& labels, uint16_t address, uint16_t* outSize = nullptr);
				static uint16_t findSymbol(const DisassemblyList& labels, const ostd::String& symbol, uint16_t* outSize = nullptr);
				static bool isValidLabelNameChar(char c);
				static void clearConsoleLine(void);
				static bool isEscapeKeyPressed(bool blocking = false);
				static ostd::ConsoleOutputHandler& printFullLine(char c, const ostd::ConsoleColors::tConsoleColor& foreground);
				static ostd::ConsoleOutputHandler& printFullLine(char c, const ostd::ConsoleColors::tConsoleColor& foreground, const ostd::ConsoleColors::tConsoleColor& background);
		};
		public: class Display
		{
			public:
				static void colorizeInstructionBody(const ostd::String& instBody, bool currentLine, const DisassemblyList& labelList);
				static void colorCodeInstructions(const ostd::String& inst, bool currentLine, const DisassemblyList& labelList);
				static void printPrompt(void);
				static void printStep(void);
				static void printDiff(void);
				static void printTrackedAddresses(void);
				static void printStack(uint16_t nrows);
				static void printCallStack(void);
				static void printHelp(void);
				static ostd::String changeScreen(void);
		};
		public:
			static void processErrors(void);
			static int32_t loadArguments(int argc, char** argv);
			static int32_t initRuntime(void);
			static ostd::String getCommandInput(void);
			static inline tDebuggerData& data(void) { return debugger; }
			static inline ostd::ConsoleOutputHandler& output(void) { return out; }
			static int32_t topLevelPrompt(void);
			static int32_t executeRuntime(void);
		
		private:
			static int32_t step_execution(bool& outUserQuit, bool exec_first_step = true);
			static int32_t normal_runtime(bool& outUserQuit);
			static void exec_watch_command(void);
			static void print_top_level_prompt_help(void);
			static void print_application_help(void);

		private:
			inline static tDebuggerData debugger;
			inline static ostd::ConsoleOutputHandler out;
			static tCloseEventListener closeEventListener;

		public:
			inline static const ostd::String InputCommandQuit = "//quit//";
	};
}