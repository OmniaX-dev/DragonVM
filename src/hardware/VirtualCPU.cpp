#include "VirtualCPU.hpp"
#include "../tools/GlobalData.hpp"

#include <ostd/io/Memory.hpp>

#include "../runtime/DragonRuntime.hpp"

namespace dragon
{
	namespace hw
	{
		VirtualCPU::VirtualCPU(IMemoryDevice& memory) : m_memory(memory)
		{
			writeRegister16(data::Registers::SP, (u16)(0xFFFF - 1));
			writeRegister16(data::Registers::FP, (u16)(0xFFFF - 1));

			for (i32 i = 0; i < 16; i++)
				m_extensions[i] = nullptr;
		}

		i16 VirtualCPU::readRegister(u8 reg)
		{
			if (reg >= data::Registers::Last) return 0x0000; //TODO: Error
			return m_registers[reg];
		}

		i16 VirtualCPU::writeRegister16(u8 reg, i16 value)
		{
			if (reg >= data::Registers::Last) return 0x0000; //TODO: Error
			m_registers[reg] = value;
			return value;
		}

		i8 VirtualCPU::writeRegister8(u8 reg, i8 value)
		{
			if (reg >= data::Registers::Last) return 0x0000; //TODO: Error
			m_registers[reg] = value & 0x00FF;
			return value;
		}

		i8 VirtualCPU::fetch8(void)
		{
			u16 nextInstAddr = readRegister(data::Registers::IP);
			m_currentAddr = nextInstAddr;
			i8 inst = m_memory.read8(nextInstAddr);
			writeRegister16(data::Registers::IP, nextInstAddr + 1);
			return inst;
		}

		i16 VirtualCPU::fetch16(void)
		{
			u16 nextInstAddr = readRegister(data::Registers::IP);
			m_currentAddr = nextInstAddr;
			i16 inst = m_memory.read16(nextInstAddr);
			writeRegister16(data::Registers::IP, nextInstAddr + 2);
			return inst;
		}

