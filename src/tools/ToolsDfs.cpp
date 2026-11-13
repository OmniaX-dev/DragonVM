#include "Tools.hpp"

#include "../storage/DPT.hpp"
#include "../storage/DFS.hpp"
#include "../storage/dfs/DfsResize.hpp"
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <algorithm>
#include <cstdio>
#include <functional>

// dtools dfs <image> <partition#> <command> [...]: the DFS file system on a
// partition of a disk image (see print_dfs_help() for the commands).

namespace dragon
{
	namespace
	{
		using ostd::ConsoleColors;
		namespace fs = std::filesystem;

		// The partition, its device and the volume on it.
		struct DfsSession
		{
			storage::FileDisk disk;
			std::unique_ptr<storage::DiskRegion> region;
			dfs::Device dev {};
			std::unique_ptr<dfs::Volume> vol { new dfs::Volume() };
			u64 partitionSize { 0 };
			bool mounted { false };

			~DfsSession() { if (mounted) dfs::unmount(vol.get()); }
		};

		const dfs::u8* cpath(const String& path) { return reinterpret_cast<const dfs::u8*>(path.c_str()); }

		String format_time(u32 seconds)
		{
			if (seconds == 0)
				return "-";
			std::time_t t = cast<std::time_t>(seconds);
			char text[32] = { 0 };
			std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
			return text;
		}

		String format_mode(u16 mode)
		{
			String text = "";
			const u16 type = mode & dfs::ModeTypeMask;
			text += (type == dfs::ModeDir ? 'd' : (type == dfs::ModeCharDevice ? 'c' : (type == dfs::ModeSymlink ? 'l' : '-')));
			const char* letters = "rwxrwxrwx";
			for (i32 i = 0; i < 9; i++)
				text += ((mode >> (8 - i)) & 1) ? letters[i] : '-';
			return text;
		}

		String type_name(u16 mode)
		{
			switch (mode & dfs::ModeTypeMask)
			{
				case dfs::ModeFile: return "file";
				case dfs::ModeDir: return "directory";
				case dfs::ModeCharDevice: return "character device";
				case dfs::ModeSymlink: return "symlink";
				default: return "unknown";
			}
		}

		String octal(u32 value)
		{
			char text[16] = { 0 };
			std::snprintf(text, sizeof(text), "%04o", value);
			return text;
		}

		String join_path(const String& dir, const String& name)
		{
			if (dir.cpp_str().ends_with("/"))
				return dir + name;
			return dir + "/" + name;
		}
	}

	i32 Tools::dfs_fail(const String& what, i32 error)
	{
		out.fg(ConsoleColors::Red).p("Error: ").p(what);
		if (error < 0)
			out.p(": ").p(dfs::error_name(error));
		out.reset().nl();
		return ErrorDfsFailed;
	}

