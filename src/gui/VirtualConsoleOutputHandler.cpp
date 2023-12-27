#include "VirtualConsoleOutputHandler.hpp"
#include "RawTextRenderer.hpp"

#include <ostd/Defines.hpp>
#include <ostd/Logger.hpp>

namespace dragon
{
	tRichChar RichString::at(uint32_t index) const
	{
		if (index >= m_text.size())
		{
			OX_WARN("ox::RichString::at(...): Index out of bounds.");
			return tRichChar();
		}
		return { (unsigned char)m_text[index], m_foreground[index], m_background[index] };
	}

	void RichString::add(tRichChar rchar)
	{
		m_text += rchar.ascii;
		m_foreground.push_back(rchar.foreground);
		m_background.push_back(rchar.background);
	}

	void RichString::add(ostd::String str, ostd::Color fg, ostd::Color bg)
	{
		for (auto& c : str)
		{
			m_text += c;
			m_foreground.push_back(fg);
			m_background.push_back(bg);
		}
	}

	void RichString::add(ostd::String str)
	{
		ostd::Color fcol(255);
		ostd::Color bcol(0, 0);
		if (m_text.length() > 0)
		{
			fcol = m_foreground[m_foreground.size() - 1];
			bcol = m_background[m_background.size() - 1];
		}
		add(str, fcol, bcol);
	}

	void RichString::clear(void)
	{
		m_text = "";
		m_background.clear();
		m_foreground.clear();
	}

	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::col(ostd::String color)
	{
		//TODO: Maybe implement??
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::col(const ostd::Color& color)
	{
		m_currentForegroundColor = color;
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::p(char c)
	{
		check_if_new_line();
		m_currentBufferEditor.add(c);
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::p(const ostd::StringEditor& se)
	{
		check_if_new_line();
		m_currentBufferEditor.add(se.str());
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::pi(uint8_t i)
	{
		check_if_new_line();
		m_currentBufferEditor.addi(i);
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::pi(int8_t i)
	{
		check_if_new_line();
		m_currentBufferEditor.addi(i);
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::pi(uint16_t i)
	{
		check_if_new_line();
		m_currentBufferEditor.addi(i);
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::pi(int16_t i)
	{
		check_if_new_line();
		m_currentBufferEditor.addi(i);
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::pi(uint32_t i)
	{
		check_if_new_line();
		m_currentBufferEditor.addi(i);
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::pi(int32_t i)
	{
		check_if_new_line();
		m_currentBufferEditor.addi(i);
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::pi(uint64_t i)
	{
		check_if_new_line();
		m_currentBufferEditor.addi(i);
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::pi(int64_t i)
	{
		check_if_new_line();
		m_currentBufferEditor.addi(i);
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::pf(float f, uint8_t precision)
	{
		//TODO: Implement precision
		check_if_new_line();
		m_currentBufferEditor.addf(f);
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::pf(double f, uint8_t precision)
	{
		//TODO: Implement precision
		check_if_new_line();
		m_currentBufferEditor.addf(f);
		add_current_to_rich_buffer();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::nl(void)
	{
		m_newLine = true;
		flush();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::flush(void)
	{
		if (!isInitialized()) return *this;
		if (m_currentBuffer.getText() == "") return *this;

		m_lines.push_back(m_currentBuffer);
		m_currentBuffer.clear();
		return *this;
	}
	
	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::reset(void)
	{
		m_currentBuffer.clear();
		m_currentBufferEditor.clr();
		m_lines.clear();
		m_newLine = true;
		resetColors();
		return *this;
	}

	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::clear(void)
	{
		//TODO: Implement
		return *this;
	}

	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::bgcol(const ostd::Color& color)
	{
		m_currentBackgroundColor = color;
		return *this;
	}

	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::bgcol(ostd::String color)
	{
		//TODO: Maybe implement
		return *this;
	}

	ostd::legacy::IOutputHandler& VirtualConsoleOutputHandler::resetColors(void)
	{
		m_currentForegroundColor = m_defaultForegroundColor;
		m_currentBackgroundColor = m_defaultBackgroundColor;
		return *this;
	}

	void VirtualConsoleOutputHandler::init(uint32_t* screenPixels, int32_t screenWidth, int32_t screenHeight, uint32_t* fontPixels)
	{
		m_screenPixels = screenPixels;
		m_fontPixels = fontPixels;
		m_screenHeight = screenHeight;
		m_screenWidth = screenWidth;

		m_initialized = m_screenHeight > 0 && m_screenWidth > 0 && m_screenPixels != nullptr && m_fontPixels != nullptr;
	}
	
	void VirtualConsoleOutputHandler::add_current_to_rich_buffer(void)
	{
		if (m_currentBuffer.getText().length() + m_currentBufferEditor.len() <= RawTextRenderer::CONSOLE_CHARS_H)
		{
			m_currentBuffer.add(m_currentBufferEditor.str(), m_currentForegroundColor, m_currentBackgroundColor);
			m_currentBufferEditor.clr();
			if (m_currentBuffer.getText().length() == RawTextRenderer::CONSOLE_CHARS_H)
				nl();
			else 
				m_newLine = false;
			return;
		}
		int32_t extraStringLength = (m_currentBuffer.getText().length() + m_currentBufferEditor.len()) - RawTextRenderer::CONSOLE_CHARS_H;
		ostd::String excess = m_currentBufferEditor.substr(RawTextRenderer::CONSOLE_CHARS_H);
		ostd::String end = m_currentBufferEditor.substr(0, RawTextRenderer::CONSOLE_CHARS_H);
		m_currentBuffer.add(end, m_currentForegroundColor, m_currentBackgroundColor);
		nl();
		m_currentBufferEditor = excess;
		add_current_to_rich_buffer();
	}

	void VirtualConsoleOutputHandler::check_if_new_line(void)
	{
		if (m_newLine) return;
		if (m_lines.size() > 0)
		{
			m_currentBuffer = m_lines[m_lines.size() - 1];
			m_lines.pop_back();
		}
		
	}

	void VirtualConsoleOutputHandler::draw_rich_String(const RichString& rstr, uint32_t row)
	{
		for (int32_t i = 0; i <	rstr.getText().length(); i++)
		{
			auto rchar = rstr.at(i);
			RawTextRenderer::drawString(ostd::StringEditor().add(rchar.ascii).str(), i, row, m_screenPixels, m_screenWidth, m_screenHeight, m_fontPixels, rchar.foreground, rchar.background);
		}
	}

	void VirtualConsoleOutputHandler::refreshScreen(void)
	{
		uint32_t row = 0;
		if (m_lines.size() > RawTextRenderer::CONSOLE_CHARS_V - 1)
		{
			if (m_fixedScreen)
			{
				for (int32_t i = 0; i < RawTextRenderer::CONSOLE_CHARS_V; i++)
				{
					draw_rich_String(m_lines[i], row);
					row++;
				}
			}
			else
			{
				for (int32_t i = m_lines.size() - RawTextRenderer::CONSOLE_CHARS_V + 1; i < m_lines.size(); i++)
				{
					draw_rich_String(m_lines[i], row);
					row++;
				}
			}
		}
		else
		{
			for (auto& line : m_lines)
			{
				draw_rich_String(line, row);
				row++;
			}
		}

		m_lines.clear();
		m_currentBuffer.clear();
		m_currentBufferEditor.clr();
	}
}