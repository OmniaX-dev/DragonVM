#include "LegacyOstdSerial.hpp"
#include <ostd/io/Memory.hpp>

namespace ostd
{
	namespace serial
	{
		bool SerialIO::init(u64 size, u8 endianness)
		{
			if (size < 1) return false;
			for (u64 i = 0; i < size; i++)
				m_data.push_back(0);
			m_endianness = endianness;
			return true;
		}


		bool SerialIO::r_QWord(StreamIndex addr, QWord& outVal)
		{
			if (!is_validAddr(addr, tTypeSize::QWORD)) return false;
			if (isLittleEndian())
			{
				outVal =  (        m_data[addr + 7]        & 0x00000000000000FFU)
						| ((       m_data[addr + 6] <<  8) & 0x000000000000FF00U)
						| ((       m_data[addr + 5] << 16) & 0x0000000000FF0000U)
						| ((       m_data[addr + 4] << 24) & 0x00000000FF000000U)
						| (((QWord)m_data[addr + 3] << 32) & 0x000000FF00000000U)
						| (((QWord)m_data[addr + 2] << 40) & 0x0000FF0000000000U)
						| (((QWord)m_data[addr + 1] << 48) & 0x00FF000000000000U)
						| (((QWord)m_data[addr + 0] << 56) & 0xFF00000000000000U);
			}
			else if (isBigEndian())
			{
				outVal =  (((QWord)m_data[addr + 0] << 56) & 0xFF00000000000000U)
						| (((QWord)m_data[addr + 1] << 48) & 0x00FF000000000000U)
						| (((QWord)m_data[addr + 2] << 40) & 0x0000FF0000000000U)
						| (((QWord)m_data[addr + 3] << 32) & 0x000000FF00000000U)
						| ((       m_data[addr + 4] << 24) & 0x00000000FF000000U)
						| ((       m_data[addr + 5] << 16) & 0x0000000000FF0000U)
						| ((       m_data[addr + 6] <<  8) & 0x000000000000FF00U)
						| (        m_data[addr + 7]        & 0x00000000000000FFU);
			}
			else return false;
			return true;
		}

		bool SerialIO::r_DWord(StreamIndex addr, DWord& outVal)
		{
			if (!is_validAddr(addr, tTypeSize::DWORD)) return false;
			if (isLittleEndian())
			{
				outVal =  ((m_data[addr + 0]      ) & 0x000000FF)
						| ((m_data[addr + 1] <<  8) & 0x0000FF00)
						| ((m_data[addr + 2] << 16) & 0x00FF0000)
						| ((m_data[addr + 3] << 24) & 0xFF000000);
			}
			else if (isBigEndian())
			{
				outVal =  ((m_data[addr + 0] << 24) & 0xFF000000U)
						| ((m_data[addr + 1] << 16) & 0x00FF0000U)
						| ((m_data[addr + 2] <<  8) & 0x0000FF00U)
						| ( m_data[addr + 3]        & 0x000000FFU);
			}
			else return false;
			return true;
		}

		bool SerialIO::r_Word(StreamIndex addr, Word& outVal)
		{
			if (!is_validAddr(addr, tTypeSize::WORD)) return false;
			if (isLittleEndian())
			{
				outVal =  ((m_data[addr + 0]     ) & 0x00FFU)
						| ((m_data[addr + 1] << 8) & 0xFF00U);
			}
			else if (isBigEndian())
			{
				outVal =  ((m_data[addr + 0] <<  8) & 0xFF00U)
						| ( m_data[addr + 1]        & 0x00FFU);
			}
			else return false;
			return true;
		}

		bool SerialIO::r_Byte(StreamIndex addr, Byte& outVal)
		{
			if (!is_validAddr(addr, tTypeSize::BYTE)) return false;
			outVal = m_data[addr];
			return true;
		}

