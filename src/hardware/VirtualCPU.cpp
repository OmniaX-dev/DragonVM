#include "VirtualCPU.hpp"
#include "../tools/GlobalData.hpp"
#include "../runtime/DragonRuntime.hpp"

#include <ostd/io/Memory.hpp>
#include <cstring>
#include <iostream>
#include <algorithm>

namespace dragon
{
	namespace hw
	{
		VirtualCPU::VirtualCPU(IMemoryDevice& memory) : m_memory(memory)
		{
			for (i32 i = 0; i < data::Registers::Last; i++)
				m_registers[i] = 0;
			for (i32 i = 0; i < 16; i++)
				m_extensions[i] = nullptr;
		}

		void VirtualCPU::setStackTop(u32 stackTop)
		{
			m_stackTop = stackTop;
			writeRegister32(data::Registers::SP, cast<i32>(stackTop));
			writeRegister32(data::Registers::FP, cast<i32>(stackTop));
		}

		void VirtualCPU::reset(void)
		{
			for (i32 i = 0; i < data::Registers::Last; i++)
				m_registers[i] = 0;
			m_stackFrameSize = 0;
			m_halt = false;
			m_currentInst = 0x00;
			m_currentAddr = 0x0000;
			m_supervisor = true;
			m_interruptHandlerCount = 0;
			std::memset(m_interruptInService, 0, sizeof(m_interruptInService));
			m_isDebugBreakPoint = false;
			m_ramDumped = false;
			m_subroutineCounter = 0;
			m_debugProfilerStarted = false;
			m_isOffsetAddressingEnabled = false;
			m_currentOffset = 0x0000;
			m_currentExtension = nullptr;
			m_currentExtInst = 0x00;
			writeRegister32(data::Registers::IP, 0x00000000);
			setStackTop(m_stackTop);
		}

		i32 VirtualCPU::readRegister(u8 reg)
		{
			if (!data::Registers::isValid(reg)) return 0x00000000; //TODO: Error
			const u32 value = cast<u32>(m_registers[reg & data::Registers::IndexMask]);
			switch (reg & data::Registers::ViewMask)
			{
				case data::Registers::ViewLow: return cast<i32>(value & 0xFFFF);
				case data::Registers::ViewHigh: return cast<i32>(value >> 16);
				default: return cast<i32>(value);
			}
		}

		void VirtualCPU::write_register(u8 reg, u32 value)
		{
			if (!data::Registers::isValid(reg)) return; //TODO: Error
			i32& r = m_registers[reg & data::Registers::IndexMask];
			if (!m_supervisor && (reg & data::Registers::IndexMask) == data::Registers::FL)
			{
				// User mode can't change the interrupt flag (the other flags: yes).
				u32 next = cast<u32>(r);
				switch (reg & data::Registers::ViewMask)
				{
					case data::Registers::ViewLow: next = (next & 0xFFFF0000u) | (value & 0xFFFF); break;
					case data::Registers::ViewHigh: next = (next & 0x0000FFFFu) | ((value & 0xFFFF) << 16); break;
					default: next = value; break;
				}
				if (((next ^ cast<u32>(r)) & (1u << data::Flags::InterruptsEnabled)) != 0 && !privileged("Changing the interrupt flag"))
					return;
			}
			switch (reg & data::Registers::ViewMask)
			{
				case data::Registers::ViewLow: r = cast<i32>((cast<u32>(r) & 0xFFFF0000u) | (value & 0xFFFF)); break;
				case data::Registers::ViewHigh: r = cast<i32>((cast<u32>(r) & 0x0000FFFFu) | ((value & 0xFFFF) << 16)); break;
				default: r = cast<i32>(value); break;
			}
		}

		i32 VirtualCPU::writeRegister32(u8 reg, i32 value)
		{
			write_register(reg, cast<u32>(value));
			return value;
		}

		i16 VirtualCPU::writeRegister16(u8 reg, i16 value)
		{
			write_register(reg, cast<u32>(cast<u16>(value)));
			return value;
		}

		i8 VirtualCPU::writeRegister8(u8 reg, i8 value)
		{
			write_register(reg, cast<u32>(cast<u8>(value)));
			return value;
		}

		i8 VirtualCPU::fetch8(void)
		{
			u32 nextInstAddr = cast<u32>(readRegister(data::Registers::IP));
			m_currentAddr = nextInstAddr;
			i8 inst = m_memory.read8(nextInstAddr);
			writeRegister32(data::Registers::IP, cast<i32>(nextInstAddr + 1));
			return inst;
		}

		i16 VirtualCPU::fetch16(void)
		{
			u32 nextInstAddr = cast<u32>(readRegister(data::Registers::IP));
			m_currentAddr = nextInstAddr;
			i16 inst = m_memory.read16(nextInstAddr);
			writeRegister32(data::Registers::IP, cast<i32>(nextInstAddr + 2));
			return inst;
		}

		i32 VirtualCPU::fetch32(void)
		{
			u32 nextInstAddr = cast<u32>(readRegister(data::Registers::IP));
			m_currentAddr = nextInstAddr;
			i32 inst = m_memory.read32(nextInstAddr);
			writeRegister32(data::Registers::IP, cast<i32>(nextInstAddr + data::WordSize));
			return inst;
		}