	i32 Tools::tool_dfs(int argc, char** argv)
	{
		if (argc < 5)
		{
			print_dfs_help();
			return ErrorDfsTooFewArgs;
		}
		const String image = argv[2];
		const String partArg = argv[3];
		const String command = String(argv[4]).toLower();
		stdvec<String> args;
		for (i32 i = 5; i < argc; i++)
			args.push_back(argv[i]);

		// The partition (1-based, as in read-dpt's order).
		DfsSession s;
		if (!s.disk.open(image))
			return dfs_fail(String("unable to open the disk image '").add(image).add("'"));
		if (!partArg.isInt() || partArg.toInt() < 1)
			return dfs_fail("<partition#> must be a number from 1");
		const storage::DPTScheme dpt;
		storage::tPartitionTable table;
		String error = "";
		if (!dpt.read(s.disk, table, error))
			return dfs_fail(error);
		const u32 partIndex = cast<u32>(partArg.toInt()) - 1;
		if (partIndex >= table.partitions.size())
			return dfs_fail(String("the disk has ").add(cast<u64>(table.partitions.size())).add(" partitions"));
		const storage::tPartition& part = table.partitions[partIndex];
		if (!s.disk.contains(part.start, part.size) || part.size == 0)
			return dfs_fail("the partition doesn't fit in the disk image");
		s.partitionSize = part.size;
		s.region = std::make_unique<storage::DiskRegion>(s.disk, part.start, part.size);
		s.dev = storage::DFS::makeDevice(*s.region);

		if (command == "mkfs")
			return dfs_mkfs(s.vol.get(), s.dev, s.partitionSize, args);
		if (command == "resize")
			return dfs_resize(s.vol.get(), s.dev, s.partitionSize, args);

		// Everything else works on the mounted volume.
		const bool readOnly = (command == "info" || command == "ls" || command == "tree" || command == "cat" || command == "get"
								|| command == "stat" || command == "fsck" || command == "dump-inode" || command == "dump-block");
		dfs::i32 r = dfs::mount(s.vol.get(), &s.dev, readOnly);
		if (r < 0)
			return dfs_fail(String("unable to mount partition ").add(partArg), r);
		s.mounted = true;
		dfs::Volume* vol = s.vol.get();

		auto need = [&](size_t count, const char* usage) -> bool {
			if (args.size() >= count)
				return true;
			out.fg(ConsoleColors::Red).p("Error: too few arguments.").nl().p("  Usage: ./dtools dfs <image> <partition#> ").p(usage).reset().nl();
			return false;
		};

		if (command == "info")
			return dfs_info(vol);
		if (command == "ls")
		{
			bool longFormat = false;
			String path = "/";
			for (const auto& arg : args)
			{
				if (arg == "-l")
					longFormat = true;
				else
					path = arg;
			}
			return dfs_ls(vol, path, longFormat);
		}
		if (command == "tree")
			return dfs_tree(vol, args.empty() ? String("/") : args[0]);
		if (command == "cat")
		{
			if (!need(1, "cat <path>"))
				return ErrorDfsTooFewArgs;
			return dfs_cat(vol, args[0]);
		}
		if (command == "put")
		{
			const bool recursive = (!args.empty() && args[0] == "-r");
			if (recursive)
				args.erase(args.begin());
			if (!need(2, "put [-r] <host_path> <path>"))
				return ErrorDfsTooFewArgs;
			return dfs_put(vol, args[0], args[1], recursive);
		}
		if (command == "install-kernel")
		{
			if (!need(1, "install-kernel <kernel.bin> [path]"))
				return ErrorDfsTooFewArgs;
			return dfs_install_kernel(vol, args[0], args.size() > 1 ? args[1] : String("/boot/kernel0"), part.hasFlag(0));
		}
		if (command == "get")
		{
			if (!need(2, "get <path> <host_file>"))
				return ErrorDfsTooFewArgs;
			return dfs_get(vol, args[0], args[1]);
		}
		if (command == "mkdir" || command == "rm" || command == "rmdir")
		{
			if (!need(1, (command + " <path>").c_str()))
				return ErrorDfsTooFewArgs;
			if (command == "mkdir")
				r = dfs::mkdir(vol, cpath(args[0]), 0755, nullptr);
			else if (command == "rm")
				r = dfs::unlink(vol, cpath(args[0]));
			else
				r = dfs::rmdir(vol, cpath(args[0]));
			return (r < 0 ? dfs_fail(command + " " + args[0], r) : ErrorNoError);
		}
		if (command == "mv" || command == "ln")
		{
			if (!need(2, (command + " <from> <to>").c_str()))
				return ErrorDfsTooFewArgs;
			if (command == "mv")
				r = dfs::rename(vol, cpath(args[0]), cpath(args[1]));
			else
				r = dfs::link(vol, cpath(args[0]), cpath(args[1]));
			return (r < 0 ? dfs_fail(command + " " + args[0] + " " + args[1], r) : ErrorNoError);
		}
		if (command == "stat")
		{
			if (!need(1, "stat <path>"))
				return ErrorDfsTooFewArgs;
			return dfs_stat(vol, args[0]);
		}
		if (command == "chmod")
		{
			if (!need(2, "chmod <octal_mode> <path>"))
				return ErrorDfsTooFewArgs;
			u32 mode = 0;
			try { mode = cast<u32>(std::stoul(args[0].cpp_str(), nullptr, 8)); }
			catch (...) { return dfs_fail("<mode> must be octal (e.g. 755)"); }
			r = dfs::chmod(vol, cpath(args[1]), cast<u16>(mode));
			return (r < 0 ? dfs_fail("chmod " + args[1], r) : ErrorNoError);
		}
		if (command == "chown")
		{
			if (!need(2, "chown <uid:gid> <path>"))
				return ErrorDfsTooFewArgs;
			const std::string ids = args[0].cpp_str();
			const size_t colon = ids.find(':');
			u32 uid = 0, gid = 0;
			try
			{
				if (colon == std::string::npos)
					throw 0;
				uid = cast<u32>(std::stoul(ids.substr(0, colon)));
				gid = cast<u32>(std::stoul(ids.substr(colon + 1)));
			}
			catch (...) { return dfs_fail("<uid:gid> must be two numbers (e.g. 0:0)"); }
			r = dfs::chown(vol, cpath(args[1]), cast<u16>(uid), cast<u16>(gid));
			return (r < 0 ? dfs_fail("chown " + args[1], r) : ErrorNoError);
		}
		if (command == "fsck")
			return dfs_fsck(vol);
		if (command == "dump-inode" || command == "dump-block")
		{
			if (!need(1, (command + " <number>").c_str()))
				return ErrorDfsTooFewArgs;
			if (!args[0].isInt() || args[0].toInt() < 0)
				return dfs_fail("<number> must be a number");
			return dfs_dump(vol, cast<u32>(args[0].toInt()), command == "dump-inode");
		}
		out.fg(ConsoleColors::Red).p("Error: unknown dfs command '").p(command).p("'.").reset().nl();
		print_dfs_help();
		return ErrorDfsTooFewArgs;
	}

