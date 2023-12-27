#pragma once

#include <fstream>
#include <ostd/Utils.hpp>

namespace dragon
{
	namespace hw
	{
		class VirtualHardDrive
		{
			public:
				inline VirtualHardDrive(void) { m_initialized = false; }
				inline VirtualHardDrive(const ostd::String& dataFilePath) { init(dataFilePath); }
				void init(const ostd::String& dataFilePath);

				bool read(uint32_t addr, uint16_t size, ostd::ByteStream& outData);
				bool write(uint32_t addr, int8_t value);
				void bufferedWrite(int8_t value);
				bool writeBuffer(uint32_t addr);

				void unmount(void);
				
				inline bool isInitialized(void) const { return m_initialized; }
				inline uint64_t getSize(void) const { return m_fileSize; };
				inline bool isSame(VirtualHardDrive& vhdd) { return m_diskID == vhdd.m_diskID; }

			private:
				std::fstream m_dataFile;
				bool m_initialized { false };
				uint64_t m_fileSize { 0 };
				ostd::ByteStream m_writeBuffer;
				uint32_t m_diskID { 0 };

				inline static uint32_t s_nextDiskID = 0;
		};
	}
}