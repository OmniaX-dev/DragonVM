#include "CPUExtensions.hpp"
#include "VirtualCPU.hpp"
#include "../runtime/DragonRuntime.hpp"

namespace dragon
{
	namespace hw
	{
		namespace cpuext
		{
			ostd::String ExtMov::getOpCodeString(uint8_t opCode)
			{
				switch (opCode)
				{
					case 0x01: return m_name + "_Test";
					default: return "UNKNOWN_INST";
				}
			}

			uint8_t ExtMov::getInstructionSIze(uint8_t opCode)
			{
				switch (opCode)
				{
					case 0x01: return 2;
					default: return 0;
				}
			}

			bool ExtMov::execute(VirtualCPU& vcpu)
			{
				uint8_t inst = vcpu.fetch8();
				switch (inst)
				{
					case 0x01:
					{
						
					}
					break;
					default:
					{
						//TODO: Error
						return false;
					}
				}
				return true;
			}

		}
	}
}