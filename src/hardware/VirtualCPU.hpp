#pragma once

#include <ostd/data/Types.hpp>
#include <ostd/data/Bitfields.hpp>
#include <ostd/utils/Time.hpp>
#include "IMemoryDevice.hpp"

#include "../debugger/Debugger.hpp"
#include  "../tools/GlobalData.hpp"

namespace dragon
{
	class DragonRuntime;
	namespace hw
	{
		class VirtualCPU
		{
			public: enum class eDebugProfilerTimeUnits { Millis = 0, Secs = 1, Micros = 2, Nanos = 3 };
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

			private:
				void __debug_store_stack_frame_string_on_push(void);

			private:
				i16 m_registers[20];
				ostd::BitField_16 m_tempFlags;
				IMemoryDevice& m_memory;
				u16 m_stackFrameSize { 0 };
				bool m_halt { false };
				u8 m_currentInst { 0x00 };
				u8 m_currentAddr { 0x00 };
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

				std::vector<String> m_debug_stackFrameStrings;

				ostd::Counter m_profilerTimer;

			friend class dragon::DragonRuntime;
			friend class dragon::Debugger::Display;
			friend class dragon::Debugger;
		};
	}
}
