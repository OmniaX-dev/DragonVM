#pragma once

#include <ostd/io/IOHandlers.hpp>
#include <chrono>
#include <set>
#include <unordered_map>
#include "../assembler/Assembler.hpp"
#include "../hardware/VirtualCPU.hpp"
#include "../hardware/VirtualIODevices.hpp"
#include "History.hpp"

namespace dragon
{
	namespace hw { class VirtualDisplay; }
	using debugger::History;
	typedef std::vector<dragon::code::Assembler::tDisassemblyLine> DisassemblyList;

	// ============================================================================
	// Debugger — the machine under control: running, stepping (into / over / out,
	// forwards and backwards in time), breakpoints, tracked data, the call trace
	// and the console commands (the old TUI debugger's, plus new ones).
	//
	// The machine runs in slices (update(), from the window's loop), at the
	// configured clock speed or as fast as possible. Every cycle is recorded in the
	// History (see History.hpp), so the machine can be stepped back, run backwards
	// to a breakpoint, or moved to any recorded cycle; stepping forwards over
	// recorded cycles replays them (the future is kept until the machine executes
	// something different, or something is edited).
	//
	// One cycle (step_machine()): the CPU executes one instruction, the display
	// handles its video signal (and redraws every cyclesPerRedraw cycles), the disk
	// controller does its step; then the cycle is recorded and the stop conditions
	// are checked (breakpoints, debug_break, halt, errors, tracked data changes,
	// step over / out targets).
	//
	// No widgets here: the window (DebuggerWindow) shows the state, and the views
	// are printed by Debugger::Display (the old TUI printers) into any
	// ostd::OutputHandlerBase, e.g. a RichTextOutputHandler.
	// ============================================================================
	class Debugger
	{
		public: struct tCommandLineArgs
		{
			inline tCommandLineArgs(void) {  }
			String machine_config_path = "";
			bool verbose_load = false;
			bool force_load = false;
			bool step_exec = true;
			bool track_step_diff = true;
			bool auto_start_debug = false;
			bool hide_virtual_display = false;
			bool track_call_stack = true;
			bool auto_track_all_data_symbols = true;
			bool max_speed = false;
			u64 history_limit_mb = History::DefaultMemoryLimit / (1024 * 1024);
			String force_load_file = "";
			u16 force_load_mem_offset = 0x00;
			// Console commands run at the start, ';' separated (each one once the machine stopped).
			String exec_commands = "";
		};
		// A tracked value: shown in the tracker (previous and current value), and
		// optionally a watchpoint (the machine stops when it changes).
		public: struct tTrackedData
		{
			u16 address { 0 };
			u16 size { 1 };
			String label { "" };
			bool breakOnChange { false };
		};
		// One call / return / interrupt, with the history position it happened at (the
		// trace is cut when the history is rewritten) and the call depth after it.
		public: struct tCallInfo
		{
			String info;
			u16 addr { 0 };
			u16 inst_addr { 0 };
			bool interrupts_disabled { false };
			u64 position { 0 };
			i32 depth { 0 };
		};
		// What the diff view compares: the machine before and after the last step.
		public: struct tMachineInfo
		{
			u64 cycle { 0 };
			u16 instructionAddress { 0 };
			String opCode { "" };
			u8 footprint[8] { 0 };
			u8 footprintSize { 0 };
			u16 stackFrameSize { 0 };
			bool debugBreak { false };
			i32 interruptHandlerCount { 0 };
			bool biosMode { false };
			i32 subroutineCounter { 0 };
			bool inSubroutine { false };
			bool halted { false };
			i16 registers[data::Registers::Last] { 0 };
			stdvec<u8> trackedValues;
		};
		public: enum class eMode { Paused = 0, Running, StepOver, StepOut, RunTo, ReverseRunning };
		public: enum class eView { Disassembly = 0, Registers, Memory, Stack, CallTrace, Tracker, Breakpoints, History, Console };
		public: struct tDebuggerData
		{
			inline tDebuggerData(void) {  }
			tCommandLineArgs args;
			DisassemblyList code;
			DisassemblyList labels;
			DisassemblyList data;
			stdvec<tTrackedData> tracked;
			i32 labelLineLength { 40 };
			String disassemblyDirectory { "disassembly" };
			std::set<u16> breakpoints;
		};
		public: class Utils
		{
			public:
				static String findSymbol(const DisassemblyList& labels, u16 address, u16* outSize = nullptr);
				static u16 findSymbol(const DisassemblyList& labels, const String& symbol, u16* outSize = nullptr);
				// The label at or before the address ("$label+0x4"); "" if none.
				static String findNearestSymbol(const DisassemblyList& labels, u16 address);
				static bool isValidLabelNameChar(char c);
				static ostd::OutputHandlerBase& printFullLine(ostd::OutputHandlerBase& out, char c, const ostd::ConsoleColors::tConsoleColor& foreground, i32 width);
				static ostd::OutputHandlerBase& printFullLine(ostd::OutputHandlerBase& out, char c, const ostd::ConsoleColors::tConsoleColor& foreground, const ostd::ConsoleColors::tConsoleColor& background, i32 width);
				// Name and size of the instruction at an address, decoded from memory (extension
				// instructions included).
				static String decodeInstruction(u16 address, u8* outSize = nullptr);
				// "0x1234", "1234", "$symbol" (data or label) -> address. False if invalid.
				static bool parseAddress(const Debugger& dbg, const String& text, u16& outAddress, u16* outSize = nullptr);
				static bool parseValue(const String& text, i64& outValue);
		};
		// The old TUI's screens, printed into any output handler.
		public: class Display
		{
			public:
				static void colorizeInstructionBody(ostd::OutputHandlerBase& out, const String& instBody, const DisassemblyList& labelList);
				static void colorCodeInstructions(ostd::OutputHandlerBase& out, const String& inst, const DisassemblyList& labelList);
				static void printDisassemblyLine(ostd::OutputHandlerBase& out, const Debugger& dbg, const code::Assembler::tDisassemblyLine& line);
				static void printDiff(ostd::OutputHandlerBase& out, const Debugger& dbg);
				static void printTrackedAddresses(ostd::OutputHandlerBase& out, const Debugger& dbg, i32 width);
				static void printStack(ostd::OutputHandlerBase& out, const Debugger& dbg, u16 nrows, i32 width);
				static void printCallStack(ostd::OutputHandlerBase& out, const Debugger& dbg, i32 width);
				static void printBreakpoints(ostd::OutputHandlerBase& out, const Debugger& dbg);
				static void printHistory(ostd::OutputHandlerBase& out, const Debugger& dbg, i32 rows);
				static void printHelp(ostd::OutputHandlerBase& out);
				static void printApplicationHelp(ostd::OutputHandlerBase& out);
		};
		public: using StateCallback = std::function<void(void)>;
		public: using ViewCallback = std::function<void(eView view, i64 argument)>;

