#include "Text16Console.hpp"
#include "MemoryMapper.hpp"
#include <memory>

namespace dragon
{
	namespace hw
	{
		namespace text16console
		{
			using Errors = tLayout::Errors;
			using VPolicy = tLayout::VOverflowPolicy;
			using HPolicy = tLayout::HOverflowPolicy;

			// Text16Console::scroll(false): the buffer is a ring of rows (m_yOffset is the row
			// shown at the top); the row that comes in at the bottom is cleared.
			static void scroll(tLayout& con, u8* buffer, u32 cols, u32 rows)
			{
				const u32 yOffset = (con.m_yOffset + 1) % rows;
				con.m_yOffset = yOffset;
				u8* row = buffer + ((yOffset + rows - 1) % rows) * cols * 4;
				for (u32 x = 0; x < cols; x++, row += 4)
				{
					row[0] = ' ';
					row[1] = con.m_fg_col;
					row[2] = con.m_bg_col;
					row[3] = 0;
				}
				con.m_scrollOffset = con.m_scrollOffset + 1;
			}

			// Text16Console::check_new_line()
			static bool check_new_line(const tLayout& con, u32 rows)
			{
				return con.m_overflowPolicyV == VPolicy::Scroll || cast<u32>(con.m_cursor_y) < rows - 1;
			}

			// Text16Console::nl() (called with no error set)
			static void nl(tLayout& con, u8* buffer, u32 cols, u32 rows)
			{
				if (!check_new_line(con, rows))
				{
					if (con.m_overflowPolicyV != VPolicy::Ignore)
						con.m_error = Errors::VerticalOverflow;
					return;
				}
				const u32 y = con.m_cursor_y;
				if (y < rows - 1)
					con.m_cursor_y = cast<u16>(y + 1);
				else if (y == rows - 1)
					scroll(con, buffer, cols, rows);
				else
				{
					con.m_error = Errors::InvalidState;
					return;
				}
				con.m_cursor_x = 0;
			}

			bool print(MemoryMapper& mem, u32 consoleAddr, u32 textAddr)
			{
				u8* conBytes = mem.writableSpan(consoleAddr, sizeof(tLayout));
				if (conBytes == nullptr)
					return false;
				tLayout& con = *std::start_lifetime_as<tLayout>(conBytes);
				if (!con.m_initialized || con.m_error != Errors::NoError)
					return true;
				const u32 cols = con.m_screen_x;
				const u32 rows = con.m_screen_y;
				u8* buffer = nullptr;
				if (cols != 0 && rows != 0 && cast<u64>(cols) * rows * 4 <= con.m_screenBufferSize)
					buffer = mem.writableSpan(con.m_screenBufferPtr, cols * rows * 4);
				if (buffer == nullptr)
				{
					con.m_error = Errors::InvalidState;
					return true;
				}

				auto l_row = [&](void) -> u8* { return buffer + ((con.m_cursor_y + con.m_yOffset) % rows) * cols * 4; };
				const u8 fg = con.m_fg_col;
				const u8 bg = con.m_bg_col;
				u32 x = con.m_cursor_x;
				u8* row = l_row();
				bool skip = false;		// Ignore policy: past the right edge until '\n'
				for (u32 addr = textAddr; ; addr++)
				{
					const u8 c = mem.peek8(addr);
					if (c == 0)
						break;
					if (c == '\n' || (x >= cols && !skip))
					{
						if (c != '\n' && con.m_overflowPolicyH != HPolicy::Wrap)
						{
							if (con.m_overflowPolicyH == HPolicy::Error)
							{
								con.m_error = Errors::HorizontalOverflow;
								break;
							}
							skip = true;		// Ignore
						}
						else
						{
							con.m_cursor_x = cast<u16>(x);
							const u16 y0 = con.m_cursor_y;
							const u32 s0 = con.m_scrollOffset;
							nl(con, buffer, cols, rows);
							// An error, or no new line (vertical Ignore on the last row): the rest is dropped.
							if (con.m_error != Errors::NoError || (con.m_cursor_y == y0 && con.m_scrollOffset == s0))
								break;
							x = 0;
							skip = false;
							row = l_row();
							if (c == '\n')
								continue;
						}
					}
					if (!skip)
					{
						u8* cell = row + x * 4;
						cell[0] = c;
						cell[1] = fg;
						cell[2] = bg;
						cell[3] = 0;
						x++;
					}
				}
				con.m_cursor_x = cast<u16>(x);
				return true;
			}
		}
	}
}
