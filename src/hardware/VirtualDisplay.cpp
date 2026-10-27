#include "VirtualDisplay.hpp"
#include "../runtime/DragonRuntime.hpp"
#include "../tools/GlobalData.hpp"
#include <cctype>

namespace dragon
{
	namespace hw
	{
		using ogfx::gui::TextConsoleCanvas;

		VirtualDisplay::VirtualDisplay(ogfx::gui::Window& window) : TextConsoleCanvas(window, ScreenWidth, ScreenHeight)
		{
			setTypeName("dragon::hw::VirtualDisplay");
			setLetterboxColor({ 0, 0, 0 });
		}

		VirtualDisplay::~VirtualDisplay(void)
		{
			for (auto* palette : m_text16_palettes)
				delete palette;
			m_text16_palettes.clear();
		}

		void VirtualDisplay::initialize(const String& fontPath)
		{
			auto& config = DragonRuntime::machine_config;
			m_singlePalette.setColor(SingleForeground, config.singleColor_foreground);
			m_singlePalette.setColor(SingleBackground, config.singleColor_background);

			text16_load_palettes();
			m_currentPaletteID = config.text16_palette;
			if (m_currentPaletteID >= m_text16_palettes.size())
				m_text16_Currentpalette = m_text16_palettes[0];
			else
				m_text16_Currentpalette = m_text16_palettes[m_currentPaletteID];
			// VM color i is palette entry i. Entry 0 can't hold a color (it is the console's
			// default), so the console's default colors are set to VM color 0 in this mode.
			for (u32 i = 1; i < 256; i++)
				m_text16Palette.setColor(cast<u8>(i), m_text16_Currentpalette->getColor(cast<u8>(i)));

			if (!m_font.loadFromFile(fontPath))
				return;
			setFont(m_font);
			enter_text_mode();
		}

		void VirtualDisplay::enter_text_mode(void)
		{
			if (m_textModeActive || !m_font.isLoaded())
				return;
			// Cells are always CharWidth x CharHeight, so the screen is Columns x Rows whatever
			// the font's own cell size.
			enterTextConsoleMode(m_cells, 0, m_singlePalette, CharWidth, CharHeight);
			m_textModeActive = isTextConsoleMode();
			m_videoMode = 0xFF; // re-applied on the next frame
		}

		void VirtualDisplay::apply_video_mode(u8 videoMode)
		{
			if (videoMode == m_videoMode || !m_textModeActive)
				return;
			m_videoMode = videoMode;
			auto& config = DragonRuntime::machine_config;
			if (videoMode == tVideoModeValues::Text16Colors)
			{
				const ostd::Color color0 = m_text16_Currentpalette->getColor(0);
				setPalette(m_text16Palette);
				setDefaultColors(color0, color0);
			}
			else
			{
				setPalette(m_singlePalette);
				setDefaultColors(config.singleColor_foreground, config.singleColor_background);
			}
			m_redrawScreen = true;
		}

		void VirtualDisplay::onUpdate(void)
		{
			auto& mem = DragonRuntime::memMap;
			const u16 vga_addr = data::MemoryMapAddresses::VideoCardInterface_Start;
			apply_video_mode(mem.read8(vga_addr + tRegisters::VideoMode));
			m_redrawScreen = m_redrawScreen && !DragonRuntime::vGraphicsInterface.readFlag(hw::interface::Graphics::tFlags::ScreenRedrawDisabled);
			if (m_redrawScreen)
				__redraw_screen();
			TextConsoleCanvas::onUpdate();
		}

		void VirtualDisplay::processSignal(void)
		{
			auto& mem = DragonRuntime::memMap;
			const u16 vga_addr = data::MemoryMapAddresses::VideoCardInterface_Start;
			const u8 signal = mem.read8(vga_addr + tRegisters::Signal);
			if (signal == tSignalValues::Continue)
				return;
			const u8 video_mode = mem.read8(vga_addr + tRegisters::VideoMode);
			if (video_mode == tVideoModeValues::TextSingleColor)
			{
				switch (signal)
				{
					case tSignalValues::TextSingleColor_DirectPrintChar:
						single_text_add_char_to_line((char)mem.read8(vga_addr + tRegisters::TextSingleCharacter));
						break;
					case tSignalValues::TextSingleColor_DirectPrintString:
					{
						u16 char_addr = mem.read16(vga_addr + tRegisters::TextSingleString);
						for (char c = (char)mem.read8(char_addr); c != 0; c = (char)mem.read8(++char_addr))
							single_text_add_char_to_line(c);
						break;
					}
					case tSignalValues::TextSingleColor_StoreChar:
						single_text_add_char_to_buffer((char)mem.read8(vga_addr + tRegisters::TextSingleCharacter));
						break;
					case tSignalValues::TextSingleColor_DirectPrintBuffNoFlush:
						single_text_print_buffer_no_flush();
						break;
					case tSignalValues::TextSingleColor_DirectPrintBuffAndFlush:
						single_text_print_buffer_and_flush();
						break;
					case tSignalValues::TextSingleColor_FlushBuffer:
						single_text_flush_buffer();
						break;
					case tSignalValues::ClearSCreen:
						single_text_clear_screen();
						break;
					case tSignalValues::RefreshScreen:
						// The text is turned into cells on every redraw: nothing to do.
						break;
					default:
						break;
				}
			}
			else if (video_mode == tVideoModeValues::Text16Colors)
			{
				auto& graphics = DragonRuntime::vGraphicsInterface;
				switch (signal)
				{
					case tSignalValues::Text16Color_WriteMemory:
					{
						const u8 fg = mem.read8(vga_addr + tRegisters::MemControllerFGCol);
						const u8 bg = mem.read8(vga_addr + tRegisters::MemControllerBGCol);
						const u8 character = mem.read8(vga_addr + tRegisters::MemControllerChar);
						const i16 x = mem.read16(vga_addr + tRegisters::MemControllerX);
						const i16 y = mem.read16(vga_addr + tRegisters::MemControllerY);
						graphics.writeVRAM_16Colors(static_cast<u8>(x), static_cast<u8>(y), character, bg, fg);
						break;
					}
					case tSignalValues::ClearSCreen:
					{
						const u8 fg = mem.read8(vga_addr + tRegisters::MemControllerFGCol);
						const u8 bg = mem.read8(vga_addr + tRegisters::MemControllerBGCol);
						const u8 character = mem.read8(vga_addr + tRegisters::MemControllerChar);
						graphics.clearVRAM_16Colors(character, bg, fg);
						m_redrawScreen = true;
						break;
					}
					case tSignalValues::RedrawScreen:
						__redraw_screen();
						break;
					case tSignalValues::Text16Color_SwapBuffers:
						graphics.swapBuffers_16Colors();
						__redraw_screen();
						break;
					case tSignalValues::Text16Color_Scroll:
						graphics.scroll_16Colors();
						__redraw_screen();
						break;
					default:
						break;
				}
			}
			else
			{
				return;
			}
			mem.write8(vga_addr + tRegisters::Signal, tSignalValues::Continue);
		}

