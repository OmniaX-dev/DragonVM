#include "Utils.hpp"
#include <ostd/math/Random.hpp>

namespace dragon
{
	static const std::vector<char> g_symbols = { '_', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', '0', '1', '2', '3', '4', '5', '6', '7', '8' };

	String Utils::genRandomName(u8 length)
	{
		auto rnd_char = []() -> char {
			return g_symbols[ostd::Random::getui8(0, g_symbols.size())];
		};
		String name = "";
		for (i32 i = 0; i < length; i++)
			name.addChar(rnd_char());
		return name;
	}
}
