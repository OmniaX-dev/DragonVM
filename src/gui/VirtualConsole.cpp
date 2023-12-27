#include "VirtualConsole.hpp"
#include "RawTextRenderer.hpp"
#include <ostd/Defines.hpp>

#include "../hardware/VirtualIODevices.hpp"
#include "../hardware/VirtualCPU.hpp"

namespace dragon
{
	void VirtualConsole::initialize(void)
	{
		m_windowWidth = RawTextRenderer::CONSOLE_CHARS_H * RawTextRenderer::FONT_CHAR_W; //60 * 16;
		m_windowHeight = RawTextRenderer::CONSOLE_CHARS_V * RawTextRenderer::FONT_CHAR_H; //60 * 9;
		
		SDL_Init(SDL_INIT_VIDEO);
		m_window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_windowWidth, m_windowHeight, SDL_WINDOW_RESIZABLE);
		SDL_SetWindowResizable(m_window, SDL_FALSE);
		m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
		SDL_SetWindowMinimumSize(m_window, m_windowWidth, m_windowHeight);
		m_screenTexture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, m_windowWidth, m_windowHeight);
		m_fontSurface = SDL_LoadBMP("font.bmp");
		if (m_fontSurface == NULL)
			out.bg(ostd::ConsoleColors::Red).p("Error loading font.").nl();
		m_fontPixels = (uint32_t*)m_fontSurface->pixels;
		m_screenPixels = (uint32_t*)malloc(m_windowWidth * m_windowHeight * 4);	

		dragon::RawTextRenderer::initialize();

		vout.init(m_screenPixels, m_windowWidth, m_windowHeight, m_fontPixels);
		vout.enableFixedScreen();

		m_palettes.push_back(new data::BiosVideoDefaultPalette); //TODO: Possible Memory Leak, palettes are never destroyed

		m_initialized = true;
		m_running = true;
	}

	void VirtualConsole::update(void)
	{
		if (!m_initialized) return;
		Uint64 start = SDL_GetPerformanceCounter();
		handleEvents();
		if (m_redrawConsole)
		{
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
			m_redrawConsole = false;
		}
		finalizeRender();
		Uint64 end = SDL_GetPerformanceCounter();
		float elapsed = (end - start) / (float)SDL_GetPerformanceFrequency();
		m_redrawAccumulator += elapsed;
		if (m_redrawAccumulator >= 0.2f)
		{
			m_redrawConsole = true;
			m_redrawAccumulator = 0.0f;
		}
		end = SDL_GetPerformanceCounter();
		elapsed = (end - start) / (float)SDL_GetPerformanceFrequency();
		m_timeAccumulator += elapsed;
		if (m_timeAccumulator >= 0.5f)
		{
			m_title.clr().add("FPS: ").addi((int)(1.0f / elapsed));
			SDL_SetWindowTitle(m_window, m_title.c_str());
			m_timeAccumulator = 0.0f;
		}
	}

	void VirtualConsole::handleEvents(void)
	{
		if (!m_initialized) return;
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT) exit(0);
		}
	}

	void VirtualConsole::finalizeRender(void)
	{
		SDL_RenderClear(m_renderer);
		SDL_UpdateTexture(m_screenTexture, NULL, m_screenPixels, m_windowWidth * 4);
		SDL_RenderCopy(m_renderer, m_screenTexture, NULL, NULL);
		SDL_RenderPresent(m_renderer);
	}

	void VirtualConsole::drawConsole(void)
	{
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
			for (int y = 0; y < m_windowHeight; ++y)
			{
				for (int x = 0; x < m_windowWidth; ++x)
				{
					m_screenPixels[x + y * m_windowWidth] = clrCol.asInteger();
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
}