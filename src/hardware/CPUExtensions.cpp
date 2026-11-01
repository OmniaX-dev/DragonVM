#include "CPUExtensions.hpp"
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

		}
	}
}