		void VirtualDisplay::__redraw_screen(void)
		{
			if (m_textModeActive)
			{
				const u8 video_mode = DragonRuntime::memMap.read8(data::MemoryMapAddresses::VideoCardInterface_Start + tRegisters::VideoMode);
				apply_video_mode(video_mode);
				if (video_mode == tVideoModeValues::Text16Colors)
					text16_to_cells();
				else if (video_mode == tVideoModeValues::TextSingleColor)
					single_text_to_cells();
			}
			DragonRuntime::cpu.handleInterrupt(data::InterruptCodes::Text16ModeScreenRefreshed, true);
			m_redrawScreen = false;
		}



		// ===================================================== SINGLE COLOR MODE =====================================================
		void VirtualDisplay::single_text_add_char_to_line(char c)
		{
			const u16 vga_addr = data::MemoryMapAddresses::VideoCardInterface_Start;
			const bool invert = DragonRuntime::memMap.read8(vga_addr + tRegisters::TextSingleInvertColors) != 0;
			if (m_singleTextLines.empty())
				m_singleTextLines.emplace_back();
			if (c == '\n')
			{
				m_singleTextLines.emplace_back();
			}
			else if (std::isprint(cast<u8>(c)))
			{
				if (m_singleTextLines.back().size() >= cast<size_t>(Columns))
					m_singleTextLines.emplace_back();
				const u8 fg = invert ? SingleBackground : SingleForeground;
				const u8 bg = invert ? SingleForeground : SingleBackground;
				m_singleTextLines.back().push_back(TextConsoleCanvas::makeCell(cast<u8>(c), fg, bg));
			}
			else
			{
				return;
			}
			while (m_singleTextLines.size() > cast<size_t>(Rows))
				m_singleTextLines.pop_front();
		}

		void VirtualDisplay::single_text_add_char_to_buffer(char c)
		{
			if (c == '\n' || m_singleTextBuffer.len() == cast<u32>(Columns))
			{
				single_text_print_buffer_and_flush();
				single_text_add_char_to_line(c);
			}
			else
			{
				m_singleTextBuffer.addChar(c);
			}
		}

		void VirtualDisplay::single_text_flush_buffer(void)
		{
			m_singleTextBuffer = "";
		}

		void VirtualDisplay::single_text_print_buffer_and_flush(void)
		{
			single_text_print_buffer_no_flush();
			single_text_flush_buffer();
		}

		void VirtualDisplay::single_text_print_buffer_no_flush(void)
		{
			for (const char c : m_singleTextBuffer.cpp_str())
				single_text_add_char_to_line(c);
		}

		void VirtualDisplay::single_text_clear_screen(void)
		{
			m_singleTextLines.clear();
		}

		void VirtualDisplay::single_text_to_cells(void)
		{
			// The lines go to the first screen of the cell buffer (the canvas shows only that).
			for (i32 row = 0; row < Rows; row++)
			{
				u32* dst = m_cells.data() + cast<size_t>(row) * Columns;
				const stdvec<u32>* line = cast<size_t>(row) < m_singleTextLines.size() ? &m_singleTextLines[row] : nullptr;
				const size_t used = line ? std::min(line->size(), cast<size_t>(Columns)) : 0;
				if (used > 0)
					std::copy(line->begin(), line->begin() + used, dst);
				std::fill(dst + used, dst + Columns, 0);
			}
		}
		// ===================================================== SINGLE COLOR MODE =====================================================



		// ===================================================== TEXT 16 COLORS MODE =====================================================
		void VirtualDisplay::text16_to_cells(void)
		{
			hw::interface::Graphics::tText16_Cell cell;
			for (i32 y = 0; y < Rows; y++)
			{
				for (i32 x = 0; x < Columns; x++)
				{
					if (!DragonRuntime::vGraphicsInterface.readVRAM_16Colors(cast<u8>(x), cast<u8>(y), cell))
						continue;
					m_cells[cast<size_t>(y) * Columns + x] = TextConsoleCanvas::makeCell(cell.character, cell.foregroundColor, cell.backgroundColor);
				}
			}
		}

		void VirtualDisplay::text16_load_palettes(void)
		{
			if (m_text16_palettes.empty())
				m_text16_palettes.push_back(new data::BiosVideoDefaultPalette);
		}
		// ===================================================== TEXT 16 COLORS MODE =====================================================
	}
}
