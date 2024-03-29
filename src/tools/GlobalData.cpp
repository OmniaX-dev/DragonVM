#include "GlobalData.hpp"
#include "../runtime/DragonRuntime.hpp"

namespace dragon
{
	namespace data
	{
		ostd::String OpCodes::getOpCodeString(uint8_t opCode)
		{
			CPUExtension* ext = DragonRuntime::cpu.getCurrentCPUExtension();
			if (ext != nullptr)
				return ext->getOpCodeString(DragonRuntime::cpu.getCurrentCPUExtensionInstruction());
			switch (opCode)
			{
				case data::OpCodes::NoOp: return "NoOp";
				case data::OpCodes::DEBUG_Break: return "debug_break";
				case data::OpCodes::BIOSModeImm: return "BIOSModeImm";
				case data::OpCodes::MovImmReg: return "MovImmReg";
				case data::OpCodes::MovImmMem: return "MovImmMem";
				case data::OpCodes::MovRegReg: return "MovRegReg";
				case data::OpCodes::MovRegMem: return "MovRegMem";
				case data::OpCodes::MovMemReg: return "MovMemReg";
				case data::OpCodes::MovDerefRegReg: return "MovDerefRegReg";
				case data::OpCodes::MovDerefRegMem: return "MovDerefRegMem";
				case data::OpCodes::MovImmRegOffReg: return "MovImmRegOffReg";
				case data::OpCodes::MovRegDerefReg: return "MovRegDerefReg";
				case data::OpCodes::MovMemDerefReg: return "MovMemDerefReg";
				case data::OpCodes::MovImmDerefReg: return "MovImmDerefReg";
				case data::OpCodes::MovDerefRegDerefReg: return "MovDerefRegDerefReg";
				case data::OpCodes::MovByteImmMem: return "MovByteImmMem";
				case data::OpCodes::MovByteRegMem: return "MovByteRegMem";
				case data::OpCodes::MovByteDerefRegMem: return "MovByteDerefRegMem";
				case data::OpCodes::MovByteImmDerefReg: return "MovByteImmDerefReg";
				case data::OpCodes::MovByteRegDerefReg: return "MovByteRegDerefReg";
				case data::OpCodes::MovByteMemDerefReg: return "MovByteMemDerefReg";
				case data::OpCodes::MovByteDerefRegDerefReg: return "MovByteDerefRegDerefReg";
				case data::OpCodes::MovByteMemReg: return "MovByteMemReg";
				case data::OpCodes::MovByteImmReg: return "MovByteImmReg";
				case data::OpCodes::MovByteDerefRegReg: return "MovByteDerefRegReg";
				case data::OpCodes::AddImmReg: return "AddImmReg";
				case data::OpCodes::AddRegReg: return "AddRegReg";
				case data::OpCodes::SubImmReg: return "SubImmReg";
				case data::OpCodes::SubRegReg: return "SubRegReg";
				case data::OpCodes::MulImmReg: return "MulImmReg";
				case data::OpCodes::MulRegReg: return "MulRegReg";
				case data::OpCodes::DivImmReg: return "DivImmReg";
				case data::OpCodes::DivRegReg: return "DivRegReg";
				case data::OpCodes::IncReg: return "IncReg";
				case data::OpCodes::DecReg: return "DecReg";
				case data::OpCodes::RShiftRegImm: return "RShiftRegImm";
				case data::OpCodes::RShiftRegReg: return "RShiftRegReg";
				case data::OpCodes::LShiftRegImm: return "LShiftRegImm";
				case data::OpCodes::LShiftRegReg: return "LShiftRegReg";
				case data::OpCodes::AndRegImm: return "AndRegImm";
				case data::OpCodes::AndRegReg: return "AndRegReg";
				case data::OpCodes::OrRegImm: return "OrRegImm";
				case data::OpCodes::OrRegReg: return "OrRegReg";
				case data::OpCodes::XorRegImm: return "XorRegImm";
				case data::OpCodes::XorRegReg: return "XorRegReg";
				case data::OpCodes::NotReg: return "NotReg";
				case data::OpCodes::NegReg: return "NegReg";
				case data::OpCodes::NegByteReg: return "NegByteReg";
				case data::OpCodes::JmpNotEqImm: return "JmpNotEqImm";
				case data::OpCodes::JmpNotEqReg: return "JmpNotEqReg";
				case data::OpCodes::JmpEqImm: return "JmpEqImm";
				case data::OpCodes::JmpEqReg: return "JmpEqReg";
				case data::OpCodes::JmpGrImm: return "JmpGrImm";
				case data::OpCodes::JmpGrReg: return "JmpGrReg";
				case data::OpCodes::JmpLessImm: return "JmpLessImm";
				case data::OpCodes::JmpLessReg: return "JmpLessReg";
				case data::OpCodes::JmpGeImm: return "JmpGeImm";
				case data::OpCodes::JmpGeReg: return "JmpGeReg";
				case data::OpCodes::JmpLeImm: return "JmpLeImm";
				case data::OpCodes::JmpLeReg: return "JmpLeReg";
				case data::OpCodes::Jmp: return "Jmp";
				case data::OpCodes::Halt: return "Halt";
				case data::OpCodes::PushImm: return "PushImm";
				case data::OpCodes::PushReg: return "PushReg";
				case data::OpCodes::PopReg: return "PopReg";
				case data::OpCodes::CallImm: return "CallImm";
				case data::OpCodes::CallReg: return "CallReg";
				case data::OpCodes::Ret: return "Ret";
				case data::OpCodes::ArgReg: return "ArgReg";
				case data::OpCodes::RetInt: return "RetInt";
				case data::OpCodes::Int: return "Int";
				case data::OpCodes::Ext01: return "Ext01";
				case data::OpCodes::Ext02: return "Ext02";
				case data::OpCodes::Ext03: return "Ext03";
				case data::OpCodes::Ext04: return "Ext04";
				case data::OpCodes::Ext05: return "Ext05";
				case data::OpCodes::Ext06: return "Ext06";
				case data::OpCodes::Ext07: return "Ext07";
				case data::OpCodes::Ext08: return "Ext08";
				case data::OpCodes::Ext09: return "Ext09";
				case data::OpCodes::Ext10: return "Ext10";
				case data::OpCodes::Ext11: return "Ext11";
				case data::OpCodes::Ext12: return "Ext12";
				case data::OpCodes::Ext13: return "Ext13";
				case data::OpCodes::Ext14: return "Ext14";
				case data::OpCodes::Ext15: return "Ext15";
				case data::OpCodes::Ext16: return "Ext16";
				default: return "UNKNOWN_INST";
			}
		}

