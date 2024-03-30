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

			uint8_t ExtMov::getInstructionSIze(uint8_t opCode)
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
				uint8_t inst = vcpu.fetch8();
				switch (inst)
				{
					case OpCodes::wimm_in_dreg_immoffw:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int16_t src_wimm = vcpu.fetch16();
						uint16_t offset = vcpu.fetch16();

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_dreg_regoff:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int16_t src_wimm = vcpu.fetch16();
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_dreg_immoffb:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int16_t src_wimm = vcpu.fetch16();
						uint8_t offset = vcpu.fetch8();

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::bimm_in_dreg_immoffw:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int8_t src_bimm = vcpu.fetch8();
						uint16_t offset = vcpu.fetch16();

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_dreg_regoff:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int8_t src_bimm = vcpu.fetch8();
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_dreg_immoffb:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int8_t src_bimm = vcpu.fetch8();
						uint8_t offset = vcpu.fetch8();

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::wdreg_in_dreg_immoffw:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int16_t src_mem = vcpu.readRegister(vcpu.fetch8());
						uint16_t offset = vcpu.fetch16();
						
						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, mem.read16(src_mem));
					}
					break;
					case OpCodes::wdreg_in_dreg_regoff:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int16_t src_mem = vcpu.readRegister(vcpu.fetch8());
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, mem.read16(src_mem));
					}
					break;
					case OpCodes::wdreg_in_dreg_immoffb:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int16_t src_mem = vcpu.readRegister(vcpu.fetch8());
						uint8_t offset = vcpu.fetch8();

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write16(dest_mem + offset, mem.read16(src_mem));
					}
					break;
					case OpCodes::bdreg_in_dreg_immoffw:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int16_t src_mem = vcpu.readRegister(vcpu.fetch8());
						uint16_t offset = vcpu.fetch16();

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, mem.read8(src_mem));
					}
					break;
					case OpCodes::bdreg_in_dreg_regoff:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int16_t src_mem = vcpu.readRegister(vcpu.fetch8());
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, mem.read8(src_mem));
					}
					break;
					case OpCodes::bdreg_in_dreg_immoffb:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						int16_t src_mem = vcpu.readRegister(vcpu.fetch8());
						uint8_t offset = vcpu.fetch8();

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						mem.write8(dest_mem + offset, mem.read8(src_mem));
					}
					break;
					case OpCodes::wimm_in_mem_immoffw:
					{
						uint16_t dest_mem = vcpu.fetch16();
						int16_t src_wimm = vcpu.fetch16();
						uint16_t offset = vcpu.fetch16();

						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_mem_regoff:
					{
						uint16_t dest_mem = vcpu.fetch16();
						int16_t src_wimm = vcpu.fetch16();
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::wimm_in_mem_immoffb:
					{
						uint16_t dest_mem = vcpu.fetch16();
						int16_t src_wimm = vcpu.fetch16();
						uint8_t offset = vcpu.fetch8();

						mem.write16(dest_mem + offset, src_wimm);
					}
					break;
					case OpCodes::bimm_in_mem_immoffw:
					{
						uint16_t dest_mem = vcpu.fetch16();
						int8_t src_bimm = vcpu.fetch8();
						uint16_t offset = vcpu.fetch16();

						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_mem_regoff:
					{
						uint16_t dest_mem = vcpu.fetch16();
						int8_t src_bimm = vcpu.fetch8();
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::bimm_in_mem_immoffb:
					{
						uint16_t dest_mem = vcpu.fetch16();
						int8_t src_bimm = vcpu.fetch8();
						uint8_t offset = vcpu.fetch8();

						mem.write8(dest_mem + offset, src_bimm);
					}
					break;
					case OpCodes::wdreg_immoffw_in_dreg:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint16_t offset = vcpu.fetch16();

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						uint16_t src_mem = vcpu.readRegister(src_dreg);
						mem.write16(dest_mem, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_regoff_in_dreg:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						uint16_t src_mem = vcpu.readRegister(src_dreg);
						mem.write16(dest_mem, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_immoffb_in_dreg:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint8_t offset = vcpu.fetch8();

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						uint16_t src_mem = vcpu.readRegister(src_dreg);
						mem.write16(dest_mem, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffw_in_dreg:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint16_t offset = vcpu.fetch16();

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						uint16_t src_mem = vcpu.readRegister(src_dreg);
						mem.write8(dest_mem, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_regoff_in_dreg:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						uint16_t src_mem = vcpu.readRegister(src_dreg);
						mem.write8(dest_mem, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffb_in_dreg:
					{
						uint8_t dest_dreg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint8_t offset = vcpu.fetch8();

						uint16_t dest_mem = vcpu.readRegister(dest_dreg);
						uint16_t src_mem = vcpu.readRegister(src_dreg);
						mem.write8(dest_mem, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::wmem_immoffw_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint16_t src_mem = vcpu.fetch16();
						uint16_t offset = vcpu.fetch16();

						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wmem_regoff_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint16_t src_mem = vcpu.fetch16();
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wmem_immoffb_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint16_t src_mem = vcpu.fetch16();
						uint8_t offset = vcpu.fetch8();
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::bmem_immoffw_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint16_t src_mem = vcpu.fetch16();
						uint16_t offset = vcpu.fetch16();

						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bmem_regoff_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint16_t src_mem = vcpu.fetch16();
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bmem_immoffb_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint16_t src_mem = vcpu.fetch16();
						uint8_t offset = vcpu.fetch8();

						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_immoffw_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint16_t offset = vcpu.fetch16();

						uint16_t src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_regoff_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						uint16_t src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::wdreg_immoffb_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint8_t offset = vcpu.fetch8();

						uint16_t src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister16(dest_reg, mem.read16(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffw_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint16_t offset = vcpu.fetch16();

						uint16_t src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_regoff_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint16_t offset = vcpu.readRegister(vcpu.fetch8());

						uint16_t src_mem = vcpu.readRegister(src_dreg);
						vcpu.writeRegister8(dest_reg, mem.read8(src_mem + offset));
					}
					break;
					case OpCodes::bdreg_immoffb_in_reg:
					{
						uint8_t dest_reg = vcpu.fetch8();
						uint8_t src_dreg = vcpu.fetch8();
						uint8_t offset = vcpu.fetch8();

						uint16_t src_mem = vcpu.readRegister(src_dreg);
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

		}
	}
}