	i32 Tools::dfs_mkfs(dfs::Volume* vol, const dfs::Device& dev, u64 partitionSize, const stdvec<String>& args)
	{
		u32 blockSize = 4096, bytesPerInode = 16384;
		String label = "";
		for (size_t i = 0; i < args.size(); i++)
		{
			const bool hasValue = (i + 1 < args.size());
			u64 value = 0;
			if (args[i] == "--block-size" && hasValue && storage::parseSize(args[i + 1], value))
				blockSize = cast<u32>(value), i++;
			else if (args[i] == "--bytes-per-inode" && hasValue && storage::parseSize(args[i + 1], value))
				bytesPerInode = cast<u32>(value), i++;
			else if (args[i] == "--label" && hasValue)
				label = args[++i];
			else
				return dfs_fail(String("invalid mkfs option '").add(args[i]).add("'"));
		}
		u32 log2 = 0;
		while ((1u << log2) < blockSize && log2 < 31)
			log2++;
		if ((1u << log2) != blockSize || log2 < dfs::MinBlockSizeLog2 || log2 > dfs::MaxBlockSizeLog2)
			return dfs_fail("--block-size must be 512, 1024, 2048 or 4096");
		if (partitionSize > 0xFFFFFFFFull)
			return dfs_fail("DFS partitions are at most 4 GiB");
		if (label.len() > 16)
			return dfs_fail("the label is at most 16 characters");

		dfs::u8 uuid[16];
		std::random_device random;
		for (auto& byte : uuid)
			byte = cast<dfs::u8>(random());
		dfs::MkfsParams params { cast<u32>(partitionSize), log2, bytesPerInode, cpath(label), uuid };
		dfs::i32 r = dfs::mkfs(vol, &dev, &params);
		if (r < 0)
			return dfs_fail("mkfs failed", r);
		const dfs::Layout& l = vol->layout;
		out.fg(ConsoleColors::Green).p("DFS created: ").reset();
		out.p(storage::formatSize(cast<u64>(l.total_blocks) << l.block_size_log2)).p(", ").p(l.total_blocks).p(" blocks of ").p(l.block_size).p(" bytes, ");
		out.p(l.inode_count).p(" inodes, data from block ").p(l.first_data_block).nl();
		return ErrorNoError;
	}

	// The file system to a size (the partition's by default; it can't be bigger).
	i32 Tools::dfs_resize(dfs::Volume* vol, const dfs::Device& dev, u64 partitionSize, const stdvec<String>& args)
	{
		u64 size = std::min<u64>(partitionSize, 0xFFFFFFFFull);
		if (!args.empty() && (!storage::parseSize(args[0], size) || size == 0))
			return dfs_fail(String("invalid size '").add(args[0]).add("'"));
		if (size > partitionSize)
			return dfs_fail(String("the partition is ").add(storage::formatSize(partitionSize)).add(": grow it first"));
		if (size > 0xFFFFFFFFull)
			return dfs_fail("DFS partitions are at most 4 GiB");
		dfs::ResizeInfo info;
		dfs::FsckResult check;
		dfs::i32 r = dfs::resize(vol, &dev, cast<u32>(size), &info, &check);
		for (const auto& message : check.messages)
			out.fg(ConsoleColors::Yellow).p("  ").p(message).reset().nl();
		if (r == dfs::NoSpace)
			return dfs_fail(String("too small: the file system needs at least ").add(storage::formatSize(info.min_size)), r);
		if (r == dfs::Corrupt)
			return dfs_fail("the file system has errors (see fsck)", r);
		if (r < 0)
			return dfs_fail("resize failed", r);
		out.fg(ConsoleColors::Green).p("Resized: ").reset().p(info.old_blocks).p(" -> ").p(info.new_blocks).p(" blocks (");
		out.p(storage::formatSize(cast<u64>(info.new_blocks) * info.block_size)).p("), ").p(info.moved_blocks).p(" blocks moved");
		out.p(info.table_moves ? ", the inode table moved" : "").nl();
		return ErrorNoError;
	}