		public:
			// ================================== SETUP ==================================
			i32 loadArguments(int argc, char** argv);
			// Loads the machine (DragonRuntime) and the disassembly; the display must be in a window.
			i32 initRuntime(hw::VirtualDisplay& display);
			inline tDebuggerData& data(void) { return m_data; }
			inline const tDebuggerData& data(void) const { return m_data; }
			// Command output, messages, errors.
			inline void setConsole(ostd::OutputHandlerBase& console) { m_console = &console; }
			inline ostd::OutputHandlerBase& console(void) { return *m_console; }
			// Called when the machine stops (or its state changed while paused: an edit, a seek...).
			inline void setStateChangedCallback(StateCallback callback) { m_onStateChanged = std::move(callback); }
			// The commands that show a view ("stack", "x 0x1234", ...).
			inline void setViewCallback(ViewCallback callback) { m_onShowView = std::move(callback); }
			inline void setQuitCallback(StateCallback callback) { m_onQuit = std::move(callback); }

			// =============================== RUN CONTROL ===============================
			// Runs a slice of the machine if it's running (call it from the window's loop).
			void update(void);
			void resume(void);
			void pause(const String& reason = "");
			void stepInto(u32 count = 1);
			void stepOver(void);
			void stepOut(void);
			void runTo(u16 address);
			void stepBack(u32 count = 1);
			void reverseContinue(void);
			// Moves to a recorded history position.
			void seek(u64 position);
			inline bool isRunning(void) const { return m_mode != eMode::Paused; }
			inline eMode getMode(void) const { return m_mode; }
			inline const String& getStopReason(void) const { return m_stopReason; }
			inline u64 getCycle(void) const { return m_cycle; }
			inline i32 getCallDepth(void) const { return m_callDepth; }
			inline bool isMaxSpeed(void) const { return m_maxSpeed; }
			inline void setMaxSpeed(bool max) { m_maxSpeed = max; m_lastSliceTime = Clock::now(); m_cycleBudget = 0; }
			inline f64 getClockRate(void) const { return m_clockRate; }
			// Cycles per second actually executed (measured while running).
			inline f64 getMeasuredSpeed(void) const { return m_measuredSpeed; }

