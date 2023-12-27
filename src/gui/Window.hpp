#pragma once

#include "../tools/SDLInclude.hpp"
#include <ostd/Utils.hpp>
#include <ostd/Signals.hpp>
#include "VirtualConsoleOutputHandler.hpp"
#include "../tools/GlobalData.hpp"
#include "../gui/widgets/Widget.hpp"

namespace dragon
{
	class Window
	{
		public:
			inline Window(void) {  }
			inline bool isInitialized(void) const { return m_initialized; }
			inline bool isRunning(void) const { return m_running; }
			inline void hide(void) { SDL_HideWindow(m_window); }
			inline void show(void) { SDL_ShowWindow(m_window); }
			void initialize(int32_t width, int32_t height, const ostd::String& fontPath);
			void update(void);
			bool addWidget(Widget& widget);
			void setSize(int32_t width, int32_t height);

		private:
			void handleEvents(void);
			void finalizeRender(void);

		private:
			ostd::ConsoleOutputHandler out;

			std::vector<Widget*> m_widgets;

			SDL_Window* m_window { nullptr };
			SDL_Renderer* m_renderer { nullptr };
			// SDL_Texture* m_screenTexture { nullptr };
			SDL_Surface* m_fontSurface { nullptr };

			int32_t m_windowWidth { 0 };
			int32_t m_windowHeight { 0 };

			uint32_t* m_fontPixels { nullptr };
			// uint32_t* m_screenPixels { nullptr };

			float m_timeAccumulator { 0.0f };
			bool m_running { false };

			ostd::StringEditor m_title { "" };
			float m_redrawAccumulator { 0.0f };
			bool m_initialized { false };

			//Signals
			inline static const uint32_t Signal_OnMousePressed = ostd::SignalHandler::newCustomSignal(4096);

		friend class VirtualConsoleWidtget;
	};
}