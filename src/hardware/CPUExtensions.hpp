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
				public: class OpCodes
				{
					public:
						inline static constexpr u8 wimm_in_dreg_immoffw		=	0x10;
						inline static constexpr u8 wimm_in_dreg_regoff			=	0x11;
						inline static constexpr u8 wimm_in_dreg_immoffb		=	0x12;
						inline static constexpr u8 bimm_in_dreg_immoffw		=	0x13;
						inline static constexpr u8 bimm_in_dreg_regoff			=	0x14;
						inline static constexpr u8 bimm_in_dreg_immoffb		=	0x15;
						inline static constexpr u8 wdreg_in_dreg_immoffw		=	0x16;
						inline static constexpr u8 wdreg_in_dreg_regoff		=	0x17;
						inline static constexpr u8 wdreg_in_dreg_immoffb		=	0x18;
						inline static constexpr u8 bdreg_in_dreg_immoffw		=	0x19;
						inline static constexpr u8 bdreg_in_dreg_regoff		=	0x1A;
						inline static constexpr u8 bdreg_in_dreg_immoffb		=	0x1B;

						inline static constexpr u8 wimm_in_mem_immoffw			=	0x30;
						inline static constexpr u8 wimm_in_mem_regoff			=	0x31;
						inline static constexpr u8 wimm_in_mem_immoffb			=	0x32;
						inline static constexpr u8 bimm_in_mem_immoffw			=	0x33;
						inline static constexpr u8 bimm_in_mem_regoff			=	0x34;
						inline static constexpr u8 bimm_in_mem_immoffb			=	0x35;

						inline static constexpr u8 wdreg_immoffw_in_dreg		=	0x40;
						inline static constexpr u8 wdreg_regoff_in_dreg		=	0x41;
						inline static constexpr u8 wdreg_immoffb_in_dreg		=	0x42;
						inline static constexpr u8 bdreg_immoffw_in_dreg		=	0x43;
						inline static constexpr u8 bdreg_regoff_in_dreg		=	0x44;
						inline static constexpr u8 bdreg_immoffb_in_dreg		=	0x45;

						inline static constexpr u8 wmem_immoffw_in_reg			=	0x50;
						inline static constexpr u8 wmem_regoff_in_reg			=	0x51;
						inline static constexpr u8 wmem_immoffb_in_reg			=	0x52;
						inline static constexpr u8 bmem_immoffw_in_reg			=	0x53;
						inline static constexpr u8 bmem_regoff_in_reg			=	0x54;
						inline static constexpr u8 bmem_immoffb_in_reg			=	0x55;
						inline static constexpr u8 wdreg_immoffw_in_reg		=	0x56;
						inline static constexpr u8 wdreg_regoff_in_reg			=	0x57;
						inline static constexpr u8 wdreg_immoffb_in_reg		=	0x58;
						inline static constexpr u8 bdreg_immoffw_in_reg		=	0x59;
						inline static constexpr u8 bdreg_regoff_in_reg			=	0x5A;
						inline static constexpr u8 bdreg_immoffb_in_reg		=	0x5B;
				};
				public:
					inline ExtMov(void) : data::CPUExtension(data::OpCodes::Ext01, "extmov") {  }
					String getOpCodeString(u8 opCode) override;
					u8 getInstructionSIze(u8 opCode) override;
					bool execute(VirtualCPU& vcpu) override;
			};

			class ExtAlu : public data::CPUExtension
			{
				public: class OpCodes
				{
					public:
						inline static constexpr u8 addipu_reg_in_reg		=	0x10;
						inline static constexpr u8 addipu_imm_in_reg		=	0x11;
						inline static constexpr u8 subipu_reg_in_reg		=	0x12;
						inline static constexpr u8 subipu_imm_in_reg		=	0x13;
						inline static constexpr u8 mulipu_reg_in_reg		=	0x14;
						inline static constexpr u8 mulipu_imm_in_reg		=	0x15;
						inline static constexpr u8 divipu_reg_in_reg		=	0x16;
						inline static constexpr u8 divipu_imm_in_reg		=	0x17;
						
						inline static constexpr u8 addip_reg_in_reg		=	0x20;
						inline static constexpr u8 addip_imm_in_reg		=	0x21;
						inline static constexpr u8 subip_reg_in_reg		=	0x22;
						inline static constexpr u8 subip_imm_in_reg		=	0x23;
						inline static constexpr u8 mulip_reg_in_reg		=	0x24;
						inline static constexpr u8 mulip_imm_in_reg		=	0x25;
						inline static constexpr u8 divip_reg_in_reg		=	0x26;
						inline static constexpr u8 divip_imm_in_reg		=	0x27;

						inline static constexpr u8 andip_reg_in_reg		=	0x30;
						inline static constexpr u8 andip_imm_in_reg		=	0x31;
						inline static constexpr u8 orip_reg_in_reg			=	0x32;
						inline static constexpr u8 orip_imm_in_reg			=	0x33;
						inline static constexpr u8 xorip_reg_in_reg		=	0x34;
						inline static constexpr u8 xorip_imm_in_reg		=	0x35;
						inline static constexpr u8 notip_reg				=	0x36;

				};
				public:
					inline ExtAlu(void) : data::CPUExtension(data::OpCodes::Ext02, "extalu") {  }
					String getOpCodeString(u8 opCode) override;
					u8 getInstructionSIze(u8 opCode) override;
					bool execute(VirtualCPU& vcpu) override;
			};
		}	

		
	}
}