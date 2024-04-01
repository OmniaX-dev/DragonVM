#pragma once

#include <unordered_map>
#include <ostd/Color.hpp>

namespace dragon
{
	class RawTextRenderer
	{
		private:
			inline static std::unordered_map<char, int32_t> characterMap;

		public:
			static void initialize(void);
			static bool drawString(ostd::String str, uint32_t column, uint32_t row, uint32_t* screenPixels, int32_t screenWidth, int32_t screenHeight, uint32_t* fontPixels,  ostd::Color color = { 255, 255, 255, 255 }, ostd::Color background = { 255, 255, 255, 0 });

		private:
			static int32_t getCharacterIndex(char c);
			static ostd::Color applyTint(ostd::Color baseColor, ostd::Color tintColor);
			static void drawCharacter(uint8_t* screenPixels, int32_t screenWidth, int32_t screenHeight, uint8_t* fontPixels, int32_t x, int32_t y, char c, ostd::Color color = { 255, 255, 255, 255 }, ostd::Color background = { 255, 255, 255, 0 });

		public:
			inline static constexpr int32_t FONT_CHAR_W = 11;
			inline static constexpr int32_t FONT_CHAR_H = 26;
			inline static constexpr int32_t FONT_V_CHARS = 6;
			inline static constexpr int32_t FONT_H_CHARS = 16;
			inline static constexpr int32_t CONSOLE_CHARS_H = 90;
			inline static constexpr int32_t CONSOLE_CHARS_V = 21;

			inline static int8_t s_cursor_pos_x = 0;
	};
}