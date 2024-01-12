#pragma once

#include <ostd/Color.hpp>
#include "../tools/SDLInclude.hpp"

namespace dragon
{
	class Window;
	class Renderer : public ostd::BaseObject
	{
		public:
			inline Renderer(void) { invalidate(); }
			~Renderer(void);
			void initialize(Window& parent);
			void handleSignal(ostd::tSignal& signal) override;
			void updateBuffer(void);
			void displayBuffer(void);
			inline uint32_t* getScreenPixels(void) { return m_pixels; }

			void clear(const ostd::Color& color);
			
		private:
			uint32_t* m_pixels { nullptr };
			SDL_Texture* m_texture { nullptr };
			Window* m_parent { nullptr };
			int32_t m_windowWidth { 0 };
			int32_t m_windowHeight { 0 };
	};
}