#include "Window.hpp"

namespace dragon
{
	void Window::initialize(int32_t width, int32_t height, const ostd::String& fontPath)
	{
		m_windowWidth = width;
		m_windowHeight = height;
		
		SDL_Init(SDL_INIT_VIDEO);
		m_window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_windowWidth, m_windowHeight, SDL_WINDOW_RESIZABLE);
		SDL_SetWindowResizable(m_window, SDL_FALSE);
		m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
		SDL_SetWindowMinimumSize(m_window, m_windowWidth, m_windowHeight);
		// m_screenTexture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, m_windowWidth, m_windowHeight);
		m_fontSurface = SDL_LoadBMP(fontPath.c_str());
		if (m_fontSurface == NULL)
			out.bg(ostd::ConsoleColors::Red).p("Error loading font.").reset().nl();
		m_fontPixels = (uint32_t*)m_fontSurface->pixels;
		// m_screenPixels = (uint32_t*)malloc(m_windowWidth * m_windowHeight * 4);	

		m_initialized = true;
		m_running = true;
	}

	void Window::update(void)
	{
		if (!m_initialized) return;
		Uint64 start = SDL_GetPerformanceCounter();
		handleEvents();
		SDL_RenderClear(m_renderer);
		for (int32_t i = 0; i < m_widgets.size(); i++)
		{
			if (m_widgets[i] == nullptr) continue;
			m_widgets[i]->draw();
		}
		SDL_RenderPresent(m_renderer);
		// finalizeRender();
		Uint64 end = SDL_GetPerformanceCounter();
		float elapsed = (end - start) / (float)SDL_GetPerformanceFrequency();
		m_redrawAccumulator += elapsed;
		if (m_redrawAccumulator >= 0.2f)
		{
			for (int32_t i = 0; i < m_widgets.size(); i++)
			{
				if (m_widgets[i] == nullptr) continue;
				m_widgets[i]->fixedUpdate();
			}
			m_redrawAccumulator = 0.0f;
		}
		end = SDL_GetPerformanceCounter();
		elapsed = (end - start) / (float)SDL_GetPerformanceFrequency();
		m_timeAccumulator += elapsed;
		if (m_timeAccumulator >= 0.5f)
		{
			for (int32_t i = 0; i < m_widgets.size(); i++)
			{
				if (m_widgets[i] == nullptr) continue;
				m_widgets[i]->slowUpdate();
			}
			m_title.clr().add("FPS: ").addi((int)(1.0f / elapsed));
			SDL_SetWindowTitle(m_window, m_title.c_str());
			m_timeAccumulator = 0.0f;
		}
	}

	bool Window::addWidget(Widget& widget)
	{
		widget.__init(*this);
		m_widgets.push_back(&widget);
		return true;
	}

	void Window::setSize(int32_t width, int32_t height)
	{
		if (!isInitialized()) return;
		SDL_SetWindowSize(m_window, width, height);
	}

	void Window::handleEvents(void)
	{
		if (!m_initialized) return;
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
			{
				m_running = false;
			}
		}
	}

	void Window::finalizeRender(void)
	{
		// SDL_UpdateTexture(m_screenTexture, NULL, m_screenPixels, m_windowWidth * 4);
		// SDL_RenderCopy(m_renderer, m_screenTexture, NULL, NULL);
	}
}