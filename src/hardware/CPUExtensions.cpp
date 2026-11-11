#include "CPUExtensions.hpp"
#include <cmath>
#include <bit>
#include <limits>
#include "VirtualCPU.hpp"
#include "../runtime/DragonRuntime.hpp"

namespace dragon
{
	namespace hw
	{
		namespace cpuext
		{
			String ExtMov::getOpCodeString(u8 opCode)
			{
				switch (opCode)
				{
					case OpCodes::wimm_in_dreg_immoffw:		return m_name + "_wimm_in_dreg_immoffw";
					case OpCodes::wimm_in_dreg_regoff:		return m_name + "_wimm_in_dreg_regoff";
					case OpCodes::wimm_in_dreg_immoffb:		return m_name + "_wimm_in_dreg_immoffb";
					case OpCodes::bimm_in_dreg_immoffw:		return m_name + "_bimm_in_dreg_immoffw";
					case OpCodes::bimm_in_dreg_regoff:		return m_name + "_bimm_in_dreg_regoff";
					case OpCodes::bimm_in_dreg_immoffb:		return m_name + "_bimm_in_dreg_immoffb";
					case OpCodes::wdreg_in_dreg_immoffw:	return m_name + "_wdreg_in_dreg_immoffw";
					case OpCodes::wdreg_in_dreg_regoff:		return m_name + "_wdreg_in_dreg_regoff";
					case OpCodes::wdreg_in_dreg_immoffb:	return m_name + "_wdreg_in_dreg_immoffb";
					case OpCodes::bdreg_in_dreg_immoffw:	return m_name + "_bdreg_in_dreg_immoffw";
					case OpCodes::bdreg_in_dreg_regoff:		return m_name + "_bdreg_in_dreg_regoff";
					case OpCodes::bdreg_in_dreg_immoffb:	return m_name + "_bdreg_in_dreg_immoffb";
					case OpCodes::wimm_in_mem_immoffw:		return m_name + "_wimm_in_mem_immoffw";
					case OpCodes::wimm_in_mem_regoff:		return m_name + "_wimm_in_mem_regoff";
					case OpCodes::wimm_in_mem_immoffb:		return m_name + "_wimm_in_mem_immoffb";
					case OpCodes::bimm_in_mem_immoffw:		return m_name + "_bimm_in_mem_immoffw";
					case OpCodes::bimm_in_mem_regoff:		return m_name + "_bimm_in_mem_regoff";
					case OpCodes::bimm_in_mem_immoffb:		return m_name + "_bimm_in_mem_immoffb";
					case OpCodes::wdreg_immoffw_in_dreg:	return m_name + "_wdreg_immoffw_in_dreg";
					case OpCodes::wdreg_regoff_in_dreg:		return m_name + "_wdreg_regoff_in_dreg";
					case OpCodes::wdreg_immoffb_in_dreg:	return m_name + "_wdreg_immoffb_in_dreg";
					case OpCodes::bdreg_immoffw_in_dreg:	return m_name + "_bdreg_immoffw_in_dreg";
					case OpCodes::bdreg_regoff_in_dreg:		return m_name + "_bdreg_regoff_in_dreg";
					case OpCodes::bdreg_immoffb_in_dreg:	return m_name + "_bdreg_immoffb_in_dreg";
					case OpCodes::wmem_immoffw_in_reg:		return m_name + "_wmem_immoffw_in_reg";
					case OpCodes::wmem_regoff_in_reg:		return m_name + "_wmem_regoff_in_reg";
					case OpCodes::wmem_immoffb_in_reg:		return m_name + "_wmem_immoffb_in_reg";
					case OpCodes::bmem_immoffw_in_reg:		return m_name + "_bmem_immoffw_in_reg";
					case OpCodes::bmem_regoff_in_reg:		return m_name + "_bmem_regoff_in_reg";
					case OpCodes::bmem_immoffb_in_reg:		return m_name + "_bmem_immoffb_in_reg";
					case OpCodes::wdreg_immoffw_in_reg:		return m_name + "_wdreg_immoffw_in_reg";
					case OpCodes::wdreg_regoff_in_reg:		return m_name + "_wdreg_regoff_in_reg";
					case OpCodes::wdreg_immoffb_in_reg:		return m_name + "_wdreg_immoffb_in_reg";
					case OpCodes::bdreg_immoffw_in_reg:		return m_name + "_bdreg_immoffw_in_reg";
					case OpCodes::bdreg_regoff_in_reg:		return m_name + "_bdreg_regoff_in_reg";
					case OpCodes::bdreg_immoffb_in_reg:		return m_name + "_bdreg_immoffb_in_reg";
					case OpCodes::himm_in_dreg_immoffw:	return m_name + "_himm_in_dreg_immoffw";
					case OpCodes::himm_in_dreg_regoff:	return m_name + "_himm_in_dreg_regoff";
					case OpCodes::himm_in_dreg_immoffb:	return m_name + "_himm_in_dreg_immoffb";
					case OpCodes::hdreg_in_dreg_immoffw:	return m_name + "_hdreg_in_dreg_immoffw";
					case OpCodes::hdreg_in_dreg_regoff:	return m_name + "_hdreg_in_dreg_regoff";
					case OpCodes::hdreg_in_dreg_immoffb:	return m_name + "_hdreg_in_dreg_immoffb";
					case OpCodes::himm_in_mem_immoffw:	return m_name + "_himm_in_mem_immoffw";
					case OpCodes::himm_in_mem_regoff:	return m_name + "_himm_in_mem_regoff";
					case OpCodes::himm_in_mem_immoffb:	return m_name + "_himm_in_mem_immoffb";
					case OpCodes::hdreg_immoffw_in_dreg:	return m_name + "_hdreg_immoffw_in_dreg";
					case OpCodes::hdreg_regoff_in_dreg:	return m_name + "_hdreg_regoff_in_dreg";
					case OpCodes::hdreg_immoffb_in_dreg:	return m_name + "_hdreg_immoffb_in_dreg";
					case OpCodes::hmem_immoffw_in_reg:	return m_name + "_hmem_immoffw_in_reg";
					case OpCodes::hmem_regoff_in_reg:	return m_name + "_hmem_regoff_in_reg";
					case OpCodes::hmem_immoffb_in_reg:	return m_name + "_hmem_immoffb_in_reg";
					case OpCodes::hdreg_immoffw_in_reg:	return m_name + "_hdreg_immoffw_in_reg";
					case OpCodes::hdreg_regoff_in_reg:	return m_name + "_hdreg_regoff_in_reg";
					case OpCodes::hdreg_immoffb_in_reg:	return m_name + "_hdreg_immoffb_in_reg";
					default: return m_name + "_UNKNOWN_INST";
				}
			}