		bool SerialIO::r_Addr(StreamIndex addr, StreamIndex& outVal)
		{
			if (!is_validAddr(addr, tTypeSize::ADDR)) return false;
			if (isLittleEndian())
			{
				outVal =  ((m_data[addr + 0]      ) & 0x000000FF)
						| ((m_data[addr + 1] <<  8) & 0x0000FF00)
						| ((m_data[addr + 2] << 16) & 0x00FF0000)
						| ((m_data[addr + 3] << 24) & 0xFF000000);
			}
			else if (isBigEndian())
			{
				outVal =  ((m_data[addr + 0] << 24) & 0xFF000000U)
						| ((m_data[addr + 1] << 16) & 0x00FF0000U)
						| ((m_data[addr + 2] <<  8) & 0x0000FF00U)
						| ( m_data[addr + 3]        & 0x000000FFU);
			}
			else return false;
			return true;
		}

		bool SerialIO::r_Float(StreamIndex addr, f32& outVal)
		{
			if (!is_validAddr(addr, tTypeSize::FLOAT)) return false;
			__float_parser fp;
			if (isLittleEndian())
			{
				fp.data[3] = m_data[addr + 0];
				fp.data[2] = m_data[addr + 1];
				fp.data[1] = m_data[addr + 2];
				fp.data[0] = m_data[addr + 3];
			}
			else if (isBigEndian())
			{
				fp.data[0] = m_data[addr + 0];
				fp.data[1] = m_data[addr + 1];
				fp.data[2] = m_data[addr + 2];
				fp.data[3] = m_data[addr + 3];
			}
			else return false;
			outVal = fp.val;
			return true;
		}

		bool SerialIO::r_Double(StreamIndex addr, f64& outVal)
		{
			if (!is_validAddr(addr, tTypeSize::DOUBLE)) return false;
			__double_parser dp;
			if (isLittleEndian())
			{
				dp.data[7] = m_data[addr++];
				dp.data[6] = m_data[addr++];
				dp.data[5] = m_data[addr++];
				dp.data[4] = m_data[addr++];
				dp.data[3] = m_data[addr++];
				dp.data[2] = m_data[addr++];
				dp.data[1] = m_data[addr++];
				dp.data[0] = m_data[addr  ];
			}
			else if (isBigEndian())
			{
				dp.data[0] = m_data[addr++];
				dp.data[1] = m_data[addr++];
				dp.data[2] = m_data[addr++];
				dp.data[3] = m_data[addr++];
				dp.data[4] = m_data[addr++];
				dp.data[5] = m_data[addr++];
				dp.data[6] = m_data[addr++];
				dp.data[7] = m_data[addr  ];
			}
			else return false;
			outVal = dp.val;
			return true;
		}

		bool SerialIO::r_ByteStream(StreamIndex addr, ByteStream& outStream)
		{
			if (!is_validAddr(addr, tTypeSize::ADDR)) return false;
			u32 stream_size;
			if (!r_Addr(addr, stream_size)) return false;
			addr += tTypeSize::ADDR;
			if (!is_validAddr(addr, stream_size)) return false;
			outStream.clear();
			outStream.reserve(stream_size);
			for (StreamIndex j = addr; j < addr + stream_size; j++)
				outStream.push_back(m_data[j]);
			return true;
		}

		bool SerialIO::r_ByteStream(StreamIndex addr, ByteStream& outStream, u32 size)
		{
			if (!is_validAddr(addr, size)) return false;
			outStream.clear();
			outStream.reserve(size);
			for (StreamIndex j = addr; j < addr + size; j++)
				outStream.push_back(m_data[j]);
			return true;
		}

		bool SerialIO::r_String(StreamIndex addr, String& outString)
		{
			if (!is_validAddr(addr, tTypeSize::ADDR)) return false;
			u32 stream_size;
			if (!r_Addr(addr, stream_size)) return false;
			addr += tTypeSize::ADDR;
			if (!is_validAddr(addr, stream_size)) return false;
			outString = "";
			for (StreamIndex j = addr; j < addr + stream_size; j++)
				outString += (char)(m_data[j]);
			return true;
		}

		bool SerialIO::r_String(StreamIndex addr, String& outString, u32 size)
		{
			if (!is_validAddr(addr, size)) return false;
			outString = "";
			for (StreamIndex j = addr; j < addr + size; j++)
				outString += (char)(m_data[j]);
			return true;
		}

