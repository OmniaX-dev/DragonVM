#pragma once

#include "DFS.hpp"
#include "dfs/DfsResize.hpp"
#include <memory>

namespace dragon
{
	namespace storage
	{
		// ============================================================================
		// DfsVolume — a DFS file system on a partition, for the host tools (the
		// debugger's Partition Manager and browser): mounted while the object lives,
		// with the operations on paths and host files the core doesn't have
		// (recursive copies in and out, recursive delete, mkdir -p).
		//
		// The partition is a Disk whose address 0 is the partition's start (a
		// DiskRegion); it must outlive the volume. Errors are dfs::Error codes.
		// ============================================================================
		class DfsVolume
		{
			public: struct Entry
			{
				String name { "" };
				u32 inode { 0 };
				dfs::Inode node {};
				inline bool isDir(void) const { return (node.mode & dfs::ModeTypeMask) == dfs::ModeDir; }
			};
			// What a copy in or out did.
			public: struct CopyResult
			{
				u32 files { 0 };
				u32 directories { 0 };
				u64 bytes { 0 };
				String failedPath { "" };	// the host or DFS path that failed
			};

			public:
				DfsVolume(Disk& partition);
				~DfsVolume(void);
				DfsVolume(const DfsVolume&) = delete;
				DfsVolume& operator=(const DfsVolume&) = delete;

				dfs::i32 mount(bool readOnly);
				dfs::i32 unmount(void);
				inline bool isMounted(void) const { return m_mounted; }
				inline dfs::Volume* get(void) { return m_vol.get(); }
				inline const dfs::Superblock& superblock(void) const { return m_vol->sb; }
				inline const dfs::Layout& layout(void) const { return m_vol->layout; }
				inline const dfs::Device& device(void) const { return m_dev; }
				String label(void) const;
				String uuid(void) const;

				dfs::i32 stat(const String& path, dfs::Inode& outNode, u32* outInode = nullptr);
				// A directory's entries, without . and .., directories first, then by name.
				dfs::i32 list(const String& path, stdvec<Entry>& outEntries);
				dfs::i32 readFile(const String& path, stdvec<u8>& outData);
				// Creates the file, or replaces an existing file's content.
				dfs::i32 writeFile(const String& path, const stdvec<u8>& data);
				dfs::i32 makeDir(const String& path);
				// Every missing directory of the path.
				dfs::i32 makeDirs(const String& path);
				// A file, or a directory with everything in it.
				dfs::i32 removeTree(const String& path);
				dfs::i32 rename(const String& from, const String& to);
				// The data blocks a file uses (pointer blocks included).
				u32 blockCount(const dfs::Inode& node);

				// Copies a host file or directory (recursively) into a DFS directory.
				dfs::i32 importHost(const String& hostPath, const String& dir, CopyResult& result);
				// Copies a DFS file or directory (recursively) to a host path (created).
				dfs::i32 exportHost(const String& path, const String& hostPath, CopyResult& result);

				// mkfs on a partition (4096 / 16384 / no label by default).
				static dfs::i32 format(Disk& partition, u32 blockSize, u32 bytesPerInode, const String& label);

				static String joinPath(const String& dir, const String& name);
				static String parentPath(const String& path);
				static String baseName(const String& path);
				static String formatMode(u16 mode);
				static String formatTime(u32 seconds);
				static String typeName(u16 mode);
				static String errorText(dfs::i32 error);

			private:
				dfs::i32 remove_tree(const String& path, u32 depth);
				dfs::i32 export_tree(const String& path, const String& hostPath, CopyResult& result, u32 depth);
				dfs::i32 import_tree(const String& hostPath, const String& path, CopyResult& result, u32 depth);

			private:
				std::unique_ptr<dfs::Volume> m_vol;
				dfs::Device m_dev {};
				bool m_mounted { false };

			public:
				inline static constexpr u32 MaxDepth { 64 };
		};
	}
}
