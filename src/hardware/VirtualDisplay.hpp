#pragma once

#include <ogfx/gui/Window.hpp>
#include <ogfx/gui/widgets/TextConsoleCanvas.hpp>
#include <ogfx/render/PixelFont.hpp>
#include <chrono>
#include <deque>

namespace dragon
{
	namespace data { class IBiosVideoPalette; }
	namespace hw
	{
		// The machine's screen: a window showing a TextConsoleCanvas (Columns x Rows character
		// cells). The video card protocol (registers + signals in the VGA memory area) is
		// unchanged:
		//   - TextSingleColor: a teletype. Characters are appended to lines (directly, or through
		//     a line buffer), lines scroll up when the screen is full, and the "invert" register
		//     swaps the two colors (of the characters written while it is set).
		//   - Text16Colors: the BIOS writes cells into VRAM through the Graphics interface;
		//     the screen shows VRAM, colored with the configured palette.
		// What is shown only changes when the screen is redrawn (the CMOS redraw rate, or a
		// redraw/swap/scroll signal), unless redrawing is disabled by the ScreenRedrawDisabled
		// flag; every redraw raises the Text16ModeScreenRefreshed interrupt.
		//
		// mainLoop() is called by the runtime once per CPU cycle: it handles the video signal
		// right away (the BIOS doesn't wait for it to be consumed), while events and drawing
		// run at the window's own frame rate.
		class VirtualDisplay : public ogfx::gui::Window
		{
			public: struct tRegisters
			{
				inline static constexpr u8 VideoMode = 0x00;
				inline static constexpr u8 ClearColor = 0x01;
				inline static constexpr u8 Palette = 0x02;
				inline static constexpr u8 Signal = 0x03;
				inline static constexpr u8 TextSingleCharacter = 0x04;
				inline static constexpr u8 TextSingleInvertColors = 0x05;
				inline static constexpr u8 TextSingleString = 0x06;

				inline static constexpr u8 Flags = 0x7E;

				inline static constexpr u8 MemControllerX = 0x80;
				inline static constexpr u8 MemControllerY = 0x82;
				inline static constexpr u8 MemControllerChar = 0x84;
				inline static constexpr u8 MemControllerBGCol = 0x85;
				inline static constexpr u8 MemControllerFGCol = 0x86;
			};
			public: struct tVideoModeValues
			{
				inline static constexpr u8 TextSingleColor = 0x00;
				inline static constexpr u8 Text16Colors = 0x01;
			};
			public: struct tSignalValues
			{
				inline static constexpr u8 Continue = 0x00;

				inline static constexpr u8 TextSingleColor_DirectPrintChar = 0x02;
				inline static constexpr u8 TextSingleColor_StoreChar = 0x03;
				inline static constexpr u8 TextSingleColor_DirectPrintBuffAndFlush = 0x04;
				inline static constexpr u8 TextSingleColor_FlushBuffer = 0x05;
				inline static constexpr u8 TextSingleColor_DirectPrintBuffNoFlush = 0x06;
				inline static constexpr u8 TextSingleColor_DirectPrintString = 0x07;

				inline static constexpr u8 Text16Color_SwapBuffers = 0x10;
				inline static constexpr u8 Text16Color_WriteMemory = 0x11;
				inline static constexpr u8 Text16Color_Scroll = 0x12;

				inline static constexpr u8 RefreshScreen = 0xE0;
				inline static constexpr u8 ClearSCreen = 0xE1;
				inline static constexpr u8 RedrawScreen = 0xE2;
			};

			public:
				// Text screen size, in characters, and the size of a character cell in pixels
				// (the font's cell size).
				inline static constexpr i32 Columns = 90;
				inline static constexpr i32 Rows = 21;
				inline static constexpr i32 CharWidth = 11;
				inline static constexpr i32 CharHeight = 26;
				inline static constexpr i32 ScreenWidth = Columns * CharWidth;
				inline static constexpr i32 ScreenHeight = Rows * CharHeight;

			public:
				// Loads the screen font (a 16 x 6 grid of CharWidth x CharHeight glyphs).
				~VirtualDisplay(void);
				void setFont(const String& fontPath);
				// The CRT / bloom look of the screen (off by default).
				inline void enableCRTEffect(bool enable = true) { m_screen.enableCRTEffect(enable); }
				inline void enableBloomEffect(bool enable = true) { m_screen.enableBloomEffect(enable); }

				void onInitialize(void) override;
				void onUpdate(f64 delta) override;

				inline void redrawScreen(void) { m_redrawScreen = true; }

			protected:
				// One step per call (see above), instead of gui::Window's blocking loop.
				void __main_loop(void) override;

			private:
				void process_signal(void);
				void __redraw_screen(void);
				void enter_text_mode(void);
				void apply_video_mode(u8 videoMode);

				void single_text_add_char_to_line(char c);
				void single_text_add_char_to_buffer(char c);
				void single_text_flush_buffer(void);
				void single_text_print_buffer_and_flush(void);
				void single_text_print_buffer_no_flush(void);
				void single_text_clear_screen(void);
				void single_text_to_cells(void);

				void text16_to_cells(void);
				void text16_load_palettes(void);

			private:
				// Single color mode palette entries (index 0 is the console's default: background).
				inline static constexpr u8 SingleForeground = 1;
				inline static constexpr u8 SingleBackground = 2;

				ogfx::PixelFont m_font;
				ogfx::gui::TextConsoleCanvas m_screen { *this, ScreenWidth, ScreenHeight };
				stdvec<u32> m_cells;
				bool m_textModeActive { false };
				u8 m_videoMode { 0xFF };

				ogfx::gui::PixelCanvas::ColorPalette256 m_singlePalette;
				ogfx::gui::PixelCanvas::ColorPalette256 m_text16Palette;

				// Single color mode: the lines of text, as cells (character + colors).
				std::deque<stdvec<u32>> m_singleTextLines;
				String m_singleTextBuffer { "" };

				stdvec<data::IBiosVideoPalette*> m_text16_palettes;
				data::IBiosVideoPalette* m_text16_Currentpalette { nullptr };
				u8 m_currentPaletteID { 0 };

				bool m_redrawScreen { true };
				std::chrono::steady_clock::time_point m_lastWindowStep;
		};
	}
}
