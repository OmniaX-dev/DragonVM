#pragma once

#include "../tools/LegacyOstdSerial.hpp"

namespace dragon
{
	namespace hw
	{
		class VirtualMMU //TODO: Implement for later use
		{
			public:
				inline VirtualMMU(void) { init(); }
				VirtualMMU& init(void);

			private:
				ostd::serial::SerialIO m_data;

			public:
				inline static constexpr u16 MMUSize = 6144;
				inline static constexpr u16 PageSize = 512;
		};
	}
}
