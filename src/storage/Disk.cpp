#include "Disk.hpp"

#include "../hardware/VirtualHardDrive.hpp"
#include <cmath>

namespace dragon
{
	namespace storage
	{
		//Disk
		bool Disk::readU16(u64 address, u16& out)
		{
			u8 bytes[2] { 0, 0 };
			if (!read(address, bytes))
				return false;
			out = cast<u16>((bytes[0] << 8) | bytes[1]);
			return true;
		}

		bool Disk::readU32(u64 address, u32& out)
		{
			u8 bytes[4] { 0, 0, 0, 0 };
			if (!read(address, bytes))
				return false;
			out = (cast<u32>(bytes[0]) << 24) | (cast<u32>(bytes[1]) << 16) | (cast<u32>(bytes[2]) << 8) | bytes[3];
			return true;
		}

		bool Disk::writeU16(u64 address, u16 value)
		{
			const u8 bytes[2] { cast<u8>(value >> 8), cast<u8>(value) };
			return write(address, bytes);
		}

		bool Disk::writeU32(u64 address, u32 value)
		{
			const u8 bytes[4] { cast<u8>(value >> 24), cast<u8>(value >> 16), cast<u8>(value >> 8), cast<u8>(value) };
			return write(address, bytes);
		}




		//FileDisk
		bool FileDisk::open(const String& path)
		{
			close();
			m_file.open(path.cpp_str(), std::ios::in | std::ios::out | std::ios::binary);
			if (!m_file)
				return false;
			m_file.seekg(0, std::ios::end);
			m_size = cast<u64>(cast<i64>(m_file.tellg()));
			m_file.seekg(0, std::ios::beg);
			m_path = path;
			return true;
		}

		void FileDisk::close(void)
		{
			if (m_file.is_open())
				m_file.close();
			m_file.clear();
			m_size = 0;
		}

		bool FileDisk::read(u64 address, std::span<u8> out)
		{
			if (!isOpen() || !contains(address, out.size()))
				return false;
			m_file.seekg(cast<std::streamoff>(address));
			m_file.read(reinterpret_cast<char*>(out.data()), cast<std::streamsize>(out.size()));
			const bool ok = cast<u64>(m_file.gcount()) == out.size();
			m_file.clear();
			return ok;
		}

		bool FileDisk::write(u64 address, std::span<const u8> data)
		{
			if (!isOpen() || !contains(address, data.size()))
				return false;
			m_file.seekp(cast<std::streamoff>(address));
			m_file.write(reinterpret_cast<const char*>(data.data()), cast<std::streamsize>(data.size()));
			m_file.flush();
			const bool ok = m_file.good();
			m_file.clear();
			return ok;
		}




		//DriveDisk
		u64 DriveDisk::getSize(void) const
		{
			return m_drive.getSize();
		}

		bool DriveDisk::read(u64 address, std::span<u8> out)
		{
			if (!contains(address, out.size()))
				return false;
			return m_drive.readBlock(cast<u32>(address), cast<u32>(out.size()), out.data());
		}

		bool DriveDisk::write(u64 address, std::span<const u8> data)
		{
			if (!contains(address, data.size()))
				return false;
			return m_drive.writeBlock(cast<u32>(address), cast<u32>(data.size()), data.data());
		}




		//MemoryDisk
		bool MemoryDisk::read(u64 address, std::span<u8> out)
		{
			if (!contains(address, out.size()))
				return false;
			std::copy_n(m_data.begin() + cast<i64>(address), out.size(), out.begin());
			return true;
		}

		bool MemoryDisk::write(u64 address, std::span<const u8> data)
		{
			if (!contains(address, data.size()))
				return false;
			std::copy(data.begin(), data.end(), m_data.begin() + cast<i64>(address));
			if (m_onWrite)
				m_onWrite(address, data.size());
			return true;
		}




		//DiskRegion
		bool DiskRegion::read(u64 address, std::span<u8> out)
		{
			if (!contains(address, out.size()))
				return false;
			return m_disk.read(m_start + address, out);
		}

		bool DiskRegion::write(u64 address, std::span<const u8> data)
		{
			if (!contains(address, data.size()))
				return false;
			return m_disk.write(m_start + address, data);
		}




