#pragma once

#include <ostd/Serial.hpp>

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
				inline static constexpr uint16_t MMUSize = 6144;
				inline static constexpr uint16_t PageSize = 512;
		};
	}
}