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
				i16 registers[data::Registers::Last];
				u16 stackFrameSize;
				u16 currentAddr;
				u16 currentOffset;
				i32 interruptHandlerCount;
				i32 subroutineCounter;
				u8 currentInst;
				u8 currentExtInst;
				i8 currentExtension;	// index in the extension table, -1 = none
				bool halt;
				bool biosMode;
				bool isDebugBreakPoint;
				bool ramDumped;
				bool isOffsetAddressingEnabled;
				bool debugProfilerStarted;
			};
			// Called whenever an interrupt handler is entered (hardware = raised by a device,
			// otherwise by the int instruction).
			public: using InterruptListener = std::function<void(u8 intValue, bool hardware, u16 handlerAddress)>;
			public:
				VirtualCPU(IMemoryDevice& memory);
				i16 readRegister(u8 reg);
				i16 writeRegister16(u8 reg, i16 value);
				i8 writeRegister8(u8 reg, i8 value);
				//TODO: Implement readRegister8 and readRegister16 functions

				i8 fetch8(void);
				i16 fetch16(void);

				void pushToStack(i16 value);
				i16 popFromStack(void);

				void pushStackFrame(void);
				void popStackFrame(void);

				bool readFlag(u8 flg);
				void setFlag(u8 flg, bool val = true);

				void handleInterrupt(u8 intValue, bool hardware);

				bool loadExtension(void);
				bool execute(void);

				inline bool isHalted(void) const { return m_halt; }
				inline u8 getCurrentInstruction(void) const { return m_currentInst; }
				inline bool isInDebugBreakPoint(void) const { return m_isDebugBreakPoint; }
				inline bool isRamDumped(void) const { return m_ramDumped; }
				inline bool isInBIOSMOde(void) const { return m_biosMode; }
				inline bool isInSubRoutine(void) const { return m_subroutineCounter > 0; }
				inline i32 getSubRoutineCounter(void) const { return m_subroutineCounter; }
				inline data::CPUExtension* getCurrentCPUExtension(void) const { return m_currentExtension; }
				inline u8 getCurrentCPUExtensionInstruction(void) const { return m_currentExtInst; }
				inline bool isOffsetAddressingModeEnabled(void) const { return m_isOffsetAddressingEnabled; }
				inline u16 getCurrentOffset(void) const { return m_currentOffset; }
				inline u16 getStackFrameSize(void) const { return m_stackFrameSize; }
				inline i32 getInterruptHandlerCount(void) const { return m_interruptHandlerCount; }
				inline bool isDebugModeEnabled(void) const { return m_debugModeEnabled; }
				// The extension with the given opcode (Ext01..Ext16), or nullptr.
				data::CPUExtension* getExtension(u8 opCode) const;

				void saveState(tState& outState) const;
				void loadState(const tState& state);
				inline void setInterruptListener(InterruptListener listener) { m_interruptListener = std::move(listener); }

			private:
				i16 m_registers[20];
				ostd::BitField_16 m_tempFlags;
				IMemoryDevice& m_memory;
				u16 m_stackFrameSize { 0 };
				bool m_halt { false };
				u8 m_currentInst { 0x00 };
				u16 m_currentAddr { 0x0000 };
				bool m_biosMode { true };
				i32 m_interruptHandlerCount { 0 };
				bool m_isDebugBreakPoint { false };
				bool m_ramDumped { false };
				bool m_debugModeEnabled { false };
				i32 m_subroutineCounter { 0 };
				bool m_debugProfilerStarted { false };

				bool m_isOffsetAddressingEnabled { false };
				u16 m_currentOffset { 0x0000 };

				data::CPUExtension* m_extensions[16];
				data::CPUExtension* m_currentExtension { nullptr };
				u8 m_currentExtInst { 0x00 };

				ostd::Counter m_profilerTimer;
				InterruptListener m_interruptListener { nullptr };

			friend class dragon::DragonRuntime;
		};
	}
}
