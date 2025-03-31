#include "VirtualCPU.hpp"
#include "../tools/GlobalData.hpp"

#include <ostd/Defines.hpp>
#include <ostd/Utils.hpp>
#include <iostream>

#include "../runtime/DragonRuntime.hpp"

namespace dragon
{
	namespace hw
	{
		VirtualCPU::VirtualCPU(IMemoryDevice& memory) : m_memory(memory)
		{
			writeRegister16(data::Registers::SP, (uint16_t)(0xFFFF - 1));
			writeRegister16(data::Registers::FP, (uint16_t)(0xFFFF - 1));

			for (int32_t i = 0; i < 16; i++)
				m_extensions[i] = nullptr;
		}

		int16_t VirtualCPU::readRegister(uint8_t reg)
		{
			if (reg >= data::Registers::Last) return 0x0000; //TODO: Error
			return m_registers[reg];
		}

		int16_t VirtualCPU::writeRegister16(uint8_t reg, int16_t value)
		{
			if (reg >= data::Registers::Last) return 0x0000; //TODO: Error
			m_registers[reg] = value;
			return value;
		}

		int8_t VirtualCPU::writeRegister8(uint8_t reg, int8_t value)
		{
			if (reg >= data::Registers::Last) return 0x0000; //TODO: Error
			m_registers[reg] = value & 0x00FF;
			return value;
		}

		int8_t VirtualCPU::fetch8(void)
		{
			uint16_t nextInstAddr = readRegister(data::Registers::IP);
			m_currentAddr = nextInstAddr;
			int8_t inst = m_memory.read8(nextInstAddr);
			writeRegister16(data::Registers::IP, nextInstAddr + 1);
			return inst;
		}

		int16_t VirtualCPU::fetch16(void)
		{
			uint16_t nextInstAddr = readRegister(data::Registers::IP);
			m_currentAddr = nextInstAddr;
			int16_t inst = m_memory.read16(nextInstAddr);
			writeRegister16(data::Registers::IP, nextInstAddr + 2);
			return inst;
		}

		void VirtualCPU::pushToStack(int16_t value)
		{
			uint16_t stackAddr = readRegister(data::Registers::SP);
			m_memory.write16(stackAddr, value);
			writeRegister16(data::Registers::SP, stackAddr - 2);
			m_stackFrameSize += 2;
		}

		int16_t VirtualCPU::popFromStack(void)
		{
			uint16_t nextSP = readRegister(data::Registers::SP) + 2;
			writeRegister16(data::Registers::SP, nextSP);
			int16_t value = m_memory.read16(nextSP);
			m_stackFrameSize -= 2;
			return value;
		}

		void VirtualCPU::pushStackFrame(void)
		{
			if (m_debugModeEnabled)
			{
				__debug_store_stack_frame_string_on_push();
				return;
			}
			uint16_t argStartAddr = readRegister(data::Registers::SP) + 2;
			uint16_t argCount = m_memory.read16(argStartAddr);
			if (argCount == 0)
				argStartAddr = 0;
			else
				argStartAddr += (argCount * 2);

			pushToStack(readRegister(data::Registers::R1));
			pushToStack(readRegister(data::Registers::R2));
			pushToStack(readRegister(data::Registers::R3));
			pushToStack(readRegister(data::Registers::R4));
			pushToStack(readRegister(data::Registers::R5));
			pushToStack(readRegister(data::Registers::R6));
			pushToStack(readRegister(data::Registers::R7));
			pushToStack(readRegister(data::Registers::R8));
			pushToStack(readRegister(data::Registers::R9));
			pushToStack(readRegister(data::Registers::R10));
			pushToStack(readRegister(data::Registers::PP));
			pushToStack(readRegister(data::Registers::ACC));
			pushToStack(readRegister(data::Registers::IP));
			pushToStack(readRegister(data::Registers::FP));
			pushToStack(m_stackFrameSize);

			writeRegister16(data::Registers::PP, argStartAddr);
			writeRegister16(data::Registers::FP, readRegister(data::Registers::SP));
			m_stackFrameSize = 0;
		}

