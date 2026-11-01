// drun: runs a machine without a window (tests, scripts).
//
//   drun <machine.dvm> [program] [options]
//
// Without a program the machine boots normally. With a program (a DRX, relocated to its load
// address, or a raw binary loaded at --at), the program is called (push 0; call entry) from a
// small trampoline that halts when it returns; with --boot the BIOS runs first (until it jumps
// to the MBR), so its interrupts (int 0x20 / 0x30) work. Text printed in the single color text
// mode goes to stdout.

#include "DragonRuntime.hpp"
#include "../tools/DRX.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>

using namespace dragon;

static bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return true;
}

// The single color text mode of the video card, as text on stdout.
static std::string s_textBuffer;
static bool s_echo { true };		// off while the BIOS boots (only the program's output is shown)
static void processVideoSignal(void)
{
	auto& mem = DragonRuntime::memMap;
	const u32 vga = data::MemoryMapAddresses::VideoCardInterface_Start;
	const u8 signal = cast<u8>(mem.peek8(vga + hw::VirtualDisplay::tRegisters::Signal));
	if (signal == hw::VirtualDisplay::tSignalValues::Continue)
		return;
	const u8 mode = mem.peek8(vga + hw::VirtualDisplay::tRegisters::VideoMode);
	if (mode == hw::VirtualDisplay::tVideoModeValues::TextSingleColor && s_echo)
	{
		const char c = cast<char>(mem.peek8(vga + hw::VirtualDisplay::tRegisters::TextSingleCharacter));
		switch (signal)
		{
			case hw::VirtualDisplay::tSignalValues::TextSingleColor_DirectPrintChar: std::cout << c; break;
			case hw::VirtualDisplay::tSignalValues::TextSingleColor_StoreChar:
				if (c == '\n')
				{
					std::cout << s_textBuffer << "\n";
					s_textBuffer.clear();
				}
				else
					s_textBuffer += c;
			break;
			case hw::VirtualDisplay::tSignalValues::TextSingleColor_DirectPrintBuffAndFlush: std::cout << s_textBuffer; s_textBuffer.clear(); break;
			case hw::VirtualDisplay::tSignalValues::TextSingleColor_DirectPrintBuffNoFlush: std::cout << s_textBuffer; break;
			case hw::VirtualDisplay::tSignalValues::TextSingleColor_FlushBuffer: s_textBuffer.clear(); break;
			case hw::VirtualDisplay::tSignalValues::TextSingleColor_DirectPrintString:
			{
				u32 addr = mem.peek32(vga + hw::VirtualDisplay::tRegisters::TextSingleString);
				for (u8 ch = mem.peek8(addr); ch != 0; ch = mem.peek8(++addr))
					std::cout << (char)ch;
			}
			break;
			default: break;
		}
		std::cout.flush();
	}
	DragonRuntime::vGraphicsInterface.write8(hw::VirtualDisplay::tRegisters::Signal, hw::VirtualDisplay::tSignalValues::Continue);
}

static bool step(u64& cycles)
{
	auto& cpu = DragonRuntime::cpu;
	cpu.execute();
	DragonRuntime::vDiskInterface.cycleStep();
	processVideoSignal();
	cycles++;
	if (data::ErrorHandler::hasError())
	{
		DragonRuntime::processErrors();
		return false;
	}
	return true;
}

static void usage(void)
{
	std::cout << "Usage: drun <machine.dvm> [program.drx | program.bin --at <address>] [options]\n"
				 "  --at <address>         Load address (DRX: default 0x00020000; raw binaries: required)\n"
				 "  --boot                 Runs the BIOS first (its interrupts then work)\n"
				 "  --max-cycles <n>       Stops after n cycles (default 50000000)\n"
				 "  --dump <address>[:n]   Prints n bytes (default 4) of memory at the end\n"
				 "  --quiet                Only the program's output\n"
				 "Exit code: 0 if the machine halted without errors, 1 on errors, 2 when out of cycles.\n";
}

