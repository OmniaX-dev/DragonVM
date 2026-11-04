#include "DPT.hpp"

#include "../tools/GlobalData.hpp"

namespace dragon
{
	namespace storage
	{
		using DPT = data::DPTStructure;

		bool DPTScheme::detect(Disk& disk) const
		{
			u16 id = 0;
			return disk.getSize() >= getMinDiskSize() && disk.readU16(DPT::DiskAddress + DPT::DPTID, id) && id == DPT::DPT_ID_CODE;
		}

		bool DPTScheme::read(Disk& disk, tPartitionTable& outTable, String& outError) const
		{
			if (disk.getSize() < getMinDiskSize())
			{
				outError = "Disk too small.";
				return false;
			}
			stdvec<u8> block(DPT::DPTBlockSizeBytes, 0);
			if (!disk.read(DPT::DiskAddress, block))
			{
				outError = "Unable to read from disk.";
				return false;
			}
			auto l_u16 = [&block](u32 at) -> u16 { return cast<u16>((block[at] << 8) | block[at + 1]); };
			auto l_u32 = [&block](u32 at) -> u32 { return (cast<u32>(block[at]) << 24) | (cast<u32>(block[at + 1]) << 16) | (cast<u32>(block[at + 2]) << 8) | block[at + 3]; };
			if (l_u16(DPT::DPTID) != DPT::DPT_ID_CODE)
			{
				outError = "No DPT partition table on virtual disk.";
				return false;
			}
			const u32 count = block[DPT::PartitionCount];
			if (count > DPT::MaxPartCount)
			{
				outError = String("Too many partitions. Maximum is ").add(DPT::MaxPartCount).add(".");
				return false;
			}
			outTable = tPartitionTable();
			outTable.scheme = getName();
			outTable.version = String("").add(block[DPT::DPTVersionMaj]).add(".").add(block[DPT::DPTVersionMin]);
			for (u32 i = 0; i < count; i++)
			{
				const u32 entry = DPT::EntriesStart + DPT::EntrySizeBytes * i;
				tPartition part;
				part.start = l_u32(entry + DPT::EntryStartAddress);
				part.size = l_u32(entry + DPT::EntryPartitionSize);
				part.flags = l_u16(entry + DPT::EntryFlags);
				String label = "";
				for (u32 j = 0; j < DPT::EntryLabelSizeBytes && block[entry + DPT::EntryPartitionLabel + j] != 0; j++)
					label += cast<char>(block[entry + DPT::EntryPartitionLabel + j]);
				part.label = label.trim();
				outTable.partitions.push_back(part);
			}
			return true;
		}

		stdvec<u8> DPTScheme::encode(const tPartitionTable& table) const
		{
			// Same bytes as dtools always wrote: reserved fields 0xFF, unused entries and
			// the label's tail 0x00.
			stdvec<u8> block(DPT::DPTBlockSizeBytes, 0);
			auto l_u16 = [&block](u32 at, u16 value) { block[at] = cast<u8>(value >> 8); block[at + 1] = cast<u8>(value); };
			auto l_u32 = [&block](u32 at, u32 value) { for (u32 i = 0; i < 4; i++) block[at + i] = cast<u8>(value >> (24 - 8 * i)); };
			l_u16(DPT::DPTID, DPT::DPT_ID_CODE);
			block[DPT::DPTVersionMaj] = DPT::CurrentDPTVersionMaj;
			block[DPT::DPTVersionMin] = DPT::CurrentDPTVersionMin;
			const u32 count = cast<u32>(std::min<size_t>(table.partitions.size(), DPT::MaxPartCount));
			block[DPT::PartitionCount] = cast<u8>(count);
			for (u32 i = 0; i < DPT::HeaderReservedSizeBytes; i++)
				block[DPT::PartitionCount + 1 + i] = 0xFF;
			for (u32 i = 0; i < count; i++)
			{
				const tPartition& part = table.partitions[i];
				const u32 entry = DPT::EntriesStart + DPT::EntrySizeBytes * i;
				l_u32(entry + DPT::EntryStartAddress, cast<u32>(part.start));
				l_u32(entry + DPT::EntryPartitionSize, cast<u32>(part.size));
				l_u16(entry + DPT::EntryFlags, cast<u16>(part.flags));
				for (u32 j = 0; j < DPT::EntryReservedSizeBytes; j++)
					block[entry + DPT::EntryFlags + 2 + j] = 0xFF;
				const ostd::cpp_string& label = part.label.cpp_str();
				for (u32 j = 0; j < label.size() && j < getMaxLabelLength(); j++)
					block[entry + DPT::EntryPartitionLabel + j] = cast<u8>(label[j]);
			}
			return block;
		}

		bool DPTScheme::write(Disk& disk, const tPartitionTable& table, String& outError) const
		{
			if (disk.getSize() < getMinDiskSize())
			{
				outError = "Disk too small.";
				return false;
			}
			if (table.partitions.size() > DPT::MaxPartCount)
			{
				outError = "Too many partitions.";
				return false;
			}
			if (!disk.write(DPT::DiskAddress, encode(table)))
			{
				outError = "Unable to write to disk.";
				return false;
			}
			return true;
		}

		u32 DPTScheme::getMaxPartitions(void) const
		{
			return DPT::MaxPartCount;
		}

		u32 DPTScheme::getMaxLabelLength(void) const
		{
			return DPT::EntryLabelSizeBytes - 1;
		}

		u64 DPTScheme::getMinDiskSize(void) const
		{
			return DPT::DiskStartAddr;
		}

		u64 DPTScheme::getFirstUsableAddress(u64 diskSize) const
		{
			return DPT::DiskStartAddr;
		}

		stdvec<tReservedArea> DPTScheme::getReservedAreas(u64 diskSize) const
		{
			return {
				{ 0, DPT::BootAreaSize, "boot code" },
				{ DPT::DiskAddress, DPT::DPTBlockSizeBytes, "DPT" }
			};
		}

		stdvec<tPartitionFlag> DPTScheme::getFlags(void) const
		{
			return { { DPT::tFlags::Boot, "boot", "Bootable partition" } };
		}
	}
}