		void VirtualCPU::popStackFrame(void)
		{
			uint16_t framePointerAddr = readRegister(data::Registers::FP);
			writeRegister16(data::Registers::SP, framePointerAddr);
			m_stackFrameSize = popFromStack();
			// uint16_t tmpStackFrameSize = m_stackFrameSize;

			writeRegister16(data::Registers::FP, popFromStack());
			writeRegister16(data::Registers::IP, popFromStack());
			writeRegister16(data::Registers::ACC, popFromStack());
			writeRegister16(data::Registers::PP, popFromStack());
			writeRegister16(data::Registers::R10, popFromStack());
			writeRegister16(data::Registers::R9, popFromStack());
			writeRegister16(data::Registers::R8, popFromStack());
			writeRegister16(data::Registers::R7, popFromStack());
			writeRegister16(data::Registers::R6, popFromStack());
			writeRegister16(data::Registers::R5, popFromStack());
			writeRegister16(data::Registers::R4, popFromStack());
			writeRegister16(data::Registers::R3, popFromStack());
			writeRegister16(data::Registers::R2, popFromStack());
			writeRegister16(data::Registers::R1, popFromStack());

			uint16_t nArgs = popFromStack();
			for (int32_t i = 0; i < nArgs; i++)
			{
				popFromStack();
				// writeRegister(data::Registers::FP, readRegister(data::Registers::FP) - 2);
			}

			// writeRegister(data::Registers::FP, framePointerAddr + tmpStackFrameSize);
		}

		bool VirtualCPU::readFlag(uint8_t flg)
		{
			if (flg >= 16) return false;
			m_tempFlags.value = readRegister(data::Registers::FL);
			return ostd::Bits::get(m_tempFlags, flg);
		}

		void VirtualCPU::setFlag(uint8_t flg, bool val)
		{
			if (flg >= 16) return;
			m_tempFlags.value = readRegister(data::Registers::FL);
			ostd::Bits::val(m_tempFlags, flg, val);
			writeRegister16(data::Registers::FL, m_tempFlags.value);
		}

		void VirtualCPU::handleInterrupt(uint8_t intValue, bool hardware)
		{
			uint16_t entryPointer = data::MemoryMapAddresses::IntVector_Start + (intValue * 3);
			uint8_t interruptStatus = m_memory.read8(entryPointer);
			if (interruptStatus != 0xFF) return;
			uint16_t handlerAddress = m_memory.read16(entryPointer + 1);
			pushToStack(0);
			pushStackFrame();
			m_subroutineCounter++;
			m_interruptHandlerCount++;
			writeRegister16(data::Registers::IP, handlerAddress);
			if (m_debugModeEnabled && hardware)
			{
				DragonRuntime::tCallInfo interruptData;
				interruptData.info = "HW INT";
				interruptData.addr = intValue;
				interruptData.inst_addr = 0x0000;
				interruptData.interrupts_disabled = !readFlag(data::Flags::InterruptsEnabled);
				ostd::SignalHandler::emitSignal(DragonRuntime::SignalListener::Signal_HardwareInterruptOccurred, ostd::tSignalPriority::RealTime, interruptData);
			}
		}

		bool VirtualCPU::loadExtension(void)
		{
			if (m_currentInst < data::OpCodes::Ext01 || m_currentInst > data::OpCodes::Ext16)
				return false;
			for (int32_t i = 0; i < 16; i++)
			{
				if (m_extensions[i] == nullptr)
					continue;
				if (m_extensions[i]->m_code == m_currentInst)
				{
					m_currentExtension = m_extensions[i];
					m_currentExtInst = m_memory.read8(readRegister(data::Registers::IP));
					return true;
				}
			}
			return false;
		}

