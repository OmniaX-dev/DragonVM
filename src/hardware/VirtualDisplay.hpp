#pragma once

#include "../gui/Window.hpp"
#include "../gui/Renderer.hpp"

namespace dragon
{
	namespace hw
	{
		class VirtualDisplay : public Window
		{
			public: struct tRegisters
			{
				inline static constexpr uint8_t VideoMode = 0x00;	
				inline static constexpr uint8_t Signal = 0x03;
				inline static constexpr uint8_t TextSingleCharacter = 0x04;
				inline static constexpr uint8_t TextSingleInvertColors = 0x05;
				inline static constexpr uint8_t TextSingleString = 0x06;
			};
			public: struct tVideoModeValues
			{
				inline static constexpr uint8_t TextSingleColor = 0x00;	
			};
			public: struct tSignalValues
			{
				inline static constexpr uint8_t Continue = 0x00;	

				inline static constexpr uint8_t TextSingleColor_DirectPrintChar = 0x02;	
				inline static constexpr uint8_t TextSingleColor_StoreChar = 0x03;	
				inline static constexpr uint8_t TextSingleColor_DirectPrintBuffAndFlush = 0x04;	
				inline static constexpr uint8_t TextSingleColor_FlushBuffer = 0x05;	
				inline static constexpr uint8_t TextSingleColor_DirectPrintBuffNoFlush = 0x06;	
				inline static constexpr uint8_t TextSingleColor_DirectPrintString = 0x07;

				inline static constexpr uint8_t RefreshScreen = 0xE0;	
				inline static constexpr uint8_t ClearSCreen = 0xE1;	
			};
			public:
				void onInitialize(void) override;
				void onDestroy(void) override;
				void onRender(void) override;
				void onUpdate(void) override;
				void onFixedUpdate(void) override;
				void onSlowUpdate(void) override;

			private:
				void single_text_add_char_to_line(char c);
				void single_text_add_char_to_buffer(char c);
				void single_text_flush_buffer(void);
				void single_text_print_buffer_and_flush(void);
				void single_text_print_buffer_no_flush(void);

				void single_text_clear_screen(void);
				void single_text_refresh_screen(void);

			private:
				Renderer m_renderer;

				std::vector<ostd::String> m_singleTextLines;
				ostd::String m_singleTextBuffer { "" };
		};
	}
}