#pragma once

#include "../tools/SDLInclude.hpp"
#include <ostd/Utils.hpp>
#include <ostd/Signals.hpp>
#include <ostd/IOHandlers.hpp>

namespace dragon
{
	class Window : public  ostd::BaseObject
	{
		public:
			inline Window(void) {  }
			~Window(void);
			inline Window(int32_t width, int32_t height, const ostd::String& windowTitle, const ostd::String& fontPath) { initialize(width, height, windowTitle, fontPath); }
			void initialize(int32_t width, int32_t height, const ostd::String& windowTitle, const ostd::String& fontPath);

			void update(void);
			void setSize(int32_t width, int32_t height);
			void setTitle(const ostd::String& title);

			inline virtual void onRender(void) {  }
			inline virtual void onUpdate(void) {  }
			inline virtual void onFixedUpdate(void) {  }
			inline virtual void onSlowUpdate(void) {  }
			inline virtual void onInitialize(void) {  }
			inline virtual void onDestroy(void) {  }

			inline bool isInitialized(void) const { return m_initialized; }
			inline bool isRunning(void) const { return m_running; }
			inline bool isVisible(void) const { return m_visible; }
			inline void hide(void) { SDL_HideWindow(m_window); m_visible = false; }
			inline void show(void) { SDL_ShowWindow(m_window); m_visible = true; }
			inline ostd::String getTitle(void) const { return m_title; }
			inline int32_t getFPS(void) const { return m_fps; }
			inline int32_t getWindowWidth(void) const { return m_windowWidth; }
			inline int32_t getWindowHeight(void) const { return m_windowHeight; }
			inline SDL_Renderer* getSDLRenderer(void) const { return m_renderer; }
			inline bool isMouseDragEventEnabled(void) const { return m_deagEventEnabled; }
			inline void enableMouseDragEvent(bool enable = true) { m_deagEventEnabled = enable; }

		private:
			void handleEvents(void);

		protected:
			ostd::ConsoleOutputHandler out;

			SDL_Window* m_window { nullptr };
			SDL_Renderer* m_renderer { nullptr };
			SDL_Surface* m_fontSurface { nullptr };
			uint32_t* m_fontPixels { nullptr };

			bool m_refreshScreen { true };

		private:
			int32_t m_windowWidth { 0 };
			int32_t m_windowHeight { 0 };
			ostd::String m_title { "" };
			int32_t m_fps { 0 };

			float m_timeAccumulator { 0.0f };
			float m_redrawAccumulator { 0.0f };

			bool m_deagEventEnabled { false };
			bool m_running { false };
			bool m_initialized { false };
			bool m_visible { true };
	};
	class WindowResizedData : public ostd::BaseObject
	{
		public:
			inline WindowResizedData(Window& parent, int32_t _oldx, int32_t _oldy, int32_t _newx, int32_t _newy) : parentWindow(parent), old_width(_oldx), old_height(_oldy), new_width(_newx), new_height(_newy)
			{
				setTypeName("dragon::WindowResizedData");
				validate();
			} 
		
		public:
			int32_t new_width;
			int32_t new_height;
			int32_t old_width;
			int32_t old_height;
			Window& parentWindow;
	};
	class MouseEventData : public ostd::BaseObject
	{
		public: enum class eButton { None = 0, Left, Middle, Right };
		public:
			inline MouseEventData(Window& parent, int32_t mousex, int32_t mousey, eButton btn) : parentWindow(parent), position_x(mousex), position_y(mousey), button(btn)
			{
				setTypeName("dragon::MouseEventData");
				validate();
			}

		public:
			int32_t position_x;
			int32_t position_y;
			eButton button;
			Window& parentWindow;
	};
	class KeyEventData : public ostd::BaseObject
	{
		public: enum class eKeyEvent { Pressed = 0, Released, Text };
		public:
			inline KeyEventData(Window& parent, int32_t key, char _text, eKeyEvent evt) : parentWindow(parent), keyCode(key), text(_text), eventType(evt)
			{
				setTypeName("dragon::KeyEventData");
				validate();
			}

		public:
			int32_t keyCode;
			char text;
			eKeyEvent eventType;
			Window& parentWindow;
	};
}