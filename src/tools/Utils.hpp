#pragma once

#include <ostd/data/Types.hpp>
#include <ostd/string/String.hpp>

namespace dragon
{
	class Utils
	{
		public:
			static ostd::String genRandomName(uint8_t length);
	};
}
