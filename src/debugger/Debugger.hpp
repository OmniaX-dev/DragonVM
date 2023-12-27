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
			bool track_step_diff = false;
			bool auto_start_debug = false;
			bool hide_virtual_display = false;
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
			ostd::StringEditor command;
			int32_t labelLineLength { 20 };
			uint16_t currentAddress { 0 };
			bool userQuit { false };
			ostd::StringEditor disassemblyDirectory { "disassembly" };
		};
		public: class Utils
		{
			public:
				static DisassemblyList findCodeRegion(const DisassemblyList& code, uint16_t address, uint16_t codeRegionMargin);
				static ostd::String findSymbol(const DisassemblyList& labels, uint16_t address);
				static uint16_t findSymbol(const DisassemblyList& labels, const ostd::StringEditor& symbol);
				static bool isValidLabelNameChar(char c);
				static ostd::StringEditor fillString(const ostd::StringEditor& str, char fill, int32_t totalLength); //TODO: Implement in omnia-framework
		};
		public: class Display
		{
			public:
				static void colorizeInstructionBody(const ostd::String& instBody, bool currentLine, const DisassemblyList& labelList);
				static void colorCodeInstructions(const ostd::String& inst, bool currentLine, const DisassemblyList& labelList);
				static void printPrompt(void);
				static void printStep(void);
				static void printDiff(void);
				static void printTrackedAddresses(const std::vector<uint16_t>& trackedAddresses);
				static ostd::StringEditor changeScreen(void);
		};
		public:
			static void processErrors(void);
			static int32_t loadArguments(int argc, char** argv);
			static int32_t initRuntime(void);
			static ostd::StringEditor getCommandInput(void);
			static inline tDebuggerData& data(void) { return debugger; }
			static inline ostd::legacy::ConsoleOutputHandler& output(void) { return out; }

		private:
			inline static tDebuggerData debugger;
			inline static ostd::legacy::ConsoleOutputHandler out;
	};
}