		bool VirtualCPU::execute(void)
		{
			if (m_halt) return true;
			m_currentExtension = nullptr;
			m_currentExtInst = 0x00;
			m_isDebugBreakPoint = false;
			m_isOffsetAddressingEnabled = readFlag(data::Flags::OffsetModeEnabled);
			if (m_isOffsetAddressingEnabled)
				m_currentOffset = readRegister(data::Registers::OFFSET);
			else
				m_currentOffset = 0x0000;
			uint8_t inst = fetch8();
			m_currentInst = inst;
			if (loadExtension())
				return m_currentExtension->execute(*this);
			switch (inst)
			{
				case data::OpCodes::NoOp:
				{
					
				}
				break;
				case data::OpCodes::DEBUG_Break:
				{
					m_isDebugBreakPoint = true;
				}
				break;
				case data::OpCodes::BIOSModeImm:
				{
					uint16_t tmpAddr = m_currentAddr;
					int8_t value = fetch8();
					if (tmpAddr >= data::MemoryMapAddresses::BIOS_End)
						m_biosMode = false;
					else
						m_biosMode = value != 0;
				}
				break;
				case data::OpCodes::DEBUG_StartProfile:
				{
					int8_t id = fetch8();
					int8_t timeUnit = fetch8();
					ostd::eTimeUnits tu = ostd::eTimeUnits::Milliseconds;
					if (static_cast<eDebugProfilerTimeUnits>(timeUnit) == eDebugProfilerTimeUnits::Micros)
						tu = ostd::eTimeUnits::Microseconds;
					else if (static_cast<eDebugProfilerTimeUnits>(timeUnit) == eDebugProfilerTimeUnits::Nanos)
						tu = ostd::eTimeUnits::Nanoseconds;
					else if (static_cast<eDebugProfilerTimeUnits>(timeUnit) == eDebugProfilerTimeUnits::Secs)
						tu = ostd::eTimeUnits::Seconds;
					m_profilerTimer.start(true, ostd::String("DebugProfiler [").add(ostd::Utils::getHexStr(id, true, 1)).add("]"), tu);
					m_debugProfilerStarted = true;
				}
				break;
				case data::OpCodes::DEBUG_StopProfile:
				{
					if (m_debugProfilerStarted)
						m_profilerTimer.end(true);
					m_debugProfilerStarted = false;
				}
				break;
				case data::OpCodes::MovImmReg:
				{
					uint8_t regAddr = fetch8();
					int16_t literal = fetch16();
					writeRegister16(regAddr, literal);
				}
				break;
				case data::OpCodes::MovImmMem:
				{
					uint16_t addr = fetch16();
					int16_t literal = fetch16();
					m_memory.write16(addr, literal);
				}
				break;
				case data::OpCodes::MovRegReg:
				{
					uint8_t destRegAddr = fetch8();
					uint8_t srcRegAddr = fetch8();
					int16_t value = readRegister(srcRegAddr);
					writeRegister16(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovRegMem:
				{
					uint16_t addr = fetch16();
					uint8_t srcRegAddr = fetch8();
					int16_t value = readRegister(srcRegAddr);
					m_memory.write16(addr, value);
				}
				break;
				case data::OpCodes::MovMemReg:
				{
					uint8_t destRegAddr = fetch8();
					uint16_t addr = fetch16();
					int16_t value = m_memory.read16(addr);
					writeRegister16(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovDerefRegReg:
				{
					uint8_t destRegAddr = fetch8();
					uint8_t srcRegAddr = fetch8();
					uint16_t addr = readRegister(srcRegAddr);
					int16_t value = m_memory.read16(addr); 
					writeRegister16(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovDerefRegMem:
				{
					uint16_t destAddr = fetch16();
					uint8_t srcRegAddr = fetch8();
					uint16_t addr = readRegister(srcRegAddr);
					int16_t value = m_memory.read16(addr);
					m_memory.write16(destAddr, value);
				}
				break;
				// case data::OpCodes::MovImmRegOffReg:
				// {
				// 	uint8_t destRegAddr = fetch8();
				// 	uint16_t addr = fetch16();
				// 	uint8_t offRegAddr = fetch8();
				// 	int16_t offset = readRegister(offRegAddr);
				// 	int16_t value = m_memory.read16(addr + offset);
				// 	writeRegister(destRegAddr, value);
				// }
				// break;
				case data::OpCodes::MovRegDerefReg:
				{
					uint8_t destRegAddr = fetch8();
					uint8_t srcRegAddr = fetch8();
					int16_t value = readRegister(srcRegAddr);
					uint16_t addr = readRegister(destRegAddr);
					m_memory.write16(addr, value);
				}
				break;
				case data::OpCodes::MovMemDerefReg:
				{
					uint8_t destRegAddr = fetch8();
					uint16_t srcAddr = fetch16();
					int16_t value = m_memory.read16(srcAddr);
					uint16_t addr = readRegister(destRegAddr);
					m_memory.write16(addr, value);
				}
				break;
				case data::OpCodes::MovImmDerefReg:
				{
					uint8_t destRegAddr = fetch8();
					int16_t value = fetch16();
					uint16_t addr = readRegister(destRegAddr);
					m_memory.write16(addr, value);
				}
				break;
				case data::OpCodes::MovDerefRegDerefReg:
				{
					uint8_t destRegAddr = fetch8();
					uint8_t srcRegAddr = fetch8();
					uint16_t srcAddr = readRegister(srcRegAddr);
					uint16_t destAddr = readRegister(destRegAddr);
					int16_t value = m_memory.read16(srcAddr);
					m_memory.write16(destAddr, value);
				}
				break;
				case data::OpCodes::MovByteImmMem:
				{
					uint16_t addr = fetch16();
					int8_t literal = fetch8();
					m_memory.write8(addr, literal);
				}
				break;
				case data::OpCodes::MovByteDerefRegMem:
				{
					uint16_t destAddr = fetch16();
					uint8_t srcRegAddr = fetch8();
					uint16_t addr = readRegister(srcRegAddr);
					int8_t value = m_memory.read8(addr); 
					m_memory.write8(destAddr, value);
				}
				break;
				case data::OpCodes::MovByteRegDerefReg:
				{
					uint8_t destRegAddr = fetch8();
					uint8_t srcRegAddr = fetch8();
					int16_t value = readRegister(srcRegAddr);
					uint16_t addr = readRegister(destRegAddr);
					m_memory.write8(addr, (int8_t)value);
				}
				break;
				case data::OpCodes::MovByteMemDerefReg:
				{
					uint8_t destRegAddr = fetch8();
					uint16_t srcAddr = fetch16();
					int8_t value = m_memory.read8(srcAddr);
					uint16_t addr = readRegister(destRegAddr);
					m_memory.write8(addr, value);
				}
				break;
				case data::OpCodes::MovByteImmDerefReg:
				{
					uint8_t destRegAddr = fetch8();
					int8_t value = fetch8();
					uint16_t addr = readRegister(destRegAddr);
					m_memory.write8(addr, value);
				}
				break;
				case data::OpCodes::MovByteDerefRegDerefReg:
				{
					uint8_t destRegAddr = fetch8();
					uint8_t srcRegAddr = fetch8();
					uint16_t srcAddr = readRegister(srcRegAddr);
					uint16_t destAddr = readRegister(destRegAddr);
					int8_t value = m_memory.read8(srcAddr);
					m_memory.write8(destAddr, value);
				}
				break;
				case data::OpCodes::MovByteMemReg:
				{
					uint8_t destRegAddr = fetch8();
					uint16_t srcAddr = fetch16();
					int8_t value = m_memory.read8(srcAddr);
					writeRegister8(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovByteImmReg:
				{
					uint8_t destRegAddr = fetch8();
					int8_t value = fetch8();
					writeRegister8(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovByteDerefRegReg:
				{
					uint8_t destRegAddr = fetch8();
					uint8_t srcRegAddr = fetch8();
					uint16_t srcAddr = readRegister(srcRegAddr);
					int8_t value = m_memory.read8(srcAddr);
					writeRegister8(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovByteRegMem:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					m_memory.write8(addr, (int8_t)(value & 0x00FF));
				}
				break;
				case data::OpCodes::AddImmReg:
				{
					uint8_t regAddr = fetch8();
					int16_t literal = fetch16();
					int16_t regValue = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, regValue + literal);
				}
				break;
				case data::OpCodes::AddRegReg:
				{
					uint8_t regAddr1 = fetch8();
					uint8_t regAddr2 = fetch8();
					int16_t regValue1 = readRegister(regAddr1);
					int16_t regValue2 = readRegister(regAddr2);
					writeRegister16(data::Registers::ACC, regValue1 + regValue2);
				}
				break;
				case data::OpCodes::SubImmReg:
				{
					uint8_t regAddr = fetch8();
					int16_t literal = fetch16();
					int16_t regValue = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, regValue - literal);
				}
				break;
				case data::OpCodes::SubRegReg:
				{
					uint8_t regAddr1 = fetch8();
					uint8_t regAddr2 = fetch8();
					int16_t regValue1 = readRegister(regAddr1);
					int16_t regValue2 = readRegister(regAddr2);
					writeRegister16(data::Registers::ACC, regValue1 - regValue2);
				}
				break;
				case data::OpCodes::MulImmReg:
				{
					uint8_t regAddr = fetch8();
					int16_t literal = fetch16();
					int16_t regValue = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, regValue * literal);
				}
				break;
				case data::OpCodes::MulRegReg:
				{
					uint8_t regAddr1 = fetch8();
					uint8_t regAddr2 = fetch8();
					int16_t regValue1 = readRegister(regAddr1);
					int16_t regValue2 = readRegister(regAddr2);
					writeRegister16(data::Registers::ACC, regValue1 * regValue2);
				}
				break;
				case data::OpCodes::DivImmReg: //TODO: Division by zero is unhandled
				{
					uint8_t regAddr = fetch8();
					int16_t literal = fetch16();
					int16_t regValue = readRegister(regAddr);
					int16_t quotient = regValue / literal;
					int16_t reminder = regValue % literal;
					writeRegister16(data::Registers::ACC, quotient);
					writeRegister16(data::Registers::RV, reminder);
				}
				break;
				case data::OpCodes::DivRegReg: //TODO: Division by zero is unhandled
				{
					uint8_t regAddr1 = fetch8();
					uint8_t regAddr2 = fetch8();
					int16_t regValue1 = readRegister(regAddr1);
					int16_t regValue2 = readRegister(regAddr2);
					int16_t quotient = regValue1 / regValue2;
					int16_t reminder = regValue1 % regValue2;
					writeRegister16(data::Registers::ACC, quotient);
					writeRegister16(data::Registers::RV, reminder);
				}
				break;
				case data::OpCodes::IncReg:
				{
					uint8_t regAddr = fetch8();
					int16_t regValue = readRegister(regAddr);
					writeRegister16(regAddr, regValue + 1);
				}
				break;
				case data::OpCodes::DecReg:
				{
					uint8_t regAddr = fetch8();
					int16_t regValue = readRegister(regAddr);
					writeRegister16(regAddr, regValue - 1);
				}
				break;
				case data::OpCodes::RShiftRegImm:
				{
					int16_t literal = fetch16();
					uint8_t regAddr = fetch8();
					int16_t regValue = readRegister(regAddr);
					regValue = regValue >> literal;
					writeRegister16(regAddr, regValue);
				}
				break;
				case data::OpCodes::RShiftRegReg:
				{
					uint8_t shiftRegAddr = fetch8();
					uint8_t regAddr = fetch8();
					int16_t shiftValue = readRegister(shiftRegAddr);
					int16_t regValue = readRegister(regAddr);
					regValue = regValue >> shiftValue;
					writeRegister16(regAddr, regValue);
				}
				break;
				case data::OpCodes::LShiftRegImm:
				{
					int16_t literal = fetch16();
					uint8_t regAddr = fetch8();
					int16_t regValue = readRegister(regAddr);
					regValue = regValue << literal;
					writeRegister16(regAddr, regValue);
				}
				break;
				case data::OpCodes::LShiftRegReg:
				{
					uint8_t shiftRegAddr = fetch8();
					uint8_t regAddr = fetch8();
					int16_t shiftValue = readRegister(shiftRegAddr);
					int16_t regValue = readRegister(regAddr);
					regValue = regValue << shiftValue;
					writeRegister16(regAddr, regValue);
				}
				break;
				case data::OpCodes::AndRegImm:
				{
					int16_t literal = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, value & literal);
				}
				break;
				case data::OpCodes::AndRegReg:
				{
					uint8_t andRegAddr = fetch8();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t andValue = readRegister(andRegAddr);
					writeRegister16(data::Registers::ACC, value & andValue);
				}
				break;
				case data::OpCodes::OrRegImm:
				{
					int16_t literal = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, value | literal);
				}
				break;
				case data::OpCodes::OrRegReg:
				{
					uint8_t andRegAddr = fetch8();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t andValue = readRegister(andRegAddr);
					writeRegister16(data::Registers::ACC, value | andValue);
				}
				break;
				case data::OpCodes::XorRegImm:
				{
					int16_t literal = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, value ^ literal);
				}
				break;
				case data::OpCodes::XorRegReg:
				{
					uint8_t andRegAddr = fetch8();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t andValue = readRegister(andRegAddr);
					writeRegister16(data::Registers::ACC, value ^ andValue);
				}
				break;
				case data::OpCodes::NotReg:
				{
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, ~value);
				}
				break;
				case data::OpCodes::NegReg:
				{
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					value *= -1;
					writeRegister16(regAddr, value);
				}
				break;
				case data::OpCodes::NegByteReg:
				{
					uint8_t regAddr = fetch8();
					int8_t value = (int8_t)readRegister(regAddr);
					value *= -1;
					writeRegister8(regAddr, value);
				}
				break;
				case data::OpCodes::JmpNotEqImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value != accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpNotEqReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value != accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpEqImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value == accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpEqReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value == accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGrImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value > accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGrReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value > accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLessImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value < accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLessReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value < accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGeImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value >= accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGeReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value >= accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLeImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value <= accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLeReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value <= accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::Jmp:
				{
					uint16_t addr = fetch16();
					writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::Halt:
				{
					m_halt = true;
					return true;
				}
				break;
				case data::OpCodes::PushImm:
				{
					int16_t value = fetch16();
					pushToStack(value);
				}
				break;
				case data::OpCodes::PushReg:
				{
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					pushToStack(value);
				}
				break;
				case data::OpCodes::PopReg:
				{
					uint8_t regAddr = fetch8();
					int16_t value = popFromStack();
					writeRegister16(regAddr, value);
				}
				break;
				case data::OpCodes::CallImm:
				{
					uint16_t subroutineAddr = fetch16();
					pushStackFrame();
					writeRegister16(data::Registers::IP, subroutineAddr);
					m_subroutineCounter++;
				}
				break;
				case data::OpCodes::CallReg:
				{
					uint8_t regAddr = fetch8();
					uint16_t subroutineAddr = readRegister(regAddr);
					pushStackFrame();
					writeRegister16(data::Registers::IP, subroutineAddr);
					m_subroutineCounter++;
				}
				break;
				case data::OpCodes::Ret:
				{
					popStackFrame();
					// m_subroutineCounter = ZERO(m_subroutineCounter - 1);
					m_subroutineCounter--;
				}
				break;
				case data::OpCodes::ArgReg:
				{
					uint8_t regAddr = fetch8();
					if (!isInSubRoutine()) break;
					int16_t pp_val = readRegister(data::Registers::PP);
					int16_t arg_data = m_memory.read16(pp_val);
					writeRegister16(data::Registers::PP, pp_val - 2);
					writeRegister16(regAddr, arg_data);
				}
				break;
				case data::OpCodes::RetInt:
				{
					m_interruptHandlerCount--;
					popStackFrame();
					// m_subroutineCounter = ZERO(m_subroutineCounter - 1);
					m_subroutineCounter--;
				}
				break;
				case data::OpCodes::Int:
				{
					uint8_t intValue = fetch8();
					if (!readFlag(data::Flags::InterruptsEnabled))
						return true;
					handleInterrupt(intValue, false);
					m_subroutineCounter++;
				}
				break;
				case data::OpCodes::ZeroFlag:
				{
					uint8_t flag = fetch8();
					setFlag(flag, false);
				}
				break;
				case data::OpCodes::SetFlag:
				{
					uint8_t flag = fetch8();
					setFlag(flag, true);
				}
				break;
				case data::OpCodes::ToggleFlag:
				{
					uint8_t flag = fetch8();
					bool value = readFlag(flag);
					setFlag(flag, !value);
				}
				break;
				case data::OpCodes::Ext01:
				case data::OpCodes::Ext02:
				case data::OpCodes::Ext03:
				case data::OpCodes::Ext04:
				case data::OpCodes::Ext05:
				case data::OpCodes::Ext06:
				case data::OpCodes::Ext07:
				case data::OpCodes::Ext08:
				case data::OpCodes::Ext09:
				case data::OpCodes::Ext10:
				case data::OpCodes::Ext11:
				case data::OpCodes::Ext12:
				case data::OpCodes::Ext13:
				case data::OpCodes::Ext14:
				case data::OpCodes::Ext15:
				case data::OpCodes::Ext16:
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CPU_UnsupportedExtension, ostd::String("Unsupported Extension: ").add(ostd::Utils::getHexStr(inst, true, 1)));
					m_halt = true;
					return false;
				}
				break;
				default:
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CPU_UnknownInstruction, ostd::String("Unknown instruction: ").add(ostd::Utils::getHexStr(inst, true, 1)));
					m_halt = true;
					return false;
				}
			}

			return true;
		}

		void VirtualCPU::__debug_store_stack_frame_string_on_push(void)
		{
			if (!m_debugModeEnabled) return;
			ostd::String stackFrameString = "";

			uint16_t argStartAddr = readRegister(data::Registers::SP) + 2;
			uint16_t argCount = m_memory.read16(argStartAddr);
			if (argCount == 0)
				argStartAddr = 0;
			else
				argStartAddr += (argCount * 2);

			stackFrameString.add("args: ").add(ostd::Utils::getHexStr(argStartAddr, true, 2)).add(", argc: ").add(argCount).add("\n");

			pushToStack(readRegister(data::Registers::R1));
			stackFrameString.add("R1: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::R1), true, 2));
			pushToStack(readRegister(data::Registers::R2));
			stackFrameString.add(" R2: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::R2), true, 2));
			pushToStack(readRegister(data::Registers::R3));
			stackFrameString.add(" R3: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::R3), true, 2));
			pushToStack(readRegister(data::Registers::R4));
			stackFrameString.add(" R4: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::R4), true, 2));
			pushToStack(readRegister(data::Registers::R5));
			stackFrameString.add(" R5: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::R5), true, 2));
			pushToStack(readRegister(data::Registers::R6));
			stackFrameString.add(" R6: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::R6), true, 2));
			pushToStack(readRegister(data::Registers::R7));
			stackFrameString.add(" R7: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::R7), true, 2));
			pushToStack(readRegister(data::Registers::R8));
			stackFrameString.add(" R8: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::R8), true, 2));
			pushToStack(readRegister(data::Registers::R9));
			stackFrameString.add(" R9: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::R9), true, 2));
			pushToStack(readRegister(data::Registers::R10));
			stackFrameString.add(" R10: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::R10), true, 2));
			stackFrameString.add("\n");
			pushToStack(readRegister(data::Registers::PP));
			stackFrameString.add("PP: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::PP), true, 2));
			pushToStack(readRegister(data::Registers::ACC));
			stackFrameString.add(" ACC: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::ACC), true, 2));
			pushToStack(readRegister(data::Registers::IP));
			stackFrameString.add(" IP: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::IP), true, 2));
			pushToStack(readRegister(data::Registers::FP));
			stackFrameString.add(" FP: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::FP), true, 2));
			stackFrameString.add("\n");
			pushToStack(m_stackFrameSize);
			stackFrameString.add("StackFrame: ").add(m_stackFrameSize).add(", ");

			writeRegister16(data::Registers::PP, argStartAddr);
			writeRegister16(data::Registers::FP, readRegister(data::Registers::SP));
			stackFrameString.add("New FP: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::FP), true, 2));
			m_stackFrameSize = 0;

			m_debug_stackFrameStrings.push_back(stackFrameString);
		}
	}
}