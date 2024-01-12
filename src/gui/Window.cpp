#include "Window.hpp"

namespace dragon
{
	Window::~Window(void)
	{
		onDestroy();
		SDL_FreeSurface(m_fontSurface);
		SDL_DestroyRenderer(m_renderer);
		SDL_DestroyWindow(m_window);
		// IMG_Quit();
		SDL_Quit();
	}

	void Window::initialize(int32_t width, int32_t height, const ostd::String& windowTitle, const ostd::String& fontPath)
	{
		if (m_initialized) return;
		m_windowWidth = width;
		m_windowHeight = height;
		m_title = windowTitle;
		if (SDL_Init(SDL_INIT_VIDEO) != 0)
		{
			printf( "SDL could not initialize! Error: %s\n", SDL_GetError() );
			exit(1);
		}
		// int imgFlags = IMG_INIT_PNG;
		// if (!(IMG_Init(imgFlags) & imgFlags))
		// {
		// 	printf( "SDL_image could not initialize! SDL_image Error: %s\n", IMG_GetError() );
		// 	exit(2);
		// }
		m_window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_windowWidth, m_windowHeight, SDL_WINDOW_RESIZABLE);
		SDL_SetWindowResizable(m_window, SDL_FALSE);
		m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
		SDL_SetWindowMinimumSize(m_window, m_windowWidth, m_windowHeight);
		SDL_SetWindowTitle(m_window, m_title.c_str());
		m_fontSurface = SDL_LoadBMP(fontPath.c_str());
		if (m_fontSurface == NULL)
			out.bg(ostd::ConsoleColors::Red).p("Error loading font.").reset().nl();
		m_fontPixels = (uint32_t*)m_fontSurface->pixels;

		m_initialized = true;
		m_running = true;

		setTypeName("lspp::app::Window");
		enableSignals(true);
		validate();

		onInitialize();
	}

	void Window::update(void)
	{
		if (!m_initialized) return;
		Uint64 start = SDL_GetPerformanceCounter();
		handleEvents();
		if (m_refreshScreen)
			SDL_RenderClear(m_renderer);
		onUpdate();
		onRender();
		SDL_RenderPresent(m_renderer);
		Uint64 end = SDL_GetPerformanceCounter();
		float elapsed = (end - start) / (float)SDL_GetPerformanceFrequency();
		m_redrawAccumulator += elapsed;
		if (m_redrawAccumulator >= 0.2f)
		{
			onFixedUpdate();
			m_redrawAccumulator = 0.0f;
		}
		end = SDL_GetPerformanceCounter();
		elapsed = (end - start) / (float)SDL_GetPerformanceFrequency();
		m_timeAccumulator += elapsed;
		if (m_timeAccumulator >= 0.5f)
		{
			onSlowUpdate();
			m_fps = (int32_t)(1.0f / elapsed);
			m_timeAccumulator = 0.0f;
		}
	}

	void Window::setSize(int32_t width, int32_t height)
	{
		if (!isInitialized()) return;
		SDL_SetWindowSize(m_window, width, height);
		ostd::SignalHandler::emitSignal(ostd::tBuiltinSignals::WindowResized, ostd::tSignalPriority::RealTime);
	}

	void Window::setTitle(const ostd::String& title)
	{
		if (!isInitialized()) return;
		m_title = title;
		SDL_SetWindowTitle(m_window, m_title.c_str());
	}

	void Window::handleEvents(void)
	{
		if (!m_initialized) return;
		auto l_getMouseState = [this](void) -> MouseEventData {
			int32_t mx = 0, my = 0;
			uint32_t btn = SDL_GetMouseState(&mx, &my);
			MouseEventData::eButton button = MouseEventData::eButton::None;
			switch (btn)
			{
				case SDL_BUTTON(1): button = MouseEventData::eButton::Left; break;
				case SDL_BUTTON(2): button = MouseEventData::eButton::Middle; break;
				case SDL_BUTTON(3): button = MouseEventData::eButton::Right; break;
				default: button = MouseEventData::eButton::None; break;
			}
			MouseEventData mmd(*this, mx, my, button);
			return mmd;
		};
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
			{
				m_running = false;
				ostd::SignalHandler::emitSignal(ostd::tBuiltinSignals::WindowClosed, ostd::tSignalPriority::Normal, *this);
			}
			else if (event.type == SDL_WINDOWEVENT)
			{
				if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					WindowResizedData wrd(*this, m_windowWidth, m_windowHeight, 0, 0);
					SDL_GetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
					wrd.new_width = m_windowWidth;
					wrd.new_height = m_windowHeight;
					ostd::SignalHandler::emitSignal(ostd::tBuiltinSignals::WindowResized, ostd::tSignalPriority::RealTime, wrd);
				}
			}
			else if (event.type == SDL_MOUSEMOTION)
			{
				MouseEventData mmd = l_getMouseState();
				if (isMouseDragEventEnabled() && mmd.button != MouseEventData::eButton::None)
					ostd::SignalHandler::emitSignal(ostd::tBuiltinSignals::WindowClosed, ostd::tSignalPriority::RealTime, mmd);
				else
					ostd::SignalHandler::emitSignal(ostd::tBuiltinSignals::MouseMoved, ostd::tSignalPriority::RealTime, mmd);
			}
			else if (event.type == SDL_MOUSEBUTTONDOWN)
			{
				MouseEventData mmd = l_getMouseState();
				ostd::SignalHandler::emitSignal(ostd::tBuiltinSignals::MousePressed, ostd::tSignalPriority::RealTime, mmd);
			}
			else if (event.type == SDL_MOUSEBUTTONUP)
			{
				MouseEventData mmd = l_getMouseState();
				ostd::SignalHandler::emitSignal(ostd::tBuiltinSignals::MouseReleased, ostd::tSignalPriority::RealTime, mmd);
			}
		}
	}
}