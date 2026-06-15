#include "VirtualHardDrive.hpp"

#include "../tools/GlobalData.hpp"
#include <iostream>

namespace dragon
{
	namespace hw
	{
		void VirtualHardDrive::init(const String& dataFilePath)
		{
			m_dataFile.open(dataFilePath.cpp_str(), std::ios::out | std::ios::in | std::ios::binary);
			if(!m_dataFile)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_UnableToMount, String("Unable to mount virtual HardDrive: ").add(dataFilePath));
				return;
			}
			m_fileSize = m_dataFile.tellg();
			m_dataFile.seekg( 0, std::ios::end );
			m_fileSize = (i64)m_dataFile.tellg() - m_fileSize;
			m_dataFile.seekg( 0, std::ios::beg );
			m_diskID = s_nextDiskID++;
			m_initialized = true;
		}
		
		bool VirtualHardDrive::read(u32 addr, u16 size, ostd::ByteStream& outData)
		{
			if (!m_initialized)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_Uninitialized, "Attempt to read uninitialized drive.");
				return false;
			}
			if (addr + size > m_fileSize)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ReadOverflow, "Read Overflow on HardDrive.");
				return false;
			}
			u8 cell = 0;
			outData.clear();
			m_dataFile.seekg(addr);
			for (i32 i = 0; i < size; i++)
			{
				m_dataFile.read((char*)&cell, sizeof(cell));
				outData.push_back(cell);
			}
			return true;
		}

		bool VirtualHardDrive::write(u32 addr, i8 value)
		{
			if (!m_initialized)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_Uninitialized, "Attempt to read uninitialized drive.");
				return false;
			}
			if (addr >= m_fileSize)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_WriteOverflow, "Write Overflow on HardDrive.");
				return false;
			}
			m_dataFile.seekp(addr);
			m_dataFile.write((char*)(&value), sizeof(value));
			return true;
		}

		void VirtualHardDrive::bufferedWrite(i8 value)
		{
			if (!m_initialized)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_Uninitialized, "Attempt to read uninitialized drive.");
				return;
			}
			m_writeBuffer.push_back(value);
		}

		bool VirtualHardDrive::writeBuffer(u32 addr)
		{
			if (!m_initialized)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_Uninitialized, "Attempt to read uninitialized drive.");
				return false;
			}
			if (m_writeBuffer.size() == 0)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_EmptyBuffer, "Buffered Write empty buffer on HardDrive.");
				return false;
			}
			if (addr + m_writeBuffer.size() > m_fileSize)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_BuffWriteOverflow, "Buffered Write Overflow on HardDrive.");
				return false;
			}
			m_dataFile.seekp(addr);
			m_dataFile.write((char*)(&m_writeBuffer[0]), m_writeBuffer.size());
			m_writeBuffer.clear();
			return true;
		}

		void VirtualHardDrive::unmount(void)
		{
			if (!m_initialized) return;
			m_dataFile.close();
			m_initialized = false;
		}
	}
}