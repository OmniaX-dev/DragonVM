#pragma once

#include <ostd/data/Types.hpp>
#include <ostd/data/Bitfields.hpp>
#include <ostd/utils/Time.hpp>
#include <functional>
#include "IMemoryDevice.hpp"

#include  "../tools/GlobalData.hpp"

namespace dragon
{
	class DragonRuntime;
	namespace hw
	{
		class VirtualCPU
		{
			public: enum class eDebugProfilerTimeUnits { Millis = 0, Secs = 1, Micros = 2, Nanos = 3 };
			// Everything execute() depends on (and changes), as plain bytes: the debugger's
			// history diffs and restores it. saveState() zeroes the padding, so equal states
			// are equal byte for byte.
			public: struct tState
			{
				i32 registers[data::Registers::Last];
				u32 stackFrameSize;
				u32 currentAddr;
				u32 currentOffset;
				i32 interruptHandlerCount;
				i32 subroutineCounter;
				u8 currentInst;
				u8 currentExtInst;
				i8 currentExtension;    // index in the extension table, -1 = none
				bool halt;
				bool biosMode;
				bool isDebugBreakPoint;
				bool ramDumped;
				bool isOffsetAddressingEnabled;
				bool debugProfilerStarted;
			};
			// Called whenever an interrupt handler is entered (hardware = raised by a device,
			// otherwise by the int instruction).
			public: using InterruptListener = std::function<void(u8 intValue, bool hardware, u32 handlerAddress)>;
			public:
				VirtualCPU(IMemoryDevice& memory);
				// Sets SP and FP to the top of the stack (the last word of the RAM).
				void setStackTop(u32 stackTop);
				// Power-on state: every register and flag 0, IP 0, SP / FP at the stack top, BIOS
				// mode, not halted, no interrupt / subroutine pending, no offset addressing. The
				// extensions, the debug mode and the handlers stay.
				void reset(void);
				inline u32 getStackTop(void) const { return m_stackTop; }
				// `reg` is a register operand (index + view, see data::Registers): a half (RxL /
				// RxH) reads zero-extended; writes are truncated to the view's width and change
				// only the view (the whole register for a full view: smaller values are zero-extended).
				i32 readRegister(u8 reg);
				i32 writeRegister32(u8 reg, i32 value);
				i16 writeRegister16(u8 reg, i16 value);
				i8 writeRegister8(u8 reg, i8 value);

				i8 fetch8(void);
				i16 fetch16(void);
				i32 fetch32(void);

				// debug_print: the zero-terminated string at addr, to the host's stdout.
				void debugPrint(u32 addr);
				inline static constexpr u32 DebugPrintMaxLength = 4096;

				void pushToStack(i32 value);
				i32 popFromStack(void);

				void pushStackFrame(void);
				void popStackFrame(void);

				bool readFlag(u8 flg);
				void setFlag(u8 flg, bool val = true);

				bool handleInterrupt(u8 intValue, bool hardware);

				bool loadExtension(void);
				bool execute(void);

				inline bool isHalted(void) const { return m_halt; }
				inline u8 getCurrentInstruction(void) const { return m_currentInst; }
				inline bool isInDebugBreakPoint(void) const { return m_isDebugBreakPoint; }
				inline bool isRamDumped(void) const { return m_ramDumped; }
				inline bool isInBIOSMOde(void) const { return m_biosMode; }
				// The debugger's writes to protected devices (the CMOS chip) need BIOS mode.
				inline void setBIOSMode(bool biosMode) { m_biosMode = biosMode; }
				inline bool isInSubRoutine(void) const { return m_subroutineCounter > 0; }
				inline i32 getSubRoutineCounter(void) const { return m_subroutineCounter; }
				inline data::CPUExtension* getCurrentCPUExtension(void) const { return m_currentExtension; }
				inline u8 getCurrentCPUExtensionInstruction(void) const { return m_currentExtInst; }
				inline bool isOffsetAddressingModeEnabled(void) const { return m_isOffsetAddressingEnabled; }
				inline u32 getCurrentOffset(void) const { return m_currentOffset; }
				inline u32 getStackFrameSize(void) const { return m_stackFrameSize; }
				inline i32 getInterruptHandlerCount(void) const { return m_interruptHandlerCount; }
				inline bool isDebugModeEnabled(void) const { return m_debugModeEnabled; }
				// The extension with the given opcode (Ext01..Ext16), or nullptr.
				data::CPUExtension* getExtension(u8 opCode) const;

				void saveState(tState& outState) const;
				void loadState(const tState& state);
				inline void setInterruptListener(InterruptListener listener) { m_interruptListener = std::move(listener); }
				// Where debug_print's text goes (nullptr: stdout), e.g. the debugger's console.
				using DebugPrintHandler = std::function<void(const std::string& text)>;
				inline void setDebugPrintHandler(DebugPrintHandler handler) { m_debugPrintHandler = std::move(handler); }

				// false (and halts, with an error) if divisor is 0.
				bool check_divisor(i32 divisor);

			private:
				void write_register(u8 reg, u32 value);

			private:
				i32 m_registers[data::Registers::Last];
				ostd::BitField_32 m_tempFlags;
				u32 m_stackTop { 0 };
				IMemoryDevice& m_memory;
				u32 m_stackFrameSize { 0 };
				bool m_halt { false };
				u8 m_currentInst { 0x00 };
				u32 m_currentAddr { 0x0000 };
				bool m_biosMode { true };
				i32 m_interruptHandlerCount { 0 };
				bool m_isDebugBreakPoint { false };
				bool m_ramDumped { false };
				bool m_debugModeEnabled { false };
				DebugPrintHandler m_debugPrintHandler { nullptr };
				i32 m_subroutineCounter { 0 };
				bool m_debugProfilerStarted { false };

				bool m_isOffsetAddressingEnabled { false };
				u32 m_currentOffset { 0x0000 };

				data::CPUExtension* m_extensions[16];
				data::CPUExtension* m_currentExtension { nullptr };
				u8 m_currentExtInst { 0x00 };

				ostd::Counter m_profilerTimer;
				InterruptListener m_interruptListener { nullptr };

			friend class dragon::DragonRuntime;
		};
	}
}
