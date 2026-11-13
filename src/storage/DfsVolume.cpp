#include "DfsVolume.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>

namespace dragon
{
	namespace storage
	{
		namespace fs = std::filesystem;

		static const dfs::u8* cpath(const String& path) { return reinterpret_cast<const dfs::u8*>(path.c_str()); }

		DfsVolume::DfsVolume(Disk& partition) : m_vol(new dfs::Volume())
		{
			m_dev = DFS::makeDevice(partition);
		}

		DfsVolume::~DfsVolume(void)
		{
			unmount();
		}

		dfs::i32 DfsVolume::mount(bool readOnly)
		{
			unmount();
			const dfs::i32 r = dfs::mount(m_vol.get(), &m_dev, readOnly);
			m_mounted = (r == dfs::Ok);
			return r;
		}

		dfs::i32 DfsVolume::unmount(void)
		{
			if (!m_mounted)
				return dfs::Ok;
			m_mounted = false;
			return dfs::unmount(m_vol.get());
		}

		String DfsVolume::label(void) const
		{
			String text = "";
			for (u32 i = 0; i < 16 && m_vol->sb.label[i] != 0; i++)
				text += cast<char>(m_vol->sb.label[i]);
			return text;
		}

		String DfsVolume::uuid(void) const
		{
			String text = "";
			for (u32 i = 0; i < 16; i++)
			{
				text += String::getHexStr(m_vol->sb.uuid[i], false, 1).toLower();
				if (i == 3 || i == 5 || i == 7 || i == 9)
					text += "-";
			}
			return text;
		}

		dfs::i32 DfsVolume::stat(const String& path, dfs::Inode& outNode, u32* outInode)
		{
			return dfs::stat(m_vol.get(), cpath(path), &outNode, outInode);
		}

		dfs::i32 DfsVolume::list(const String& path, stdvec<Entry>& outEntries)
		{
			outEntries.clear();
			dfs::Inode dir;
			dfs::i32 r = stat(path, dir);
			if (r < 0)
				return r;
			if ((dir.mode & dfs::ModeTypeMask) != dfs::ModeDir)
				return dfs::NotDir;
			u32 cursor = 0;
			dfs::DirEntry entry;
			while ((r = dfs::dir_next(m_vol.get(), &dir, &cursor, &entry)) == dfs::Ok)
			{
				const String name = reinterpret_cast<const char*>(entry.name);
				if (name == "." || name == "..")
					continue;
				outEntries.push_back({ name, entry.inode, {} });
			}
			if (r != dfs::NotFound)
				return r;
			// The inodes after the walk (it uses the directory buffer).
			for (auto& e : outEntries)
			{
				if ((r = dfs::read_inode(m_vol.get(), e.inode, &e.node)) < 0)
					return r;
			}
			std::sort(outEntries.begin(), outEntries.end(), [](const Entry& a, const Entry& b) {
				if (a.isDir() != b.isDir())
					return a.isDir();
				return a.name.cpp_str() < b.name.cpp_str();
			});
			return dfs::Ok;
		}

		dfs::i32 DfsVolume::readFile(const String& path, stdvec<u8>& outData)
		{
			dfs::Inode node;
			dfs::i32 r = stat(path, node);
			if (r < 0)
				return r;
			if ((node.mode & dfs::ModeTypeMask) == dfs::ModeDir)
				return dfs::IsDir;
			outData.assign(node.size, 0);
			if (node.size > 0 && (r = dfs::inode_read(m_vol.get(), &node, 0, outData.data(), node.size)) < 0)
				return r;
			return dfs::Ok;
		}

		dfs::i32 DfsVolume::writeFile(const String& path, const stdvec<u8>& data)
		{
			if (data.size() > 0x7FFFFFFF)
				return dfs::TooBig;
			u32 ino = 0;
			dfs::Inode node;
			dfs::i32 r = dfs::create(m_vol.get(), cpath(path), 0644, &ino);
			if (r == dfs::Exists)
			{
				if ((r = stat(path, node, &ino)) < 0)
					return r;
				if ((node.mode & dfs::ModeTypeMask) == dfs::ModeDir)
					return dfs::IsDir;
				if ((r = dfs::truncate(m_vol.get(), ino, &node, 0)) < 0)
					return r;
			}
			else if (r < 0)
				return r;
			else if ((r = dfs::read_inode(m_vol.get(), ino, &node)) < 0)
				return r;
			if (data.empty())
				return dfs::Ok;
			r = dfs::inode_write(m_vol.get(), ino, &node, 0, data.data(), cast<u32>(data.size()));
			if (r < 0)
				return r;
			return cast<size_t>(r) == data.size() ? dfs::Ok : dfs::NoSpace;
		}

