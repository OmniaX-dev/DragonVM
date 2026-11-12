#include "DFS.hpp"

#include <ctime>

namespace dragon
{
	namespace storage
	{
		bool DFS::detect(Disk& partition) const
		{
			u32 magic = 0, version = 0;
			return partition.readU32(dfs::SuperblockOffset, magic) && partition.readU32(dfs::SuperblockOffset + 4, version)
					&& magic == dfs::Magic && version == dfs::Version;
		}

		dfs::Device DFS::makeDevice(Disk& partition)
		{
			dfs::Device dev;
			dev.user = &partition;
			dev.read = [](void* user, dfs::u32 offset, dfs::u8* buffer, dfs::u32 size) -> dfs::i32 {
				return cast<Disk*>(user)->read(offset, std::span<u8>(buffer, size)) ? dfs::Ok : dfs::IOError;
			};
			dev.write = [](void* user, dfs::u32 offset, const dfs::u8* buffer, dfs::u32 size) -> dfs::i32 {
				return cast<Disk*>(user)->write(offset, std::span<const u8>(buffer, size)) ? dfs::Ok : dfs::IOError;
			};
			dev.now = [](void*) -> dfs::u32 { return cast<dfs::u32>(std::time(nullptr)); };
			return dev;
		}
	}
}
