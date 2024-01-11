#include "VirtualCPU.hpp"
#include "../tools/GlobalData.hpp"

#include <ostd/Defines.hpp>
#include <ostd/Utils.hpp>
#include <iostream>

namespace dragon
{
	namespace hw
	{
		VirtualCPU::VirtualCPU(IMemoryDevice& memory) : m_memory(memory)
		{
			writeRegister(data::Registers::SP, (uint16_t)(0xFFFF - 1));
			writeRegister(data::Registers::FP, (uint16_t)(0xFFFF - 1));
		}

		int16_t VirtualCPU::readRegister(uint8_t reg)
		{
			if (reg >= data::Registers::Last) return 0x0000; //TODO: Error
			return m_registers[reg];
		}

		int16_t VirtualCPU::writeRegister(uint8_t reg, int16_t value)
		{
			if (reg >= data::Registers::Last) return 0x0000; //TODO: Error
			m_registers[reg] = value;
			return value;
		}

		int8_t VirtualCPU::fetch8(void)
		{
			uint16_t nextInstAddr = readRegister(data::Registers::IP);
			int8_t inst = m_memory.read8(nextInstAddr);
			writeRegister(data::Registers::IP, nextInstAddr + 1);
			return inst;
		}

		int16_t VirtualCPU::fetch16(void)
		{
			uint16_t nextInstAddr = readRegister(data::Registers::IP);
			int16_t inst = m_memory.read16(nextInstAddr);
			writeRegister(data::Registers::IP, nextInstAddr + 2);
			return inst;
		}

		void VirtualCPU::pushToStack(int16_t value)
		{
			uint16_t stackAddr = readRegister(data::Registers::SP);
			m_memory.write16(stackAddr, value);
			writeRegister(data::Registers::SP, stackAddr - 2);
			m_stackFrameSize += 2;
		}

		int16_t VirtualCPU::popFromStack(void)
		{
			uint16_t nextSP = readRegister(data::Registers::SP) + 2;
			writeRegister(data::Registers::SP, nextSP);
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

			writeRegister(data::Registers::PP, argStartAddr);
			writeRegister(data::Registers::FP, readRegister(data::Registers::SP));
			m_stackFrameSize = 0;
		}

		void VirtualCPU::popStackFrame(void)
		{
			uint16_t framePointerAddr = readRegister(data::Registers::FP);
			writeRegister(data::Registers::SP, framePointerAddr);
			m_stackFrameSize = popFromStack();
			// uint16_t tmpStackFrameSize = m_stackFrameSize;

			writeRegister(data::Registers::FP, popFromStack());
			writeRegister(data::Registers::IP, popFromStack());
			writeRegister(data::Registers::ACC, popFromStack());
			writeRegister(data::Registers::PP, popFromStack());
			writeRegister(data::Registers::R10, popFromStack());
			writeRegister(data::Registers::R9, popFromStack());
			writeRegister(data::Registers::R8, popFromStack());
			writeRegister(data::Registers::R7, popFromStack());
			writeRegister(data::Registers::R6, popFromStack());
			writeRegister(data::Registers::R5, popFromStack());
			writeRegister(data::Registers::R4, popFromStack());
			writeRegister(data::Registers::R3, popFromStack());
			writeRegister(data::Registers::R2, popFromStack());
			writeRegister(data::Registers::R1, popFromStack());

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
			writeRegister(data::Registers::FL, m_tempFlags.value);
		}

		void VirtualCPU::handleInterrupt(uint8_t intValue)
		{
			uint16_t entryPointer = data::MemoryMapAddresses::IntVector_Start + (intValue * 3);
			uint8_t interruptStatus = m_memory.read8(entryPointer);
			if (interruptStatus != 0xFF) return;
			uint16_t handlerAddress = m_memory.read16(entryPointer + 1);
			if (!m_isInInterruptHandler)
			{
				pushToStack(0);
				pushStackFrame();
			}
			m_isInInterruptHandler = true;
			writeRegister(data::Registers::IP, handlerAddress);
		}