			// ================================== HISTORY ==================================
			inline History& history(void) { return m_history; }
			inline const History& history(void) const { return m_history; }
			inline bool isHistoryEnabled(void) const { return m_historyEnabled; }
			void enableHistory(bool enable);
			// Records the changes made from the outside (memory / register edits) as one entry.
			void commitEdit(void);

			// ============================ BREAKPOINTS / DATA ============================
			// Returns whether the address has a breakpoint now.
			bool toggleBreakPoint(u16 address);
			inline bool isBreakPoint(u16 address) const { return m_data.breakpoints.count(address) > 0; }
			inline void addBreakPoint(u16 address) { m_data.breakpoints.insert(address); }
			inline void removeBreakPoint(u16 address) { m_data.breakpoints.erase(address); }
			void track(u16 address, u16 size, const String& label, bool breakOnChange = false);
			bool untrack(u16 address);
			// Writes a register (an edit, recorded in the history).
			bool setRegister(const String& name, i64 value);
			// Writes memory (an edit, recorded in the history).
			bool writeMemory(u16 address, u64 value, u8 size);

			// ================================ MACHINE INFO ================================
			inline const tMachineInfo& getPreviousInfo(void) const { return m_previousInfo; }
			inline const tMachineInfo& getCurrentInfo(void) const { return m_currentInfo; }
			inline const std::deque<tCallInfo>& getCallTrace(void) const { return m_callTrace; }
			// Calls up to the current history position.
			size_t getVisibleCallTraceSize(void) const;
			inline hw::VirtualDisplay& getDisplay(void) const { return *m_display; }
			// The 64 KiB the CPU sees (refreshed by refreshBusImage()).
			inline const stdvec<u8>& getBusImage(void) const { return m_busImage; }
			void refreshBusImage(void);

			// ================================== COMMANDS ==================================
			// Executes a console command (see Display::printHelp()).
			void command(const String& commandLine);

		private:
			using Clock = std::chrono::steady_clock;

			bool step_machine(void);
			bool check_stop_conditions(u16 previousIP);
			void stopped(const String& reason);
			void track_call(u16 ip);
			void after_navigation(void);
			void capture_info(tMachineInfo& info) const;
			void setup_history(void);
			void exec_track_command(const String& args, bool breakOnChange);
			void exec_print_command(const String& args);
			void exec_break_command(const String& args);
			void process_errors(void);
			// Offset in the history state of the byte the CPU sees at a bus address; -1 if none.
			i64 bus_state_offset(u16 address) const;
			void notify_state_changed(void);
			void show_view(eView view, i64 argument = -1);

		private:
			tDebuggerData m_data;
			ostd::ConsoleOutputHandler m_stdout;
			ostd::OutputHandlerBase* m_console { &m_stdout };
			hw::VirtualDisplay* m_display { nullptr };
			StateCallback m_onStateChanged { nullptr };
			ViewCallback m_onShowView { nullptr };
			StateCallback m_onQuit { nullptr };

			eMode m_mode { eMode::Paused };
			String m_stopReason { "" };
			u64 m_cycle { 0 };
			i32 m_callDepth { 0 };
			i32 m_targetDepth { 0 };
			u16 m_runToAddress { 0 };
			u32 m_stepsLeft { 0 };

			f64 m_clockRate { 2000.0 };
			bool m_maxSpeed { false };
			u64 m_cyclesPerRedraw { 66 };
			Clock::time_point m_lastSliceTime;
			f64 m_cycleBudget { 0.0 };
			f64 m_measuredSpeed { 0.0 };
			u64 m_speedCycles { 0 };
			Clock::time_point m_speedTime;

			History m_history;
			bool m_historyEnabled { true };
			hw::VirtualCPU::tState m_cpuState;
			hw::interface::Graphics::tState m_graphicsState;
			u8 m_diskBusy { 0 };
			stdvec<u8> m_displayState;
			std::unordered_map<const hw::IMemoryDevice*, String> m_deviceSegments;

			std::deque<tCallInfo> m_callTrace;
			tMachineInfo m_previousInfo;
			tMachineInfo m_currentInfo;
			stdvec<u8> m_busImage;

		public:
			inline static constexpr size_t MaxCallTraceSize { 200000 };
			inline static const String InputCommandQuit = "//quit//";
	};
}
