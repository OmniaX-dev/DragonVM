#pragma once

#include "Widget.hpp"

#include "../../tools/SDLInclude.hpp"

#include "../../gui/VirtualConsoleOutputHandler.hpp"
#include "../../tools/GlobalData.hpp"

namespace dragon
{
	namespace hw
	{
		class VirtualBIOSVideo;
		class VirtualCPU;
	}
	class VirtualConsoleWidtget : public Widget
	{
		public: struct tSignals
		{
			inline static constexpr uint8_t Continue = 0x00;
			inline static constexpr uint8_t ClearScreen = 0x01;
		};
		public:
			inline VirtualConsoleWidtget(hw::VirtualBIOSVideo& biosVideo, hw::VirtualCPU& cpu) : m_biosVideo(biosVideo), m_cpu(cpu) { invalidate(); }
			void init(void) override;
			void draw(void) override;
			void update(void) override;
			void fixedUpdate(void) override;
			void slowUpdate(void) override;

			inline bool isInitialized(void) const { return m_initialized; }

		private:
			void drawConsole(void);

		private:
			dragon::VirtualConsoleOutputHandler vout;

			// SDL_Window* m_window { nullptr };
			// SDL_Renderer* m_renderer { nullptr };
			// SDL_Texture* m_screenTexture { nullptr };
			// SDL_Surface* m_fontSurface { nullptr };

			// int32_t m_windowWidth { 0 };
			// int32_t m_windowHeight { 0 };

			// uint32_t* m_fontPixels { nullptr };
			// uint32_t* m_screenPixels { nullptr };

			uint32_t* m_pixels { nullptr };
			SDL_Texture* m_texture;

			// bool m_redrawConsole { true };
			// float m_timeAccumulator { 0.0f };
			// bool m_running { false };

			// ostd::StringEditor m_title { "" };
			// float m_redrawAccumulator { 0.0f };
			bool m_initialized { false };
			bool m_clearMemory { false };

			hw::VirtualBIOSVideo& m_biosVideo;
			hw::VirtualCPU& m_cpu;
			std::vector<data::IBiosVideoPalette*> m_palettes;
	};
}