		bool createDiskFile(const String& path, u64 size, String& outError)
		{
			std::ofstream file(path.cpp_str(), std::ios::out | std::ios::binary | std::ios::trunc);
			if (!file)
			{
				outError = String("Unable to create the file: ").add(path);
				return false;
			}
			const stdvec<char> zeros(64 * 1024, 0);
			u64 left = size;
			while (left > 0 && file)
			{
				const u64 chunk = std::min<u64>(left, zeros.size());
				file.write(zeros.data(), cast<std::streamsize>(chunk));
				left -= chunk;
			}
			if (!file)
			{
				outError = String("Unable to write the file: ").add(path);
				return false;
			}
			return true;
		}

		bool readFile(const String& filePath, stdvec<u8>& outData, String& outError)
		{
			std::ifstream file(filePath.cpp_str(), std::ios::in | std::ios::binary);
			if (!file)
			{
				outError = String("Unable to open the file: ").add(filePath);
				return false;
			}
			file.seekg(0, std::ios::end);
			const i64 size = cast<i64>(file.tellg());
			file.seekg(0, std::ios::beg);
			outData.assign(cast<size_t>(std::max<i64>(0, size)), 0);
			file.read(reinterpret_cast<char*>(outData.data()), cast<std::streamsize>(outData.size()));
			if (cast<u64>(file.gcount()) != outData.size())
			{
				outError = String("Unable to read the file: ").add(filePath);
				return false;
			}
			return true;
		}

		bool loadBinary(Disk& disk, const String& filePath, u64 address, u64& outSize, String& outError)
		{
			stdvec<u8> data;
			if (!readFile(filePath, data, outError))
				return false;
			outSize = data.size();
			if (!disk.contains(address, data.size()))
			{
				outError = String("The data (").add(formatSize(data.size())).add(") doesn't fit on the disk at ").add(String::getHexStr(address, true, 4)).add(".");
				return false;
			}
			if (!disk.write(address, data))
			{
				outError = "Unable to write to the disk.";
				return false;
			}
			return true;
		}

		String formatSize(u64 bytes)
		{
			if (bytes < 1024)
				return String("").add(bytes).add(bytes == 1 ? " byte" : " bytes");
			const char* units[] = { "KiB", "MiB", "GiB", "TiB" };
			f64 value = cast<f64>(bytes) / 1024.0;
			i32 unit = 0;
			while (value >= 1024.0 && unit < 3)
			{
				value /= 1024.0;
				unit++;
			}
			return String("").add(value, 2).add(" ").add(units[unit]);
		}

		bool parseSize(const String& text, u64& outValue)
		{
			String str = text.new_trim().toLower();
			if (str == "")
				return false;
			if (str.startsWith("0x"))
			{
				const String digits = str.new_substr(2);
				if (digits == "" || digits.len() > 16)
					return false;
				u64 value = 0;
				for (char c : digits.cpp_str())
				{
					value <<= 4;
					if (c >= '0' && c <= '9') value |= cast<u64>(c - '0');
					else if (c >= 'a' && c <= 'f') value |= cast<u64>(c - 'a' + 10);
					else return false;
				}
				outValue = value;
				return true;
			}
			u64 multiplier = 1;
			if (str.endsWith("ib"))
				str = str.new_substr(0, str.len() - 2);
			else if (str.endsWith("b"))
				str = str.new_substr(0, str.len() - 1);
			if (str.endsWith("k")) multiplier = 1024ull;
			else if (str.endsWith("m")) multiplier = 1024ull * 1024;
			else if (str.endsWith("g")) multiplier = 1024ull * 1024 * 1024;
			if (multiplier != 1)
				str = str.new_substr(0, str.len() - 1).trim();
			if (str == "")
				return false;
			bool dot = false;
			for (char c : str.cpp_str())
			{
				if (c == '.' && !dot) { dot = true; continue; }
				if (c < '0' || c > '9')
					return false;
			}
			const f64 value = std::stod(str.cpp_str()) * cast<f64>(multiplier);
			if (value < 0 || value > 1.8e19)
				return false;
			outValue = cast<u64>(std::llround(value));
			return true;
		}
	}
}
