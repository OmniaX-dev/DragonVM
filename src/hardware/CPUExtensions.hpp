#pragma once

#include "../tools/GlobalData.hpp"

namespace dragon
{
	namespace hw
	{
		namespace cpuext
		{
			class ExtMov : public data::CPUExtension
			{
				public:
					inline ExtMov(void) : data::CPUExtension(data::OpCodes::Ext01, "extmov") {  }
					ostd::String getOpCodeString(uint8_t opCode) override;
					uint8_t getInstructionSIze(uint8_t opCode) override;
					bool execute(VirtualCPU& vcpu) override;
			};
		}
	}
}