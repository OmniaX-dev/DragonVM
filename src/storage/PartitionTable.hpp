#pragma once

#include "Disk.hpp"
#include <memory>

namespace dragon
{
	namespace storage
	{
		struct tPartition
		{
			u64 start { 0 };
			u64 size { 0 };
			u32 flags { 0 };	// bits, see PartitionScheme::getFlags()
			String label { "" };

			inline u64 end(void) const { return start + size; }
			inline bool hasFlag(u8 bit) const { return (flags >> bit) & 1; }
			inline void setFlag(u8 bit, bool set) { if (set) flags |= (1u << bit); else flags &= ~(1u << bit); }
			inline bool operator==(const tPartition& other) const { return start == other.start && size == other.size && flags == other.flags && label == other.label; }
		};

		struct tPartitionTable
		{
			String scheme { "" };	// PartitionScheme::getName()
			String version { "" };	// as read from the disk (written: the scheme's current one)
			stdvec<tPartition> partitions;

			inline bool operator==(const tPartitionTable& other) const { return scheme == other.scheme && partitions == other.partitions; }
		};

		// A part of the disk that partitions can't use (boot code, the table itself).
		struct tReservedArea
		{
			u64 start { 0 };
			u64 size { 0 };
			String name { "" };
		};

		struct tPartitionFlag
		{
			u8 bit { 0 };
			String name { "" };
			String description { "" };
		};

		// ============================================================================
		// PartitionScheme — a partition table format. A new format is a subclass
		// registered in PartitionSchemes (see PartitionSchemes::list()); the tools and
		// the Partition Manager only use this interface.
		//
		// The limits (getMaxPartitions(), the usable area, the reserved areas, the
		// label length, the flags) describe the format to the editor, and
		// validate() checks a table against them before it's written.
		// ============================================================================
		class PartitionScheme
		{
			public:
				inline virtual ~PartitionScheme(void) = default;
				virtual String getName(void) const = 0;
				virtual String getDescription(void) const = 0;
				// Whether the disk holds a table of this format.
				virtual bool detect(Disk& disk) const = 0;
				virtual bool read(Disk& disk, tPartitionTable& outTable, String& outError) const = 0;
				// Writes the table (call validate() first).
				virtual bool write(Disk& disk, const tPartitionTable& table, String& outError) const = 0;

				virtual u32 getMaxPartitions(void) const = 0;
				// In bytes, without a terminator.
				virtual u32 getMaxLabelLength(void) const = 0;
				virtual u64 getMinDiskSize(void) const = 0;
				// Highest address a partition can end at (e.g. 32 bit fields).
				virtual u64 getMaxAddress(void) const = 0;
				// [first, last) usable by partitions.
				virtual u64 getFirstUsableAddress(u64 diskSize) const = 0;
				inline virtual u64 getLastUsableAddress(u64 diskSize) const { return std::min(diskSize, getMaxAddress()); }
				virtual stdvec<tReservedArea> getReservedAreas(u64 diskSize) const = 0;
				virtual stdvec<tPartitionFlag> getFlags(void) const = 0;
				// The flags' names, comma separated ("boot").
				String flagsToString(u32 flags) const;

				// The table's problems (empty: it can be written to a disk of that size).
				virtual stdvec<String> validate(const tPartitionTable& table, u64 diskSize) const;
		};

		class PartitionSchemes
		{
			public:
				// The known formats (DPT first).
				static const stdvec<std::unique_ptr<PartitionScheme>>& list(void);
				static const PartitionScheme* find(const String& name);
				// The format of the disk's table, or nullptr.
				static const PartitionScheme* detect(Disk& disk);
				static void add(std::unique_ptr<PartitionScheme> scheme);

			private:
				static stdvec<std::unique_ptr<PartitionScheme>>& schemes(void);
		};
	}
}
