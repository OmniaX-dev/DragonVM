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
					default: return m_name + "_UNKNOWN_INST";
				}
			}

			u8 ExtMov::getInstructionSIze(u8 opCode)
			{
				switch (opCode)
				{
					case OpCodes::wimm_in_dreg_immoffw:		return 6;
					case OpCodes::wimm_in_dreg_regoff:		return 5;
					case OpCodes::wimm_in_dreg_immoffb:		return 5;
					case OpCodes::bimm_in_dreg_immoffw:		return 5;
					case OpCodes::bimm_in_dreg_regoff:		return 4;
					case OpCodes::bimm_in_dreg_immoffb:		return 4;
					case OpCodes::wdreg_in_dreg_immoffw:	return 5;
					case OpCodes::wdreg_in_dreg_regoff:		return 4;
					case OpCodes::wdreg_in_dreg_immoffb:	return 4;
					case OpCodes::bdreg_in_dreg_immoffw:	return 5;
					case OpCodes::bdreg_in_dreg_regoff:		return 4;
					case OpCodes::bdreg_in_dreg_immoffb:	return 4;
					case OpCodes::wimm_in_mem_immoffw:		return 7;
					case OpCodes::wimm_in_mem_regoff:		return 6;
					case OpCodes::wimm_in_mem_immoffb:		return 6;
					case OpCodes::bimm_in_mem_immoffw:		return 6;
					case OpCodes::bimm_in_mem_regoff:		return 5;
					case OpCodes::bimm_in_mem_immoffb:		return 5;
					case OpCodes::wdreg_immoffw_in_dreg:	return 5;
					case OpCodes::wdreg_regoff_in_dreg:		return 4;
					case OpCodes::wdreg_immoffb_in_dreg:	return 4;
					case OpCodes::bdreg_immoffw_in_dreg:	return 5;
					case OpCodes::bdreg_regoff_in_dreg:		return 4;
					case OpCodes::bdreg_immoffb_in_dreg:	return 4;
					case OpCodes::wmem_immoffw_in_reg:		return 6;
					case OpCodes::wmem_regoff_in_reg:		return 5;
					case OpCodes::wmem_immoffb_in_reg:		return 5;
					case OpCodes::bmem_immoffw_in_reg:		return 6;
					case OpCodes::bmem_regoff_in_reg:		return 5;
					case OpCodes::bmem_immoffb_in_reg:		return 5;
					case OpCodes::wdreg_immoffw_in_reg:		return 5;
					case OpCodes::wdreg_regoff_in_reg:		return 4;
					case OpCodes::wdreg_immoffb_in_reg:		return 4;
					case OpCodes::bdreg_immoffw_in_reg:		return 5;
					case OpCodes::bdreg_regoff_in_reg:		return 4;
					case OpCodes::bdreg_immoffb_in_reg:		return 4;
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
						i16 src_wimm = vcpu.fetch16();
						u16 offset = vcpu.fetch16();

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_dreg_regoff:
					{
						u8 dest_dreg = vcpu.fetch8();
						i16 src_wimm = vcpu.fetch16();
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_dreg_immoffb:
					{
						u8 dest_dreg = vcpu.fetch8();
						i16 src_wimm = vcpu.fetch16();
						u8 offset = vcpu.fetch8();

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::bimm_in_dreg_immoffw:
					{
						u8 dest_dreg = vcpu.fetch8();
						i8 src_bimm = vcpu.fetch8();
						u16 offset = vcpu.fetch16();

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_dreg_regoff:
					{
						u8 dest_dreg = vcpu.fetch8();
						i8 src_bimm = vcpu.fetch8();
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_dreg_immoffb:
					{
						u8 dest_dreg = vcpu.fetch8();
						i8 src_bimm = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::wdreg_in_dreg_immoffw:
					{
						u8 dest_dreg = vcpu.fetch8();
						i16 src_mem = vcpu.readRegister(vcpu.fetch8());
						u16 offset = vcpu.fetch16();
						
						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, mem.read16(src_mem));
					}
					break;
					case OpCodes::wdreg_in_dreg_regoff:
					{
						u8 dest_dreg = vcpu.fetch8();
						i16 src_mem = vcpu.readRegister(vcpu.fetch8());
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, mem.read16(src_mem));
					}
					break;
					case OpCodes::wdreg_in_dreg_immoffb:
					{
						u8 dest_dreg = vcpu.fetch8();
						i16 src_mem = vcpu.readRegister(vcpu.fetch8());
						u8 offset = vcpu.fetch8();

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, mem.read16(src_mem));
					}
					break;
					case OpCodes::bdreg_in_dreg_immoffw:
					{
						u8 dest_dreg = vcpu.fetch8();
						i16 src_mem = vcpu.readRegister(vcpu.fetch8());
						u16 offset = vcpu.fetch16();

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, mem.read8(src_mem));
					}
					break;
					case OpCodes::bdreg_in_dreg_regoff:
					{
						u8 dest_dreg = vcpu.fetch8();
						i16 src_mem = vcpu.readRegister(vcpu.fetch8());
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, mem.read8(src_mem));
					}
					break;
					case OpCodes::bdreg_in_dreg_immoffb:
					{
						u8 dest_dreg = vcpu.fetch8();
						i16 src_mem = vcpu.readRegister(vcpu.fetch8());
						u8 offset = vcpu.fetch8();

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, mem.read8(src_mem));
					}
					break;
					case OpCodes::wimm_in_mem_immoffw:
					{
						u16 dest_mem = vcpu.fetch16();
						i16 src_wimm = vcpu.fetch16();
						u16 offset = vcpu.fetch16();

						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_mem_regoff:
					{
						u16 dest_mem = vcpu.fetch16();
						i16 src_wimm = vcpu.fetch16();
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_mem_immoffb:
					{
						u16 dest_mem = vcpu.fetch16();
						i16 src_wimm = vcpu.fetch16();
						u8 offset = vcpu.fetch8();

						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::bimm_in_mem_immoffw:
					{
						u16 dest_mem = vcpu.fetch16();
						i8 src_bimm = vcpu.fetch8();
						u16 offset = vcpu.fetch16();

						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_mem_regoff:
					{
						u16 dest_mem = vcpu.fetch16();
						i8 src_bimm = vcpu.fetch8();
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_mem_immoffb:
					{
						u16 dest_mem = vcpu.fetch16();
						i8 src_bimm = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::wdreg_immoffw_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u16 offset = vcpu.fetch16();

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						u16 src_mem = vcpu.readRegister(src_dreg);
						mem.write16(dest_mem, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_regoff_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						u16 src_mem = vcpu.readRegister(src_dreg);
						mem.write16(dest_mem, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_immoffb_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						u16 src_mem = vcpu.readRegister(src_dreg);
						mem.write16(dest_mem, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffw_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u16 offset = vcpu.fetch16();

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						u16 src_mem = vcpu.readRegister(src_dreg);
						mem.write8(dest_mem, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_regoff_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						u16 src_mem = vcpu.readRegister(src_dreg);
						mem.write8(dest_mem, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffb_in_dreg:
					{
						u8 dest_dreg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						u16 dest_mem = vcpu.readRegister(dest_dreg);
						u16 src_mem = vcpu.readRegister(src_dreg);
						mem.write8(dest_mem, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::wmem_immoffw_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_mem = vcpu.fetch16();
						u16 offset = vcpu.fetch16();

						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wmem_regoff_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_mem = vcpu.fetch16();
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wmem_immoffb_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_mem = vcpu.fetch16();
						u8 offset = vcpu.fetch8();
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::bmem_immoffw_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_mem = vcpu.fetch16();
						u16 offset = vcpu.fetch16();

						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bmem_regoff_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_mem = vcpu.fetch16();
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bmem_immoffb_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_mem = vcpu.fetch16();
						u8 offset = vcpu.fetch8();

						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_immoffw_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u16 offset = vcpu.fetch16();

						u16 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_regoff_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						u16 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_immoffb_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						u16 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffw_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u16 offset = vcpu.fetch16();

						u16 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_regoff_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u16 offset = vcpu.readRegister(vcpu.fetch8());

						u16 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffb_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_dreg = vcpu.fetch8();
						u8 offset = vcpu.fetch8();

						u16 src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
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
					default: return m_name + "_UNKNOWN_INST";
				}
			}

			u8 ExtAlu::getInstructionSIze(u8 opCode)
			{
				switch (opCode)
				{
					case OpCodes::addipu_reg_in_reg:		return 3;
					case OpCodes::addipu_imm_in_reg:		return 4;
					case OpCodes::subipu_reg_in_reg:		return 3;
					case OpCodes::subipu_imm_in_reg:		return 4;
					case OpCodes::mulipu_reg_in_reg:		return 3;
					case OpCodes::mulipu_imm_in_reg:		return 4;
					case OpCodes::divipu_reg_in_reg:		return 3;
					case OpCodes::divipu_imm_in_reg:		return 4;
					case OpCodes::addip_reg_in_reg:			return 3;
					case OpCodes::addip_imm_in_reg:			return 4;
					case OpCodes::subip_reg_in_reg:			return 3;
					case OpCodes::subip_imm_in_reg:			return 4;
					case OpCodes::mulip_reg_in_reg:			return 3;
					case OpCodes::mulip_imm_in_reg:			return 4;
					case OpCodes::divip_reg_in_reg:			return 3;
					case OpCodes::divip_imm_in_reg:			return 4;
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
						u16 dest_val = vcpu.readRegister(dest_reg);
						u16 src_val = vcpu.readRegister(src_reg);
						u16 res = dest_val + src_val;
						vcpu.writeRegister16(dest_reg, (i16)res);
					}
					break;
					case OpCodes::addipu_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_val = vcpu.fetch16();
						u16 dest_val = vcpu.readRegister(dest_reg);
						u16 res = dest_val + src_val;
						vcpu.writeRegister16(dest_reg, (i16)res);
					}
					break;
					case OpCodes::subipu_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						u16 dest_val = vcpu.readRegister(dest_reg);
						u16 src_val = vcpu.readRegister(src_reg);
						u16 res = dest_val - src_val;
						vcpu.writeRegister16(dest_reg, (i16)res);
					}
					break;
					case OpCodes::subipu_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_val = vcpu.fetch16();
						u16 dest_val = vcpu.readRegister(dest_reg);
						u16 res = dest_val - src_val;
						vcpu.writeRegister16(dest_reg, (i16)res);
					}
					break;
					case OpCodes::mulipu_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						u16 dest_val = vcpu.readRegister(dest_reg);
						u16 src_val = vcpu.readRegister(src_reg);
						u16 res = dest_val * src_val;
						vcpu.writeRegister16(dest_reg, (i16)res);
					}
					break;
					case OpCodes::mulipu_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_val = vcpu.fetch16();
						u16 dest_val = vcpu.readRegister(dest_reg);
						u16 res = dest_val * src_val;
						vcpu.writeRegister16(dest_reg, (i16)res);
					}
					break;
					case OpCodes::divipu_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						u16 dest_val = vcpu.readRegister(dest_reg);
						u16 src_val = vcpu.readRegister(src_reg);
						u16 res = dest_val / src_val;
						u16 rv = dest_val % src_val;
						vcpu.writeRegister16(dest_reg, (i16)res);
						vcpu.writeRegister16(data::Registers::RV, (i16)rv);
					}
					break;
					case OpCodes::divipu_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_val = vcpu.fetch16();
						u16 dest_val = vcpu.readRegister(dest_reg);
						u16 res = dest_val / src_val;
						u16 rv = dest_val % src_val;
						vcpu.writeRegister16(dest_reg, (i16)res);
						vcpu.writeRegister16(data::Registers::RV, (i16)rv);
					}
					break;
					case OpCodes::addip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i16 dest_val = vcpu.readRegister(dest_reg);
						i16 src_val = vcpu.readRegister(src_reg);
						i16 res = dest_val + src_val;
						vcpu.writeRegister16(dest_reg, res);
					}
					break;
					case OpCodes::addip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_val = vcpu.fetch16();
						i16 dest_val = vcpu.readRegister(dest_reg);
						i16 res = dest_val + src_val;
						vcpu.writeRegister16(dest_reg, res);
					}
					break;
					case OpCodes::subip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i16 dest_val = vcpu.readRegister(dest_reg);
						i16 src_val = vcpu.readRegister(src_reg);
						i16 res = dest_val - src_val;
						vcpu.writeRegister16(dest_reg, res);
					}
					break;
					case OpCodes::subip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_val = vcpu.fetch16();
						i16 dest_val = vcpu.readRegister(dest_reg);
						i16 res = dest_val - src_val;
						vcpu.writeRegister16(dest_reg, res);
					}
					break;
					case OpCodes::mulip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i16 dest_val = vcpu.readRegister(dest_reg);
						i16 src_val = vcpu.readRegister(src_reg);
						i16 res = dest_val * src_val;
						vcpu.writeRegister16(dest_reg, res);
					}
					break;
					case OpCodes::mulip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_val = vcpu.fetch16();
						i16 dest_val = vcpu.readRegister(dest_reg);
						i16 res = dest_val * src_val;
						vcpu.writeRegister16(dest_reg, res);
					}
					break;
					case OpCodes::divip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i16 dest_val = vcpu.readRegister(dest_reg);
						i16 src_val = vcpu.readRegister(src_reg);
						i16 res = dest_val / src_val;
						i16 rv = dest_val % src_val;
						vcpu.writeRegister16(dest_reg, res);
						vcpu.writeRegister16(data::Registers::RV, rv);
					}
					break;
					case OpCodes::divip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_val = vcpu.fetch16();
						i16 dest_val = vcpu.readRegister(dest_reg);
						i16 res = dest_val / src_val;
						i16 rv = dest_val % src_val;
						vcpu.writeRegister16(dest_reg, res);
						vcpu.writeRegister16(data::Registers::RV, rv);
					}
					break;
					case OpCodes::andip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i16 src_val = vcpu.readRegister(src_reg);
						i16 dest_val = vcpu.readRegister(dest_reg);
						vcpu.writeRegister16(dest_reg, src_val & dest_val);
					}
					break;
					case OpCodes::andip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_val = vcpu.fetch16();
						i16 value = vcpu.readRegister(dest_reg);
						vcpu.writeRegister16(dest_reg, value & src_val);
					}
					break;
					case OpCodes::orip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i16 src_val = vcpu.readRegister(src_reg);
						i16 dest_val = vcpu.readRegister(dest_reg);
						vcpu.writeRegister16(dest_reg, src_val | dest_val);
					}
					break;
					case OpCodes::orip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_val = vcpu.fetch16();
						i16 value = vcpu.readRegister(dest_reg);
						vcpu.writeRegister16(dest_reg, value | src_val);
					}
					break;
					case OpCodes::xorip_reg_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u8 src_reg = vcpu.fetch8();
						i16 src_val = vcpu.readRegister(src_reg);
						i16 dest_val = vcpu.readRegister(dest_reg);
						vcpu.writeRegister16(dest_reg, src_val ^ dest_val);
					}
					break;
					case OpCodes::xorip_imm_in_reg:
					{
						u8 dest_reg = vcpu.fetch8();
						u16 src_val = vcpu.fetch16();
						i16 value = vcpu.readRegister(dest_reg);
						vcpu.writeRegister16(dest_reg, value ^ src_val);
					}
					break;
					case OpCodes::notip_reg:
					{
						u8 regAddr = vcpu.fetch8();
						i16 value = vcpu.readRegister(regAddr);
						vcpu.writeRegister16(regAddr, ~value);
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