		dfs::i32 DfsVolume::makeDir(const String& path)
		{
			return dfs::mkdir(m_vol.get(), cpath(path), 0755, nullptr);
		}

		dfs::i32 DfsVolume::makeDirs(const String& path)
		{
			String current = "";
			for (const auto& part : String(path).tokenize("/").getRawData())
			{
				if (part == "")
					continue;
				current = current + "/" + part;
				const dfs::i32 r = makeDir(current);
				if (r < 0 && r != dfs::Exists)
					return r;
			}
			dfs::Inode node;
			const dfs::i32 r = stat(path, node);
			if (r < 0)
				return r;
			return (node.mode & dfs::ModeTypeMask) == dfs::ModeDir ? dfs::Ok : dfs::NotDir;
		}

		dfs::i32 DfsVolume::removeTree(const String& path)
		{
			return remove_tree(path, 0);
		}

		dfs::i32 DfsVolume::remove_tree(const String& path, u32 depth)
		{
			if (depth > MaxDepth)
				return dfs::Corrupt;
			dfs::Inode node;
			dfs::i32 r = stat(path, node);
			if (r < 0)
				return r;
			if ((node.mode & dfs::ModeTypeMask) != dfs::ModeDir)
				return dfs::unlink(m_vol.get(), cpath(path));
			stdvec<Entry> entries;
			if ((r = list(path, entries)) < 0)
				return r;
			for (const auto& entry : entries)
			{
				if ((r = remove_tree(joinPath(path, entry.name), depth + 1)) < 0)
					return r;
			}
			return dfs::rmdir(m_vol.get(), cpath(path));
		}

		dfs::i32 DfsVolume::rename(const String& from, const String& to)
		{
			return dfs::rename(m_vol.get(), cpath(from), cpath(to));
		}

		u32 DfsVolume::blockCount(const dfs::Inode& node)
		{
			const dfs::Layout& l = m_vol->layout;
			dfs::Inode copy = node;
			u32 blocks = 0;
			const u32 count = (node.size + l.block_mask) >> l.block_size_log2;
			for (u32 i = 0; i < count; i++)
			{
				u32 block = 0;
				if (dfs::bmap(m_vol.get(), &copy, i, false, &block) >= 0 && block != 0)
					blocks++;
			}
			if (node.indirect != 0)
				blocks++;
			if (node.dindirect != 0)
			{
				// The double indirect block and the pointer blocks it points to.
				blocks++;
				const u32 P = l.pointers_per_block;
				const u32 first = dfs::DirectPointers + P;
				if (count > first)
					blocks += (count - first + P - 1) / P;
			}
			return blocks;
		}

		dfs::i32 DfsVolume::importHost(const String& hostPath, const String& dir, CopyResult& result)
		{
			const String name = String(fs::path(hostPath.cpp_str()).filename().string());
			if (name == "" || name.len() > dfs::MaxNameLength)
			{
				result.failedPath = hostPath;
				return dfs::NameTooLong;
			}
			return import_tree(hostPath, joinPath(dir, name), result, 0);
		}

		dfs::i32 DfsVolume::import_tree(const String& hostPath, const String& path, CopyResult& result, u32 depth)
		{
			std::error_code ec;
			if (depth > MaxDepth)
				return dfs::TooBig;
			if (fs::is_directory(hostPath.cpp_str(), ec))
			{
				dfs::i32 r = makeDir(path);
				if (r < 0 && r != dfs::Exists)
				{
					result.failedPath = path;
					return r;
				}
				result.directories++;
				stdvec<fs::path> children;
				for (const auto& entry : fs::directory_iterator(hostPath.cpp_str(), ec))
					children.push_back(entry.path());
				std::sort(children.begin(), children.end());
				for (const auto& child : children)
				{
					const String name = String(child.filename().string());
					if (name.len() > dfs::MaxNameLength)
					{
						result.failedPath = String(child.string());
						return dfs::NameTooLong;
					}
					if ((r = import_tree(String(child.string()), joinPath(path, name), result, depth + 1)) < 0)
						return r;
				}
				return dfs::Ok;
			}
			stdvec<u8> data;
			String error = "";
			if (!storage::readFile(hostPath, data, error))
			{
				result.failedPath = hostPath;
				return dfs::IOError;
			}
			const dfs::i32 r = writeFile(path, data);
			if (r < 0)
			{
				result.failedPath = path;
				return r;
			}
			result.files++;
			result.bytes += data.size();
			return dfs::Ok;
		}

		dfs::i32 DfsVolume::exportHost(const String& path, const String& hostPath, CopyResult& result)
		{
			return export_tree(path, hostPath, result, 0);
		}

