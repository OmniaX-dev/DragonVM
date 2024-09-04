#pragma once

#include <ostd/Types.hpp>
#include <ostd/String.hpp>

namespace dragon
{
	class Utils
	{
		public:
			static ostd::String genRandomName(uint8_t length);
	};	
}