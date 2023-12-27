#pragma once

#include <ostd/Types.hpp>
#include <ostd/Utils.hpp>
#include <ostd/Bitfields.hpp>
#include "IMemoryDevice.hpp"

namespace dragon
{
	class DragonRuntime;
	namespace hw
	{
		class VirtualCPU
		{
			public:
				VirtualCPU(IMemoryDevice& memory);
				int16_t readRegister(uint8_t reg);
				int16_t writeRegister(uint8_t reg, int16_t value);

				int8_t fetch8(void);
				int16_t fetch16(void);

				void pushToStack(int16_t value);
				int16_t popFromStack(void);

				void pushStackFrame(void);
				void popStackFrame(void);

				bool readFlag(uint8_t flg);
				void setFlag(uint8_t flg, bool val = true);

				void handleInterrupt(uint8_t intValue);

				bool execute(void);

				inline bool isHalted(void) const { return m_halt; }
				inline uint8_t getCurrentInstruction(void) const { return m_currentInst; }
				inline bool isInDebugBreakPoint(void) const { return m_isDebugBreakPoint; }

			private:
				int16_t m_registers[20];
				ostd::BitField_16 m_tempFlags;
				IMemoryDevice& m_memory;
				uint16_t m_stackFrameSize { 0 };
				bool m_halt { false };
				uint8_t m_currentInst { 0x00 };
				bool m_biosMode { true };
				bool m_isInInterruptHandler { false };
				bool m_isDebugBreakPoint { false };

			friend class dragon::DragonRuntime;
		};
	}
}