		dfs::i32 DfsVolume::export_tree(const String& path, const String& hostPath, CopyResult& result, u32 depth)
		{
			if (depth > MaxDepth)
				return dfs::Corrupt;
			dfs::Inode node;
			dfs::i32 r = stat(path, node);
			if (r < 0)
			{
				result.failedPath = path;
				return r;
			}
			std::error_code ec;
			if ((node.mode & dfs::ModeTypeMask) == dfs::ModeDir)
			{
				fs::create_directories(hostPath.cpp_str(), ec);
				if (ec || !fs::is_directory(hostPath.cpp_str(), ec))
				{
					result.failedPath = hostPath;
					return dfs::IOError;
				}
				result.directories++;
				stdvec<Entry> entries;
				if ((r = list(path, entries)) < 0)
				{
					result.failedPath = path;
					return r;
				}
				for (const auto& entry : entries)
				{
					if ((r = export_tree(joinPath(path, entry.name), String((fs::path(hostPath.cpp_str()) / entry.name.cpp_str()).string()), result, depth + 1)) < 0)
						return r;
				}
				return dfs::Ok;
			}
			stdvec<u8> data;
			if ((r = readFile(path, data)) < 0)
			{
				result.failedPath = path;
				return r;
			}
			std::ofstream file(hostPath.cpp_str(), std::ios::binary | std::ios::trunc);
			if (!file || !file.write(reinterpret_cast<const char*>(data.data()), cast<std::streamsize>(data.size())))
			{
				result.failedPath = hostPath;
				return dfs::IOError;
			}
			result.files++;
			result.bytes += data.size();
			return dfs::Ok;
		}

		dfs::i32 DfsVolume::format(Disk& partition, u32 blockSize, u32 bytesPerInode, const String& label)
		{
			u32 log2 = 0;
			while ((1u << log2) < blockSize && log2 < 31)
				log2++;
			if ((1u << log2) != blockSize || log2 < dfs::MinBlockSizeLog2 || log2 > dfs::MaxBlockSizeLog2)
				return dfs::Invalid;
			if (partition.getSize() > 0xFFFFFFFFull || label.len() > 16)
				return dfs::Invalid;
			dfs::u8 uuid[16];
			std::random_device random;
			for (auto& byte : uuid)
				byte = cast<dfs::u8>(random());
			const dfs::Device dev = DFS::makeDevice(partition);
			std::unique_ptr<dfs::Volume> vol(new dfs::Volume());
			dfs::MkfsParams params { cast<u32>(partition.getSize()), log2, bytesPerInode, cpath(label), uuid };
			return dfs::mkfs(vol.get(), &dev, &params);
		}

		String DfsVolume::joinPath(const String& dir, const String& name)
		{
			if (dir.cpp_str().ends_with("/"))
				return dir + name;
			return dir + "/" + name;
		}

		String DfsVolume::parentPath(const String& path)
		{
			const std::string p = path.cpp_str();
			const size_t slash = p.rfind('/');
			if (slash == std::string::npos || slash == 0)
				return "/";
			return String(p.substr(0, slash));
		}

		String DfsVolume::baseName(const String& path)
		{
			const std::string p = path.cpp_str();
			const size_t slash = p.rfind('/');
			return String(slash == std::string::npos ? p : p.substr(slash + 1));
		}

		String DfsVolume::formatMode(u16 mode)
		{
			String text = "";
			const u16 type = mode & dfs::ModeTypeMask;
			text += (type == dfs::ModeDir ? 'd' : (type == dfs::ModeCharDevice ? 'c' : (type == dfs::ModeSymlink ? 'l' : '-')));
			const char* letters = "rwxrwxrwx";
			for (i32 i = 0; i < 9; i++)
				text += ((mode >> (8 - i)) & 1) ? letters[i] : '-';
			char octal[16] = { 0 };
			std::snprintf(octal, sizeof(octal), " (%04o)", mode & dfs::ModePermMask);
			return text + octal;
		}

		String DfsVolume::formatTime(u32 seconds)
		{
			if (seconds == 0)
				return "-";
			std::time_t t = cast<std::time_t>(seconds);
			char text[32] = { 0 };
			std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
			return text;
		}

		String DfsVolume::typeName(u16 mode)
		{
			switch (mode & dfs::ModeTypeMask)
			{
				case dfs::ModeFile: return "File";
				case dfs::ModeDir: return "Directory";
				case dfs::ModeCharDevice: return "Character device";
				case dfs::ModeSymlink: return "Symbolic link";
				default: return "Unknown";
			}
		}

		String DfsVolume::errorText(dfs::i32 error)
		{
			return dfs::error_name(error);
		}
	}
}
