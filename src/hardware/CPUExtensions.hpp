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

						// 16-bit (h) variants. Naming: w = 32-bit word, h = 16-bit, b = byte;
						// immoffw / immoffb: 32-bit / 8-bit immediate offset, regoff: offset in a register.
						inline static constexpr u8 himm_in_dreg_immoffw	=	0x1C;
						inline static constexpr u8 himm_in_dreg_regoff	=	0x1D;
						inline static constexpr u8 himm_in_dreg_immoffb	=	0x1E;
						inline static constexpr u8 hdreg_in_dreg_immoffw	=	0x1F;
						inline static constexpr u8 hdreg_in_dreg_regoff	=	0x20;
						inline static constexpr u8 hdreg_in_dreg_immoffb	=	0x21;
						inline static constexpr u8 himm_in_mem_immoffw	=	0x36;
						inline static constexpr u8 himm_in_mem_regoff	=	0x37;
						inline static constexpr u8 himm_in_mem_immoffb	=	0x38;
						inline static constexpr u8 hdreg_immoffw_in_dreg	=	0x46;
						inline static constexpr u8 hdreg_regoff_in_dreg	=	0x47;
						inline static constexpr u8 hdreg_immoffb_in_dreg	=	0x48;
						inline static constexpr u8 hmem_immoffw_in_reg	=	0x5C;
						inline static constexpr u8 hmem_regoff_in_reg	=	0x5D;
						inline static constexpr u8 hmem_immoffb_in_reg	=	0x5E;
						inline static constexpr u8 hdreg_immoffw_in_reg	=	0x5F;
						inline static constexpr u8 hdreg_regoff_in_reg	=	0x60;
						inline static constexpr u8 hdreg_immoffb_in_reg	=	0x61;
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

			// Floating point (f32, IEEE 754 single precision), on the general registers: a
			// register holds the float's 32 bits, so loads, stores, moves and calls are the
			// usual ones. Results go in the first register (in place), like extalu; the
			// comparisons write 1 / 0 in ACC (false when an operand is NaN). No traps: dividing
			// by zero gives an infinity. imm: the float's 32 bits.
			class ExtFpu : public data::CPUExtension
			{
				public: class OpCodes
				{
					public:
						// rd = rd op rs / imm
						inline static constexpr u8 fadd_reg_in_reg		=	0x10;
						inline static constexpr u8 fadd_imm_in_reg		=	0x11;
						inline static constexpr u8 fsub_reg_in_reg		=	0x12;
						inline static constexpr u8 fsub_imm_in_reg		=	0x13;
						inline static constexpr u8 fmul_reg_in_reg		=	0x14;
						inline static constexpr u8 fmul_imm_in_reg		=	0x15;
						inline static constexpr u8 fdiv_reg_in_reg		=	0x16;
						inline static constexpr u8 fdiv_imm_in_reg		=	0x17;
						inline static constexpr u8 fmin_reg_in_reg		=	0x18;
						inline static constexpr u8 fmin_imm_in_reg		=	0x19;
						inline static constexpr u8 fmax_reg_in_reg		=	0x1A;
						inline static constexpr u8 fmax_imm_in_reg		=	0x1B;
						inline static constexpr u8 fpow_reg_in_reg		=	0x1C;
						inline static constexpr u8 fpow_imm_in_reg		=	0x1D;
						inline static constexpr u8 fatan2_reg_in_reg	=	0x1E;	// rd = atan2(rd, rs)
						inline static constexpr u8 fatan2_imm_in_reg	=	0x1F;

						// rd = op(rd)
						inline static constexpr u8 fneg_reg				=	0x20;
						inline static constexpr u8 fabs_reg				=	0x21;
						inline static constexpr u8 fsqrt_reg			=	0x22;
						inline static constexpr u8 ffloor_reg			=	0x23;
						inline static constexpr u8 fceil_reg			=	0x24;
						inline static constexpr u8 fround_reg			=	0x25;	// halves away from zero
						inline static constexpr u8 ftrunc_reg			=	0x26;
						inline static constexpr u8 fsin_reg				=	0x27;
						inline static constexpr u8 fcos_reg				=	0x28;
						inline static constexpr u8 ftan_reg				=	0x29;
						inline static constexpr u8 fexp_reg				=	0x2A;
						inline static constexpr u8 flog_reg				=	0x2B;	// natural logarithm

						// Conversions, in place. ftoi / ftou truncate toward zero and saturate
						// (out of range: the nearest limit; NaN: 0).
						inline static constexpr u8 itof_reg				=	0x30;	// i32 -> f32
						inline static constexpr u8 utof_reg				=	0x31;	// u32 -> f32
						inline static constexpr u8 ftoi_reg				=	0x32;	// f32 -> i32
						inline static constexpr u8 ftou_reg				=	0x33;	// f32 -> u32

						// ACC = (ra op rb / imm) ? 1 : 0
						inline static constexpr u8 feq_reg_reg			=	0x40;
						inline static constexpr u8 feq_reg_imm			=	0x41;
						inline static constexpr u8 flt_reg_reg			=	0x42;
						inline static constexpr u8 flt_reg_imm			=	0x43;
						inline static constexpr u8 fle_reg_reg			=	0x44;
						inline static constexpr u8 fle_reg_imm			=	0x45;
				};
				public:
					inline ExtFpu(void) : data::CPUExtension(data::OpCodes::Ext03, "extfpu") {  }
					String getOpCodeString(u8 opCode) override;
					u8 getInstructionSIze(u8 opCode) override;
					bool execute(VirtualCPU& vcpu) override;
			};
		}	

		
	}
}