		bool VirtualCPU::execute(void)
		{
			if (m_halt) return false;
			m_isDebugBreakPoint = false;
			uint8_t inst = fetch8();
			m_currentInst = inst;
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
				case data::OpCodes::MovImmReg:
				{
					uint8_t regAddr = fetch8();
					int16_t literal = fetch16();
					writeRegister(regAddr, literal);
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
					writeRegister(destRegAddr, value);
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
					writeRegister(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovDerefRegReg:
				{
					uint8_t destRegAddr = fetch8();
					uint8_t srcRegAddr = fetch8();
					uint16_t addr = readRegister(srcRegAddr);
					int16_t value = m_memory.read16(addr); 
					writeRegister(destRegAddr, value);
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
				case data::OpCodes::MovImmRegOffReg:
				{
					uint8_t destRegAddr = fetch8();
					uint16_t addr = fetch16();
					uint8_t offRegAddr = fetch8();
					int16_t offset = readRegister(offRegAddr);
					int16_t value = m_memory.read16(addr + offset);
					writeRegister(destRegAddr, value);
				}
				break;
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
					writeRegister(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovByteImmReg:
				{
					uint8_t destRegAddr = fetch8();
					int8_t value = fetch8();
					writeRegister(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovByteDerefRegReg:
				{
					uint8_t destRegAddr = fetch8();
					uint8_t srcRegAddr = fetch8();
					uint16_t srcAddr = readRegister(srcRegAddr);
					int8_t value = m_memory.read8(srcAddr);
					writeRegister(destRegAddr, value);
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
					writeRegister(data::Registers::ACC, regValue + literal);
				}
				break;
				case data::OpCodes::AddRegReg:
				{
					uint8_t regAddr1 = fetch8();
					uint8_t regAddr2 = fetch8();
					int16_t regValue1 = readRegister(regAddr1);
					int16_t regValue2 = readRegister(regAddr2);
					writeRegister(data::Registers::ACC, regValue1 + regValue2);
				}
				break;
				case data::OpCodes::SubImmReg:
				{
					uint8_t regAddr = fetch8();
					int16_t literal = fetch16();
					int16_t regValue = readRegister(regAddr);
					writeRegister(data::Registers::ACC, regValue - literal);
				}
				break;
				case data::OpCodes::SubRegReg:
				{
					uint8_t regAddr1 = fetch8();
					uint8_t regAddr2 = fetch8();
					int16_t regValue1 = readRegister(regAddr1);
					int16_t regValue2 = readRegister(regAddr2);
					writeRegister(data::Registers::ACC, regValue1 - regValue2);
				}
				break;
				case data::OpCodes::MulImmReg:
				{
					uint8_t regAddr = fetch8();
					int16_t literal = fetch16();
					int16_t regValue = readRegister(regAddr);
					writeRegister(data::Registers::ACC, regValue * literal);
				}
				break;
				case data::OpCodes::MulRegReg:
				{
					uint8_t regAddr1 = fetch8();
					uint8_t regAddr2 = fetch8();
					int16_t regValue1 = readRegister(regAddr1);
					int16_t regValue2 = readRegister(regAddr2);
					writeRegister(data::Registers::ACC, regValue1 * regValue2);
				}
				break;
				case data::OpCodes::IncReg:
				{
					uint8_t regAddr = fetch8();
					int16_t regValue = readRegister(regAddr);
					writeRegister(regAddr, regValue + 1);
				}
				break;
				case data::OpCodes::DecReg:
				{
					uint8_t regAddr = fetch8();
					int16_t regValue = readRegister(regAddr);
					writeRegister(regAddr, regValue - 1);
				}
				break;
				case data::OpCodes::RShiftRegImm:
				{
					int16_t literal = fetch16();
					uint8_t regAddr = fetch8();
					int16_t regValue = readRegister(regAddr);
					regValue = regValue >> literal;
					writeRegister(regAddr, regValue);
				}
				break;
				case data::OpCodes::RShiftRegReg:
				{
					uint8_t shiftRegAddr = fetch8();
					uint8_t regAddr = fetch8();
					int16_t shiftValue = readRegister(shiftRegAddr);
					int16_t regValue = readRegister(regAddr);
					regValue = regValue >> shiftValue;
					writeRegister(regAddr, regValue);
				}
				break;
				case data::OpCodes::LShiftRegImm:
				{
					int16_t literal = fetch16();
					uint8_t regAddr = fetch8();
					int16_t regValue = readRegister(regAddr);
					regValue = regValue << literal;
					writeRegister(regAddr, regValue);
				}
				break;
				case data::OpCodes::LShiftRegReg:
				{
					uint8_t shiftRegAddr = fetch8();
					uint8_t regAddr = fetch8();
					int16_t shiftValue = readRegister(shiftRegAddr);
					int16_t regValue = readRegister(regAddr);
					regValue = regValue << shiftValue;
					writeRegister(regAddr, regValue);
				}
				break;
				case data::OpCodes::AndRegImm:
				{
					int16_t literal = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					writeRegister(data::Registers::ACC, value & literal);
				}
				break;
				case data::OpCodes::AndRegReg:
				{
					uint8_t andRegAddr = fetch8();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t andValue = readRegister(andRegAddr);
					writeRegister(data::Registers::ACC, value & andValue);
				}
				break;
				case data::OpCodes::OrRegImm:
				{
					int16_t literal = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					writeRegister(data::Registers::ACC, value | literal);
				}
				break;
				case data::OpCodes::OrRegReg:
				{
					uint8_t andRegAddr = fetch8();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t andValue = readRegister(andRegAddr);
					writeRegister(data::Registers::ACC, value | andValue);
				}
				break;
				case data::OpCodes::XorRegImm:
				{
					int16_t literal = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					writeRegister(data::Registers::ACC, value ^ literal);
				}
				break;
				case data::OpCodes::XorRegReg:
				{
					uint8_t andRegAddr = fetch8();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t andValue = readRegister(andRegAddr);
					writeRegister(data::Registers::ACC, value ^ andValue);
				}
				break;
				case data::OpCodes::NotReg:
				{
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					writeRegister(data::Registers::ACC, ~value);
				}
				break;
				case data::OpCodes::JmpNotEqImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value != accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpNotEqReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value != accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpEqImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value == accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpEqReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value == accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGrImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value > accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGrReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value > accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLessImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value < accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLessReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value < accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGeImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value >= accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGeReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value >= accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLeImm:
				{
					uint16_t addr = fetch16();
					int16_t value = fetch16();
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value <= accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLeReg:
				{
					uint16_t addr = fetch16();
					uint8_t regAddr = fetch8();
					int16_t value = readRegister(regAddr);
					int16_t accValue = readRegister(data::Registers::ACC);
					if (value <= accValue)
						writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::Jmp:
				{
					uint16_t addr = fetch16();
					writeRegister(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::Halt:
				{
					m_halt = true;
					return false;
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
					writeRegister(regAddr, value);
				}
				break;
				case data::OpCodes::CallImm:
				{
					uint16_t subroutineAddr = fetch16();
					pushStackFrame();
					writeRegister(data::Registers::IP, subroutineAddr);
					m_subroutineCounter++;
				}
				break;
				case data::OpCodes::CallReg:
				{
					uint8_t regAddr = fetch8();
					uint16_t subroutineAddr = readRegister(regAddr);
					pushStackFrame();
					writeRegister(data::Registers::IP, subroutineAddr);
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
					writeRegister(data::Registers::PP, pp_val - 2);
					writeRegister(regAddr, arg_data);
				}
				break;
				case data::OpCodes::RetInt:
				{
					m_isInInterruptHandler = false;
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
					handleInterrupt(intValue);
					m_subroutineCounter++;
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

			writeRegister(data::Registers::PP, argStartAddr);
			writeRegister(data::Registers::FP, readRegister(data::Registers::SP));
			stackFrameString.add("New FP: ").add(ostd::Utils::getHexStr(readRegister(data::Registers::FP), true, 2));
			m_stackFrameSize = 0;

			m_debug_stackFrameStrings.push_back(stackFrameString);
		}
	}
}