	i32 Tools::dfs_info(dfs::Volume* vol)
	{
		const dfs::Superblock& sb = vol->sb;
		const dfs::Layout& l = vol->layout;
		auto row = [](const String& name) -> ostd::OutputHandlerBase& { return out.fg(ConsoleColors::Blue).p(String(name).new_fixedLength(22)).fg(ConsoleColors::Cyan); };
		String label = "";
		for (u32 i = 0; i < 16 && sb.label[i] != 0; i++)
			label += cast<char>(sb.label[i]);
		String uuid = "";
		for (u32 i = 0; i < 16; i++)
		{
			uuid += String::getHexStr(sb.uuid[i], false, 1).toLower();
			if (i == 3 || i == 5 || i == 7 || i == 9)
				uuid += "-";
		}
		row("Label").p(label == "" ? String("<none>") : label).nl();
		row("UUID").p(uuid).nl();
		row("Version").p(sb.version).nl();
		row("State").p(sb.state == dfs::StateClean ? "clean" : (sb.state == dfs::StateDirty ? "dirty (not unmounted)" : "unknown")).nl();
		row("Block size").p(l.block_size).p(" bytes").nl();
		row("Blocks").p(l.total_blocks).p(" (").p(sb.free_blocks).p(" free, ").p(storage::formatSize(cast<u64>(sb.free_blocks) << l.block_size_log2)).p(")").nl();
		row("Inodes").p(l.inode_count).p(" (").p(sb.free_inodes).p(" free), ").p(l.inode_size).p(" bytes each").nl();
		row("Directory entries").p(l.dirent_size).p(" bytes, names up to ").p(dfs::MaxNameLength).nl();
		row("Inode bitmap").p("block ").p(l.inode_bitmap_block).p(", ").p(l.inode_bitmap_len).p(" blocks").nl();
		row("Block bitmap").p("block ").p(l.block_bitmap_block).p(", ").p(l.block_bitmap_len).p(" blocks").nl();
		row("Inode table").p("block ").p(l.inode_table_block).p(", ").p(l.inode_table_len).p(" blocks").nl();
		row("First data block").p(l.first_data_block).nl();
		row("Features").p("compat ").p(String::getHexStr(sb.feature_compat, true, 4)).p(", ro_compat ").p(String::getHexStr(sb.feature_ro_compat, true, 4))
				.p(", incompat ").p(String::getHexStr(sb.feature_incompat, true, 4)).nl();
		row("Mount count").p(sb.mount_count).nl();
		row("Created").p(format_time(sb.created)).nl();
		row("Last mount").p(format_time(sb.last_mount)).nl();
		row("Last write").p(format_time(sb.last_write)).reset().nl();
		return ErrorNoError;
	}

