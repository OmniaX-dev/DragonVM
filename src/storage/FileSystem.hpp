#pragma once

#include "Disk.hpp"
#include <memory>

namespace dragon
{
	namespace storage
	{
		// ============================================================================
		// FileSystem — a file system format. There are none yet: a new format is a
		// subclass registered in FileSystems (see FileSystems::list()), and the
		// Partition Manager shows the detected one for each partition.
		//
		// A file system only sees its partition (a DiskRegion: address 0 is the
		// partition's start), so it doesn't depend on the partition table format.
		// The operations (format, list, read and write files...) will be added here
		// with the first implementation.
		// ============================================================================
		class FileSystem
		{
			public:
				inline virtual ~FileSystem(void) = default;
				virtual String getName(void) const = 0;
				// Whether the partition holds this file system.
				virtual bool detect(Disk& partition) const = 0;
		};

		class FileSystems
		{
			public:
				static const stdvec<std::unique_ptr<FileSystem>>& list(void);
				// The file system on the partition, or nullptr.
				static const FileSystem* detect(Disk& partition);
				static void add(std::unique_ptr<FileSystem> fileSystem);

			private:
				static stdvec<std::unique_ptr<FileSystem>>& fileSystems(void);
		};
	}
}
