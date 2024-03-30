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
						inline static constexpr uint8_t wimm_in_dreg_immoffw		=	0x10;
						inline static constexpr uint8_t wimm_in_dreg_regoff			=	0x11;
						inline static constexpr uint8_t wimm_in_dreg_immoffb		=	0x12;
						inline static constexpr uint8_t bimm_in_dreg_immoffw		=	0x13;
						inline static constexpr uint8_t bimm_in_dreg_regoff			=	0x14;
						inline static constexpr uint8_t bimm_in_dreg_immoffb		=	0x15;
						inline static constexpr uint8_t wdreg_in_dreg_immoffw		=	0x16;
						inline static constexpr uint8_t wdreg_in_dreg_regoff		=	0x17;
						inline static constexpr uint8_t wdreg_in_dreg_immoffb		=	0x18;
						inline static constexpr uint8_t bdreg_in_dreg_immoffw		=	0x19;
						inline static constexpr uint8_t bdreg_in_dreg_regoff		=	0x1A;
						inline static constexpr uint8_t bdreg_in_dreg_immoffb		=	0x1B;

						inline static constexpr uint8_t wimm_in_mem_immoffw			=	0x30;
						inline static constexpr uint8_t wimm_in_mem_regoff			=	0x31;
						inline static constexpr uint8_t wimm_in_mem_immoffb			=	0x32;
						inline static constexpr uint8_t bimm_in_mem_immoffw			=	0x33;
						inline static constexpr uint8_t bimm_in_mem_regoff			=	0x34;
						inline static constexpr uint8_t bimm_in_mem_immoffb			=	0x35;

						inline static constexpr uint8_t wdreg_immoffw_in_dreg		=	0x40;
						inline static constexpr uint8_t wdreg_regoff_in_dreg		=	0x41;
						inline static constexpr uint8_t wdreg_immoffb_in_dreg		=	0x42;
						inline static constexpr uint8_t bdreg_immoffw_in_dreg		=	0x43;
						inline static constexpr uint8_t bdreg_regoff_in_dreg		=	0x44;
						inline static constexpr uint8_t bdreg_immoffb_in_dreg		=	0x45;

						inline static constexpr uint8_t wmem_immoffw_in_reg			=	0x50;
						inline static constexpr uint8_t wmem_regoff_in_reg			=	0x51;
						inline static constexpr uint8_t wmem_immoffb_in_reg			=	0x52;
						inline static constexpr uint8_t bmem_immoffw_in_reg			=	0x53;
						inline static constexpr uint8_t bmem_regoff_in_reg			=	0x54;
						inline static constexpr uint8_t bmem_immoffb_in_reg			=	0x55;
						inline static constexpr uint8_t wdreg_immoffw_in_reg		=	0x56;
						inline static constexpr uint8_t wdreg_regoff_in_reg			=	0x57;
						inline static constexpr uint8_t wdreg_immoffb_in_reg		=	0x58;
						inline static constexpr uint8_t bdreg_immoffw_in_reg		=	0x59;
						inline static constexpr uint8_t bdreg_regoff_in_reg			=	0x5A;
						inline static constexpr uint8_t bdreg_immoffb_in_reg		=	0x5B;
				};
				public:
					inline ExtMov(void) : data::CPUExtension(data::OpCodes::Ext01, "extmov") {  }
					ostd::String getOpCodeString(uint8_t opCode) override;
					uint8_t getInstructionSIze(uint8_t opCode) override;
					bool execute(VirtualCPU& vcpu) override;
			};
		}
	}
}