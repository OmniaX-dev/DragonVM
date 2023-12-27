#include "VirtualHardDrive.hpp"

#include "../tools/GlobalData.hpp"
#include <iostream>

namespace dragon
{
	namespace hw
	{
		void VirtualHardDrive::init(const ostd::String& dataFilePath)
		{
			m_dataFile.open(dataFilePath, std::ios::out | std::ios::in | std::ios::binary);
			if(!m_dataFile)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_UnableToMount, ostd::StringEditor("Unable to mount virtual HardDrive: ").add(dataFilePath).str());
				return;
			}
			m_fileSize = m_dataFile.tellg();
			m_dataFile.seekg( 0, std::ios::end );
			m_fileSize = (int64_t)m_dataFile.tellg() - m_fileSize;
			m_dataFile.seekg( 0, std::ios::beg );
			m_diskID = s_nextDiskID++;
			m_initialized = true;
		}
		
		bool VirtualHardDrive::read(uint32_t addr, uint16_t size, ostd::ByteStream& outData)
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
			uint8_t cell = 0;
			outData.clear();
			m_dataFile.seekg(addr);
			for (int32_t i = 0; i < size; i++)
			{
				m_dataFile.read((char*)&cell, sizeof(cell));
				outData.push_back(cell);
			}
			return true;
		}

		bool VirtualHardDrive::write(uint32_t addr, int8_t value)
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

		void VirtualHardDrive::bufferedWrite(int8_t value)
		{
			if (!m_initialized)
			{
				data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_Uninitialized, "Attempt to read uninitialized drive.");
				return;
			}
			m_writeBuffer.push_back(value);
		}

		bool VirtualHardDrive::writeBuffer(uint32_t addr)
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