	i32 Tools::dfs_ls(dfs::Volume* vol, const String& path, bool longFormat)
	{
		dfs::Inode node;
		u32 ino = 0;
		dfs::i32 r = dfs::stat(vol, cpath(path), &node, &ino);
		if (r < 0)
			return dfs_fail(path, r);
		auto print = [&](const String& name, u32 entryIno, const dfs::Inode& entry) {
			const bool dir = (entry.mode & dfs::ModeTypeMask) == dfs::ModeDir;
			if (longFormat)
			{
				out.fg(ConsoleColors::BrightGray).p(format_mode(entry.mode)).p(" ").p(String("").add(entry.links).new_fixedLength(4));
				out.p(String("").add(entry.uid).add(":").add(entry.gid).new_fixedLength(10));
				out.p(String("").add(entry.size).new_fixedLength(12)).p(format_time(entry.mtime)).p("  ");
				out.p(String("#").add(entryIno).new_fixedLength(7));
			}
			out.fg(dir ? ConsoleColors::Blue : ConsoleColors::Cyan).p(name).p(dir ? "/" : "").reset().nl();
		};
		if ((node.mode & dfs::ModeTypeMask) != dfs::ModeDir)
		{
			print(path, ino, node);
			return ErrorNoError;
		}
		u32 cursor = 0;
		dfs::DirEntry entry;
		while ((r = dfs::dir_next(vol, &node, &cursor, &entry)) == dfs::Ok)
		{
			dfs::Inode child;
			if (longFormat && (r = dfs::read_inode(vol, entry.inode, &child)) < 0)
				return dfs_fail(String("inode ").add(entry.inode), r);
			print(reinterpret_cast<const char*>(entry.name), entry.inode, child);
		}
		return (r == dfs::NotFound ? ErrorNoError : dfs_fail(path, r));
	}

	i32 Tools::dfs_tree(dfs::Volume* vol, const String& path)
	{
		u32 ino = 0;
		dfs::i32 r = dfs::resolve(vol, cpath(path), &ino);
		if (r < 0)
			return dfs_fail(path, r);
		out.fg(ConsoleColors::Blue).p(path).reset().nl();
		u32 dirs = 0, files = 0;
		std::function<dfs::i32(u32, const String&, u32)> walk = [&](u32 dirIno, const String& prefix, u32 depth) -> dfs::i32 {
			if (depth > 64)
				return dfs::Corrupt;
			dfs::Inode dir;
			dfs::i32 r = dfs::read_inode(vol, dirIno, &dir);
			if (r < 0)
				return r;
			if ((dir.mode & dfs::ModeTypeMask) != dfs::ModeDir)
				return dfs::Ok;
			// Collect first: the walk below reuses the volume's directory buffer.
			stdvec<std::pair<String, dfs::DirEntry>> entries;
			u32 cursor = 0;
			dfs::DirEntry entry;
			while ((r = dfs::dir_next(vol, &dir, &cursor, &entry)) == dfs::Ok)
			{
				String name = reinterpret_cast<const char*>(entry.name);
				if (name != "." && name != "..")
					entries.push_back({ name, entry });
			}
			if (r != dfs::NotFound)
				return r;
			for (size_t i = 0; i < entries.size(); i++)
			{
				const bool last = (i + 1 == entries.size());
				const bool isDir = (entries[i].second.type == (dfs::ModeDir >> 12));
				out.fg(ConsoleColors::BrightGray).p(prefix).p(last ? "└── " : "├── ");
				out.fg(isDir ? ConsoleColors::Blue : ConsoleColors::Cyan).p(entries[i].first).p(isDir ? "/" : "").reset().nl();
				if (isDir)
				{
					dirs++;
					if ((r = walk(entries[i].second.inode, prefix + (last ? "    " : "│   "), depth + 1)) < 0)
						return r;
				}
				else
					files++;
			}
			return dfs::Ok;
		};
		if ((r = walk(ino, "", 0)) < 0)
			return dfs_fail(path, r);
		out.nl().p(dirs).p(" directories, ").p(files).p(" files").nl();
		return ErrorNoError;
	}

	i32 Tools::dfs_cat(dfs::Volume* vol, const String& path)
	{
		dfs::Inode node;
		dfs::i32 r = dfs::stat(vol, cpath(path), &node, nullptr);
		if (r < 0)
			return dfs_fail(path, r);
		if ((node.mode & dfs::ModeTypeMask) == dfs::ModeDir)
			return dfs_fail(path, dfs::IsDir);
		stdvec<dfs::u8> chunk(64 * 1024);
		for (u32 offset = 0; offset < node.size; )
		{
			if ((r = dfs::inode_read(vol, &node, offset, chunk.data(), cast<u32>(chunk.size()))) < 0)
				return dfs_fail(path, r);
			std::cout.write(reinterpret_cast<const char*>(chunk.data()), r);
			offset += cast<u32>(r);
		}
		std::cout.flush();
		return ErrorNoError;
	}

	i32 Tools::dfs_put_file(dfs::Volume* vol, const String& hostPath, const String& path)
	{
		stdvec<u8> data;
		String error = "";
		if (!storage::readFile(hostPath, data, error))
			return dfs_fail(error);
		if (data.size() > 0x7FFFFFFF)
			return dfs_fail(String("'").add(hostPath).add("' is too big"));
		return dfs_write_data(vol, data, path);
	}

