#pragma once

#include <ostd/Utils.hpp>
#include <ostd/Color.hpp>

#include <ostd/IOHandlers.hpp>

#include <vector>

namespace dragon
{
	struct tRichChar
	{
		unsigned char ascii { 0 };
		ostd::Color foreground { 0, 0 };
		ostd::Color background { 0, 0 };
	};

	class RichString //TODO: Legacy RichString should be replaced with new tStyledString
	{
		public:
			inline RichString(void) {  }
			inline ostd::String getText(void) const { return m_text; }
			tRichChar at(uint32_t index) const;
			void add(tRichChar rchar);
			void add(ostd::String str, ostd::Color fg, ostd::Color bg);
			void add(ostd::String str);
			void clear(void);
 
		private:
			ostd::String m_text { "" };
			std::vector<ostd::Color> m_foreground;
			std::vector<ostd::Color> m_background; 
	};

	class VirtualConsoleOutputHandler : public ostd::legacy::IOutputHandler
	{
		public:
			ostd::legacy::IOutputHandler& col(ostd::String color) override;
			ostd::legacy::IOutputHandler& col(const ostd::Color& color) override;
			ostd::legacy::IOutputHandler& p(char c) override;
			ostd::legacy::IOutputHandler& p(const ostd::StringEditor& se) override;
			ostd::legacy::IOutputHandler& pi(uint8_t i) override;
			ostd::legacy::IOutputHandler& pi(int8_t i) override;
			ostd::legacy::IOutputHandler& pi(uint16_t i) override;
			ostd::legacy::IOutputHandler& pi(int16_t i) override;
			ostd::legacy::IOutputHandler& pi(uint32_t i) override;
			ostd::legacy::IOutputHandler& pi(int32_t i) override;
			ostd::legacy::IOutputHandler& pi(uint64_t i) override;
			ostd::legacy::IOutputHandler& pi(int64_t i) override;
			ostd::legacy::IOutputHandler& pf(float f, uint8_t precision = 0) override;
			ostd::legacy::IOutputHandler& pf(double f, uint8_t precision = 0) override;
			ostd::legacy::IOutputHandler& nl(void) override;
			inline ostd::legacy::IOutputHandler& pStyled(const ostd::StringEditor& styled) override { return *this; } //TODO: Implement
			inline ostd::legacy::IOutputHandler& pStyled(const ostd::TextStyleBuilder::IRichStringBase& styled) override { return *this; } //TODO: Implement
			inline ostd::legacy::IOutputHandler& pStyled(const ostd::TextStyleParser::tStyledString& styled) override { return *this; }; //TODO: Implement
			ostd::legacy::IOutputHandler& flush(void) override;
			ostd::legacy::IOutputHandler& reset(void) override;
			ostd::legacy::IOutputHandler& clear(void) override;

			ostd::legacy::IOutputHandler& bgcol(const ostd::Color& color) override;
			ostd::legacy::IOutputHandler& bgcol(ostd::String color) override;
			ostd::legacy::IOutputHandler& resetColors(void) override;

			void init(uint32_t* screenPixels, int32_t screenWidth, int32_t screenHeight, uint32_t* fontPixels);
			inline bool isInitialized(void) const { return m_initialized; }
			inline bool isFixedScreen(void) const { return m_fixedScreen; }
			inline void enableFixedScreen(bool fs = true) { m_fixedScreen = fs; }

			void refreshScreen(void);

		private:
			void add_current_to_rich_buffer(void);
			void check_if_new_line(void);
			void draw_rich_String(const RichString& rstr, uint32_t row);

		private:
			std::vector<RichString> m_lines;
			RichString m_currentBuffer;
			ostd::StringEditor m_currentBufferEditor;

			// std::vector<ostd::String> m_allLines;
			// ostd::StringEditor m_buffer;
			// int32_t m_currentRow { 0 };
			// int32_t m_currentColumn { 0 };
			ostd::Color m_currentForegroundColor { 255, 255, 255, 255 };
			ostd::Color m_currentBackgroundColor { 255, 255, 255, 0 };
			ostd::Color m_defaultForegroundColor { 255, 255, 255, 255 };
			ostd::Color m_defaultBackgroundColor { 255, 255, 255, 0 };

			uint32_t* m_screenPixels { nullptr };
			uint32_t* m_fontPixels { nullptr };
			int32_t m_screenWidth { 0 };
			int32_t m_screenHeight { 0 };
			bool m_initialized { false };
			bool m_newLine { true };
			bool m_fixedScreen { false };
	};
}