		void VirtualCPU::pushToStack(i32 value)
		{
			u32 stackAddr = cast<u32>(readRegister(data::Registers::SP));
			// The stack grows down from the stack top: SP is the next free word.
			if (!stack_allowed(stackAddr))
			{
				data::ErrorHandler::pushError(data::ErrorCodes::CPU_StackOverflow, String("Stack Overflow: SP = ").add(String::getHexStr(stackAddr, true, 4)));
				m_halt = true;
				return;
			}
			m_memory.write32(stackAddr, value);
			writeRegister32(data::Registers::SP, cast<i32>(stackAddr - data::WordSize));
			m_stackFrameSize += data::WordSize;
		}

		bool VirtualCPU::stack_allowed(u32 sp)
		{
			auto l_in = [sp](u32 top, u32 size) { return sp <= top && top - sp < size; };
			if (l_in(m_stackTop, cast<u32>(DragonRuntime::vCMOS.read32(data::CMOSRegisters::StackSize))))
				return true;
			using CR = VirtualCPUControl::tRegisters;
			const auto& control = DragonRuntime::vCPUControl;
			const u32 kernelTop = control.get(CR::KernelStackTop);
			if (kernelTop != 0 && l_in(kernelTop, control.get(CR::KernelStackSize)))
				return true;
			const u32 userTop = control.get(CR::UserStackTop);
			return userTop != 0 && l_in(userTop, control.get(CR::UserStackSize));
		}

		bool VirtualCPU::privileged(const char* what)
		{
			if (m_supervisor)
				return true;
			data::ErrorHandler::pushError(data::ErrorCodes::CPU_PrivilegedInstruction, String(what).add(" in user mode (a privileged instruction) at address: ").add(String::getHexStr(m_currentAddr, true, 4)));
			return false;
		}

		i32 VirtualCPU::popFromStack(void)
		{
			u32 nextSP = cast<u32>(readRegister(data::Registers::SP)) + data::WordSize;
			writeRegister32(data::Registers::SP, cast<i32>(nextSP));
			i32 value = m_memory.read32(nextSP);
			m_stackFrameSize -= data::WordSize;
			return value;
		}

		void VirtualCPU::pushStackFrame(void)
		{
			// The argument count was pushed last (right above SP); the arguments are above it.
			u32 argStartAddr = cast<u32>(readRegister(data::Registers::SP)) + data::WordSize;
			u32 argCount = cast<u32>(m_memory.read32(argStartAddr));
			if (argCount == 0)
				argStartAddr = 0;
			else
				argStartAddr += (argCount * data::WordSize);

			for (u8 n = 1; n <= data::Registers::GeneralCount; n++)
				pushToStack(readRegister(data::Registers::general(n)));
			pushToStack(readRegister(data::Registers::PP));
			pushToStack(readRegister(data::Registers::ACC));
			pushToStack(readRegister(data::Registers::IP));
			pushToStack(readRegister(data::Registers::FP));
			pushToStack(cast<i32>(m_stackFrameSize));

			writeRegister32(data::Registers::PP, cast<i32>(argStartAddr));
			writeRegister32(data::Registers::FP, readRegister(data::Registers::SP));
			m_stackFrameSize = 0;
		}

		void VirtualCPU::popStackFrame(void)
		{
			u32 framePointerAddr = cast<u32>(readRegister(data::Registers::FP));
			writeRegister32(data::Registers::SP, cast<i32>(framePointerAddr));
			m_stackFrameSize = cast<u32>(popFromStack());

			writeRegister32(data::Registers::FP, popFromStack());
			writeRegister32(data::Registers::IP, popFromStack());
			writeRegister32(data::Registers::ACC, popFromStack());
			writeRegister32(data::Registers::PP, popFromStack());
			for (u8 n = data::Registers::GeneralCount; n >= 1; n--)
				writeRegister32(data::Registers::general(n), popFromStack());

			u32 nArgs = cast<u32>(popFromStack());
			for (u32 i = 0; i < nArgs; i++)
				popFromStack();
		}

		bool VirtualCPU::readFlag(u8 flg)
		{
			if (flg >= 32) return false;
			m_tempFlags.value = cast<u32>(readRegister(data::Registers::FL));
			return ostd::Bits::get(m_tempFlags, flg);
		}

		void VirtualCPU::setFlag(u8 flg, bool val)
		{
			if (flg >= 32) return;
			m_tempFlags.value = cast<u32>(readRegister(data::Registers::FL));
			ostd::Bits::val(m_tempFlags, flg, val);
			writeRegister32(data::Registers::FL, cast<i32>(m_tempFlags.value));
		}

