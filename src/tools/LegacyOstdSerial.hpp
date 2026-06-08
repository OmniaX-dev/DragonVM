/*
    OmniaFramework - A collection of useful functionality
    Copyright (C) 2026  OmniaX-Dev

    This file is part of OmniaFramework.

    OmniaFramework is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OmniaFramework is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with OmniaFramework.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <ostd/data/Types.hpp>
#include <ostd/data/Bitfields.hpp>
#include <ostd/data/BaseObject.hpp>

namespace ostd
{
	class OutputHandlerBase;

	namespace serial
	{
		class SerialIO
		{
			public: struct tEndianness {
				inline static constexpr u8 LittleEndian = 0x00;
				inline static constexpr u8 BigEndian = 0x01;
			};

			public:
				inline SerialIO(void) { init(); }
				inline SerialIO(u64 size, u8 endianness = tEndianness::BigEndian) { init(size, endianness); }
				inline SerialIO(ByteStream& data, u8 endianness = tEndianness::BigEndian) { m_data = data; m_endianness = endianness; }
				bool init(u64 size = SerialIO::DefaultMaxSize, u8 endianness = tEndianness::BigEndian);
				bool r_QWord(StreamIndex addr, QWord& outVal);
				bool r_DWord(StreamIndex addr, DWord& outVal);
				bool r_Word(StreamIndex addr, Word& outVal);
				bool r_Byte(StreamIndex addr, Byte& outVal);
				bool r_Addr(StreamIndex addr, StreamIndex& outVal);
				bool r_Float(StreamIndex addr, f32& outVal);
				bool r_Double(StreamIndex addr, f64& outVal);
				bool r_ByteStream(StreamIndex addr, ByteStream& outStream);
				bool r_ByteStream(StreamIndex addr, ByteStream& outStream, u32 size);
				bool r_String(StreamIndex addr, String& outString);
				bool r_String(StreamIndex addr, String& outString, u32 size);
				bool r_NullTerminatedString(StreamIndex addr, String& outString);

				bool w_QWord(StreamIndex addr, QWord val);
				bool w_DWord(StreamIndex addr, DWord val);
				bool w_Word(StreamIndex addr, Word val);
				bool w_Byte(StreamIndex addr, Byte val);
				bool w_Addr(StreamIndex addr, StreamIndex val);
				bool w_Float(StreamIndex addr, f32 val);
				bool w_Double(StreamIndex addr, f64 val);
				bool w_ByteStream(StreamIndex addr, const ByteStream& stream, bool store_size = true);
				bool w_String(StreamIndex addr, const String& str, bool store_size = false, bool null_terminate = true);
				bool is_validAddr(StreamIndex addr, StreamIndex offset = 1);
				inline u64 size(void) { return m_data.size(); }
				inline ByteStream& getData(void) { return m_data; }

				void print(StreamIndex start, OutputHandlerBase& out);
				bool saveToFile(const String& filePath);

				inline u8 getEndianness(void) const { return m_endianness; }
				inline bool isLittleEndian(void) const { return m_endianness == tEndianness::LittleEndian; }
				inline bool isBigEndian(void) const { return m_endianness == tEndianness::BigEndian; }

				inline bool isAutoResizeEnabled(void) const { return m_autoResize; }
				inline void enableAutoResize(bool val = true, i32 resizeAmount = 1024) { m_autoResize = val; if (!val) return; m_resizeAmount = resizeAmount; }

			private:
				ByteStream m_data;
				u8 m_endianness { tEndianness::BigEndian };
				bool m_autoResize { true };
				i32 m_resizeAmount { 1024 };
				bool m_statuWriting { false };

			public:
				inline static constexpr u64 DefaultMaxSize = 512;
		};
	}
}
