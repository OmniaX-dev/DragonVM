#include "VirtualDisplay.hpp"
#include "../gui/RawTextRenderer.hpp"
#include "../runtime/DragonRuntime.hpp"
#include "../tools/GlobalData.hpp"

namespace dragon
{
	namespace hw
	{
		void VirtualDisplay::onInitialize(void)
		{
			m_renderer.initialize(*this);
			RawTextRenderer::initialize();

			text16_load_palettes();
			m_currentPaletteID = DragonRuntime::machine_config.text16_palette;
			if (m_currentPaletteID >= m_text16_palettes.size())
				m_text16_Currentpalette = m_text16_palettes[0];
			else
				m_text16_Currentpalette = m_text16_palettes[m_currentPaletteID];

			text16_init_buffer();
		}

		void VirtualDisplay::onDestroy(void) {  }

		void VirtualDisplay::onRender(void)
		{
			if (!isVisible()) return;
			auto& config = DragonRuntime::machine_config;
			auto& mem = DragonRuntime::memMap;
			uint16_t vga_addr = data::MemoryMapAddresses::VideoCardInterface_Start;
			uint8_t video_mode = mem.read8(vga_addr + tRegisters::VideoMode);
			if (video_mode == tVideoModeValues::TextSingleColor)
			{
				uint8_t invert_colors = mem.read8(vga_addr + tRegisters::TextSingleInvertColors);
				if (m_refreshScreen)
				{
					if (invert_colors == 0)
						m_renderer.clear(config.singleColor_background);
					else
						m_renderer.clear(config.singleColor_foreground);
					for (int32_t i = 0; i < m_singleTextLines.size(); i++)
					{
						auto& line = m_singleTextLines[i];
						if (invert_colors == 0)
							RawTextRenderer::drawString(line, 0, i, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_foreground, config.singleColor_background);
						else
							RawTextRenderer::drawString(line, 0, i, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_background, config.singleColor_foreground);
					}
					m_refreshScreen = false;
				}
			}
			else if (video_mode == tVideoModeValues::Text16Colors)
			{
				if (m_refreshScreen)
				{
					uint8_t clear_color = mem.read8(vga_addr + tRegisters::ClearColor);
					ostd::Color clearColor = m_text16_Currentpalette->getColor(clear_color);
					m_renderer.clear(clearColor);
					for (int32_t i = 0; i < RawTextRenderer::CONSOLE_CHARS_V * RawTextRenderer::CONSOLE_CHARS_H; i++)
					{
						auto& cell = m_text16_buffer[i];
						ostd::Color background = m_text16_Currentpalette->getColor(cell.backgroundColor);
						ostd::Color foreground = m_text16_Currentpalette->getColor(cell.foregroundColor);
						char character = static_cast<char>(cell.character);
						auto xy = CONVERT_1D_2D(i, RawTextRenderer::CONSOLE_CHARS_H);
						RawTextRenderer::drawString(ostd::String().addChar(character), xy.x, xy.y, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, foreground, background);
					}
					m_refreshScreen = false;
				}
			}
			
			m_redrawScreen = m_redrawScreen && !DragonRuntime::vGraphicsInterface.readFlag(hw::interface::Graphics::tFlags::ScreenRedrawDisabled);
			if (m_redrawScreen)
			{
				__redraw_screen();
			}
			m_renderer.displayBuffer();
		}

		// static char c = 'A';

