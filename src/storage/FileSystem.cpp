#include "FileSystem.hpp"

namespace dragon
{
	namespace storage
	{
		stdvec<std::unique_ptr<FileSystem>>& FileSystems::fileSystems(void)
		{
			static stdvec<std::unique_ptr<FileSystem>> s_fileSystems;
			return s_fileSystems;
		}

		const stdvec<std::unique_ptr<FileSystem>>& FileSystems::list(void)
		{
			return fileSystems();
		}

		const FileSystem* FileSystems::detect(Disk& partition)
		{
			for (const auto& fileSystem : fileSystems())
			{
				if (fileSystem->detect(partition))
					return fileSystem.get();
			}
			return nullptr;
		}

		void FileSystems::add(std::unique_ptr<FileSystem> fileSystem)
		{
			fileSystems().push_back(std::move(fileSystem));
		}
	}
}
