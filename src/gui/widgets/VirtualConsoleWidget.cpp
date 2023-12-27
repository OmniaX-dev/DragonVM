#include "VirtualConsoleWidget.hpp"
#include "../../gui/RawTextRenderer.hpp"
#include "../../gui/Window.hpp"
#include "../../hardware/VirtualIODevices.hpp"
#include "../../hardware/VirtualCPU.hpp"

namespace dragon
{
	void VirtualConsoleWidtget::init(void)
	{
		setw(RawTextRenderer::CONSOLE_CHARS_H * RawTextRenderer::FONT_CHAR_W); //60 * 16;
		seth(RawTextRenderer::CONSOLE_CHARS_V * RawTextRenderer::FONT_CHAR_H); //60 * 9;
		setPosition(0, 0);
		
		dragon::RawTextRenderer::initialize();

		m_pixels = new uint32_t[getw() * geth()]; //TODO: Delete (Memory Leak)

		vout.init(m_pixels, getw(), geth(), m_parent->m_fontPixels);
		vout.enableFixedScreen();

		m_palettes.push_back(new data::BiosVideoDefaultPalette); //TODO: Possible Memory Leak, palettes are never destroyed
	
		m_texture = SDL_CreateTexture(m_parent->m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, getw(), geth());

		m_initialized = true;
		validate();
	}

	void VirtualConsoleWidtget::draw(void)
	{
		if (isInvalid()) return;
		SDL_Rect rect { (int)getx(), (int)gety(), (int)getw(), (int)geth() };
		SDL_RenderCopy(m_parent->m_renderer, m_texture, NULL, &rect);
	}

	void VirtualConsoleWidtget::drawConsole(void)
	{
		if (isInvalid()) return;
		const uint8_t TRANSPARENCY_COLOR_INDEX = 0x5;

		uint8_t clearColor = m_biosVideo.read8(hw::VirtualBIOSVideo::tRegisters::ClearColor);
		uint8_t palette = m_biosVideo.read8(hw::VirtualBIOSVideo::tRegisters::Palette);
		bool useTransparency = m_biosVideo.read8(hw::VirtualBIOSVideo::tRegisters::UseTransparencyOn0x5) != 0;

		if (palette >= m_palettes.size())
			palette = 0; //TODO: Error

		auto& pal = *m_palettes[palette];

		ostd::Color clrCol = pal.getColor(clearColor);
		if (m_clearMemory)
		{
			for (int y = 0; y < m_parent->m_windowHeight; ++y)
			{
				for (int x = 0; x < m_parent->m_windowWidth; ++x)
				{
					m_pixels[x + y * m_parent->m_windowWidth] = clrCol.asInteger();
				}
			}
			m_clearMemory = false;
		}

		ostd::Color fgCol;
		ostd::Color bgCol;
		uint16_t index = hw::VirtualBIOSVideo::tRegisters::VideoMemoryStart;

		// if (m_clearMemory)
		// {
		// 	for (int32_t i = index; i < (index + (RawTextRenderer::CONSOLE_CHARS_H * RawTextRenderer::CONSOLE_CHARS_V * 2)); i += 2)
		// 	{
		// 		m_biosVideo.write8((uint16_t)i, 0x00);
		// 		m_biosVideo.write8((uint16_t)(i + 1), clearColor);
		// 	}
		// 	m_clearMemory = false;
		// }

		for (int32_t i = index; i < (index + (RawTextRenderer::CONSOLE_CHARS_H * RawTextRenderer::CONSOLE_CHARS_V * 2)); i += 2)
		{
			uint8_t character = m_biosVideo.read8((uint16_t)i);
			uint8_t colors = m_biosVideo.read8((uint16_t)(i + 1));
			uint8_t bgcol = (colors >> 4) & 0xF;
			uint8_t fgcol = (colors & 0xF);
			if (!isprint(character))
			{
				character = ' ';
				fgCol = pal.getColor(fgcol);
				bgCol = clrCol;
			}
			else
			{
				fgCol = pal.getColor(fgcol);
				bgCol = pal.getColor(bgcol);
			}
			vout.bgcol(bgCol).col(fgCol).p((char)character).flush().bgcol(clrCol);
		}
		vout.refreshScreen();
		m_cpu.handleInterrupt(data::InterruptCodes::BiosVideoScreenRefresh);
	}
	
	void VirtualConsoleWidtget::update(void)
	{

	}
	
	void VirtualConsoleWidtget::fixedUpdate(void)
	{
		if (isInvalid()) return;
		uint8_t signal = m_biosVideo.read8(hw::VirtualBIOSVideo::tRegisters::Signal);
		switch (signal)
		{
			case tSignals::ClearScreen:
				m_clearMemory = true;
			break;
			case tSignals::Continue: break;
			default: break;
		}
		m_biosVideo.write8(hw::VirtualBIOSVideo::tRegisters::Signal, tSignals::Continue);
		drawConsole();
		SDL_UpdateTexture(m_texture, NULL, m_pixels, getw() * 4);
	}
	
	void VirtualConsoleWidtget::slowUpdate(void)
	{

	}
	
}