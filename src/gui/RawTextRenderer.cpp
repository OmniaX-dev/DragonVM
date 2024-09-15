#include "RawTextRenderer.hpp"
#include <ostd/Utils.hpp>
#include <ostd/Geometry.hpp>
#include <ostd/Defines.hpp>

namespace dragon
{
	void RawTextRenderer::initialize(void)
	{
		for (char c = ' '; c <= '~'; c++)
			characterMap[c] = getCharacterIndex(c);
	}

	bool RawTextRenderer::drawString(ostd::String str, uint32_t column, uint32_t row, uint32_t* screenPixels, int32_t screenWidth, int32_t screenHeight, uint32_t* fontPixels,  ostd::Color color, ostd::Color background)
	{
		ostd::String se(str);
		if (se == "") return false;
		if (row >= CONSOLE_CHARS_V) return false;
		if (column >= CONSOLE_CHARS_H) return false;
		if (column + str.len() > CONSOLE_CHARS_H) return false;
		int32_t x = column * FONT_CHAR_W;
		int32_t y = row * FONT_CHAR_H;
		for (auto& c : str)
		{
			drawCharacter((uint8_t*)screenPixels, screenWidth, screenHeight, (uint8_t*)fontPixels, x, y, c, color, background);
			x += FONT_CHAR_W;
		}
		s_cursor_pos_x = x;
		if (s_cursor_pos_x >= CONSOLE_CHARS_H)
			s_cursor_pos_x = 0;
		return true;
	}

	int32_t RawTextRenderer::getCharacterIndex(char c)
	{
		using namespace ostd;

		int32_t charIndex = (int)c - 32;
		IPoint charCoords = CONVERT_1D_2D(charIndex, FONT_H_CHARS);
		charCoords.x *= FONT_CHAR_W * 4;
		charCoords.y *= FONT_CHAR_H;
		charIndex = CONVERT_2D_1D(charCoords.x, charCoords.y, (FONT_H_CHARS * FONT_CHAR_W * 4));

		return charIndex;
	}

	ostd::Color RawTextRenderer::applyTint(ostd::Color baseColor, ostd::Color tintColor)
	{
		auto nBase = baseColor.getNormalizedColor();
		auto nTint = tintColor.getNormalizedColor();

		float r = nBase.r * nTint.r;
		float g = nBase.r * nTint.g;
		float b = nBase.r * nTint.b;

		ostd::Color::FloatCol nTinted(r, g, b, 1.0f);

		return ostd::Color(nTinted);
	}

	void RawTextRenderer::drawCharacter(uint8_t* screenPixels, int32_t screenWidth, int32_t screenHeight, uint8_t* fontPixels, int32_t x, int32_t y, char c, ostd::Color color, ostd::Color background)
	{
		using namespace ostd;
		int32_t charIndex = characterMap[c];
		IPoint charCoords = CONVERT_1D_2D(charIndex, (FONT_CHAR_W * FONT_H_CHARS * 4));

		int32_t screenx = x * 4, screeny = y;

		ostd::Color tintedColor;

		bool applyBackground = false;
		for (int32_t y = charCoords.y; y < charCoords.y + (FONT_CHAR_H); y += 1)
		{
			for (int32_t x = charCoords.x; x < charCoords.x + (FONT_CHAR_W * 4); x += 4)
			{
				int32_t index = CONVERT_2D_1D(x, y, (FONT_CHAR_W * FONT_H_CHARS * 4));
				int32_t screenIndex = CONVERT_2D_1D(screenx, screeny, (screenWidth * 4));
				screenx += 4;
				if (fontPixels[index] == 0x00 && fontPixels[index + 1] == 0x00 && fontPixels[index + 2] == 0x00)
				{
					if (background.a == 0)
						continue;
					applyBackground = true;
				}
				if (applyBackground)
				{
					screenPixels[screenIndex + 0] = background.b;
					screenPixels[screenIndex + 1] = background.g;
					screenPixels[screenIndex + 2] = background.r;
					screenPixels[screenIndex + 3] = 255;
					applyBackground = false;
					continue;
				}
				tintedColor = applyTint({ fontPixels[index], fontPixels[index + 1], fontPixels[index + 2], 255 }, color);
				screenPixels[screenIndex + 0] = tintedColor.b;
				screenPixels[screenIndex + 1] = tintedColor.g;
				screenPixels[screenIndex + 2] = tintedColor.r;
				screenPixels[screenIndex + 3] = fontPixels[index + 3];
			}
			screeny += 1;
			screenx = x * 4;
		}
	}
}