		bool VirtualCPU::handleInterrupt(u8 intValue, bool hardware)
		{
			// int in user mode is a system call: it works while interrupts are disabled too (only
			// the kernel can disable them), but not on the fault vectors (they are the CPU's).
			const bool systemCall = !hardware && !m_supervisor;
			if (!systemCall && !readFlag(data::Flags::InterruptsEnabled))
				return false;
			if (systemCall && intValue <= data::InterruptCodes::LastFault)
			{
				privileged("int to a fault vector");
				return false;
			}
			u32 entryPointer = data::MemoryMapAddresses::IntVector_Start + (intValue * data::MemoryMapAddresses::IntVector_EntrySize);
			u8 interruptStatus = cast<u8>(m_memory.read8(entryPointer));
			if (interruptStatus != 0xFF) return false;
			// A hardware interrupt comes between any two instructions, so it also keeps what the
			// stack frame doesn't (rti puts it back): RV (e.g. between a call and the mov that
			// reads its return value), S1 / S2 (low blocks), OFFSET and FL. A software interrupt
			// (int) is a call: it can return a value in RV (and flags).
			trap(intValue, hardware);
			return true;
		}

		void VirtualCPU::trap(u8 vector, bool saveContext)
		{
			u32 entryPointer = data::MemoryMapAddresses::IntVector_Start + (vector * data::MemoryMapAddresses::IntVector_EntrySize);
			u32 handlerAddress = cast<u32>(m_memory.read32(entryPointer + 1));
			const bool fromUser = !m_supervisor;
			const i32 previousSP = readRegister(data::Registers::SP);
			m_supervisor = true;
			if (fromUser)
			{
				const u32 kernelTop = DragonRuntime::vCPUControl.get(VirtualCPUControl::tRegisters::KernelStackTop);
				if (kernelTop != 0)
					writeRegister32(data::Registers::SP, cast<i32>(kernelTop));
			}
			pushToStack(previousSP);
			pushToStack(fromUser ? 0 : 1);
			if (saveContext)
			{
				pushToStack(readRegister(data::Registers::RV));
				pushToStack(readRegister(data::Registers::S1));
				pushToStack(readRegister(data::Registers::S2));
				pushToStack(readRegister(data::Registers::OFFSET));
				pushToStack(readRegister(data::Registers::FL));
			}
			pushToStack(saveContext ? HardwareInterruptMarker : 0);
			pushToStack(0);
			pushStackFrame();
			m_subroutineCounter++;
			m_interruptHandlerCount++;
			if (m_interruptInService[vector] == 0)
				m_interruptInService[vector] = cast<u16>(std::min(m_interruptHandlerCount, 0xFFFF));
			writeRegister32(data::Registers::IP, cast<i32>(handlerAddress));
			if (m_interruptListener)
				m_interruptListener(vector, saveContext, handlerAddress);
		}

		bool VirtualCPU::raise_fault(u32 instructionAddress, size_t firstError)
		{
			const auto error = data::ErrorHandler::at(firstError);
			u8 vector = data::InterruptCodes::Fault_General;
			bool memory = false;
			switch (error.code)
			{
				case data::ErrorCodes::CPU_DivisionByZero: vector = data::InterruptCodes::Fault_DivisionByZero; break;
				case data::ErrorCodes::CPU_UnknownInstruction:
				case data::ErrorCodes::CPU_UnsupportedExtension: vector = data::InterruptCodes::Fault_InvalidInstruction; break;
				case data::ErrorCodes::CPU_PrivilegedInstruction: vector = data::InterruptCodes::Fault_PrivilegedInstruction; break;
				case data::ErrorCodes::CPU_StackOverflow: vector = data::InterruptCodes::Fault_StackOverflow; break;
				default:
				{
					// The memory map and the devices' registers (protected, read-only, missing).
					switch (error.code >> 60)
					{
						case 0x0: case 0x1: case 0x3: case 0x5: case 0x6: case 0x7: case 0x8: case 0x9: case 0xA: case 0xB:
							vector = data::InterruptCodes::Fault_AccessViolation;
							memory = true;
						break;
						default: break;
					}
				}
				break;
			}
			const u32 entryPointer = data::MemoryMapAddresses::IntVector_Start + (vector * data::MemoryMapAddresses::IntVector_EntrySize);
			if (cast<u8>(m_memory.read8(entryPointer)) != 0xFF)
				return false;
			using CR = VirtualCPUControl::tRegisters;
			auto& control = DragonRuntime::vCPUControl;
			control.set(CR::FaultVector, vector);
			control.set(CR::FaultIP, instructionAddress);
			control.set(CR::FaultAddress, memory ? error.address : instructionAddress);
			data::ErrorHandler::resize(firstError);
			m_halt = false;
			// The handler returns to the instruction (rti): it can fix the cause, or change the
			// return address in the stack frame, or never return (killing the program).
			writeRegister32(data::Registers::IP, cast<i32>(instructionAddress));
			trap(vector, true);
			return true;
		}

		bool VirtualCPU::check_divisor(i32 divisor)
		{
			if (divisor != 0) return true;
			data::ErrorHandler::pushError(data::ErrorCodes::CPU_DivisionByZero, String("Division by zero at address: ").add(String::getHexStr(m_currentAddr, true, 4)));
			m_halt = true;
			return false;
		}

		data::CPUExtension* VirtualCPU::getExtension(u8 opCode) const
		{
			for (i32 i = 0; i < 16; i++)
			{
				if (m_extensions[i] != nullptr && m_extensions[i]->m_code == opCode)
					return m_extensions[i];
			}
			return nullptr;
		}

