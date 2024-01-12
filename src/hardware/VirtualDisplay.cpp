#include "VirtualDisplay.hpp"
#include "../gui/RawTextRenderer.hpp"
#include "../runtime/DragonRuntime.hpp"

namespace dragon
{
	namespace hw
	{
		void VirtualDisplay::onInitialize(void)
		{
			m_renderer.initialize(*this);
			RawTextRenderer::initialize();
		}

		void VirtualDisplay::onDestroy(void) {  }

		void VirtualDisplay::onRender(void)
		{
			auto& config = DragonRuntime::machine_config;
			if (m_refreshScreen)
			{
				m_renderer.clear(config.singleColor_background);
				for (int32_t i = 0; i < m_singleTextLines.size(); i++)
				{
					auto& line = m_singleTextLines[i];
					RawTextRenderer::drawString(line, 0, i, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_foreground);
				}
				m_refreshScreen = false;
			}
			m_renderer.updateBuffer();
			m_renderer.displayBuffer();
		}

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
			else return;
			mem.write8(vga_addr + tRegisters::Signal, tSignalValues::Continue);
		}

		void VirtualDisplay::onFixedUpdate(void) {  }

		void VirtualDisplay::onSlowUpdate(void) {  }

		void VirtualDisplay::single_text_add_char_to_line(char c)
		{
			auto& config = DragonRuntime::machine_config;
			if (m_singleTextLines.size() == 0)
			{
				m_singleTextLines.push_back(ostd::String().addChar(c));
				RawTextRenderer::drawString(ostd::String().addChar(c), 0, 0, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_foreground);
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
					RawTextRenderer::drawString(ostd::String().addChar(c), line.len() - 1, m_singleTextLines.size() - 1, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_foreground);
				}
				else
				{
					line.addChar(c);
					RawTextRenderer::drawString(ostd::String().addChar(c), line.len() - 1, m_singleTextLines.size() - 1, m_renderer.getScreenPixels(), getWindowWidth(), getWindowHeight(), m_fontPixels, config.singleColor_foreground);
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
		
	}
}