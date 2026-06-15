#pragma once

#include <ogfx/gui/Window.hpp>
#include <ogfx/render/PixelRenderer.hpp>
#include "../hardware/VirtualIODevices.hpp"

namespace dragon
{
	namespace data { class IBiosVideoPalette; }
	namespace hw
	{
		class VirtualDisplay : public ogfx::GraphicsWindow
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
				inline void setFont(const String& fontPath) { m_font.init(fontPath); }

				void onInitialize(void) override;
				void onDestroy(void) override;
				void onRender(void) override;
				void onUpdate(void) override;
				void onFixedUpdate(f64 frameTime_s) override;

				inline void redrawScreen(void) { m_redrawScreen = true; }

			private:
				void __redraw_screen(void);

				void single_text_add_char_to_line(char c);
				void single_text_add_char_to_buffer(char c);
				void single_text_flush_buffer(void);
				void single_text_print_buffer_and_flush(void);
				void single_text_print_buffer_no_flush(void);

				void single_text_clear_screen(void);
				void single_text_refresh_screen(void);

				void text16_init_buffer(void);
				void text16_load_palettes(void);

			private:
				ogfx::PixelRenderer m_renderer;
				ogfx::PixelRenderer::Font m_font;

				std::vector<String> m_singleTextLines;
				String m_singleTextBuffer { "" };

				std::vector<hw::interface::Graphics::tText16_Cell> m_text16_buffer;
				std::vector<data::IBiosVideoPalette*> m_text16_palettes;
				data::IBiosVideoPalette* m_text16_Currentpalette { nullptr };
				u8 m_currentPaletteID { 0 };

				bool m_redrawScreen { true };
		};
	}
}