		bool SerialIO::r_NullTerminatedString(StreamIndex addr, String& outString)
		{
			outString = "";
			i8 c = 0;
			if (!r_Byte(addr, c)) return false;
			addr += tTypeSize::BYTE;
			while (c != 0)
			{
				outString += (char)c;
				if (!r_Byte(addr, c)) return false;
				addr += tTypeSize::BYTE;
			}
			return true;
		}


		bool SerialIO::w_QWord(StreamIndex addr, QWord val)
		{
			m_statuWriting = true;
			if (!is_validAddr(addr, tTypeSize::QWORD)) return false;
			if (isLittleEndian())
			{
				m_data[addr + 0] = val & 0xFF;
				m_data[addr + 1] = (val >> 8) & 0xFF;
				m_data[addr + 2] = (val >> 16) & 0xFF;
				m_data[addr + 3] = (val >> 24) & 0xFF;
				m_data[addr + 4] = (val >> 32) & 0xFF;
				m_data[addr + 5] = (val >> 40) & 0xFF;
				m_data[addr + 6] = (val >> 48) & 0xFF;
				m_data[addr + 7] = (val >> 56) & 0xFF;
			}
			else if (isBigEndian())
			{
				m_data[addr + 0] = (val >> 56) & 0xFF;
				m_data[addr + 1] = (val >> 48) & 0xFF;
				m_data[addr + 2] = (val >> 40) & 0xFF;
				m_data[addr + 3] = (val >> 32) & 0xFF;
				m_data[addr + 4] = (val >> 24) & 0xFF;
				m_data[addr + 5] = (val >> 16) & 0xFF;
				m_data[addr + 6] = (val >> 8) & 0xFF;
				m_data[addr + 7] = val & 0xFF;
			}
			else return false;
			return true;
		}

		bool SerialIO::w_DWord(StreamIndex addr, DWord val)
		{
			m_statuWriting = true;
			if (!is_validAddr(addr, tTypeSize::DWORD)) return false;
			if (isLittleEndian())
			{
				m_data[addr + 0] = val & 0xFF;
				m_data[addr + 1] = (val >> 8) & 0xFF;
				m_data[addr + 2] = (val >> 16) & 0xFF;
				m_data[addr + 3] = (val >> 24) & 0xFF;
			}
			else if (isBigEndian())
			{
				m_data[addr + 0] = (val >> 24) & 0xFF;
				m_data[addr + 1] = (val >> 16) & 0xFF;
				m_data[addr + 2] = (val >> 8) & 0xFF;
				m_data[addr + 3] = val & 0xFF;
			}
			else return false;
			return true;
		}

		bool SerialIO::w_Word(StreamIndex addr, Word val)
		{
			m_statuWriting = true;
			if (!is_validAddr(addr, tTypeSize::WORD)) return false;
			if (isLittleEndian())
			{
				m_data[addr + 0] = val & 0xFF;
				m_data[addr + 1] = (val >> 8) & 0xFF;
			}
			else if (isBigEndian())
			{
				m_data[addr + 0] = (val >> 8) & 0xFF;
				m_data[addr + 1] = val & 0xFF;
			}
			else return false;
			return true;
		}

		bool SerialIO::w_Byte(StreamIndex addr, Byte val)
		{
			m_statuWriting = true;
			if (!is_validAddr(addr, tTypeSize::BYTE)) return false;
			m_data[addr] = val;
			return true;
		}

		bool SerialIO::w_Addr(StreamIndex addr, StreamIndex val)
		{
			m_statuWriting = true;
			if (!is_validAddr(addr, tTypeSize::ADDR)) return false;
			if (isLittleEndian())
			{
				m_data[addr + 0] = val & 0xFF;
				m_data[addr + 1] = (val >> 8) & 0xFF;
				m_data[addr + 2] = (val >> 16) & 0xFF;
				m_data[addr + 3] = (val >> 24) & 0xFF;
			}
			else if (isBigEndian())
			{
				m_data[addr + 0] = (val >> 24) & 0xFF;
				m_data[addr + 1] = (val >> 16) & 0xFF;
				m_data[addr + 2] = (val >> 8) & 0xFF;
				m_data[addr + 3] = val & 0xFF;
			}
			else return false;
			return true;
		}

