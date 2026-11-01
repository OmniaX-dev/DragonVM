#pragma once

#include <ostd/string/String.hpp>
#include <fstream>
#include <functional>
#include <span>

namespace dragon
{
	namespace hw { class VirtualHardDrive; }

	// ============================================================================
	// storage — disk images, partition tables and file systems, shared by the
	// tools (dtools) and the debugger's Disk Manager.
	//
	//   Disk             byte-addressable storage (an image file, a machine drive,
	//                    a buffer in memory, a partition of another disk)
	//   PartitionScheme  a partition table format (DPT, ...), see PartitionTable.hpp
	//   FileSystem       a file system format, see FileSystem.hpp
	//
	// Partition schemes and file systems only see a Disk, so they work the same on
	// an image file, on a drive connected to the running machine and on an
	// editor's unsaved buffer.
	// ============================================================================
	namespace storage
	{
		class Disk
		{
			public:
				inline virtual ~Disk(void) = default;
				virtual u64 getSize(void) const = 0;
				// Both fail (and do nothing) when the range doesn't fit in the disk.
				virtual bool read(u64 address, std::span<u8> out) = 0;
				virtual bool write(u64 address, std::span<const u8> data) = 0;

				inline bool contains(u64 address, u64 size) const { return address <= getSize() && size <= getSize() - address; }
				// Big endian, like the machine.
				bool readU16(u64 address, u16& out);
				bool readU32(u64 address, u32& out);
				bool writeU16(u64 address, u16 value);
				bool writeU32(u64 address, u32 value);
		};

		// A disk image file.
		class FileDisk : public Disk
		{
			public:
				inline FileDisk(void) = default;
				inline FileDisk(const String& path) { open(path); }
				bool open(const String& path);
				void close(void);
				inline bool isOpen(void) const { return m_file.is_open(); }
				inline const String& getPath(void) const { return m_path; }

				inline u64 getSize(void) const override { return m_size; }
				bool read(u64 address, std::span<u8> out) override;
				bool write(u64 address, std::span<const u8> data) override;

			private:
				std::fstream m_file;
				String m_path { "" };
				u64 m_size { 0 };
		};

		// A drive of the machine: reads and writes go through it, so the machine sees
		// them (and the debugger journals the writes).
		class DriveDisk : public Disk
		{
			public:
				inline DriveDisk(hw::VirtualHardDrive& drive) : m_drive(drive) {  }
				inline hw::VirtualHardDrive& getDrive(void) { return m_drive; }

				u64 getSize(void) const override;
				bool read(u64 address, std::span<u8> out) override;
				bool write(u64 address, std::span<const u8> data) override;

			private:
				hw::VirtualHardDrive& m_drive;
		};

		// A buffer in memory (it must outlive the disk).
		class MemoryDisk : public Disk
		{
			public: using WriteCallback = std::function<void(u64 address, u64 size)>;
			public:
				inline MemoryDisk(std::span<u8> data) : m_data(data) {  }
				// Called after every write (e.g. to mark the bytes as changed).
				inline void setWriteCallback(WriteCallback callback) { m_onWrite = std::move(callback); }

				inline u64 getSize(void) const override { return m_data.size(); }
				bool read(u64 address, std::span<u8> out) override;
				bool write(u64 address, std::span<const u8> data) override;

			private:
				std::span<u8> m_data;
				WriteCallback m_onWrite { nullptr };
		};

		// A window on another disk (a partition): addresses start at the window's start.
		class DiskRegion : public Disk
		{
			public:
				inline DiskRegion(Disk& disk, u64 start, u64 size) : m_disk(disk), m_start(start), m_size(size) {  }
				inline u64 getStart(void) const { return m_start; }

				inline u64 getSize(void) const override { return m_size; }
				bool read(u64 address, std::span<u8> out) override;
				bool write(u64 address, std::span<const u8> data) override;

			private:
				Disk& m_disk;
				u64 m_start { 0 };
				u64 m_size { 0 };
		};

		// A new image file filled with zeros (overwrites an existing one).
		bool createDiskFile(const String& path, u64 size, String& outError);
		// Writes a file's content to the disk, at the address.
		bool loadBinary(Disk& disk, const String& filePath, u64 address, u64& outSize, String& outError);
		bool readFile(const String& filePath, stdvec<u8>& outData, String& outError);
		// "1.50 MiB", "512 bytes".
		String formatSize(u64 bytes);
		// Decimal, 0x hex or a size with a unit suffix: "4096", "0x1000", "4K", "1.5M",
		// "2MiB" (K / M / G = 1024-based).
		bool parseSize(const String& text, u64& outValue);
	}
}