		void VirtualCPU::saveState(tState& outState) const
		{
			std::memset(&outState, 0, sizeof(tState));
			std::memcpy(outState.registers, m_registers, sizeof(outState.registers));
			outState.stackFrameSize = m_stackFrameSize;
			outState.currentAddr = m_currentAddr;
			outState.currentOffset = m_currentOffset;
			outState.interruptHandlerCount = m_interruptHandlerCount;
			outState.subroutineCounter = m_subroutineCounter;
			std::memcpy(outState.interruptInService, m_interruptInService, sizeof(m_interruptInService));
			outState.currentInst = m_currentInst;
			outState.currentExtInst = m_currentExtInst;
			outState.currentExtension = -1;
			for (i32 i = 0; i < 16; i++)
			{
				if (m_currentExtension != nullptr && m_extensions[i] == m_currentExtension)
					outState.currentExtension = cast<i8>(i);
			}
			outState.halt = m_halt;
			outState.supervisor = m_supervisor;
			outState.isDebugBreakPoint = m_isDebugBreakPoint;
			outState.ramDumped = m_ramDumped;
			outState.isOffsetAddressingEnabled = m_isOffsetAddressingEnabled;
			outState.debugProfilerStarted = m_debugProfilerStarted;
		}

		void VirtualCPU::loadState(const tState& state)
		{
			std::memcpy(m_registers, state.registers, sizeof(state.registers));
			m_stackFrameSize = state.stackFrameSize;
			m_currentAddr = state.currentAddr;
			m_currentOffset = state.currentOffset;
			m_interruptHandlerCount = state.interruptHandlerCount;
			m_subroutineCounter = state.subroutineCounter;
			std::memcpy(m_interruptInService, state.interruptInService, sizeof(m_interruptInService));
			m_currentInst = state.currentInst;
			m_currentExtInst = state.currentExtInst;
			m_currentExtension = (state.currentExtension >= 0 && state.currentExtension < 16 ? m_extensions[state.currentExtension] : nullptr);
			m_halt = state.halt;
			m_supervisor = state.supervisor;
			m_isDebugBreakPoint = state.isDebugBreakPoint;
			m_ramDumped = state.ramDumped;
			m_isOffsetAddressingEnabled = state.isOffsetAddressingEnabled;
			m_debugProfilerStarted = state.debugProfilerStarted;
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

		void VirtualCPU::debugPrint(u32 addr)
		{
			// The zero-terminated string at addr, to stdout (at most DebugPrintMaxLength bytes).
			std::string str;
			for (u32 i = 0; i < DebugPrintMaxLength; i++)
			{
				char c = cast<char>(m_memory.read8(addr + i));
				if (c == 0) break;
				str += c;
			}
			if (m_debugPrintHandler)
				m_debugPrintHandler(str);
			else
				std::cout << str << std::flush;
		}

		bool VirtualCPU::execute(void)
		{
			const bool user = !m_supervisor;
			const u32 instructionAddress = cast<u32>(readRegister(data::Registers::IP));
			const size_t errors = data::ErrorHandler::count();
			const bool result = execute_instruction();
			if (user && data::ErrorHandler::count() > errors && raise_fault(instructionAddress, errors))
				return true;
			return result;
		}

		bool VirtualCPU::execute_instruction(void)
		{
			// System signals (CMOS). Read from the CMOS itself, not through the memory map: it
			// is checked before every instruction, and would take the RAM's place in the
			// mapper's last-region cache. Before the halt check: a halted CPU can be rebooted.
			ostd::BitField_16 systemSignals;
			systemSignals.value = cast<u16>(DragonRuntime::vCMOS.read16(data::CMOSRegisters::SystemSignals));
			if (ostd::Bits::get(systemSignals, data::SystemSignals::Reboot))
			{
				DragonRuntime::resetMachine();
				return true;
			}
			if (m_halt) return true;
			m_currentExtension = nullptr;
			m_currentExtInst = 0x00;
			m_isDebugBreakPoint = false;
			m_ramDumped = false;
			m_isOffsetAddressingEnabled = readFlag(data::Flags::OffsetModeEnabled);
			if (m_isOffsetAddressingEnabled)
				m_currentOffset = readRegister(data::Registers::OFFSET);
			else
				m_currentOffset = 0x00000000;
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
				case data::OpCodes::DEBUG_PrintImm:
				case data::OpCodes::DEBUG_PrintReg:
				{
					u32 addr = (inst == data::OpCodes::DEBUG_PrintImm) ? cast<u32>(fetch32()) : cast<u32>(readRegister(fetch8()));
					debugPrint(addr);
				}
				break;
				case data::OpCodes::DEBUG_DrxLoad:
				{
					const u32 record = cast<u32>(readRegister(fetch8()));
					if (m_drxListener)
					{
						const u32 base = cast<u32>(m_memory.read32(record));
						const u32 size = cast<u32>(m_memory.read32(record + 4));
						const u64 id = (cast<u64>(cast<u32>(m_memory.read32(record + 8))) << 32) | cast<u32>(m_memory.read32(record + 12));
						m_drxListener(true, base, size, id);
					}
				}
				break;
				case data::OpCodes::DEBUG_DrxUnload:
				{
					const u32 base = cast<u32>(readRegister(fetch8()));
					if (m_drxListener)
						m_drxListener(false, base, 0, 0);
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
				case data::OpCodes::ModeImm:
				{
					i8 value = fetch8();
					if (!privileged("umode"))
						break;
					if (value == 0)
						m_supervisor = false;
				}
				break;
				case data::OpCodes::MovImmReg:
				{
					u8 regAddr = fetch8();
					i32 literal = fetch32();
					writeRegister32(regAddr, literal);
				}
				break;
				case data::OpCodes::MovImmMem:
				{
					u32 addr = fetch32();
					i32 literal = fetch32();
					m_memory.write32(addr, literal);
				}
				break;
				case data::OpCodes::MovRegReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					i32 value = readRegister(srcRegAddr);
					writeRegister32(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovRegMem:
				{
					u32 addr = fetch32();
					u8 srcRegAddr = fetch8();
					i32 value = readRegister(srcRegAddr);
					m_memory.write32(addr, value);
				}
				break;
				case data::OpCodes::MovMemReg:
				{
					u8 destRegAddr = fetch8();
					u32 addr = fetch32();
					i32 value = m_memory.read32(addr);
					writeRegister32(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovDerefRegReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					u32 addr = readRegister(srcRegAddr);
					i32 value = m_memory.read32(addr);
					writeRegister32(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovDerefRegMem:
				{
					u32 destAddr = fetch32();
					u8 srcRegAddr = fetch8();
					u32 addr = readRegister(srcRegAddr);
					i32 value = m_memory.read32(addr);
					m_memory.write32(destAddr, value);
				}
				break;
				case data::OpCodes::MovRegDerefReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					i32 value = readRegister(srcRegAddr);
					u32 addr = readRegister(destRegAddr);
					m_memory.write32(addr, value);
				}
				break;
				case data::OpCodes::MovMemDerefReg:
				{
					u8 destRegAddr = fetch8();
					u32 srcAddr = fetch32();
					i32 value = m_memory.read32(srcAddr);
					u32 addr = readRegister(destRegAddr);
					m_memory.write32(addr, value);
				}
				break;
				case data::OpCodes::MovImmDerefReg:
				{
					u8 destRegAddr = fetch8();
					i32 value = fetch32();
					u32 addr = readRegister(destRegAddr);
					m_memory.write32(addr, value);
				}
				break;
				case data::OpCodes::MovDerefRegDerefReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					u32 srcAddr = readRegister(srcRegAddr);
					u32 destAddr = readRegister(destRegAddr);
					i32 value = m_memory.read32(srcAddr);
					m_memory.write32(destAddr, value);
				}
				break;
				case data::OpCodes::MovByteImmMem:
				{
					u32 addr = fetch32();
					i8 literal = fetch8();
					m_memory.write8(addr, literal);
				}
				break;
				case data::OpCodes::MovByteDerefRegMem:
				{
					u32 destAddr = fetch32();
					u8 srcRegAddr = fetch8();
					u32 addr = readRegister(srcRegAddr);
					i8 value = m_memory.read8(addr);
					m_memory.write8(destAddr, value);
				}
				break;
				case data::OpCodes::MovByteRegDerefReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					i32 value = readRegister(srcRegAddr);
					u32 addr = readRegister(destRegAddr);
					m_memory.write8(addr, (i8)value);
				}
				break;
				case data::OpCodes::MovByteMemDerefReg:
				{
					u8 destRegAddr = fetch8();
					u32 srcAddr = fetch32();
					i8 value = m_memory.read8(srcAddr);
					u32 addr = readRegister(destRegAddr);
					m_memory.write8(addr, value);
				}
				break;
				case data::OpCodes::MovByteImmDerefReg:
				{
					u8 destRegAddr = fetch8();
					i8 value = fetch8();
					u32 addr = readRegister(destRegAddr);
					m_memory.write8(addr, value);
				}
				break;
				case data::OpCodes::MovByteDerefRegDerefReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					u32 srcAddr = readRegister(srcRegAddr);
					u32 destAddr = readRegister(destRegAddr);
					i8 value = m_memory.read8(srcAddr);
					m_memory.write8(destAddr, value);
				}
				break;
				case data::OpCodes::MovByteMemReg:
				{
					u8 destRegAddr = fetch8();
					u32 srcAddr = fetch32();
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
					u32 srcAddr = readRegister(srcRegAddr);
					i8 value = m_memory.read8(srcAddr);
					writeRegister8(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovByteRegMem:
				{
					u32 addr = fetch32();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					m_memory.write8(addr, (i8)(value & 0x00FF));
				}
				break;
				case data::OpCodes::MovWordImmMem:
				{
					u32 addr = cast<u32>(fetch32());
					i16 literal = fetch16();
					m_memory.write16(addr, literal);
				}
				break;
				case data::OpCodes::MovWordDerefRegMem:
				{
					u32 destAddr = cast<u32>(fetch32());
					u8 srcRegAddr = fetch8();
					u32 addr = cast<u32>(readRegister(srcRegAddr));
					m_memory.write16(destAddr, m_memory.read16(addr));
				}
				break;
				case data::OpCodes::MovWordImmDerefReg:
				{
					u8 destRegAddr = fetch8();
					i16 value = fetch16();
					u32 addr = cast<u32>(readRegister(destRegAddr));
					m_memory.write16(addr, value);
				}
				break;
				case data::OpCodes::MovWordRegDerefReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					i32 value = readRegister(srcRegAddr);
					u32 addr = cast<u32>(readRegister(destRegAddr));
					m_memory.write16(addr, cast<i16>(value));
				}
				break;
				case data::OpCodes::MovWordMemDerefReg:
				{
					u8 destRegAddr = fetch8();
					u32 srcAddr = cast<u32>(fetch32());
					i16 value = m_memory.read16(srcAddr);
					u32 addr = cast<u32>(readRegister(destRegAddr));
					m_memory.write16(addr, value);
				}
				break;
				case data::OpCodes::MovWordDerefRegDerefReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					u32 srcAddr = cast<u32>(readRegister(srcRegAddr));
					u32 destAddr = cast<u32>(readRegister(destRegAddr));
					m_memory.write16(destAddr, m_memory.read16(srcAddr));
				}
				break;
				case data::OpCodes::MovWordMemReg:
				{
					u8 destRegAddr = fetch8();
					u32 srcAddr = cast<u32>(fetch32());
					writeRegister16(destRegAddr, m_memory.read16(srcAddr));
				}
				break;
				case data::OpCodes::MovWordImmReg:
				{
					u8 destRegAddr = fetch8();
					i16 value = fetch16();
					writeRegister16(destRegAddr, value);
				}
				break;
				case data::OpCodes::MovWordDerefRegReg:
				{
					u8 destRegAddr = fetch8();
					u8 srcRegAddr = fetch8();
					u32 srcAddr = cast<u32>(readRegister(srcRegAddr));
					writeRegister16(destRegAddr, m_memory.read16(srcAddr));
				}
				break;
				case data::OpCodes::MovWordRegMem:
				{
					u32 addr = cast<u32>(fetch32());
					u8 regAddr = fetch8();
					m_memory.write16(addr, cast<i16>(readRegister(regAddr)));
				}
				break;
				case data::OpCodes::AddImmReg:
				{
					u8 regAddr = fetch8();
					i32 literal = fetch32();
					i32 regValue = readRegister(regAddr);
					writeRegister32(data::Registers::ACC, cast<i32>(cast<u32>(regValue) + cast<u32>(literal)));
				}
				break;
				case data::OpCodes::AddRegReg:
				{
					u8 regAddr1 = fetch8();
					u8 regAddr2 = fetch8();
					i32 regValue1 = readRegister(regAddr1);
					i32 regValue2 = readRegister(regAddr2);
					writeRegister32(data::Registers::ACC, cast<i32>(cast<u32>(regValue1) + cast<u32>(regValue2)));
				}
				break;
				case data::OpCodes::SubImmReg:
				{
					u8 regAddr = fetch8();
					i32 literal = fetch32();
					i32 regValue = readRegister(regAddr);
					writeRegister32(data::Registers::ACC, cast<i32>(cast<u32>(regValue) - cast<u32>(literal)));
				}
				break;
				case data::OpCodes::SubRegReg:
				{
					u8 regAddr1 = fetch8();
					u8 regAddr2 = fetch8();
					i32 regValue1 = readRegister(regAddr1);
					i32 regValue2 = readRegister(regAddr2);
					writeRegister32(data::Registers::ACC, cast<i32>(cast<u32>(regValue1) - cast<u32>(regValue2)));
				}
				break;
				case data::OpCodes::MulImmReg:
				{
					u8 regAddr = fetch8();
					i32 literal = fetch32();
					i32 regValue = readRegister(regAddr);
					writeRegister32(data::Registers::ACC, cast<i32>(cast<u32>(regValue) * cast<u32>(literal)));
				}
				break;
				case data::OpCodes::MulRegReg:
				{
					u8 regAddr1 = fetch8();
					u8 regAddr2 = fetch8();
					i32 regValue1 = readRegister(regAddr1);
					i32 regValue2 = readRegister(regAddr2);
					writeRegister32(data::Registers::ACC, cast<i32>(cast<u32>(regValue1) * cast<u32>(regValue2)));
				}
				break;
				case data::OpCodes::DivImmReg: //TODO: Division by zero is unhandled
				{
					u8 regAddr = fetch8();
					i32 literal = fetch32();
					i32 regValue = readRegister(regAddr);
					if (!check_divisor(literal)) return false;
					i32 quotient = cast<i32>(cast<i64>(regValue) / literal);
					i32 reminder = cast<i32>(cast<i64>(regValue) % literal);
					writeRegister32(data::Registers::ACC, quotient);
					writeRegister32(data::Registers::RV, reminder);
				}
				break;
				case data::OpCodes::DivRegReg: //TODO: Division by zero is unhandled
				{
					u8 regAddr1 = fetch8();
					u8 regAddr2 = fetch8();
					i32 regValue1 = readRegister(regAddr1);
					i32 regValue2 = readRegister(regAddr2);
					if (!check_divisor(regValue2)) return false;
					i32 quotient = cast<i32>(cast<i64>(regValue1) / regValue2);
					i32 reminder = cast<i32>(cast<i64>(regValue1) % regValue2);
					writeRegister32(data::Registers::ACC, quotient);
					writeRegister32(data::Registers::RV, reminder);
				}
				break;
				case data::OpCodes::IncReg:
				{
					u8 regAddr = fetch8();
					i32 regValue = readRegister(regAddr);
					writeRegister32(regAddr, cast<i32>(cast<u32>(regValue) + 1));
				}
				break;
				case data::OpCodes::DecReg:
				{
					u8 regAddr = fetch8();
					i32 regValue = readRegister(regAddr);
					writeRegister32(regAddr, cast<i32>(cast<u32>(regValue) - 1));
				}
				break;
				case data::OpCodes::RShiftRegImm:
				{
					i32 literal = fetch32();
					u8 regAddr = fetch8();
					i32 regValue = readRegister(regAddr);
					regValue = regValue >> (literal & 0x1F);
					writeRegister32(regAddr, regValue);
				}
				break;
				case data::OpCodes::RShiftRegReg:
				{
					u8 shiftRegAddr = fetch8();
					u8 regAddr = fetch8();
					i32 shiftValue = readRegister(shiftRegAddr);
					i32 regValue = readRegister(regAddr);
					regValue = regValue >> (shiftValue & 0x1F);
					writeRegister32(regAddr, regValue);
				}
				break;
				case data::OpCodes::LShiftRegImm:
				{
					i32 literal = fetch32();
					u8 regAddr = fetch8();
					i32 regValue = readRegister(regAddr);
					regValue = cast<i32>(cast<u32>(regValue) << (literal & 0x1F));
					writeRegister32(regAddr, regValue);
				}
				break;
				case data::OpCodes::LShiftRegReg:
				{
					u8 shiftRegAddr = fetch8();
					u8 regAddr = fetch8();
					i32 shiftValue = readRegister(shiftRegAddr);
					i32 regValue = readRegister(regAddr);
					regValue = cast<i32>(cast<u32>(regValue) << (shiftValue & 0x1F));
					writeRegister32(regAddr, regValue);
				}
				break;
				case data::OpCodes::AndRegImm:
				{
					i32 literal = fetch32();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					writeRegister32(data::Registers::ACC, value & literal);
				}
				break;
				case data::OpCodes::AndRegReg:
				{
					u8 andRegAddr = fetch8();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					i32 andValue = readRegister(andRegAddr);
					writeRegister32(data::Registers::ACC, value & andValue);
				}
				break;
				case data::OpCodes::OrRegImm:
				{
					i32 literal = fetch32();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					writeRegister32(data::Registers::ACC, value | literal);
				}
				break;
				case data::OpCodes::OrRegReg:
				{
					u8 andRegAddr = fetch8();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					i32 andValue = readRegister(andRegAddr);
					writeRegister32(data::Registers::ACC, value | andValue);
				}
				break;
				case data::OpCodes::XorRegImm:
				{
					i32 literal = fetch32();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					writeRegister32(data::Registers::ACC, value ^ literal);
				}
				break;
				case data::OpCodes::XorRegReg:
				{
					u8 andRegAddr = fetch8();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					i32 andValue = readRegister(andRegAddr);
					writeRegister32(data::Registers::ACC, value ^ andValue);
				}
				break;
				case data::OpCodes::NotReg:
				{
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					writeRegister32(data::Registers::ACC, ~value);
				}
				break;
				case data::OpCodes::NegReg:
				{
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					value = cast<i32>(0u - cast<u32>(value));
					writeRegister32(regAddr, value);
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
					u32 addr = fetch32();
					i32 value = fetch32();
					i32 accValue = readRegister(data::Registers::ACC);
					if (value != accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpNotEqReg:
				{
					u32 addr = fetch32();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					i32 accValue = readRegister(data::Registers::ACC);
					if (value != accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpEqImm:
				{
					u32 addr = fetch32();
					i32 value = fetch32();
					i32 accValue = readRegister(data::Registers::ACC);
					if (value == accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpEqReg:
				{
					u32 addr = fetch32();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					i32 accValue = readRegister(data::Registers::ACC);
					if (value == accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGrImm:
				{
					u32 addr = fetch32();
					i32 value = fetch32();
					i32 accValue = readRegister(data::Registers::ACC);
					if (value > accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGrReg:
				{
					u32 addr = fetch32();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					i32 accValue = readRegister(data::Registers::ACC);
					if (value > accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLessImm:
				{
					u32 addr = fetch32();
					i32 value = fetch32();
					i32 accValue = readRegister(data::Registers::ACC);
					if (value < accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLessReg:
				{
					u32 addr = fetch32();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					i32 accValue = readRegister(data::Registers::ACC);
					if (value < accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGeImm:
				{
					u32 addr = fetch32();
					i32 value = fetch32();
					i32 accValue = readRegister(data::Registers::ACC);
					if (value >= accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGeReg:
				{
					u32 addr = fetch32();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					i32 accValue = readRegister(data::Registers::ACC);
					if (value >= accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLeImm:
				{
					u32 addr = fetch32();
					i32 value = fetch32();
					i32 accValue = readRegister(data::Registers::ACC);
					if (value <= accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLeReg:
				{
					u32 addr = fetch32();
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					i32 accValue = readRegister(data::Registers::ACC);
					if (value <= accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGrUImm:
				{
					u32 addr = fetch32();
					u32 value = fetch32();
					u32 accValue = readRegister(data::Registers::ACC);
					if (value > accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGrUReg:
				{
					u32 addr = fetch32();
					u8 regAddr = fetch8();
					u32 value = readRegister(regAddr);
					u32 accValue = readRegister(data::Registers::ACC);
					if (value > accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLessUImm:
				{
					u32 addr = fetch32();
					u32 value = fetch32();
					u32 accValue = readRegister(data::Registers::ACC);
					if (value < accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLessUReg:
				{
					u32 addr = fetch32();
					u8 regAddr = fetch8();
					u32 value = readRegister(regAddr);
					u32 accValue = readRegister(data::Registers::ACC);
					if (value < accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGeUImm:
				{
					u32 addr = fetch32();
					u32 value = fetch32();
					u32 accValue = readRegister(data::Registers::ACC);
					if (value >= accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpGeUReg:
				{
					u32 addr = fetch32();
					u8 regAddr = fetch8();
					u32 value = readRegister(regAddr);
					u32 accValue = readRegister(data::Registers::ACC);
					if (value >= accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLeUImm:
				{
					u32 addr = fetch32();
					u32 value = fetch32();
					u32 accValue = readRegister(data::Registers::ACC);
					if (value <= accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::JmpLeUReg:
				{
					u32 addr = fetch32();
					u8 regAddr = fetch8();
					u32 value = readRegister(regAddr);
					u32 accValue = readRegister(data::Registers::ACC);
					if (value <= accValue)
						writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::Jmp:
				{
					u32 addr = fetch32();
					writeRegister32(data::Registers::IP, addr);
				}
				break;
				case data::OpCodes::Halt:
				{
					if (!privileged("hlt"))
						break;
					m_halt = true;
					return true;
				}
				break;
				case data::OpCodes::PushImm:
				{
					i32 value = fetch32();
					pushToStack(value);
				}
				break;
				case data::OpCodes::PushReg:
				{
					u8 regAddr = fetch8();
					i32 value = readRegister(regAddr);
					pushToStack(value);
				}
				break;
				case data::OpCodes::PopReg:
				{
					u8 regAddr = fetch8();
					i32 value = popFromStack();
					writeRegister32(regAddr, value);
				}
				break;
				case data::OpCodes::CallImm:
				{
					u32 subroutineAddr = fetch32();
					pushStackFrame();
					writeRegister32(data::Registers::IP, subroutineAddr);
					m_subroutineCounter++;
				}
				break;
				case data::OpCodes::CallReg:
				{
					u8 regAddr = fetch8();
					u32 subroutineAddr = readRegister(regAddr);
					pushStackFrame();
					writeRegister32(data::Registers::IP, subroutineAddr);
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
					u32 pp_val = cast<u32>(readRegister(data::Registers::PP));
					i32 arg_data = m_memory.read32(pp_val);
					writeRegister32(data::Registers::PP, cast<i32>(pp_val - data::WordSize));
					writeRegister32(regAddr, arg_data);
				}
				break;
				case data::OpCodes::RetInt:
				{
					if (!privileged("rti"))
						break;
					m_interruptHandlerCount = std::max(0, m_interruptHandlerCount - 1);
					for (i32 i = 0; i < 256; i++)
					{
						if (m_interruptInService[i] > m_interruptHandlerCount)
							m_interruptInService[i] = 0;
					}
					popStackFrame();
					if (cast<u32>(popFromStack()) == HardwareInterruptMarker)
					{
						writeRegister32(data::Registers::FL, popFromStack());
						writeRegister32(data::Registers::OFFSET, popFromStack());
						writeRegister32(data::Registers::S2, popFromStack());
						writeRegister32(data::Registers::S1, popFromStack());
						writeRegister32(data::Registers::RV, popFromStack());
					}
					// The mode and the stack last: back to the user's stack (from the kernel's).
					const bool supervisor = popFromStack() != 0;
					const i32 previousSP = popFromStack();
					m_supervisor = supervisor;
					writeRegister32(data::Registers::SP, previousSP);
					m_subroutineCounter--;
				}
				break;
				case data::OpCodes::IntImm:
				{
					u8 intValue = fetch8();
					handleInterrupt(intValue, false);
				}
				break;
				case data::OpCodes::IntReg:
				{
					u8 regAddr = fetch8();
					u32 intValue = readRegister(regAddr);
					handleInterrupt(intValue, false);
				}
				break;
				case data::OpCodes::ZeroFlag:
				{
					u8 flag = fetch8();
					if (flag == data::Flags::InterruptsEnabled && !privileged("zflg (the interrupt flag)"))
						break;
					setFlag(flag, false);
				}
				break;
				case data::OpCodes::SetFlag:
				{
					u8 flag = fetch8();
					if (flag == data::Flags::InterruptsEnabled && !privileged("sflg (the interrupt flag)"))
						break;
					setFlag(flag, true);
				}
				break;
				case data::OpCodes::ToggleFlag:
				{
					u8 flag = fetch8();
					if (flag == data::Flags::InterruptsEnabled && !privileged("tflg (the interrupt flag)"))
						break;
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