			u8 ExtMov::getInstructionSIze(u8 opCode)
			{
				switch (opCode)
				{
					case OpCodes::wimm_in_dreg_immoffw:		return 10;
					case OpCodes::wimm_in_dreg_regoff:		return 7;
					case OpCodes::wimm_in_dreg_immoffb:		return 7;
					case OpCodes::bimm_in_dreg_immoffw:		return 7;
					case OpCodes::bimm_in_dreg_regoff:		return 4;
					case OpCodes::bimm_in_dreg_immoffb:		return 4;
					case OpCodes::wdreg_in_dreg_immoffw:	return 7;
					case OpCodes::wdreg_in_dreg_regoff:		return 4;
					case OpCodes::wdreg_in_dreg_immoffb:	return 4;
					case OpCodes::bdreg_in_dreg_immoffw:	return 7;
					case OpCodes::bdreg_in_dreg_regoff:		return 4;
					case OpCodes::bdreg_in_dreg_immoffb:	return 4;
					case OpCodes::wimm_in_mem_immoffw:		return 13;
					case OpCodes::wimm_in_mem_regoff:		return 10;
					case OpCodes::wimm_in_mem_immoffb:		return 10;
					case OpCodes::bimm_in_mem_immoffw:		return 10;
					case OpCodes::bimm_in_mem_regoff:		return 7;
					case OpCodes::bimm_in_mem_immoffb:		return 7;
					case OpCodes::wdreg_immoffw_in_dreg:	return 7;
					case OpCodes::wdreg_regoff_in_dreg:		return 4;
					case OpCodes::wdreg_immoffb_in_dreg:	return 4;
					case OpCodes::bdreg_immoffw_in_dreg:	return 7;
					case OpCodes::bdreg_regoff_in_dreg:		return 4;
					case OpCodes::bdreg_immoffb_in_dreg:	return 4;
					case OpCodes::wmem_immoffw_in_reg:		return 10;
					case OpCodes::wmem_regoff_in_reg:		return 7;
					case OpCodes::wmem_immoffb_in_reg:		return 7;
					case OpCodes::bmem_immoffw_in_reg:		return 10;
					case OpCodes::bmem_regoff_in_reg:		return 7;
					case OpCodes::bmem_immoffb_in_reg:		return 7;
					case OpCodes::wdreg_immoffw_in_reg:		return 7;
					case OpCodes::wdreg_regoff_in_reg:		return 4;
					case OpCodes::wdreg_immoffb_in_reg:		return 4;
					case OpCodes::bdreg_immoffw_in_reg:		return 7;
					case OpCodes::bdreg_regoff_in_reg:		return 4;
					case OpCodes::bdreg_immoffb_in_reg:		return 4;
					case OpCodes::himm_in_dreg_immoffw:	return 8;
					case OpCodes::himm_in_dreg_regoff:	return 5;
					case OpCodes::himm_in_dreg_immoffb:	return 5;
					case OpCodes::hdreg_in_dreg_immoffw:	return 7;
					case OpCodes::hdreg_in_dreg_regoff:	return 4;
					case OpCodes::hdreg_in_dreg_immoffb:	return 4;
					case OpCodes::himm_in_mem_immoffw:	return 11;
					case OpCodes::himm_in_mem_regoff:	return 8;
					case OpCodes::himm_in_mem_immoffb:	return 8;
					case OpCodes::hdreg_immoffw_in_dreg:	return 7;
					case OpCodes::hdreg_regoff_in_dreg:	return 4;
					case OpCodes::hdreg_immoffb_in_dreg:	return 4;
					case OpCodes::hmem_immoffw_in_reg:	return 10;
					case OpCodes::hmem_regoff_in_reg:	return 7;
					case OpCodes::hmem_immoffb_in_reg:	return 7;
					case OpCodes::hdreg_immoffw_in_reg:	return 7;
					case OpCodes::hdreg_regoff_in_reg:	return 4;
					case OpCodes::hdreg_immoffb_in_reg:	return 4;
					default: return 0;
				}
			}

