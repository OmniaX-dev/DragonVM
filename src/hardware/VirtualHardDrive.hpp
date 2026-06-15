#pragma once

#include <fstream>
#include <ostd/string/String.hpp>

namespace dragon
{
	namespace hw
	{
		class VirtualHardDrive
		{
			public:
				inline VirtualHardDrive(void) { m_initialized = false; }
				inline VirtualHardDrive(const String& dataFilePath) { init(dataFilePath); }
				void init(const String& dataFilePath);

				bool read(u32 addr, u16 size, ostd::ByteStream& outData);
				bool write(u32 addr, i8 value);
				void bufferedWrite(i8 value);
				bool writeBuffer(u32 addr);

				void unmount(void);

				inline bool isInitialized(void) const { return m_initialized; }
				inline u64 getSize(void) const { return m_fileSize; };
				inline bool isSame(VirtualHardDrive& vhdd) { return m_diskID == vhdd.m_diskID; }

			private:
				std::fstream m_dataFile;
				bool m_initialized { false };
				u64 m_fileSize { 0 };
				ostd::ByteStream m_writeBuffer;
				u32 m_diskID { 0 };

				inline static u32 s_nextDiskID = 0;
		};
	}
}