		void VirtualCPU::pushToStack(i16 value)
		{
			u16 stackAddr = readRegister(data::Registers::SP);
			u16 stack_size = DragonRuntime::vCMOS.read16(data::CMOSRegisters::StackSize);
			if (stackAddr <= 0xFFFF - stack_size)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::CPU_StackOverflow, "Stack Overflow: ");
				m_halt = true;
				return;
			}
			m_memory.write16(stackAddr, value);
			writeRegister16(data::Registers::SP, stackAddr - 2);
			m_stackFrameSize += 2;
		}

		i16 VirtualCPU::popFromStack(void)
		{
			u16 nextSP = readRegister(data::Registers::SP) + 2;
			writeRegister16(data::Registers::SP, nextSP);
			i16 value = m_memory.read16(nextSP);
			m_stackFrameSize -= 2;
			return value;
		}

		void VirtualCPU::pushStackFrame(void)
		{
			u16 argStartAddr = readRegister(data::Registers::SP) + 2;
			u16 argCount = m_memory.read16(argStartAddr);
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
			u16 framePointerAddr = readRegister(data::Registers::FP);
			writeRegister16(data::Registers::SP, framePointerAddr);
			m_stackFrameSize = popFromStack();

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

			u16 nArgs = popFromStack();
			for (i32 i = 0; i < nArgs; i++)
				popFromStack();
		}

		bool VirtualCPU::readFlag(u8 flg)
		{
			if (flg >= 16) return false;
			m_tempFlags.value = readRegister(data::Registers::FL);
			return ostd::Bits::get(m_tempFlags, flg);
		}

		void VirtualCPU::setFlag(u8 flg, bool val)
		{
			if (flg >= 16) return;
			m_tempFlags.value = readRegister(data::Registers::FL);
			ostd::Bits::val(m_tempFlags, flg, val);
			writeRegister16(data::Registers::FL, m_tempFlags.value);
		}

		void VirtualCPU::handleInterrupt(u8 intValue, bool hardware)
		{
			u16 entryPointer = data::MemoryMapAddresses::IntVector_Start + (intValue * 3);
			u8 interruptStatus = m_memory.read8(entryPointer);
			if (interruptStatus != 0xFF) return;
			u16 handlerAddress = m_memory.read16(entryPointer + 1);
			pushToStack(0);
			pushStackFrame();
			m_subroutineCounter++;
			m_interruptHandlerCount++;
			writeRegister16(data::Registers::IP, handlerAddress);
			// if (m_debugModeEnabled && hardware)
			// {
			//     DragonRuntime::tCallInfo interruptData;
			//     interruptData.info = "HW INT";
			//     interruptData.addr = intValue;
			//     interruptData.inst_addr = 0x0000;
			//     interruptData.interrupts_disabled = !readFlag(data::Flags::InterruptsEnabled);
			//     ostd::SignalHandler::emitSignal(DragonRuntime::SignalListener::Signal_HardwareInterruptOccurred, ostd::Signal::Priority::RealTime, interruptData);
			// }
		}

		bool VirtualCPU::loadExtension(void)
		{
			if (m_currentInst < data::OpCodes::Ext01 || m_currentInst > data::OpCodes::Ext16)
				return false;
			for (i32 i = 0; i < 16; i++)
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
			m_ramDumped = false;
			m_isOffsetAddressingEnabled = readFlag(data::Flags::OffsetModeEnabled);
			if (m_isOffsetAddressingEnabled)
				m_currentOffset = readRegister(data::Registers::OFFSET);
			else
				m_currentOffset = 0x0000;
			u8 inst = fetch8();
			m_currentInst = inst;
			if (loadExtension())
				return m_currentExtension->execute(*this);
			switch (inst)
			{
				case data::OpCodes::NoOp:
				{
					//
				}
				break;
				case data::OpCodes::DEBUG_Break:
				{
					if (!m_debugModeEnabled) break;
					m_isDebugBreakPoint = true;
				}
				break;
				case data::OpCodes::DEBUG_DumpRAM:
				{
					if (!m_debugModeEnabled) break;
					ostd::Memory::saveByteStreamToFile(*DragonRuntime::ram.getByteStream(), "ram_dump.bin");
					m_isDebugBreakPoint = true;
					m_ramDumped = true;
				}
				break;
				case data::OpCodes::DEBUG_StartProfile:
				{
					if (!m_debugModeEnabled) break;
					i8 id = fetch8();
					i8 timeUnit = fetch8();
					ostd::eTimeUnits tu = ostd::eTimeUnits::Milliseconds;
					if (static_cast<eDebugProfilerTimeUnits>(timeUnit) == eDebugProfilerTimeUnits::Micros)
						tu = ostd::eTimeUnits::Microseconds;
					else if (static_cast<eDebugProfilerTimeUnits>(timeUnit) == eDebugProfilerTimeUnits::Nanos)
						tu = ostd::eTimeUnits::Nanoseconds;
					else if (static_cast<eDebugProfilerTimeUnits>(timeUnit) == eDebugProfilerTimeUnits::Secs)
						tu = ostd::eTimeUnits::Seconds;
					m_profilerTimer.start(true, String("DebugProfiler [").add(String::getHexStr(id, true, 1)).add("]"), tu);
					m_debugProfilerStarted = true;
				}
				break;
				case data::OpCodes::DEBUG_StopProfile:
				{
					if (!m_debugModeEnabled) break;
					if (m_debugProfilerStarted)
						m_profilerTimer.end(true);
					m_debugProfilerStarted = false;
				}
				break;
				case data::OpCodes::BIOSModeImm:
				{
					u16 tmpAddr = m_currentAddr;
					i8 value = fetch8();
					if (tmpAddr >= data::MemoryMapAddresses::BIOS_End)
						m_biosMode = false;
					else
						m_biosMode = value != 0;
				}
				break;
				case data::OpCodes::MovImmReg:
				{
					u8 regAddr = fetch8();
					i16 literal = fetch16();
					writeRegister16(regAddr, literal);
				}
				break;
				case data::OpCodes::MovImmMem:
				{
					u16 addr = fetch16();
					i16 literal = fetch16();
					m_memory.write16(addr, literal);
				}
				break;
				case data::OpCodes::MovRegReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					i16 value = readRegister(srcRegAddr);
					writeRegister16(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovRegMem:
				{
					u16 addr = fetch16();
					u8 srcRegAddr = fetch8();
					i16 value = readRegister(srcRegAddr);
					m_memory.write16(addr, value);
				}
				break;
				case data::OpCodes::MovMemReg:
				{
					u8 destRegAddr = fetch8();
					u16 addr = fetch16();
					i16 value = m_memory.read16(addr);
					writeRegister16(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovDerefRegReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					u16 addr = readRegister(srcRegAddr);
					i16 value = m_memory.read16(addr);
					writeRegister16(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovDerefRegMem:
				{
					u16 destAddr = fetch16();
					u8 srcRegAddr = fetch8();
					u16 addr = readRegister(srcRegAddr);
					i16 value = m_memory.read16(addr);
					m_memory.write16(destAddr, value);
				}
				break;
				case data::OpCodes::MovRegDerefReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					i16 value = readRegister(srcRegAddr);
					u16 addr = readRegister(destRegAddr);
					m_memory.write16(addr, value);
				}
				break;
				case data::OpCodes::MovMemDerefReg:
				{
					u8 destRegAddr = fetch8();
					u16 srcAddr = fetch16();
					i16 value = m_memory.read16(srcAddr);
					u16 addr = readRegister(destRegAddr);
					m_memory.write16(addr, value);
				}
				break;
				case data::OpCodes::MovImmDerefReg:
				{
					u8 destRegAddr = fetch8();
					i16 value = fetch16();
					u16 addr = readRegister(destRegAddr);
					m_memory.write16(addr, value);
				}
				break;
				case data::OpCodes::MovDerefRegDerefReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					u16 srcAddr = readRegister(srcRegAddr);
					u16 destAddr = readRegister(destRegAddr);
					i16 value = m_memory.read16(srcAddr);
					m_memory.write16(destAddr, value);
				}
				break;
				case data::OpCodes::MovByteImmMem:
				{
					u16 addr = fetch16();
					i8 literal = fetch8();
					m_memory.write8(addr, literal);
				}
				break;
				case data::OpCodes::MovByteDerefRegMem:
				{
					u16 destAddr = fetch16();
					u8 srcRegAddr = fetch8();
					u16 addr = readRegister(srcRegAddr);
					i8 value = m_memory.read8(addr);
					m_memory.write8(destAddr, value);
				}
				break;
				case data::OpCodes::MovByteRegDerefReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					i16 value = readRegister(srcRegAddr);
					u16 addr = readRegister(destRegAddr);
					m_memory.write8(addr, (i8)value);
				}
				break;
				case data::OpCodes::MovByteMemDerefReg:
				{
					u8 destRegAddr = fetch8();
					u16 srcAddr = fetch16();
					i8 value = m_memory.read8(srcAddr);
					u16 addr = readRegister(destRegAddr);
					m_memory.write8(addr, value);
				}
				break;
				case data::OpCodes::MovByteImmDerefReg:
				{
					u8 destRegAddr = fetch8();
					i8 value = fetch8();
					u16 addr = readRegister(destRegAddr);
					m_memory.write8(addr, value);
				}
				break;
				case data::OpCodes::MovByteDerefRegDerefReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					u16 srcAddr = readRegister(srcRegAddr);
					u16 destAddr = readRegister(destRegAddr);
					i8 value = m_memory.read8(srcAddr);
					m_memory.write8(destAddr, value);
				}
				break;
				case data::OpCodes::MovByteMemReg:
				{
					u8 destRegAddr = fetch8();
					u16 srcAddr = fetch16();
					i8 value = m_memory.read8(srcAddr);
					writeRegister8(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovByteImmReg:
				{
					u8 destRegAddr = fetch8();
					i8 value = fetch8();
					writeRegister8(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovByteDerefRegReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					u16 srcAddr = readRegister(srcRegAddr);
					i8 value = m_memory.read8(srcAddr);
					writeRegister8(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovByteRegMem:
				{
					u16 addr = fetch16();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					m_memory.write8(addr, (i8)(value & 0x00FF));
				}
				break;
				case data::OpCodes::AddImmReg:
				{
					u8 regAddr = fetch8();
					i16 literal = fetch16();
					i16 regValue = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, regValue + literal);
				}
				break;
				case data::OpCodes::AddRegReg:
				{
					u8 regAddr1 = fetch8();
					u8 regAddr2 = fetch8();
					i16 regValue1 = readRegister(regAddr1);
					i16 regValue2 = readRegister(regAddr2);
					writeRegister16(data::Registers::ACC, regValue1 + regValue2);
				}
				break;
				case data::OpCodes::SubImmReg:
				{
					u8 regAddr = fetch8();
					i16 literal = fetch16();
					i16 regValue = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, regValue - literal);
				}
				break;
				case data::OpCodes::SubRegReg:
				{
					u8 regAddr1 = fetch8();
					u8 regAddr2 = fetch8();
					i16 regValue1 = readRegister(regAddr1);
					i16 regValue2 = readRegister(regAddr2);
					writeRegister16(data::Registers::ACC, regValue1 - regValue2);
				}
				break;
				case data::OpCodes::MulImmReg:
				{
					u8 regAddr = fetch8();
					i16 literal = fetch16();
					i16 regValue = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, regValue * literal);
				}
				break;
				case data::OpCodes::MulRegReg:
				{
					u8 regAddr1 = fetch8();
					u8 regAddr2 = fetch8();
					i16 regValue1 = readRegister(regAddr1);
					i16 regValue2 = readRegister(regAddr2);
					writeRegister16(data::Registers::ACC, regValue1 * regValue2);
				}
				break;
				case data::OpCodes::DivImmReg: //TODO: Division by zero is unhandled
				{
					u8 regAddr = fetch8();
					i16 literal = fetch16();
					i16 regValue = readRegister(regAddr);
					i16 quotient = regValue / literal;
					i16 reminder = regValue % literal;
					writeRegister16(data::Registers::ACC, quotient);
					writeRegister16(data::Registers::RV, reminder);
				}
				break;
				case data::OpCodes::DivRegReg: //TODO: Division by zero is unhandled
				{
					u8 regAddr1 = fetch8();
					u8 regAddr2 = fetch8();
					i16 regValue1 = readRegister(regAddr1);
					i16 regValue2 = readRegister(regAddr2);
					i16 quotient = regValue1 / regValue2;
					i16 reminder = regValue1 % regValue2;
					writeRegister16(data::Registers::ACC, quotient);
					writeRegister16(data::Registers::RV, reminder);
				}
				break;
				case data::OpCodes::IncReg:
				{
					u8 regAddr = fetch8();
					i16 regValue = readRegister(regAddr);
					writeRegister16(regAddr, regValue + 1);
				}
				break;
				case data::OpCodes::DecReg:
				{
					u8 regAddr = fetch8();
					i16 regValue = readRegister(regAddr);
					writeRegister16(regAddr, regValue - 1);
				}
				break;
				case data::OpCodes::RShiftRegImm:
				{
					i16 literal = fetch16();
					u8 regAddr = fetch8();
					i16 regValue = readRegister(regAddr);
					regValue = regValue >> literal;
					writeRegister16(regAddr, regValue);
				}
				break;
				case data::OpCodes::RShiftRegReg:
				{
					u8 shiftRegAddr = fetch8();
					u8 regAddr = fetch8();
					i16 shiftValue = readRegister(shiftRegAddr);
					i16 regValue = readRegister(regAddr);
					regValue = regValue >> shiftValue;
					writeRegister16(regAddr, regValue);
				}
				break;
				case data::OpCodes::LShiftRegImm:
				{
					i16 literal = fetch16();
					u8 regAddr = fetch8();
					i16 regValue = readRegister(regAddr);
					regValue = regValue << literal;
					writeRegister16(regAddr, regValue);
				}
				break;
				case data::OpCodes::LShiftRegReg:
				{
					u8 shiftRegAddr = fetch8();
					u8 regAddr = fetch8();
					i16 shiftValue = readRegister(shiftRegAddr);
					i16 regValue = readRegister(regAddr);
					regValue = regValue << shiftValue;
					writeRegister16(regAddr, regValue);
				}
				break;
				case data::OpCodes::AndRegImm:
				{
					i16 literal = fetch16();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, value & literal);
				}
				break;
				case data::OpCodes::AndRegReg:
				{
					u8 andRegAddr = fetch8();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					i16 andValue = readRegister(andRegAddr);
					writeRegister16(data::Registers::ACC, value & andValue);
				}
				break;
				case data::OpCodes::OrRegImm:
				{
					i16 literal = fetch16();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, value | literal);
				}
				break;
				case data::OpCodes::OrRegReg:
				{
					u8 andRegAddr = fetch8();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					i16 andValue = readRegister(andRegAddr);
					writeRegister16(data::Registers::ACC, value | andValue);
				}
				break;
				case data::OpCodes::XorRegImm:
				{
					i16 literal = fetch16();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, value ^ literal);
				}
				break;
				case data::OpCodes::XorRegReg:
				{
					u8 andRegAddr = fetch8();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					i16 andValue = readRegister(andRegAddr);
					writeRegister16(data::Registers::ACC, value ^ andValue);
				}
				break;
				case data::OpCodes::NotReg:
				{
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					writeRegister16(data::Registers::ACC, ~value);
				}
				break;
				case data::OpCodes::NegReg:
				{
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					value *= -1;
					writeRegister16(regAddr, value);
				}
				break;
				case data::OpCodes::NegByteReg:
				{
					u8 regAddr = fetch8();
					i8 value = (i8)readRegister(regAddr);
					value *= -1;
					writeRegister8(regAddr, value);
				}
				break;
				case data::OpCodes::JmpNotEqImm:
				{
					u16 addr = fetch16();
					i16 value = fetch16();
					i16 accValue = readRegister(data::Registers::ACC);
					if (value != accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpNotEqReg:
				{
					u16 addr = fetch16();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					i16 accValue = readRegister(data::Registers::ACC);
					if (value != accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpEqImm:
				{
					u16 addr = fetch16();
					i16 value = fetch16();
					i16 accValue = readRegister(data::Registers::ACC);
					if (value == accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpEqReg:
				{
					u16 addr = fetch16();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					i16 accValue = readRegister(data::Registers::ACC);
					if (value == accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGrImm:
				{
					u16 addr = fetch16();
					i16 value = fetch16();
					i16 accValue = readRegister(data::Registers::ACC);
					if (value > accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGrReg:
				{
					u16 addr = fetch16();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					i16 accValue = readRegister(data::Registers::ACC);
					if (value > accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLessImm:
				{
					u16 addr = fetch16();
					i16 value = fetch16();
					i16 accValue = readRegister(data::Registers::ACC);
					if (value < accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLessReg:
				{
					u16 addr = fetch16();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					i16 accValue = readRegister(data::Registers::ACC);
					if (value < accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGeImm:
				{
					u16 addr = fetch16();
					i16 value = fetch16();
					i16 accValue = readRegister(data::Registers::ACC);
					if (value >= accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGeReg:
				{
					u16 addr = fetch16();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					i16 accValue = readRegister(data::Registers::ACC);
					if (value >= accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLeImm:
				{
					u16 addr = fetch16();
					i16 value = fetch16();
					i16 accValue = readRegister(data::Registers::ACC);
					if (value <= accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLeReg:
				{
					u16 addr = fetch16();
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					i16 accValue = readRegister(data::Registers::ACC);
					if (value <= accValue)
						writeRegister16(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::Jmp:
				{
					u16 addr = fetch16();
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
					i16 value = fetch16();
					pushToStack(value);
				}
				break;
				case data::OpCodes::PushReg:
				{
					u8 regAddr = fetch8();
					i16 value = readRegister(regAddr);
					pushToStack(value);
				}
				break;
				case data::OpCodes::PopReg:
				{
					u8 regAddr = fetch8();
					i16 value = popFromStack();
					writeRegister16(regAddr, value);
				}
				break;
				case data::OpCodes::CallImm:
				{
					u16 subroutineAddr = fetch16();
					pushStackFrame();
					writeRegister16(data::Registers::IP, subroutineAddr);
					m_subroutineCounter++;
				}
				break;
				case data::OpCodes::CallReg:
				{
					u8 regAddr = fetch8();
					u16 subroutineAddr = readRegister(regAddr);
					pushStackFrame();
					writeRegister16(data::Registers::IP, subroutineAddr);
					m_subroutineCounter++;
				}
				break;
				case data::OpCodes::Ret:
				{
					popStackFrame();
					m_subroutineCounter--;
				}
				break;
				case data::OpCodes::ArgReg:
				{
					u8 regAddr = fetch8();
					if (!isInSubRoutine()) break;
					i16 pp_val = readRegister(data::Registers::PP);
					i16 arg_data = m_memory.read16(pp_val);
					writeRegister16(data::Registers::PP, pp_val - 2);
					writeRegister16(regAddr, arg_data);
				}
				break;
				case data::OpCodes::RetInt:
				{
					m_interruptHandlerCount--;
					popStackFrame();
					m_subroutineCounter--;
				}
				break;
				case data::OpCodes::Int:
				{
					u8 intValue = fetch8();
					if (!readFlag(data::Flags::InterruptsEnabled))
						return true;
					handleInterrupt(intValue, false);
					m_subroutineCounter++;
				}
				break;
				case data::OpCodes::ZeroFlag:
				{
					u8 flag = fetch8();
					setFlag(flag, false);
				}
				break;
				case data::OpCodes::SetFlag:
				{
					u8 flag = fetch8();
					setFlag(flag, true);
				}
				break;
				case data::OpCodes::ToggleFlag:
				{
					u8 flag = fetch8();
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
					data::ErrorHandler::pushError(data::ErrorCodes::CPU_UnsupportedExtension, String("Unsupported Extension: ").add(String::getHexStr(inst, true, 1)));
					m_halt = true;
					return false;
				}
				break;
				default:
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CPU_UnknownInstruction, String("Unknown instruction: ").add(String::getHexStr(inst, true, 1)));
					m_halt = true;
					return false;
				}
			}

			return true;
		}
	}
}