		uint8_t OpCodes::getInstructionSIze(uint8_t opCode)
		{
			CPUExtension* ext = DragonRuntime::cpu.getCurrentCPUExtension();
			if (ext != nullptr)
				return ext->getInstructionSIze(DragonRuntime::cpu.getCurrentCPUExtensionInstruction());
			switch (opCode)
			{
				case data::OpCodes::NoOp: return 1;
				case data::OpCodes::DEBUG_Break: return 1;
				case data::OpCodes::BIOSModeImm: return 2;
				case data::OpCodes::MovImmReg: return 4;
				case data::OpCodes::MovImmMem: return 5;
				case data::OpCodes::MovRegReg: return 3;
				case data::OpCodes::MovRegMem: return 4;
				case data::OpCodes::MovMemReg: return 4;
				case data::OpCodes::MovDerefRegReg: return 3;
				case data::OpCodes::MovDerefRegMem: return 4;
				case data::OpCodes::MovImmRegOffReg: return 5;
				case data::OpCodes::MovRegDerefReg: return 3;
				case data::OpCodes::MovMemDerefReg: return 4;
				case data::OpCodes::MovImmDerefReg: return 4;
				case data::OpCodes::MovDerefRegDerefReg: return 3;
				case data::OpCodes::MovByteImmMem: return 4;
				case data::OpCodes::MovByteRegMem: return 4;
				case data::OpCodes::MovByteDerefRegMem: return 4;
				case data::OpCodes::MovByteImmDerefReg: return 3;
				case data::OpCodes::MovByteRegDerefReg: return 3;
				case data::OpCodes::MovByteMemDerefReg: return 4;
				case data::OpCodes::MovByteDerefRegDerefReg: return 3;
				case data::OpCodes::MovByteMemReg: return 4;
				case data::OpCodes::MovByteImmReg: return 3;
				case data::OpCodes::MovByteDerefRegReg: return 3;
				case data::OpCodes::AddImmReg: return 4;
				case data::OpCodes::AddRegReg: return 3;
				case data::OpCodes::SubImmReg: return 4;
				case data::OpCodes::SubRegReg: return 3;
				case data::OpCodes::MulImmReg: return 4;
				case data::OpCodes::MulRegReg: return 3;
				case data::OpCodes::DivImmReg: return 4;
				case data::OpCodes::DivRegReg: return 3;
				case data::OpCodes::IncReg: return 2;
				case data::OpCodes::DecReg: return 2;
				case data::OpCodes::RShiftRegImm: return 4;
				case data::OpCodes::RShiftRegReg: return 3;
				case data::OpCodes::LShiftRegImm: return 4;
				case data::OpCodes::LShiftRegReg: return 3;
				case data::OpCodes::AndRegImm: return 4;
				case data::OpCodes::AndRegReg: return 3;
				case data::OpCodes::OrRegImm: return 4;
				case data::OpCodes::OrRegReg: return 3;
				case data::OpCodes::XorRegImm: return 4;
				case data::OpCodes::XorRegReg: return 3;
				case data::OpCodes::NotReg: return 2;
				case data::OpCodes::NegReg: return 2;
				case data::OpCodes::NegByteReg: return 2;
				case data::OpCodes::JmpNotEqImm: return 5;
				case data::OpCodes::JmpNotEqReg: return 4;
				case data::OpCodes::JmpEqImm: return 5;
				case data::OpCodes::JmpEqReg: return 4;
				case data::OpCodes::JmpGrImm: return 5;
				case data::OpCodes::JmpGrReg: return 4;
				case data::OpCodes::JmpLessImm: return 5;
				case data::OpCodes::JmpLessReg: return 4;
				case data::OpCodes::JmpGeImm: return 5;
				case data::OpCodes::JmpGeReg: return 4;
				case data::OpCodes::JmpLeImm: return 5;
				case data::OpCodes::JmpLeReg: return 4;
				case data::OpCodes::Jmp: return 3;
				case data::OpCodes::Halt: return 1;
				case data::OpCodes::PushImm: return 3;
				case data::OpCodes::PushReg: return 2;
				case data::OpCodes::PopReg: return 2;
				case data::OpCodes::CallImm: return 3;
				case data::OpCodes::CallReg: return 2;
				case data::OpCodes::Ret: return 1;
				case data::OpCodes::ArgReg: return 2;
				case data::OpCodes::RetInt: return 1;
				case data::OpCodes::Int: return 2;
				case data::OpCodes::Ext01: return 0;
				case data::OpCodes::Ext02: return 0;
				case data::OpCodes::Ext03: return 0;
				case data::OpCodes::Ext04: return 0;
				case data::OpCodes::Ext05: return 0;
				case data::OpCodes::Ext06: return 0;
				case data::OpCodes::Ext07: return 0;
				case data::OpCodes::Ext08: return 0;
				case data::OpCodes::Ext09: return 0;
				case data::OpCodes::Ext10: return 0;
				case data::OpCodes::Ext11: return 0;
				case data::OpCodes::Ext12: return 0;
				case data::OpCodes::Ext13: return 0;
				case data::OpCodes::Ext14: return 0;
				case data::OpCodes::Ext15: return 0;
				case data::OpCodes::Ext16: return 0;
				default: return 0;
			}
		}
	}
}