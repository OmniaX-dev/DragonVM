#pragma once

#include <ostd/data/Object.hpp>

namespace dragon
{
	namespace hw
	{
		class IMemoryDevice : public ostd::Object
		{
			public:
				virtual i8 read8(u16 addr) = 0;
				virtual i16 read16(u16 addr) = 0;
				virtual i8 write8(u16 addr, i8 value) = 0;
				virtual i16 write16(u16 addr, i16 value) = 0;
				virtual inline ostd::ByteStream* getByteStream(void) = 0;
		};
	}
}