		bool SerialIO::w_Float(StreamIndex addr, f32 val)
		{
			m_statuWriting = true;
			if (!is_validAddr(addr, tTypeSize::FLOAT)) return false;
			__float_parser fp;
			fp.val = val;
			if (isLittleEndian())
			{
				m_data[addr + 0] = fp.data[3];
				m_data[addr + 1] = fp.data[2];
				m_data[addr + 2] = fp.data[1];
				m_data[addr + 3] = fp.data[0];
			}
			else if (isBigEndian())
			{
				m_data[addr + 0] = fp.data[0];
				m_data[addr + 1] = fp.data[1];
				m_data[addr + 2] = fp.data[2];
				m_data[addr + 3] = fp.data[3];
			}
			else return false;
			return true;
		}

		bool SerialIO::w_Double(StreamIndex addr, f64 val)
		{
			m_statuWriting = true;
			if (!is_validAddr(addr, tTypeSize::DOUBLE)) return false;
			__double_parser dp;
			dp.val = val;
			if (isLittleEndian())
			{
				m_data[addr++] = dp.data[7];
				m_data[addr++] = dp.data[6];
				m_data[addr++] = dp.data[5];
				m_data[addr++] = dp.data[4];
				m_data[addr++] = dp.data[3];
				m_data[addr++] = dp.data[2];
				m_data[addr++] = dp.data[1];
				m_data[addr  ] = dp.data[0];
			}
			else if (isBigEndian())
			{
				m_data[addr++] = dp.data[0];
				m_data[addr++] = dp.data[1];
				m_data[addr++] = dp.data[2];
				m_data[addr++] = dp.data[3];
				m_data[addr++] = dp.data[4];
				m_data[addr++] = dp.data[5];
				m_data[addr++] = dp.data[6];
				m_data[addr  ] = dp.data[7];
			}
			else return false;
			return true;
		}

		bool SerialIO::w_ByteStream(StreamIndex addr, const ByteStream& stream, bool store_size)
		{
			m_statuWriting = true;
			u32 stream_size = stream.size();
			if (store_size)
			{
				if (!is_validAddr(addr, tTypeSize::ADDR + stream_size)) return false;
				if (!w_Addr(addr, stream_size)) return false;
				addr += tTypeSize::ADDR;
			}
			else if (!is_validAddr(addr, stream_size)) return false;
			for (StreamIndex j = addr; j < addr + stream_size; j++)
				m_data[j] = stream[j - addr];
			return true;
		}

		bool SerialIO::w_String(StreamIndex addr, const String& str, bool store_size, bool null_terminate)
		{
			m_statuWriting = true;
			auto stream = Memory::stringToByteStream(str);
			u32 stream_size = stream.size();
			if (store_size && !null_terminate)
			{
				if (!is_validAddr(addr, tTypeSize::ADDR + stream_size)) return false;
				if (!w_Addr(addr, stream_size)) return false;
				addr += tTypeSize::ADDR;
			}
			else if (!is_validAddr(addr, stream_size)) return false;
			for (StreamIndex j = addr; j < addr + stream_size; j++)
				m_data[j] = stream[j - addr];
			addr += str.len() * tTypeSize::BYTE;
			if (null_terminate)
				if (!w_Byte(addr, 0x00)) return false;
			return true;
		}


		bool SerialIO::is_validAddr(StreamIndex addr, StreamIndex offset)
		{
			if ((addr + offset - 1) >= m_data.size())
			{
				if (isAutoResizeEnabled() && m_statuWriting)
				{
					for (u32 i = 0; i <= m_resizeAmount; i++)
						m_data.push_back(0);
					m_statuWriting = false;
					return true;
				}
				else
				{
					m_statuWriting = false;
					return false;
				}
			}
			m_statuWriting = false;
			return true;
		}

		void SerialIO::print(StreamIndex start, OutputHandlerBase& out)
		{
			u32 line_len = 32;
			u64 power = 1;
			while(power < size())
				power *= 2;
			Memory::printByteStream(m_data, start, line_len, power / line_len, out);
		}

		bool SerialIO::saveToFile(const String& filePath)
		{
			return Memory::saveByteStreamToFile(getData(), filePath);
		}
	}
}