int main(int argc, char** argv)
{
	if (argc < 2 || std::string(argv[1]) == "--help")
	{
		usage();
		return argc < 2 ? 2 : 0;
	}
	std::string config = argv[1], programPath;
	i64 at = -1;
	bool boot = false, quiet = false;
	u64 maxCycles = 50000000;
	std::vector<std::pair<u32, u32>> dumps;
	for (int i = 2; i < argc; i++)
	{
		std::string a = argv[i];
		auto l_value = [&](void) -> std::string { if (i + 1 >= argc) { usage(); std::exit(2); } return argv[++i]; };
		if (a == "--at") at = (i64)std::strtoull(l_value().c_str(), nullptr, 0);
		else if (a == "--boot") boot = true;
		else if (a == "--quiet") quiet = true;
		else if (a == "--max-cycles") maxCycles = std::strtoull(l_value().c_str(), nullptr, 0);
		else if (a == "--dump")
		{
			std::string d = l_value();
			size_t colon = d.find(':');
			u32 addr = (u32)std::strtoul(d.substr(0, colon).c_str(), nullptr, 0);
			u32 n = (colon == std::string::npos) ? 4 : (u32)std::strtoul(d.substr(colon + 1).c_str(), nullptr, 0);
			dumps.push_back({ addr, n });
		}
		else if (programPath.empty() && a[0] != '-') programPath = a;
		else { usage(); return 2; }
	}

	// The machine's loading messages would mix with the program's output.
	std::ostringstream discard;
	std::streambuf* coutBuffer = std::cout.rdbuf(discard.rdbuf());
	DragonRuntime::tRuntimeInitInfo info;
	info.configFilePath = config;
	const i32 init = DragonRuntime::initMachine(info);
	std::cout.rdbuf(coutBuffer);
	if (init != DragonRuntime::RETURN_VAL_EXIT_SUCCESS)
	{
		std::cerr << "drun: cannot start the machine (" << config << ")\n";
		return 1;
	}
	if (data::ErrorHandler::hasError())
	{
		DragonRuntime::processErrors();
		return 1;
	}
	auto& cpu = DragonRuntime::cpu;
	auto& mem = DragonRuntime::memMap;
	u64 cycles = 0;

	if (!programPath.empty())
	{
		if (boot)
		{
			s_echo = false;
			while (cast<u32>(cpu.readRegister(data::Registers::IP)) != data::MemoryMapAddresses::MBR_Start && cycles < maxCycles && !cpu.isHalted())
			{
				if (!step(cycles))
					return 1;
			}
		}
		std::vector<uint8_t> file;
		if (!readFile(programPath, file))
		{
			std::cerr << "drun: cannot read " << programPath << "\n";
			return 1;
		}
		std::vector<uint8_t> image;
		u32 entry = 0;
		if (drx::isDRX(file))
		{
			drx::Program program;
			std::string message;
			if (!drx::read(file, program, message))
			{
				std::cerr << "drun: " << programPath << ": " << message << "\n";
				return 1;
			}
			const u32 base = (at < 0 ? 0x00020000u : (u32)at);
			image = drx::relocate(program, base);
			entry = base + program.entry;
			at = base;
		}
		else
		{
			if (at < 0)
			{
				std::cerr << "drun: raw binaries need --at <address>\n";
				return 1;
			}
			image = file;
			entry = (u32)at;
		}
		for (size_t i = 0; i < image.size(); i++)
		{
			if (!mem.poke8((u32)at + (u32)i, image[i]))
			{
				std::cerr << "drun: the program doesn't fit in memory at " << String::getHexStr((u32)at + (u32)i, true, 4) << "\n";
				return 1;
			}
		}
		// The trampoline, below the stack: push 0; call entry; hlt.
		const u32 stackSize = cast<u32>(DragonRuntime::vCMOS.read32(data::CMOSRegisters::StackSize));
		const u32 tramp = (cpu.getStackTop() - stackSize - 0x100) & ~0xFu;
		const u8 code[] = { data::OpCodes::PushImm, 0, 0, 0, 0, data::OpCodes::CallImm,
							(u8)(entry >> 24), (u8)(entry >> 16), (u8)(entry >> 8), (u8)entry, data::OpCodes::Halt };
		for (size_t i = 0; i < sizeof(code); i++)
			mem.poke8(tramp + (u32)i, code[i]);
		cpu.writeRegister32(data::Registers::IP, cast<i32>(tramp));
		cpu.setBIOSMode(false);
		s_echo = true;
		s_textBuffer.clear();
	}

	while (!cpu.isHalted() && cycles < maxCycles)
	{
		if (!step(cycles))
			return 1;
	}
	std::cout.flush();
	if (!s_textBuffer.empty())
		std::cout << s_textBuffer;
	const i32 rv = cpu.readRegister(data::Registers::RV);
	if (!quiet)
	{
		std::fprintf(stderr, "\n[drun] %s after %llu cycles, RV = 0x%08X (%d)\n", cpu.isHalted() ? "halted" : "out of cycles", (unsigned long long)cycles, (u32)rv, rv);
		for (const auto& d : dumps)
		{
			std::fprintf(stderr, "[drun] %08X:", d.first);
			for (u32 i = 0; i < d.second; i++)
				std::fprintf(stderr, " %02X", mem.peek8(d.first + i));
			std::fprintf(stderr, "\n");
		}
	}
	else
	{
		for (const auto& d : dumps)
		{
			std::printf("%08X:", d.first);
			for (u32 i = 0; i < d.second; i++)
				std::printf(" %02X", mem.peek8(d.first + i));
			std::printf("\n");
		}
	}
	return cpu.isHalted() ? 0 : 2;
}
