#pragma once

#include <ogfx/gui/Window.hpp>
#include <chrono>

#include "../hardware/VirtualDisplay.hpp"

namespace dragon
{
	// The runtime's window: just the machine's screen (a VirtualDisplay widget filling the window).
	//
	// runMachine() runs the machine until the CPU stops (or the window is closed). The window is
	// driven one step per CPU cycle (see __main_loop()), instead of gui::Window's blocking loop.
	class RuntimeWindow : public ogfx::gui::Window
	{
		public:
			// Opens the window (ScreenWidth x ScreenHeight) and sets up the screen; the machine
			// config must already be loaded.
			void create(const String& title, const String& fontPath);
			void runMachine(void);

			inline hw::VirtualDisplay& getDisplay(void) { return m_display; }

			void onInitialize(void) override;

		protected:
			// One step per call: handles the video signal, while events and drawing run at the
			// window's own frame rate.
			void __main_loop(void) override;

		private:
			hw::VirtualDisplay m_display { *this };
			String m_fontPath { "" };
			std::chrono::steady_clock::time_point m_lastWindowStep;
	};
}