	// Creates the file, or replaces an existing file's content.
	i32 Tools::dfs_write_data(dfs::Volume* vol, const stdvec<u8>& data, const String& path)
	{
		u32 ino = 0;
		dfs::Inode node;
		dfs::i32 r = dfs::create(vol, cpath(path), 0644, &ino);
		if (r == dfs::Exists)
		{
			// Replace an existing file's content.
			if ((r = dfs::stat(vol, cpath(path), &node, &ino)) < 0)
				return dfs_fail(path, r);
			if ((node.mode & dfs::ModeTypeMask) == dfs::ModeDir)
				return dfs_fail(path, dfs::IsDir);
			if ((r = dfs::truncate(vol, ino, &node, 0)) < 0)
				return dfs_fail(path, r);
		}
		else if (r < 0)
			return dfs_fail(path, r);
		else if ((r = dfs::read_inode(vol, ino, &node)) < 0)
			return dfs_fail(path, r);
		if (!data.empty())
		{
			r = dfs::inode_write(vol, ino, &node, 0, data.data(), cast<u32>(data.size()));
			if (r < 0)
				return dfs_fail(path, r);
			if (cast<size_t>(r) != data.size())
				return dfs_fail(String(path).add(": only ").add(r).add(" of ").add(cast<u64>(data.size())).add(" bytes written"), dfs::NoSpace);
		}
		out.fg(ConsoleColors::Green).p(path).reset().p(" (").p(storage::formatSize(data.size())).p(")").nl();
		return ErrorNoError;
	}

	// The kernel as a file (Drake loads it at 0x01410000, at most 1 MiB), and the DPT boot
	// header (0xF1C4, then 0xFF) in the partition's first 32 bytes, which DFS never uses.
	i32 Tools::dfs_install_kernel(dfs::Volume* vol, const String& hostPath, const String& path, bool bootFlag)
	{
		const u32 HeaderSize = 32;
		const u32 MaxKernelSize = 1048576;
		stdvec<u8> data;
		String error = "";
		if (!storage::readFile(hostPath, data, error))
			return dfs_fail(error);
		// Built with --header KERNEL0_BOOT: the file has the header, Drake only wants the code.
		if (data.size() >= HeaderSize && data[0] == 0xF1 && data[1] == 0xC4)
			data.erase(data.begin(), data.begin() + HeaderSize);
		if (data.empty() || data.size() > MaxKernelSize)
			return dfs_fail(String("the kernel must be 1 to ").add(MaxKernelSize).add(" bytes (it is ").add(cast<u64>(data.size())).add(")"));
		const std::string target = path.cpp_str();
		if (target.size() < 2 || target.front() != '/' || target.back() == '/')
			return dfs_fail(String("invalid kernel path '").add(path).add("'"));
		// Its directory (one level, e.g. /boot).
		const size_t slash = target.rfind('/');
		if (slash > 0)
		{
			const String dir = target.substr(0, slash);
			dfs::i32 r = dfs::mkdir(vol, cpath(dir), 0755, nullptr);
			if (r < 0 && r != dfs::Exists)
				return dfs_fail(dir, r);
		}
		const i32 result = dfs_write_data(vol, data, path);
		if (result != ErrorNoError)
			return result;
		dfs::u8 header[HeaderSize];
		std::fill(header, header + HeaderSize, cast<dfs::u8>(0xFF));
		header[0] = 0xF1;
		header[1] = 0xC4;
		if (vol->dev.write(vol->dev.user, 0, header, HeaderSize) < 0)
			return dfs_fail("unable to write the boot header", dfs::IOError);
		out.fg(ConsoleColors::Green).p("Boot header written").reset().nl();
		if (!bootFlag)
			out.fg(ConsoleColors::Yellow).p("Warning: the partition doesn't have the DPT boot flag: Drake won't list it.").reset().nl();
		return ErrorNoError;
	}