			bool ExtMov::execute(VirtualCPU& vcpu)
			{
				auto& mem = DragonRuntime::memMap;
				u8 inst = vcpu.fetch8();
				switch (inst)
				{
					case OpCodes::wimm_in_dreg_immoffw:
					{
						u8 dest_dreg = vcpu.fetch8();
						i32 src_wimm = vcpu.fetch32();
						u32 offset = vcpu.fetch32();

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write32(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_dreg_regoff:
					{
						u8 dest_dreg = vcpu.fetch8();
						i32 src_wimm = vcpu.fetch32();
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write32(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_dreg_immoffb:
					{
						u8 dest_dreg = vcpu.fetch8();
						i32 src_wimm = vcpu.fetch32();
						u8 offset = vcpu.fetch8();

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write32(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::bimm_in_dreg_immoffw:
					{
						u8 dest_dreg = vcpu.fetch8();
						i8 src_bimm = vcpu.fetch8();
						u32 offset = vcpu.fetch32();

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_dreg_regoff:
					{
						u8 dest_dreg = vcpu.fetch8();
						i8 src_bimm = vcpu.fetch8();
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_dreg_immoffb:
					{
						u8 dest_dreg = vcpu.fetch8();
						i8 src_bimm = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::wdreg_in_dreg_immoffw:
					{
						u8 dest_dreg = vcpu.fetch8();
						i32 src_mem = vcpu.readRegister(vcpu.fetch8());
						u32 offset = vcpu.fetch32();
						
						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write32(dest_mem + offset, mem.read32(src_mem));
					}
					break;
					case OpCodes::wdreg_in_dreg_regoff:
					{
						u8 dest_dreg = vcpu.fetch8();
						i32 src_mem = vcpu.readRegister(vcpu.fetch8());
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write32(dest_mem + offset, mem.read32(src_mem));
					}
					break;
					case OpCodes::wdreg_in_dreg_immoffb:
					{
						u8 dest_dreg = vcpu.fetch8();
						i32 src_mem = vcpu.readRegister(vcpu.fetch8());
						u8 offset = vcpu.fetch8();

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write32(dest_mem + offset, mem.read32(src_mem));
					}
					break;
					case OpCodes::bdreg_in_dreg_immoffw:
					{
						u8 dest_dreg = vcpu.fetch8();
						i32 src_mem = vcpu.readRegister(vcpu.fetch8());
						u32 offset = vcpu.fetch32();

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, mem.read8(src_mem));
					}
					break;
					case OpCodes::bdreg_in_dreg_regoff:
					{
						u8 dest_dreg = vcpu.fetch8();
						i32 src_mem = vcpu.readRegister(vcpu.fetch8());
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, mem.read8(src_mem));
					}
					break;
					case OpCodes::bdreg_in_dreg_immoffb:
					{
						u8 dest_dreg = vcpu.fetch8();
						i32 src_mem = vcpu.readRegister(vcpu.fetch8());
						u8 offset = vcpu.fetch8();

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, mem.read8(src_mem));
					}
					break;
					case OpCodes::wimm_in_mem_immoffw:
					{
						u32 dest_mem = vcpu.fetch32();
						i32 src_wimm = vcpu.fetch32();
						u32 offset = vcpu.fetch32();

						mem.write32(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_mem_regoff:
					{
						u32 dest_mem = vcpu.fetch32();
						i32 src_wimm = vcpu.fetch32();
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						mem.write32(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_mem_immoffb:
					{
						u32 dest_mem = vcpu.fetch32();
						i32 src_wimm = vcpu.fetch32();
						u8 offset = vcpu.fetch8();

						mem.write32(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::bimm_in_mem_immoffw:
					{
						u32 dest_mem = vcpu.fetch32();
						i8 src_bimm = vcpu.fetch8();
						u32 offset = vcpu.fetch32();

						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_mem_regoff:
					{
						u32 dest_mem = vcpu.fetch32();
						i8 src_bimm = vcpu.fetch8();
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_mem_immoffb:
					{
						u32 dest_mem = vcpu.fetch32();
						i8 src_bimm = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::wdreg_immoffw_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = vcpu.fetch32();

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						u32 src_mem = vcpu.readRegister(src_dreg);
						mem.write32(dest_mem, mem.read32(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_regoff_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						u32 src_mem = vcpu.readRegister(src_dreg);
						mem.write32(dest_mem, mem.read32(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_immoffb_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						u32 src_mem = vcpu.readRegister(src_dreg);
						mem.write32(dest_mem, mem.read32(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffw_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = vcpu.fetch32();

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						u32 src_mem = vcpu.readRegister(src_dreg);
						mem.write8(dest_mem, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_regoff_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						u32 src_mem = vcpu.readRegister(src_dreg);
						mem.write8(dest_mem, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffb_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						u32 dest_mem = vcpu.readRegister(dest_dreg);
						u32 src_mem = vcpu.readRegister(src_dreg);
						mem.write8(dest_mem, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::wmem_immoffw_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_mem = vcpu.fetch32();
						u32 offset = vcpu.fetch32();

						vcpu.writeRegister32(dest_reg, mem.read32(src_mem + offset));
					}
					break;
					case OpCodes::wmem_regoff_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_mem = vcpu.fetch32();
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						vcpu.writeRegister32(dest_reg, mem.read32(src_mem + offset));
					}
					break;
					case OpCodes::wmem_immoffb_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_mem = vcpu.fetch32();
						u8 offset = vcpu.fetch8();
						vcpu.writeRegister32(dest_reg, mem.read32(src_mem + offset));
					}
					break;
					case OpCodes::bmem_immoffw_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_mem = vcpu.fetch32();
						u32 offset = vcpu.fetch32();

						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bmem_regoff_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_mem = vcpu.fetch32();
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bmem_immoffb_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_mem = vcpu.fetch32();
						u8 offset = vcpu.fetch8();

						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_immoffw_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = vcpu.fetch32();

						u32 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister32(dest_reg, mem.read32(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_regoff_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						u32 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister32(dest_reg, mem.read32(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_immoffb_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						u32 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister32(dest_reg, mem.read32(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffw_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = vcpu.fetch32();

						u32 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_regoff_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = vcpu.readRegister(vcpu.fetch8());

						u32 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffb_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						u32 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::himm_in_dreg_immoffw:
					{
						u8 dest_dreg = vcpu.fetch8();
						i16 src_himm = vcpu.fetch16();
						u32 offset = cast<u32>(vcpu.fetch32());
						u32 dest_mem = cast<u32>(vcpu.readRegister(dest_dreg));
						mem.write16(dest_mem + offset, src_himm);
					}
					break;
					case OpCodes::hdreg_in_dreg_immoffw:
					{
						u8 dest_dreg = vcpu.fetch8();
						u32 src_mem = cast<u32>(vcpu.readRegister(vcpu.fetch8()));
						u32 offset = cast<u32>(vcpu.fetch32());
						u32 dest_mem = cast<u32>(vcpu.readRegister(dest_dreg));
						mem.write16(dest_mem + offset, mem.read16(src_mem));
					}
					break;
					case OpCodes::himm_in_mem_immoffw:
					{
						u32 dest_mem = cast<u32>(vcpu.fetch32());
						i16 src_himm = vcpu.fetch16();
						u32 offset = cast<u32>(vcpu.fetch32());
						mem.write16(dest_mem + offset, src_himm);
					}
					break;
					case OpCodes::hdreg_immoffw_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = cast<u32>(vcpu.fetch32());
						u32 dest_mem = cast<u32>(vcpu.readRegister(dest_dreg));
						u32 src_mem = cast<u32>(vcpu.readRegister(src_dreg));
						mem.write16(dest_mem, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::hmem_immoffw_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_mem = cast<u32>(vcpu.fetch32());
						u32 offset = cast<u32>(vcpu.fetch32());
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::hdreg_immoffw_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = cast<u32>(vcpu.fetch32());
						u32 src_mem = cast<u32>(vcpu.readRegister(src_dreg));
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::himm_in_dreg_regoff:
					{
						u8 dest_dreg = vcpu.fetch8();
						i16 src_himm = vcpu.fetch16();
						u32 offset = cast<u32>(vcpu.readRegister(vcpu.fetch8()));
						u32 dest_mem = cast<u32>(vcpu.readRegister(dest_dreg));
						mem.write16(dest_mem + offset, src_himm);
					}
					break;
					case OpCodes::hdreg_in_dreg_regoff:
					{
						u8 dest_dreg = vcpu.fetch8();
						u32 src_mem = cast<u32>(vcpu.readRegister(vcpu.fetch8()));
						u32 offset = cast<u32>(vcpu.readRegister(vcpu.fetch8()));
						u32 dest_mem = cast<u32>(vcpu.readRegister(dest_dreg));
						mem.write16(dest_mem + offset, mem.read16(src_mem));
					}
					break;
					case OpCodes::himm_in_mem_regoff:
					{
						u32 dest_mem = cast<u32>(vcpu.fetch32());
						i16 src_himm = vcpu.fetch16();
						u32 offset = cast<u32>(vcpu.readRegister(vcpu.fetch8()));
						mem.write16(dest_mem + offset, src_himm);
					}
					break;
					case OpCodes::hdreg_regoff_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = cast<u32>(vcpu.readRegister(vcpu.fetch8()));
						u32 dest_mem = cast<u32>(vcpu.readRegister(dest_dreg));
						u32 src_mem = cast<u32>(vcpu.readRegister(src_dreg));
						mem.write16(dest_mem, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::hmem_regoff_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_mem = cast<u32>(vcpu.fetch32());
						u32 offset = cast<u32>(vcpu.readRegister(vcpu.fetch8()));
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::hdreg_regoff_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u32 offset = cast<u32>(vcpu.readRegister(vcpu.fetch8()));
						u32 src_mem = cast<u32>(vcpu.readRegister(src_dreg));
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::himm_in_dreg_immoffb:
					{
						u8 dest_dreg = vcpu.fetch8();
						i16 src_himm = vcpu.fetch16();
						u8 offset = cast<u8>(vcpu.fetch8());
						u32 dest_mem = cast<u32>(vcpu.readRegister(dest_dreg));
						mem.write16(dest_mem + offset, src_himm);
					}
					break;
					case OpCodes::hdreg_in_dreg_immoffb:
					{
						u8 dest_dreg = vcpu.fetch8();
						u32 src_mem = cast<u32>(vcpu.readRegister(vcpu.fetch8()));
						u8 offset = cast<u8>(vcpu.fetch8());
						u32 dest_mem = cast<u32>(vcpu.readRegister(dest_dreg));
						mem.write16(dest_mem + offset, mem.read16(src_mem));
					}
					break;
					case OpCodes::himm_in_mem_immoffb:
					{
						u32 dest_mem = cast<u32>(vcpu.fetch32());
						i16 src_himm = vcpu.fetch16();
						u8 offset = cast<u8>(vcpu.fetch8());
						mem.write16(dest_mem + offset, src_himm);
					}
					break;
					case OpCodes::hdreg_immoffb_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u8 offset = cast<u8>(vcpu.fetch8());
						u32 dest_mem = cast<u32>(vcpu.readRegister(dest_dreg));
						u32 src_mem = cast<u32>(vcpu.readRegister(src_dreg));
						mem.write16(dest_mem, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::hmem_immoffb_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_mem = cast<u32>(vcpu.fetch32());
						u8 offset = cast<u8>(vcpu.fetch8());
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::hdreg_immoffb_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u8 offset = cast<u8>(vcpu.fetch8());
						u32 src_mem = cast<u32>(vcpu.readRegister(src_dreg));
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
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




			String ExtAlu::getOpCodeString(u8 opCode)
			{
				switch (opCode)
				{
					case OpCodes::addipu_reg_in_reg:		return m_name + "_addipu_reg_in_reg";
					case OpCodes::addipu_imm_in_reg:		return m_name + "_addipu_imm_in_reg";
					case OpCodes::subipu_reg_in_reg:		return m_name + "_subipu_reg_in_reg";
					case OpCodes::subipu_imm_in_reg:		return m_name + "_subipu_imm_in_reg";
					case OpCodes::mulipu_reg_in_reg:		return m_name + "_mulipu_reg_in_reg";
					case OpCodes::mulipu_imm_in_reg:		return m_name + "_mulipu_imm_in_reg";
					case OpCodes::divipu_reg_in_reg:		return m_name + "_divipu_reg_in_reg";
					case OpCodes::divipu_imm_in_reg:		return m_name + "_divipu_imm_in_reg";
					case OpCodes::addip_reg_in_reg:			return m_name + "_addip_reg_in_reg";
					case OpCodes::addip_imm_in_reg:			return m_name + "_addip_imm_in_reg";
					case OpCodes::subip_reg_in_reg:			return m_name + "_subip_reg_in_reg";
					case OpCodes::subip_imm_in_reg:			return m_name + "_subip_imm_in_reg";
					case OpCodes::mulip_reg_in_reg:			return m_name + "_mulip_reg_in_reg";
					case OpCodes::mulip_imm_in_reg:			return m_name + "_mulip_imm_in_reg";
					case OpCodes::divip_reg_in_reg:			return m_name + "_divip_reg_in_reg";
					case OpCodes::divip_imm_in_reg:			return m_name + "_divip_imm_in_reg";
					case OpCodes::andip_reg_in_reg:			return m_name + "_andip_reg_in_reg";
					case OpCodes::andip_imm_in_reg:			return m_name + "_andip_imm_in_reg";
					case OpCodes::orip_reg_in_reg:			return m_name + "_orip_reg_in_reg";
					case OpCodes::orip_imm_in_reg:			return m_name + "_orip_imm_in_reg";
					case OpCodes::xorip_reg_in_reg:			return m_name + "_xorip_reg_in_reg";
					case OpCodes::xorip_imm_in_reg:			return m_name + "_xorip_imm_in_reg";
					case OpCodes::notip_reg:				return m_name + "_notip_reg";
					default: return m_name + "_UNKNOWN_INST";
				}
			}

			u8 ExtAlu::getInstructionSIze(u8 opCode)
			{
				switch (opCode)
				{
					case OpCodes::addipu_reg_in_reg:		return 3;
					case OpCodes::addipu_imm_in_reg:		return 6;
					case OpCodes::subipu_reg_in_reg:		return 3;
					case OpCodes::subipu_imm_in_reg:		return 6;
					case OpCodes::mulipu_reg_in_reg:		return 3;
					case OpCodes::mulipu_imm_in_reg:		return 6;
					case OpCodes::divipu_reg_in_reg:		return 3;
					case OpCodes::divipu_imm_in_reg:		return 6;
					case OpCodes::addip_reg_in_reg:			return 3;
					case OpCodes::addip_imm_in_reg:			return 6;
					case OpCodes::subip_reg_in_reg:			return 3;
					case OpCodes::subip_imm_in_reg:			return 6;
					case OpCodes::mulip_reg_in_reg:			return 3;
					case OpCodes::mulip_imm_in_reg:			return 6;
					case OpCodes::divip_reg_in_reg:			return 3;
					case OpCodes::divip_imm_in_reg:			return 6;
					case OpCodes::andip_reg_in_reg:			return 3;
					case OpCodes::andip_imm_in_reg:			return 6;
					case OpCodes::orip_reg_in_reg:			return 3;
					case OpCodes::orip_imm_in_reg:			return 6;
					case OpCodes::xorip_reg_in_reg:			return 3;
					case OpCodes::xorip_imm_in_reg:			return 6;
					case OpCodes::notip_reg:				return 2;
					default: return 0;
				}
			}

			bool ExtAlu::execute(VirtualCPU& vcpu)
			{
				auto& mem = DragonRuntime::memMap;
				u8 inst = vcpu.fetch8();
				switch (inst)
				{
					case OpCodes::addipu_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						u32 dest_val = vcpu.readRegister(dest_reg);
						u32 src_val = vcpu.readRegister(src_reg);
						u32 res = dest_val + src_val;
						vcpu.writeRegister32(dest_reg, (i32)res);
					}
					break;
					case OpCodes::addipu_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_val = vcpu.fetch32();
						u32 dest_val = vcpu.readRegister(dest_reg);
						u32 res = dest_val + src_val;
						vcpu.writeRegister32(dest_reg, (i32)res);
					}
					break;
					case OpCodes::subipu_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						u32 dest_val = vcpu.readRegister(dest_reg);
						u32 src_val = vcpu.readRegister(src_reg);
						u32 res = dest_val - src_val;
						vcpu.writeRegister32(dest_reg, (i32)res);
					}
					break;
					case OpCodes::subipu_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_val = vcpu.fetch32();
						u32 dest_val = vcpu.readRegister(dest_reg);
						u32 res = dest_val - src_val;
						vcpu.writeRegister32(dest_reg, (i32)res);
					}
					break;
					case OpCodes::mulipu_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						u32 dest_val = vcpu.readRegister(dest_reg);
						u32 src_val = vcpu.readRegister(src_reg);
						u32 res = dest_val * src_val;
						vcpu.writeRegister32(dest_reg, (i32)res);
					}
					break;
					case OpCodes::mulipu_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_val = vcpu.fetch32();
						u32 dest_val = vcpu.readRegister(dest_reg);
						u32 res = dest_val * src_val;
						vcpu.writeRegister32(dest_reg, (i32)res);
					}
					break;
					case OpCodes::divipu_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						u32 dest_val = vcpu.readRegister(dest_reg);
						u32 src_val = vcpu.readRegister(src_reg);
						if (!vcpu.check_divisor(cast<i32>(src_val))) return false;
						u32 res = dest_val / src_val;
						u32 rv = dest_val % src_val;
						vcpu.writeRegister32(dest_reg, (i32)res);
						vcpu.writeRegister32(data::Registers::RV, (i32)rv);
					}
					break;
					case OpCodes::divipu_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_val = vcpu.fetch32();
						u32 dest_val = vcpu.readRegister(dest_reg);
						if (!vcpu.check_divisor(cast<i32>(src_val))) return false;
						u32 res = dest_val / src_val;
						u32 rv = dest_val % src_val;
						vcpu.writeRegister32(dest_reg, (i32)res);
						vcpu.writeRegister32(data::Registers::RV, (i32)rv);
					}
					break;
					case OpCodes::addip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i32 dest_val = vcpu.readRegister(dest_reg);
						i32 src_val = vcpu.readRegister(src_reg);
						i32 res = cast<i32>(cast<u32>(dest_val) + cast<u32>(src_val));
						vcpu.writeRegister32(dest_reg, res);
					}
					break;
					case OpCodes::addip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_val = vcpu.fetch32();
						i32 dest_val = vcpu.readRegister(dest_reg);
						i32 res = cast<i32>(cast<u32>(dest_val) + cast<u32>(src_val));
						vcpu.writeRegister32(dest_reg, res);
					}
					break;
					case OpCodes::subip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i32 dest_val = vcpu.readRegister(dest_reg);
						i32 src_val = vcpu.readRegister(src_reg);
						i32 res = cast<i32>(cast<u32>(dest_val) - cast<u32>(src_val));
						vcpu.writeRegister32(dest_reg, res);
					}
					break;
					case OpCodes::subip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_val = vcpu.fetch32();
						i32 dest_val = vcpu.readRegister(dest_reg);
						i32 res = cast<i32>(cast<u32>(dest_val) - cast<u32>(src_val));
						vcpu.writeRegister32(dest_reg, res);
					}
					break;
					case OpCodes::mulip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i32 dest_val = vcpu.readRegister(dest_reg);
						i32 src_val = vcpu.readRegister(src_reg);
						i32 res = cast<i32>(cast<u32>(dest_val) * cast<u32>(src_val));
						vcpu.writeRegister32(dest_reg, res);
					}
					break;
					case OpCodes::mulip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_val = vcpu.fetch32();
						i32 dest_val = vcpu.readRegister(dest_reg);
						i32 res = cast<i32>(cast<u32>(dest_val) * cast<u32>(src_val));
						vcpu.writeRegister32(dest_reg, res);
					}
					break;
					case OpCodes::divip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i32 dest_val = vcpu.readRegister(dest_reg);
						i32 src_val = vcpu.readRegister(src_reg);
						if (!vcpu.check_divisor(cast<i32>(src_val))) return false;
						i32 res = cast<i32>(cast<i64>(dest_val) / cast<i32>(src_val));
						i32 rv = cast<i32>(cast<i64>(dest_val) % cast<i32>(src_val));
						vcpu.writeRegister32(dest_reg, res);
						vcpu.writeRegister32(data::Registers::RV, rv);
					}
					break;
					case OpCodes::divip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_val = vcpu.fetch32();
						i32 dest_val = vcpu.readRegister(dest_reg);
						if (!vcpu.check_divisor(cast<i32>(src_val))) return false;
						i32 res = cast<i32>(cast<i64>(dest_val) / cast<i32>(src_val));
						i32 rv = cast<i32>(cast<i64>(dest_val) % cast<i32>(src_val));
						vcpu.writeRegister32(dest_reg, res);
						vcpu.writeRegister32(data::Registers::RV, rv);
					}
					break;
					case OpCodes::andip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i32 src_val = vcpu.readRegister(src_reg);
						i32 dest_val = vcpu.readRegister(dest_reg);
						vcpu.writeRegister32(dest_reg, src_val & dest_val);
					}
					break;
					case OpCodes::andip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_val = vcpu.fetch32();
						i32 value = vcpu.readRegister(dest_reg);
						vcpu.writeRegister32(dest_reg, value & src_val);
					}
					break;
					case OpCodes::orip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i32 src_val = vcpu.readRegister(src_reg);
						i32 dest_val = vcpu.readRegister(dest_reg);
						vcpu.writeRegister32(dest_reg, src_val | dest_val);
					}
					break;
					case OpCodes::orip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_val = vcpu.fetch32();
						i32 value = vcpu.readRegister(dest_reg);
						vcpu.writeRegister32(dest_reg, value | src_val);
					}
					break;
					case OpCodes::xorip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i32 src_val = vcpu.readRegister(src_reg);
						i32 dest_val = vcpu.readRegister(dest_reg);
						vcpu.writeRegister32(dest_reg, src_val ^ dest_val);
					}
					break;
					case OpCodes::xorip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u32 src_val = vcpu.fetch32();
						i32 value = vcpu.readRegister(dest_reg);
						vcpu.writeRegister32(dest_reg, value ^ src_val);
					}
					break;
					case OpCodes::notip_reg:
					{
						u8 regAddr = vcpu.fetch8();
						i32 value = vcpu.readRegister(regAddr);
						vcpu.writeRegister32(regAddr, ~value);
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

			// f32 -> i32 / u32: truncated toward zero, saturated, NaN -> 0 (no undefined behavior).
			static i32 toI32(f32 v)
			{
				if (std::isnan(v)) return 0;
				if (v >= 2147483648.0f) return std::numeric_limits<i32>::max();
				if (v <= -2147483648.0f) return std::numeric_limits<i32>::min();
				return cast<i32>(v);
			}
			static u32 toU32(f32 v)
			{
				if (std::isnan(v) || v <= 0.0f) return 0;
				if (v >= 4294967296.0f) return std::numeric_limits<u32>::max();
				return cast<u32>(v);
			}

			String ExtFpu::getOpCodeString(u8 opCode)
			{
				switch (opCode)
				{
					case OpCodes::fadd_reg_in_reg:	return m_name + "_fadd_reg_in_reg";
					case OpCodes::fadd_imm_in_reg:	return m_name + "_fadd_imm_in_reg";
					case OpCodes::fsub_reg_in_reg:	return m_name + "_fsub_reg_in_reg";
					case OpCodes::fsub_imm_in_reg:	return m_name + "_fsub_imm_in_reg";
					case OpCodes::fmul_reg_in_reg:	return m_name + "_fmul_reg_in_reg";
					case OpCodes::fmul_imm_in_reg:	return m_name + "_fmul_imm_in_reg";
					case OpCodes::fdiv_reg_in_reg:	return m_name + "_fdiv_reg_in_reg";
					case OpCodes::fdiv_imm_in_reg:	return m_name + "_fdiv_imm_in_reg";
					case OpCodes::fmin_reg_in_reg:	return m_name + "_fmin_reg_in_reg";
					case OpCodes::fmin_imm_in_reg:	return m_name + "_fmin_imm_in_reg";
					case OpCodes::fmax_reg_in_reg:	return m_name + "_fmax_reg_in_reg";
					case OpCodes::fmax_imm_in_reg:	return m_name + "_fmax_imm_in_reg";
					case OpCodes::fpow_reg_in_reg:	return m_name + "_fpow_reg_in_reg";
					case OpCodes::fpow_imm_in_reg:	return m_name + "_fpow_imm_in_reg";
					case OpCodes::fatan2_reg_in_reg:	return m_name + "_fatan2_reg_in_reg";
					case OpCodes::fatan2_imm_in_reg:	return m_name + "_fatan2_imm_in_reg";
					case OpCodes::fneg_reg:	return m_name + "_fneg_reg";
					case OpCodes::fabs_reg:	return m_name + "_fabs_reg";
					case OpCodes::fsqrt_reg:	return m_name + "_fsqrt_reg";
					case OpCodes::ffloor_reg:	return m_name + "_ffloor_reg";
					case OpCodes::fceil_reg:	return m_name + "_fceil_reg";
					case OpCodes::fround_reg:	return m_name + "_fround_reg";
					case OpCodes::ftrunc_reg:	return m_name + "_ftrunc_reg";
					case OpCodes::fsin_reg:	return m_name + "_fsin_reg";
					case OpCodes::fcos_reg:	return m_name + "_fcos_reg";
					case OpCodes::ftan_reg:	return m_name + "_ftan_reg";
					case OpCodes::fexp_reg:	return m_name + "_fexp_reg";
					case OpCodes::flog_reg:	return m_name + "_flog_reg";
					case OpCodes::itof_reg:	return m_name + "_itof_reg";
					case OpCodes::utof_reg:	return m_name + "_utof_reg";
					case OpCodes::ftoi_reg:	return m_name + "_ftoi_reg";
					case OpCodes::ftou_reg:	return m_name + "_ftou_reg";
					case OpCodes::feq_reg_reg:	return m_name + "_feq_reg_reg";
					case OpCodes::feq_reg_imm:	return m_name + "_feq_reg_imm";
					case OpCodes::flt_reg_reg:	return m_name + "_flt_reg_reg";
					case OpCodes::flt_reg_imm:	return m_name + "_flt_reg_imm";
					case OpCodes::fle_reg_reg:	return m_name + "_fle_reg_reg";
					case OpCodes::fle_reg_imm:	return m_name + "_fle_reg_imm";
					default: return m_name + "_UNKNOWN_INST";
				}
			}

			u8 ExtFpu::getInstructionSIze(u8 opCode)
			{
				switch (opCode)
				{
					case OpCodes::fadd_reg_in_reg:	return 3;
					case OpCodes::fadd_imm_in_reg:	return 6;
					case OpCodes::fsub_reg_in_reg:	return 3;
					case OpCodes::fsub_imm_in_reg:	return 6;
					case OpCodes::fmul_reg_in_reg:	return 3;
					case OpCodes::fmul_imm_in_reg:	return 6;
					case OpCodes::fdiv_reg_in_reg:	return 3;
					case OpCodes::fdiv_imm_in_reg:	return 6;
					case OpCodes::fmin_reg_in_reg:	return 3;
					case OpCodes::fmin_imm_in_reg:	return 6;
					case OpCodes::fmax_reg_in_reg:	return 3;
					case OpCodes::fmax_imm_in_reg:	return 6;
					case OpCodes::fpow_reg_in_reg:	return 3;
					case OpCodes::fpow_imm_in_reg:	return 6;
					case OpCodes::fatan2_reg_in_reg:	return 3;
					case OpCodes::fatan2_imm_in_reg:	return 6;
					case OpCodes::fneg_reg:	return 2;
					case OpCodes::fabs_reg:	return 2;
					case OpCodes::fsqrt_reg:	return 2;
					case OpCodes::ffloor_reg:	return 2;
					case OpCodes::fceil_reg:	return 2;
					case OpCodes::fround_reg:	return 2;
					case OpCodes::ftrunc_reg:	return 2;
					case OpCodes::fsin_reg:	return 2;
					case OpCodes::fcos_reg:	return 2;
					case OpCodes::ftan_reg:	return 2;
					case OpCodes::fexp_reg:	return 2;
					case OpCodes::flog_reg:	return 2;
					case OpCodes::itof_reg:	return 2;
					case OpCodes::utof_reg:	return 2;
					case OpCodes::ftoi_reg:	return 2;
					case OpCodes::ftou_reg:	return 2;
					case OpCodes::feq_reg_reg:	return 3;
					case OpCodes::feq_reg_imm:	return 6;
					case OpCodes::flt_reg_reg:	return 3;
					case OpCodes::flt_reg_imm:	return 6;
					case OpCodes::fle_reg_reg:	return 3;
					case OpCodes::fle_reg_imm:	return 6;
					default: return 0;
				}
			}

			bool ExtFpu::execute(VirtualCPU& vcpu)
			{
				auto l_float = [](u32 bits) -> f32 { return std::bit_cast<f32>(bits); };
				auto l_bits = [](f32 value) -> i32 { return std::bit_cast<i32>(value); };
				const u8 inst = vcpu.fetch8();
				switch (inst)
				{
					case OpCodes::fadd_reg_in_reg:
					case OpCodes::fadd_imm_in_reg:
					{
						const u8 dest_reg = vcpu.fetch8();
						const f32 b = (inst == OpCodes::fadd_imm_in_reg ? l_float(cast<u32>(vcpu.fetch32())) : l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8()))));
						const f32 a = l_float(cast<u32>(vcpu.readRegister(dest_reg)));
						vcpu.writeRegister32(dest_reg, l_bits(a + b));
					}
					break;
					case OpCodes::fsub_reg_in_reg:
					case OpCodes::fsub_imm_in_reg:
					{
						const u8 dest_reg = vcpu.fetch8();
						const f32 b = (inst == OpCodes::fsub_imm_in_reg ? l_float(cast<u32>(vcpu.fetch32())) : l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8()))));
						const f32 a = l_float(cast<u32>(vcpu.readRegister(dest_reg)));
						vcpu.writeRegister32(dest_reg, l_bits(a - b));
					}
					break;
					case OpCodes::fmul_reg_in_reg:
					case OpCodes::fmul_imm_in_reg:
					{
						const u8 dest_reg = vcpu.fetch8();
						const f32 b = (inst == OpCodes::fmul_imm_in_reg ? l_float(cast<u32>(vcpu.fetch32())) : l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8()))));
						const f32 a = l_float(cast<u32>(vcpu.readRegister(dest_reg)));
						vcpu.writeRegister32(dest_reg, l_bits(a * b));
					}
					break;
					case OpCodes::fdiv_reg_in_reg:
					case OpCodes::fdiv_imm_in_reg:
					{
						const u8 dest_reg = vcpu.fetch8();
						const f32 b = (inst == OpCodes::fdiv_imm_in_reg ? l_float(cast<u32>(vcpu.fetch32())) : l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8()))));
						const f32 a = l_float(cast<u32>(vcpu.readRegister(dest_reg)));
						vcpu.writeRegister32(dest_reg, l_bits(a / b));
					}
					break;
					case OpCodes::fmin_reg_in_reg:
					case OpCodes::fmin_imm_in_reg:
					{
						const u8 dest_reg = vcpu.fetch8();
						const f32 b = (inst == OpCodes::fmin_imm_in_reg ? l_float(cast<u32>(vcpu.fetch32())) : l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8()))));
						const f32 a = l_float(cast<u32>(vcpu.readRegister(dest_reg)));
						vcpu.writeRegister32(dest_reg, l_bits(std::fmin(a, b)));
					}
					break;
					case OpCodes::fmax_reg_in_reg:
					case OpCodes::fmax_imm_in_reg:
					{
						const u8 dest_reg = vcpu.fetch8();
						const f32 b = (inst == OpCodes::fmax_imm_in_reg ? l_float(cast<u32>(vcpu.fetch32())) : l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8()))));
						const f32 a = l_float(cast<u32>(vcpu.readRegister(dest_reg)));
						vcpu.writeRegister32(dest_reg, l_bits(std::fmax(a, b)));
					}
					break;
					case OpCodes::fpow_reg_in_reg:
					case OpCodes::fpow_imm_in_reg:
					{
						const u8 dest_reg = vcpu.fetch8();
						const f32 b = (inst == OpCodes::fpow_imm_in_reg ? l_float(cast<u32>(vcpu.fetch32())) : l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8()))));
						const f32 a = l_float(cast<u32>(vcpu.readRegister(dest_reg)));
						vcpu.writeRegister32(dest_reg, l_bits(std::pow(a, b)));
					}
					break;
					case OpCodes::fatan2_reg_in_reg:
					case OpCodes::fatan2_imm_in_reg:
					{
						const u8 dest_reg = vcpu.fetch8();
						const f32 b = (inst == OpCodes::fatan2_imm_in_reg ? l_float(cast<u32>(vcpu.fetch32())) : l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8()))));
						const f32 a = l_float(cast<u32>(vcpu.readRegister(dest_reg)));
						vcpu.writeRegister32(dest_reg, l_bits(std::atan2(a, b)));
					}
					break;
					case OpCodes::fneg_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(-a));
					}
					break;
					case OpCodes::fabs_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(std::fabs(a)));
					}
					break;
					case OpCodes::fsqrt_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(std::sqrt(a)));
					}
					break;
					case OpCodes::ffloor_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(std::floor(a)));
					}
					break;
					case OpCodes::fceil_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(std::ceil(a)));
					}
					break;
					case OpCodes::fround_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(std::round(a)));
					}
					break;
					case OpCodes::ftrunc_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(std::trunc(a)));
					}
					break;
					case OpCodes::fsin_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(std::sin(a)));
					}
					break;
					case OpCodes::fcos_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(std::cos(a)));
					}
					break;
					case OpCodes::ftan_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(std::tan(a)));
					}
					break;
					case OpCodes::fexp_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(std::exp(a)));
					}
					break;
					case OpCodes::flog_reg:
					{
						const u8 reg = vcpu.fetch8();
						const f32 a = l_float(cast<u32>(vcpu.readRegister(reg)));
						vcpu.writeRegister32(reg, l_bits(std::log(a)));
					}
					break;
					case OpCodes::itof_reg:
					{
						const u8 reg = vcpu.fetch8();
						vcpu.writeRegister32(reg, l_bits(cast<f32>(vcpu.readRegister(reg))));
					}
					break;
					case OpCodes::utof_reg:
					{
						const u8 reg = vcpu.fetch8();
						vcpu.writeRegister32(reg, l_bits(cast<f32>(cast<u32>(vcpu.readRegister(reg)))));
					}
					break;
					case OpCodes::ftoi_reg:
					{
						const u8 reg = vcpu.fetch8();
						vcpu.writeRegister32(reg, toI32(l_float(cast<u32>(vcpu.readRegister(reg)))));
					}
					break;
					case OpCodes::ftou_reg:
					{
						const u8 reg = vcpu.fetch8();
						vcpu.writeRegister32(reg, cast<i32>(toU32(l_float(cast<u32>(vcpu.readRegister(reg))))));
					}
					break;
					case OpCodes::feq_reg_reg:
					case OpCodes::feq_reg_imm:
					{
						const f32 a = l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8())));
						const f32 b = (inst == OpCodes::feq_reg_imm ? l_float(cast<u32>(vcpu.fetch32())) : l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8()))));
						vcpu.writeRegister32(data::Registers::ACC, (a == b) ? 1 : 0);
					}
					break;
					case OpCodes::flt_reg_reg:
					case OpCodes::flt_reg_imm:
					{
						const f32 a = l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8())));
						const f32 b = (inst == OpCodes::flt_reg_imm ? l_float(cast<u32>(vcpu.fetch32())) : l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8()))));
						vcpu.writeRegister32(data::Registers::ACC, (a < b) ? 1 : 0);
					}
					break;
					case OpCodes::fle_reg_reg:
					case OpCodes::fle_reg_imm:
					{
						const f32 a = l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8())));
						const f32 b = (inst == OpCodes::fle_reg_imm ? l_float(cast<u32>(vcpu.fetch32())) : l_float(cast<u32>(vcpu.readRegister(vcpu.fetch8()))));
						vcpu.writeRegister32(data::Registers::ACC, (a <= b) ? 1 : 0);
					}
					break;
					default:
					{
						data::ErrorHandler::pushError(data::ErrorCodes::CPU_UnknownInstruction, String("Unknown extfpu instruction: ").add(String::getHexStr(inst, true, 1)));
						return false;
					}
				}
				return true;
			}
		}
	}
}