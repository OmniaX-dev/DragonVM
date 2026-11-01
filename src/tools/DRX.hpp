#pragma once

// DRX: Dragon Relocatable eXecutable (the default output of the DScript compiler).
//
// File layout (big endian, like the machine):
//   0x00  magic          'D' 'R' 'X' '1'
//   0x04  header size    (32)
//   0x08  image size     bytes of the program image (code + data)
//   0x0C  entry offset   where to start (call) the program, from the image start
//   0x10  reloc count    entries in the relocation table
//   0x14  reloc offset   where the relocation table starts, in the file
//   0x18  flags          (0)
//   0x1C  reserved       (0)
//   image                the program, assembled at address 0
//   relocation table     reloc count x 4 bytes: offsets (in the image) of 32-bit addresses
//
// Loading at `base`: copy the image to base, then add base to the 32-bit (big endian) word at
// base + offset for every relocation; then call base + entry (push 0; call ...): the program
// returns (ret) when main returns, with main's value in RV.

#include <cstdint>
#include <vector>
#include <string>

namespace dragon
{
	namespace drx
	{
		inline constexpr uint32_t Magic = 0x44525831;	// "DRX1"
		inline constexpr uint32_t HeaderSize = 32;

		struct Program
		{
			std::vector<uint8_t> image;
			uint32_t entry { 0 };
			std::vector<uint32_t> relocations;
		};

		inline uint32_t readU32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
		inline void writeU32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

		inline bool isDRX(const std::vector<uint8_t>& file) { return file.size() >= HeaderSize && readU32(file.data()) == Magic; }

		inline std::vector<uint8_t> write(const Program& program)
		{
			std::vector<uint8_t> file(HeaderSize + program.image.size() + program.relocations.size() * 4, 0);
			writeU32(&file[0x00], Magic);
			writeU32(&file[0x04], HeaderSize);
			writeU32(&file[0x08], (uint32_t)program.image.size());
			writeU32(&file[0x0C], program.entry);
			writeU32(&file[0x10], (uint32_t)program.relocations.size());
			writeU32(&file[0x14], HeaderSize + (uint32_t)program.image.size());
			for (size_t i = 0; i < program.image.size(); i++)
				file[HeaderSize + i] = program.image[i];
			for (size_t i = 0; i < program.relocations.size(); i++)
				writeU32(&file[HeaderSize + program.image.size() + i * 4], program.relocations[i]);
			return file;
		}

		// false (with a message) if the file is not a valid DRX.
		inline bool read(const std::vector<uint8_t>& file, Program& out, std::string& message)
		{
			if (!isDRX(file)) { message = "not a DRX file"; return false; }
			const uint32_t headerSize = readU32(&file[0x04]);
			const uint32_t imageSize = readU32(&file[0x08]);
			const uint32_t relocCount = readU32(&file[0x10]);
			const uint32_t relocOffset = readU32(&file[0x14]);
			if ((uint64_t)headerSize + imageSize > file.size() || (uint64_t)relocOffset + (uint64_t)relocCount * 4 > file.size())
			{
				message = "truncated DRX file";
				return false;
			}
			out.image.assign(file.begin() + headerSize, file.begin() + headerSize + imageSize);
			out.entry = readU32(&file[0x0C]);
			out.relocations.clear();
			for (uint32_t i = 0; i < relocCount; i++)
			{
				const uint32_t offset = readU32(&file[relocOffset + i * 4]);
				if ((uint64_t)offset + 4 > imageSize)
				{
					message = "relocation outside of the image";
					return false;
				}
				out.relocations.push_back(offset);
			}
			return true;
		}

		// The image relocated to `base`.
		inline std::vector<uint8_t> relocate(const Program& program, uint32_t base)
		{
			std::vector<uint8_t> image = program.image;
			for (uint32_t offset : program.relocations)
				writeU32(&image[offset], readU32(&image[offset]) + base);
			return image;
		}
	}
}