		void VirtualDisplay::onUpdate(void)		
		{
			auto& mem = DragonRuntime::memMap;
			uint16_t vga_addr = data::MemoryMapAddresses::VideoCardInterface_Start;
			uint8_t video_mode = mem.read8(vga_addr + tRegisters::VideoMode);
			uint8_t signal = mem.read8(vga_addr + tRegisters::Signal);
			if (signal == tSignalValues::Continue) return;
			if (video_mode == tVideoModeValues::TextSingleColor)
			{
				if (signal == tSignalValues::TextSingleColor_DirectPrintChar)
				{
					char c = (char)mem.read8(vga_addr + tRegisters::TextSingleCharacter);
					single_text_add_char_to_line(c);
				}
				else if (signal == tSignalValues::TextSingleColor_DirectPrintString)
				{
					uint16_t first_char_addr = mem.read16(vga_addr + tRegisters::TextSingleString);
					char c = ' ';
					int h = 0;
					while (c != 0)
					{
						char c = (char)mem.read8(first_char_addr);
						single_text_add_char_to_line(c);
						first_char_addr++;
						if (c == 0) break;
					}
				}
				else if (signal == tSignalValues::TextSingleColor_StoreChar)
				{
					char c = (char)mem.read8(vga_addr + tRegisters::TextSingleCharacter);
					single_text_add_char_to_buffer(c);
				}
				else if (signal == tSignalValues::TextSingleColor_DirectPrintBuffNoFlush)
				{
					single_text_print_buffer_no_flush();
				}
				else if (signal == tSignalValues::TextSingleColor_DirectPrintBuffAndFlush)
				{
					single_text_print_buffer_and_flush();
				}
				else if (signal == tSignalValues::TextSingleColor_FlushBuffer)
				{
					single_text_flush_buffer();
				}
				else if (signal == tSignalValues::ClearSCreen)
				{
					single_text_clear_screen();
				}
				else if (signal == tSignalValues::RefreshScreen)
				{
					single_text_refresh_screen();
				}
			}
			else if (video_mode == tVideoModeValues::Text16Colors)
			{
				if (signal == tSignalValues::Text16Color_WriteMemory)
				{
					hw::interface::Graphics::tText16_Cell textCell;

					textCell.foregroundColor = mem.read8(vga_addr + tRegisters::MemControllerFGCol);
					textCell.backgroundColor = mem.read8(vga_addr + tRegisters::MemControllerBGCol);
					textCell.character = mem.read8(vga_addr + tRegisters::MemControllerChar);
					int16_t x = mem.read16(vga_addr + tRegisters::MemControllerX);
					int16_t y = mem.read16(vga_addr + tRegisters::MemControllerY);

					//TODO: Remove this override used for testing purposes
					// for (int32_t i = 0; i < RawTextRenderer::CONSOLE_CHARS_V * RawTextRenderer::CONSOLE_CHARS_H; i++)
					// {
					// 	auto xy = CONVERT_1D_2D(i, RawTextRenderer::CONSOLE_CHARS_H);
					// 	DragonRuntime::vGraphicsInterface.writeVRAM_16Colors(static_cast<uint8_t>(xy.x), static_cast<uint8_t>(xy.y), c++, 0, 15);
					// 	if (c > 'Z')
					// 		c = 'A';
					// }
					
					DragonRuntime::vGraphicsInterface.writeVRAM_16Colors(static_cast<uint8_t>(x), static_cast<uint8_t>(y), textCell.character, textCell.backgroundColor, textCell.foregroundColor);
				}
				else if (signal == tSignalValues::ClearSCreen)
				{
					hw::interface::Graphics::tText16_Cell textCell;

					textCell.foregroundColor = mem.read8(vga_addr + tRegisters::MemControllerFGCol);
					textCell.backgroundColor = mem.read8(vga_addr + tRegisters::MemControllerBGCol);
					textCell.character = mem.read8(vga_addr + tRegisters::MemControllerChar);

					DragonRuntime::vGraphicsInterface.clearVRAM_16Colors(textCell.character, textCell.backgroundColor, textCell.foregroundColor);

					m_redrawScreen = true;
					m_refreshScreen = true;
				}
				else if (signal == tSignalValues::RedrawScreen)
				{
					__redraw_screen();
				}
				else if (signal == tSignalValues::Text16Color_SwapBuffers)
				{
					DragonRuntime::vGraphicsInterface.swapBuffers_16Colors();
					__redraw_screen();
				}
			}
			else return;
			mem.write8(vga_addr + tRegisters::Signal, tSignalValues::Continue);
		}

		void VirtualDisplay::onFixedUpdate(void)
		{
			auto& config = DragonRuntime::machine_config;
			auto& mem = DragonRuntime::memMap;
			uint16_t vga_addr = data::MemoryMapAddresses::VideoCardInterface_Start;
			uint8_t video_mode = mem.read8(vga_addr + tRegisters::VideoMode);
			uint8_t signal = mem.read8(vga_addr + tRegisters::Signal);
			if (video_mode == tVideoModeValues::Text16Colors)
			{
				m_refreshScreen = true;
				dragon::hw::interface::Graphics::tText16_Cell outTextCell;
				for (int32_t i = 0; i < RawTextRenderer::CONSOLE_CHARS_V * RawTextRenderer::CONSOLE_CHARS_H; i++)
				{
					auto xy = CONVERT_1D_2D(i, RawTextRenderer::CONSOLE_CHARS_H);
					DragonRuntime::vGraphicsInterface.readVRAM_16Colors(xy.x, xy.y, outTextCell);
					m_text16_buffer[i] = outTextCell;
				}
			}
		}