	i32 Tools::dfs_put(dfs::Volume* vol, const String& hostPath, const String& path, bool recursive)
	{
		std::error_code ec;
		if (!fs::is_directory(hostPath.cpp_str(), ec))
		{
			// Into an existing directory: keep the host file's name.
			String target = path;
			dfs::Inode node;
			if (dfs::stat(vol, cpath(path), &node, nullptr) == dfs::Ok && (node.mode & dfs::ModeTypeMask) == dfs::ModeDir)
				target = join_path(path, fs::path(hostPath.cpp_str()).filename().string());
			return dfs_put_file(vol, hostPath, target);
		}
		if (!recursive)
			return dfs_fail(String("'").add(hostPath).add("' is a directory (use put -r)"));
		dfs::i32 r = dfs::mkdir(vol, cpath(path), 0755, nullptr);
		if (r < 0 && r != dfs::Exists)
			return dfs_fail(path, r);
		stdvec<fs::directory_entry> entries;
		for (const auto& entry : fs::directory_iterator(hostPath.cpp_str(), ec))
			entries.push_back(entry);
		std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });
		for (const auto& entry : entries)
		{
			const String target = join_path(path, entry.path().filename().string());
			i32 result = (entry.is_directory() ? dfs_put(vol, entry.path().string(), target, true) : dfs_put_file(vol, entry.path().string(), target));
			if (result != ErrorNoError)
				return result;
		}
		return ErrorNoError;
	}

	i32 Tools::dfs_get(dfs::Volume* vol, const String& path, const String& hostPath)
	{
		dfs::Inode node;
		dfs::i32 r = dfs::stat(vol, cpath(path), &node, nullptr);
		if (r < 0)
			return dfs_fail(path, r);
		if ((node.mode & dfs::ModeTypeMask) == dfs::ModeDir)
			return dfs_fail(path, dfs::IsDir);
		stdvec<dfs::u8> data(node.size);
		if (node.size > 0 && (r = dfs::inode_read(vol, &node, 0, data.data(), node.size)) < 0)
			return dfs_fail(path, r);
		std::ofstream file(hostPath.cpp_str(), std::ios::binary | std::ios::trunc);
		if (!file || !file.write(reinterpret_cast<const char*>(data.data()), cast<std::streamsize>(data.size())))
			return dfs_fail(String("unable to write '").add(hostPath).add("'"));
		out.fg(ConsoleColors::Green).p(hostPath).reset().p(" (").p(storage::formatSize(data.size())).p(")").nl();
		return ErrorNoError;
	}

	i32 Tools::dfs_stat(dfs::Volume* vol, const String& path)
	{
		dfs::Inode node;
		u32 ino = 0;
		dfs::i32 r = dfs::stat(vol, cpath(path), &node, &ino);
		if (r < 0)
			return dfs_fail(path, r);
		auto row = [](const String& name) -> ostd::OutputHandlerBase& { return out.fg(ConsoleColors::Blue).p(String(name).new_fixedLength(10)).fg(ConsoleColors::Cyan); };
		row("Path").p(path).nl();
		row("Inode").p(ino).nl();
		row("Type").p(type_name(node.mode)).nl();
		row("Mode").p(format_mode(node.mode)).p(" (").p(octal(node.mode & dfs::ModePermMask)).p(")").nl();
		row("Links").p(node.links).nl();
		row("Owner").p(node.uid).p(":").p(node.gid).nl();
		row("Size").p(node.size).p(" bytes").nl();
		u32 blocks = 0;
		for (u32 i = 0; i < (node.size + vol->layout.block_mask) >> vol->layout.block_size_log2; i++)
		{
			u32 block = 0;
			if (dfs::bmap(vol, &node, i, false, &block) >= 0 && block != 0)
				blocks++;
		}
		row("Blocks").p(blocks).p(" data").p(node.indirect ? ", indirect" : "").p(node.dindirect ? ", double indirect" : "").nl();
		row("Created").p(format_time(node.crtime)).nl();
		row("Modified").p(format_time(node.mtime)).nl();
		row("Changed").p(format_time(node.ctime)).reset().nl();
		return ErrorNoError;
	}

	i32 Tools::dfs_fsck(dfs::Volume* vol)
	{
		dfs::FsckResult result;
		dfs::i32 r = dfs::fsck(vol, result);
		if (r < 0)
			return dfs_fail("fsck stopped", r);
		for (const auto& message : result.messages)
			out.fg(ConsoleColors::Yellow).p("  ").p(message).reset().nl();
		out.p(result.directories).p(" directories, ").p(result.files).p(" files: ");
		if (result.clean())
		{
			out.fg(ConsoleColors::Green).p("clean").reset().nl();
			return ErrorNoError;
		}
		out.fg(ConsoleColors::Red).p(result.errors).p(" errors, ").p(result.leaked_blocks).p(" leaked blocks, ").p(result.leaked_inodes).p(" leaked inodes").reset().nl();
		return ErrorDfsFailed;
	}

	i32 Tools::dfs_dump(dfs::Volume* vol, u32 number, bool inode)
	{
		const dfs::Layout& l = vol->layout;
		stdvec<dfs::u8> data;
		u32 base = 0;
		if (inode)
		{
			if (number == 0 || number >= l.inode_count)
				return dfs_fail(String("inode numbers go from 1 to ").add(l.inode_count - 1));
			const u32 byte = number * l.inode_size;
			base = ((l.inode_table_block << l.block_size_log2) + byte);
			data.resize(l.inode_size);
			dfs::Inode node;
			if (dfs::i32 r = dfs::read_inode(vol, number, &node); r < 0)
				return dfs_fail(String("inode ").add(number), r);
			out.fg(ConsoleColors::Blue).p("Inode ").p(number).p(": ").fg(ConsoleColors::Cyan).p(type_name(node.mode)).p(", ").p(format_mode(node.mode)).p(", ").p(node.links).p(" links, ").p(node.size).p(" bytes").nl();
			out.p("  direct:");
			for (u32 i = 0; i < dfs::DirectPointers; i++)
				out.p(" ").p(node.direct[i]);
			out.nl().p("  indirect: ").p(node.indirect).p(", double indirect: ").p(node.dindirect).reset().nl();
		}
		else
		{
			if (number >= l.total_blocks)
				return dfs_fail(String("block numbers go from 0 to ").add(l.total_blocks - 1));
			base = number << l.block_size_log2;
			data.resize(l.block_size);
		}
		if (vol->dev.read(vol->dev.user, base, data.data(), cast<u32>(data.size())) < 0)
			return dfs_fail("read failed", dfs::IOError);
		for (u32 row = 0; row < data.size(); row += 16)
		{
			out.fg(ConsoleColors::BrightGray).p(String::getHexStr(base + row, true, 4)).p("  ").fg(ConsoleColors::Cyan);
			String ascii = "";
			for (u32 i = 0; i < 16; i++)
			{
				const u8 byte = data[row + i];
				out.p(String::getHexStr(byte, false, 1)).p(i == 7 ? "  " : " ");
				ascii += (byte >= 32 && byte < 127 ? cast<char>(byte) : '.');
			}
			out.fg(ConsoleColors::Green).p(" ").p(ascii).reset().nl();
		}
		return ErrorNoError;
	}

	void Tools::print_dfs_help(void)
	{
		out.fg(ConsoleColors::Blue).p("dfs <virtual_disk_file> <partition#> <command> [...]").nl();
		out.fg(ConsoleColors::Green).p("The <dfs> tool works on the DFS file system of a partition (1 = the first one).").nl();
		out.p("    mkfs [--block-size 4096] [--bytes-per-inode 16384] [--label NAME]").nl();
		out.p("    resize [size]                Grows / shrinks the file system (to the partition's size by").nl();
		out.p("                                 default): resize the partition first to grow, after to shrink.").nl();
		out.p("    info                         The superblock and the layout.").nl();
		out.p("    ls [-l] [path]               A directory's entries (or a file).").nl();
		out.p("    tree [path]").nl();
		out.p("    cat <path>                   A file's content, to stdout.").nl();
		out.p("    put [-r] <host_path> <path>  Copies a host file (or directory, with -r) in.").nl();
		out.p("    get <path> <host_file>       Copies a file out.").nl();
		out.p("    install-kernel <kernel.bin> [/boot/kernel0]").nl();
		out.p("                                 The kernel as a file (without a KERNEL0_BOOT header), and the").nl();
		out.p("                                 partition's boot header (Drake boots it).").nl();
		out.p("    mkdir | rm | rmdir <path>").nl();
		out.p("    mv <from> <to>               Renames / moves (the destination must not exist).").nl();
		out.p("    ln <existing> <new>          A hard link (not for directories).").nl();
		out.p("    stat <path>").nl();
		out.p("    chmod <octal_mode> <path>").nl();
		out.p("    chown <uid:gid> <path>").nl();
		out.p("    fsck                         Checks the file system (read only).").nl();
		out.p("    dump-inode <n> | dump-block <n>").nl().nl().reset();
	}
}
