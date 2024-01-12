#pragma once

#include "IMemoryDevice.hpp"
#include "../tools/GlobalData.hpp"
#include <ostd/Serial.hpp>
#include <fstream>

namespace dragon
{
	namespace hw
	{
		class VirtualHardDrive;
		class MemoryMapper;
		class VirtualCPU;
		class VirtualRAM;


		class VirtualBIOS : public IMemoryDevice
		{
			public:
				inline VirtualBIOS(void) { m_initialized = 0; }
				inline VirtualBIOS(const ostd::String& biosFilePath) { init(biosFilePath); }
				void init(const ostd::String& biosFilePath);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_bios;
				bool m_initialized { false };
		};
		class InterruptVector : public IMemoryDevice
		{
			public:
				InterruptVector(void);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_data;
		};
		class VirtualKeyboard : public IMemoryDevice
		{
			public:
				VirtualKeyboard(void);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
		};
		class VirtualMouse : public IMemoryDevice
		{
			public:
				VirtualMouse(void);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
		};
		class VirtualBootloader : public IMemoryDevice
		{
			public:
				VirtualBootloader(void);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_mbr;
		};

		namespace interface
		{
			class Disk : public IMemoryDevice
			{
				public: struct tRegisters
				{
					inline static constexpr uint16_t Signal = 0x00;
					inline static constexpr uint16_t ModeSelector = 0x01;
					inline static constexpr uint16_t DiskSelector = 0x02;
					inline static constexpr uint16_t SectorSelector = 0x03;
					inline static constexpr uint16_t AddressSelector = 0x05;
					inline static constexpr uint16_t DataSize = 0x07;
					inline static constexpr uint16_t DataSourceAddress = 0x09;

					inline static constexpr uint16_t FirstReadOnly = 0x0B;

					inline static constexpr uint16_t Status = 0x0B;
					inline static constexpr uint16_t CurrentDisk = 0x0C;
					inline static constexpr uint16_t CurrentSector = 0x0D;
					inline static constexpr uint16_t CurrentAddress = 0x0F;
					inline static constexpr uint16_t RestDataSize = 0x11;
					inline static constexpr uint16_t SourceData = 0x13;

				};

				public: struct tSignalValues
				{
					inline static constexpr uint8_t Start = 0x00;
					inline static constexpr uint8_t Cancel = 0x01;

					inline static constexpr uint8_t Ignore = 0xFF;
				};

				public: struct tModeValues
				{
					inline static constexpr uint8_t Read = 0x00;
					inline static constexpr uint8_t Write = 0x01;
				};

				public: struct tStatusValues
				{
					inline static constexpr uint8_t Free = 0x00;
					inline static constexpr uint8_t Writing = 0x01;
					inline static constexpr uint8_t Reading = 0x02;
				};

				public:
					Disk(MemoryMapper& memory, VirtualCPU& cpu);
					int8_t read8(uint16_t addr) override;
					int16_t read16(uint16_t addr) override;
					int8_t write8(uint16_t addr, int8_t value) override;
					int16_t write16(uint16_t addr, int16_t value) override;

					ostd::ByteStream* getByteStream(void) override;

					void cycleStep(void);
					data::VDiskID connectDisk(VirtualHardDrive& hdd);
					bool disconnectDisk(data::VDiskID diskID);

					inline bool isBusy(void) const { return m_busy; }

				private:
					ostd::serial::SerialIO m_data { data::MemoryMapAddresses::DiskInterface_End - data::MemoryMapAddresses::DiskInterface_Start };
					bool m_busy { false };
					std::unordered_map<data::VDiskID, VirtualHardDrive*> m_connectedDisks;
					MemoryMapper& m_memory;
					VirtualCPU& m_cpu;

					inline static data::VDiskID s_nextDiskID = 0;

			};
			class Graphics : public IMemoryDevice
			{
				public:
					Graphics(void);
					int8_t read8(uint16_t addr) override;
					int16_t read16(uint16_t addr) override;
					int8_t write8(uint16_t addr, int8_t value) override;
					int16_t write16(uint16_t addr, int16_t value) override;

					ostd::ByteStream* getByteStream(void) override;

				private:
					ostd::serial::SerialIO m_videoMemory;
			};
			class SerialPort : public IMemoryDevice
			{
				public:
					SerialPort(void);
					int8_t read8(uint16_t addr) override;
					int16_t read16(uint16_t addr) override;
					int8_t write8(uint16_t addr, int8_t value) override;
					int16_t write16(uint16_t addr, int16_t value) override;

					ostd::ByteStream* getByteStream(void) override;

				private:
			};
			class CMOS : public IMemoryDevice
			{
				public:
					inline CMOS(void) { m_initialized = false; }
					inline CMOS(const ostd::String& cmosFilePath) { init(cmosFilePath); }
					void init(const ostd::String& cmosFilePath);
					int8_t read8(uint16_t addr) override;
					int16_t read16(uint16_t addr) override;
					int8_t write8(uint16_t addr, int8_t value) override;
					int16_t write16(uint16_t addr, int16_t value) override;

					ostd::ByteStream* getByteStream(void) override;

				private:
					ostd::ByteStream m_data;
					uint16_t m_size { 0 };
					std::fstream m_dataFile;
					bool m_initialized { false };
					uint64_t m_fileSize { 0 };
			};
		}
	}
}