		void VirtualDisplay::onSlowUpdate(void) {  }

		void VirtualDisplay::__redraw_screen(void)
		{
			m_renderer.updateBuffer();
			DragonRuntime::cpu.handleInterrupt(data::InterruptCodes::Text16ModeScreenRefreshed, true);
			m_redrawScreen = false;
			m_refreshScreen = true;
		}

		void VirtualDisplay::single_text_add_char_to_line(char c)
		{
			auto& config = DragonRuntime::machine_config;
			auto& mem = DragonRuntime::memMap;
			uint16_t vga_addr = data::MemoryMapAddresses::VideoCardInterface_Start;
			uint8_t invert_colors = mem.read8(vga_addr + tRegisters::TextSingleInvertColors);
			if (m_singleTextLines.size() == 0)
			{
				m_singleTextLines.push_back(ostd::String().addChar(c));
				if (invert_colors == 0)
					RawTextRenderer::drawString(ostd::String().addChar(c), 0, 0, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_foreground, config.singleColor_background);
				else
					RawTextRenderer::drawString(ostd::String().addChar(c), 0, 0, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_background, config.singleColor_foreground);
				return;
			}
			auto& line = m_singleTextLines[m_singleTextLines.size() - 1];
			if (c == '\n')
				m_singleTextLines.push_back("");
			else if (isprint(c))
			{
				if (line.len() == RawTextRenderer::CONSOLE_CHARS_H)
				{
					m_singleTextLines.push_back(ostd::String().addChar(c));
					auto& line = m_singleTextLines[m_singleTextLines.size() - 1];
				if (invert_colors == 0)
						RawTextRenderer::drawString(ostd::String().addChar(c), line.len() - 1, m_singleTextLines.size() - 1, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_foreground, config.singleColor_background);
					else
						RawTextRenderer::drawString(ostd::String().addChar(c), line.len() - 1, m_singleTextLines.size() - 1, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_background, config.singleColor_foreground);
				}
				else
				{
					line.addChar(c);
					if (invert_colors == 0)
						RawTextRenderer::drawString(ostd::String().addChar(c), line.len() - 1, m_singleTextLines.size() - 1, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_foreground, config.singleColor_background);
					else
						RawTextRenderer::drawString(ostd::String().addChar(c), line.len() - 1, m_singleTextLines.size() - 1, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_background, config.singleColor_foreground);
				}
			}
			else return;
			if (m_singleTextLines.size() == RawTextRenderer::CONSOLE_CHARS_V + 1)
			{
				m_refreshScreen = true;
				m_singleTextLines.erase(m_singleTextLines.begin());
			}
		}

		void VirtualDisplay::single_text_add_char_to_buffer(char c)
		{
			if (c == '\n' || m_singleTextBuffer.len() == RawTextRenderer::CONSOLE_CHARS_H)
			{
				single_text_print_buffer_and_flush();
				single_text_add_char_to_line(c);
			}
			else
				m_singleTextBuffer.addChar(c);
		}

		void VirtualDisplay::single_text_flush_buffer(void)
		{
			m_singleTextBuffer = "";
		}

		void VirtualDisplay::single_text_print_buffer_and_flush(void)
		{
			for (auto& ch : m_singleTextBuffer)
				single_text_add_char_to_line(ch);
			single_text_flush_buffer();
		}

		void VirtualDisplay::single_text_print_buffer_no_flush(void)
		{
			for (auto& ch : m_singleTextBuffer)
				single_text_add_char_to_line(ch);
		}

		void VirtualDisplay::single_text_clear_screen(void)
		{
			m_singleTextLines.clear();
			m_refreshScreen = true;
		}

		void VirtualDisplay::single_text_refresh_screen(void)
		{
			m_refreshScreen = true;
		}


		void VirtualDisplay::text16_init_buffer(void)
		{
			for (int32_t i = 0; i < RawTextRenderer::CONSOLE_CHARS_V * RawTextRenderer::CONSOLE_CHARS_H; i++)
				m_text16_buffer.push_back({ 0, 0, ' ' });
		}

		void VirtualDisplay::text16_load_palettes(void)
		{
			m_text16_palettes.push_back(new data::BiosVideoDefaultPalette); //TODO: Delete, Memory Leak
		}
		
	}
}