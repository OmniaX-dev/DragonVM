#include "VirtualIODevices.hpp"
#include <ostd/Utils.hpp>

#include "VirtualHardDrive.hpp"
#include "MemoryMapper.hpp"
#include "VirtualCPU.hpp"
#include "VirtualRAM.hpp"

namespace dragon
{
	namespace hw
	{
		void VirtualBIOS::init(const ostd::String& biosFilePath)
		{
			bool loaded = ostd::Utils::loadByteStreamFromFile(biosFilePath, m_bios);
			if (!loaded)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_FailedToLoad, "Failed to load BIOS data.");
			if (m_bios.size() != 1024)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_InvalidSize, ostd::StringEditor("Invalid BIOS size: ").add(ostd::Utils::getHexStr(m_bios.size(), true, 2)).str());
			m_initialized = true;
		}

		int8_t VirtualBIOS::read8(uint16_t addr)
		{
			if (addr >= m_bios.size())
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_InvalidAddress, ostd::StringEditor("Invalid Byte BIOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			return m_bios[addr];
		}

		int16_t VirtualBIOS::read16(uint16_t addr)
		{
			if (addr >= m_bios.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_InvalidAddress, ostd::StringEditor("Invalid Word BIOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			return ((m_bios[addr + 0] <<  8) & 0xFF00U)
				 | ( m_bios[addr + 1]        & 0x00FFU);
		}

		int8_t VirtualBIOS::write8(uint16_t addr, int8_t value)
		{
			data::ErrorHandler::pushError(data::ErrorCodes::BIOS_WriteAttempt, "Attempting to write to BIOS memory map.");
			return 0x00;
		}

		int16_t VirtualBIOS::write16(uint16_t addr, int16_t value)
		{
			data::ErrorHandler::pushError(data::ErrorCodes::BIOS_WriteAttempt, "Attempting to write to BIOS memory map.");
			return 0x0000;
		}

		ostd::ByteStream* VirtualBIOS::getByteStream(void)
		{
			return &m_bios;
		}




		
		InterruptVector::InterruptVector(void)
		{
			uint32_t dataSize = data::MemoryMapAddresses::IntVector_End - data::MemoryMapAddresses::IntVector_Start;
			for (int32_t i = 0; i < dataSize; i++)
				m_data.push_back(0x00);
		}

		int8_t InterruptVector::read8(uint16_t addr)
		{
			if (addr >= m_data.size())
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::StringEditor("Invalid Byte IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			return m_data[addr];
		}

		int16_t InterruptVector::read16(uint16_t addr)
		{
			if (addr >= m_data.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::StringEditor("Invalid Word IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			return ((m_data[addr + 0] <<  8) & 0xFF00U)
				 | ( m_data[addr + 1]        & 0x00FFU);
		}

		int8_t InterruptVector::write8(uint16_t addr, int8_t value)
		{
			if (addr >= m_data.size())
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::StringEditor("Invalid Word IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			m_data[addr] = value;
			return value;
		}

		int16_t InterruptVector::write16(uint16_t addr, int16_t value)
		{
			if (addr >= m_data.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::StringEditor("Invalid Word IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			m_data[addr + 0] = (value >> 8) & 0xFF;
			m_data[addr + 1] = value & 0xFF;
			return value;
		}

		ostd::ByteStream* InterruptVector::getByteStream(void)
		{
			return &m_data;
		}




		
		VirtualKeyboard::VirtualKeyboard(void)
		{
		}

		int8_t VirtualKeyboard::read8(uint16_t addr)
		{
			return 0x00;
		}

		int16_t VirtualKeyboard::read16(uint16_t addr)
		{
			return 0x0000;
		}

		int8_t VirtualKeyboard::write8(uint16_t addr, int8_t value)
		{
			return 0x00;
		}

		int16_t VirtualKeyboard::write16(uint16_t addr, int16_t value)
		{
			return 0x0000;
		}

		ostd::ByteStream* VirtualKeyboard::getByteStream(void)
		{
			return nullptr;
		}




		
		VirtualMouse::VirtualMouse(void)
		{
		}

		int8_t VirtualMouse::read8(uint16_t addr)
		{
			return 0x00;
		}

		int16_t VirtualMouse::read16(uint16_t addr)
		{
			return 0x0000;
		}

		int8_t VirtualMouse::write8(uint16_t addr, int8_t value)
		{
			return 0x00;
		}

		int16_t VirtualMouse::write16(uint16_t addr, int16_t value)
		{
			return 0x0000;
		}

		ostd::ByteStream* VirtualMouse::getByteStream(void)
		{
			return nullptr;
		}




		
		VirtualBootloader::VirtualBootloader(void)
		{
			for (int32_t i = 0; i < 512; i++)
				m_mbr.push_back(0);
		}

		int8_t VirtualBootloader::read8(uint16_t addr)
		{
			if (addr >= m_mbr.size())
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_InvalidAddress, ostd::StringEditor("Invalid Byte MBR location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			return m_mbr[addr];
		}

		int16_t VirtualBootloader::read16(uint16_t addr)
		{
			if (addr >= m_mbr.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_InvalidAddress, ostd::StringEditor("Invalid Word MBR location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			return ((m_mbr[addr + 0] <<  8) & 0xFF00U)
				 | ( m_mbr[addr + 1]        & 0x00FFU);
		}

		int8_t VirtualBootloader::write8(uint16_t addr, int8_t value)
		{
			if (addr >= m_mbr.size())
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::StringEditor("Invalid Word IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			m_mbr[addr] = value;
			return value;
		}

		int16_t VirtualBootloader::write16(uint16_t addr, int16_t value)
		{
			if (addr >= m_mbr.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::StringEditor("Invalid Word IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			m_mbr[addr + 0] = (value >> 8) & 0xFF;
			m_mbr[addr + 1] = value & 0xFF;
			return value;
		}

		ostd::ByteStream* VirtualBootloader::getByteStream(void)
		{
			return &m_mbr;
		}




		
		VirtualBIOSVideo::VirtualBIOSVideo(VirtualRAM& memory) : m_memory(memory)
		{
			intptr_t iMemPtr = reinterpret_cast<intptr_t>(memory.getByteStream()->data());
			iMemPtr += data::MemoryMapAddresses::BIOSVideo_Start;
			m_dataPtr = reinterpret_cast<ostd::Byte*>(iMemPtr);
		}

		int8_t VirtualBIOSVideo::read8(uint16_t addr)
		{
			if (addr >= m_size)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOSVideo_InvalidAddress, ostd::StringEditor("Invalid read Byte BiosVideo location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			return m_dataPtr[addr];
		}

		int16_t VirtualBIOSVideo::read16(uint16_t addr)
		{
			if (addr >= m_size - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOSVideo_InvalidAddress, ostd::StringEditor("Invalid read Word BiosVideo location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			return ((m_dataPtr[addr + 0] <<  8) & 0xFF00U)
				 | ( m_dataPtr[addr + 1]        & 0x00FFU);
		}

		int8_t VirtualBIOSVideo::write8(uint16_t addr, int8_t value)
		{
			if (addr >= m_size)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOSVideo_InvalidAddress, ostd::StringEditor("Invalid read Byte BiosVideo location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			m_dataPtr[addr] = value;
			return value;
		}

		int16_t VirtualBIOSVideo::write16(uint16_t addr, int16_t value)
		{
			if (addr >= m_size - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOSVideo_InvalidAddress, ostd::StringEditor("Invalid read Word BiosVideo location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
			m_dataPtr[addr + 0] = (value >> 8) & 0xFF;
			m_dataPtr[addr + 1] = value & 0xFF;
			return value;
		}

		ostd::ByteStream* VirtualBIOSVideo::getByteStream(void)
		{
			m_dataCopy.clear();
			m_dataCopy.insert(m_dataCopy.end(), &m_dataPtr[0], &m_dataPtr[m_size]);
			return &m_dataCopy;
		}



		

		namespace interface
		{
			Disk::Disk(MemoryMapper& memory, VirtualCPU& cpu) : m_memory(memory), m_cpu(cpu)
			{
				m_data.w_Byte(tRegisters::Signal, tSignalValues::Ignore);
			}

			int8_t Disk::read8(uint16_t addr)
			{
				int8_t value = 0;
				if (!m_data.r_Byte(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerReadFailed, "Failed to read byte from HardDrive Controller");
					return 0;
				}
				return value;
			}

			int16_t Disk::read16(uint16_t addr)
			{
				int16_t value = 0;
				if (!m_data.r_Word(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerReadFailed, "Failed to read word from HardDrive Controller");
					return 0;
				}
				return value;
			}

			int8_t Disk::write8(uint16_t addr, int8_t value)
			{
				if (addr >= tRegisters::FirstReadOnly)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerWriteFailed, "Attempt to write byte to ReadOnly part of HardDrive Controller");
					return 0;
				}
				if (!m_data.w_Byte(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerWriteFailed, "Failed to write byte to HardDrive Controller");
					return 0;
				}
				return value;
			}

			int16_t Disk::write16(uint16_t addr, int16_t value)
			{
				if (addr >= tRegisters::FirstReadOnly)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerWriteFailed, "Attempt to write word to ReadOnly part of HardDrive Controller");
					return 0;
				}
				if (!m_data.w_Word(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerWriteFailed, "Failed to write word to HardDrive Controller");
					return 0;
				}
				return value;
			}

			ostd::ByteStream* Disk::getByteStream(void)
			{
				return &m_data.getData();
			}

			void Disk::cycleStep(void)
			{
				uint8_t signal = tSignalValues::Ignore;
				m_data.r_Byte(tRegisters::Signal, (int8_t&)signal);
				if (m_busy)
				{
					if (signal == tSignalValues::Cancel)
					{
						m_data.w_Byte(tRegisters::Status, tStatusValues::Free);
						m_data.w_Byte(tRegisters::Signal, tSignalValues::Ignore);
						m_busy = false;
						return;
					}
					if (signal != tSignalValues::Ignore)
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_InvalidConfiguration, "Invalid HardDrive configuration: <signal> register must be set to <ignore> while busy.");
						m_busy = false;
						return;
					}
					uint8_t status = 0;
					m_data.r_Byte(tRegisters::Status, (int8_t&)status);
					if (status == tStatusValues::Free)
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_InvalidConfiguration, "Invalid HardDrive configuration: <status> register set to <free> while busy.");
						m_busy = false;
						return;
					}
					uint8_t currentDisk = 0;
					uint16_t currentSector = 0, currentAddress = 0, restDataSize = 0, memoryAddress = 0;
					m_data.r_Byte(tRegisters::CurrentDisk, (int8_t&)currentDisk);
					m_data.r_Word(tRegisters::CurrentSector, (int16_t&)currentSector);
					m_data.r_Word(tRegisters::CurrentAddress, (int16_t&)currentAddress);
					m_data.r_Word(tRegisters::RestDataSize, (int16_t&)restDataSize);
					m_data.r_Word(tRegisters::SourceData, (int16_t&)memoryAddress);
					if (m_connectedDisks.count((data::VDiskID)currentDisk) == 0)
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_InvalidDiskSelected, "Invalid HardDrive configuration: selected Disk not found.");
						m_busy = false;
						return;
					}
					auto& disk = *m_connectedDisks[currentDisk];
					uint32_t hddAddress = 0;
					if (currentAddress == 0xFFFF)
					{
						if (currentSector == 0xFFFF)
						{
							data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_EndOfDisk, "HardDrive Error: Reached end of selected Disk.");
							m_busy = false;
							return;
						}
						currentSector++;
						currentAddress = 0x0000;
					}
					hddAddress = (currentSector << 16) | currentAddress;
					if (status == tStatusValues::Reading)
					{
						ostd::ByteStream _data;
						if (!disk.read(hddAddress, 1, _data))
						{
							data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ReadFailed, "HardDrive Error: Failed to read data.");
							m_busy = false;
							return;
						}
						m_memory.write8(memoryAddress, _data[0]);
					}
					else if (status == tStatusValues::Writing)
					{
						int8_t dataRead = m_memory.read8(memoryAddress);
						if (!disk.write(hddAddress, dataRead))
						{
							data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_WriteFailed, "HardDrive Error: Failed to write data.");
							m_busy = false;
							return;
						}
					}
					memoryAddress++;
					if (memoryAddress == 0xFFFF)
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_MemoryOverflow, "HardDrive Error: Reached end of Memory.");
						m_busy = false;
						return;
					}
					restDataSize--;
					if (restDataSize == 0)
					{
						m_data.w_Byte(tRegisters::Status, tStatusValues::Free);
						m_data.w_Byte(tRegisters::Signal, tSignalValues::Ignore);
						m_busy = false;
						m_cpu.handleInterrupt(data::InterruptCodes::DiskInterfaceFFinished);
						return;
					}
					currentAddress++;
					m_data.w_Word(tRegisters::CurrentSector, currentSector);
					m_data.w_Word(tRegisters::CurrentAddress, currentAddress);
					m_data.w_Word(tRegisters::RestDataSize, restDataSize);
					m_data.w_Word(tRegisters::SourceData, memoryAddress);
					return;
				}
				if (signal != tSignalValues::Start) return;
				uint8_t mode = 0, disk = 0;
				uint16_t sector = 0, address = 0, size = 0, srcAddr = 0;
				m_data.r_Byte(tRegisters::ModeSelector, (int8_t&)mode);
				m_data.r_Byte(tRegisters::DiskSelector, (int8_t&)disk);
				m_data.r_Word(tRegisters::SectorSelector, (int16_t&)sector);
				m_data.r_Word(tRegisters::AddressSelector, (int16_t&)address);
				m_data.r_Word(tRegisters::DataSize, (int16_t&)size);
				m_data.r_Word(tRegisters::DataSourceAddress, (int16_t&)srcAddr);
				if (mode == tModeValues::Read)
					m_data.w_Byte(tRegisters::Status, tStatusValues::Reading);
				else if (mode == tModeValues::Write)
					m_data.w_Byte(tRegisters::Status, tStatusValues::Writing);
				else
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_InvalidConfiguration, "Invalid HardDrive configuration: <mode> must be set to <read> or <write> befor starting operations.");
					m_busy = false;
					return;
				}
				m_data.w_Byte(tRegisters::CurrentDisk, disk);
				m_data.w_Word(tRegisters::CurrentSector, sector);
				m_data.w_Word(tRegisters::CurrentAddress, address);
				m_data.w_Word(tRegisters::RestDataSize, size);
				m_data.w_Word(tRegisters::SourceData, srcAddr);

				m_data.w_Byte(tRegisters::Signal, tSignalValues::Ignore);
				m_busy = true;
			}

			data::VDiskID Disk::connectDisk(VirtualHardDrive& hdd)
			{
				for (auto& disk : m_connectedDisks)
				{
					if (disk.second->isSame(hdd))
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_DiskAlreadyConnected, "Attempt to connect already connected Disk to Controller");
						return 0;
					}
				}
				m_connectedDisks[Disk::s_nextDiskID] = &hdd;
				return Disk::s_nextDiskID++;
			}

			bool Disk::disconnectDisk(data::VDiskID diskID)
			{
				if (m_connectedDisks.count(diskID) == 0)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_DisconnectInvalid, "Attempt to disconnect invalid Disk from Controller");
					return false;
				}
				m_connectedDisks.erase(diskID);
				return true;
			}





			Graphics::Graphics(void)
			{
			}

			int8_t Graphics::read8(uint16_t addr)
			{
				return 0x00;
			}

			int16_t Graphics::read16(uint16_t addr)
			{
				return 0x0000;
			}

			int8_t Graphics::write8(uint16_t addr, int8_t value)
			{
				return 0x00;
			}

			int16_t Graphics::write16(uint16_t addr, int16_t value)
			{
				return 0x0000;
			}

			ostd::ByteStream* Graphics::getByteStream(void)
			{
				return nullptr;
			}





			SerialPort::SerialPort(void)
			{
			}

			int8_t SerialPort::read8(uint16_t addr)
			{
				return 0x00;
			}

			int16_t SerialPort::read16(uint16_t addr)
			{
				return 0x0000;
			}

			int8_t SerialPort::write8(uint16_t addr, int8_t value)
			{
				return 0x00;
			}

			int16_t SerialPort::write16(uint16_t addr, int16_t value)
			{
				return 0x0000;
			}

			ostd::ByteStream* SerialPort::getByteStream(void)
			{
				return nullptr;
			}





			void CMOS::init(const ostd::String& cmosFilePath)
			{
				m_size = data::MemoryMapAddresses::CMOS_End - data::MemoryMapAddresses::CMOS_Start + 1;
				m_dataFile.open(cmosFilePath, std::ios::out | std::ios::in | std::ios::binary);
				if(!m_dataFile)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_UnableToMount, "Unable to mount virtual CMOS chip.");
					return;
				}
				m_fileSize = m_dataFile.tellg();
				m_dataFile.seekg( 0, std::ios::end );
				m_fileSize = (int64_t)m_dataFile.tellg() - m_fileSize;
				m_dataFile.seekg( 0, std::ios::beg );
				if (m_fileSize != m_size)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidSize, ostd::StringEditor("Invalid virtual CMOS chhip size: ").addi(m_fileSize).str());
					return;
				}
				m_initialized = true;
			}

			int8_t CMOS::read8(uint16_t addr)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return false;
				}
				if (addr >= m_size)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, ostd::StringEditor("Invalid Byte CMOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
					return false;
				}
				int8_t value = 0;
				m_dataFile.seekg(addr);
				m_dataFile.read((char*)&value, sizeof(value));
				return value;
			}

			int16_t CMOS::read16(uint16_t addr)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return false;
				}
				if (addr >= m_size - 1)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, ostd::StringEditor("Invalid Word CMOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
					return 0;
				}
				int8_t b1 = read8(addr);
				int8_t b2 = read8(addr + 1);
				return ((b1 <<  8) & 0xFF00U)
					  | (b2 	   & 0x00FFU);
			}

			int8_t CMOS::write8(uint16_t addr, int8_t value)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return false;
				}
				if (addr >= m_size)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, ostd::StringEditor("Invalid Byte CMOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
					return 0;
				}
				m_dataFile.seekp(addr);
				m_dataFile.write((char*)(&value), sizeof(value));
				return value;
			}

			int16_t CMOS::write16(uint16_t addr, int16_t value)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return false;
				}
				if (addr >= m_size - 1)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, ostd::StringEditor("Invalid Word CMOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)).str());
					return 0;
				}
				int8_t b1 = (value >> 8) & 0xFF;
				int8_t b2 = (value & 0xFF);
				write8(addr, b1);
				write8(addr + 1, b2);
				return value;
			}

			ostd::ByteStream* CMOS::getByteStream(void)
			{
				return &m_data;
			}
		}
	}
}