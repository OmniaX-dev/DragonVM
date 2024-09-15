#include "Renderer.hpp"
#include "Window.hpp"
#include <ostd/Utils.hpp>

namespace dragon
{
	Renderer::~Renderer(void)
	{
		if (isInvalid()) return;
		ostd::Utils::destroyArray(m_pixels);
		SDL_DestroyTexture(m_texture);
	}

	void Renderer::initialize(Window& parent)
	{
		if (isValid()) return; //TODO: Error
		if (!parent.isValid() || !parent.isInitialized())
			return; //TODO: Error
		m_parent = &parent;
		m_pixels = ostd::Utils::createArray<uint32_t>(parent.getWindowWidth() * parent.getWindowHeight());
		m_texture = SDL_CreateTexture(parent.getSDLRenderer(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, parent.getWindowWidth(), parent.getWindowHeight());
		m_windowWidth = parent.getWindowWidth();
		m_windowHeight = parent.getWindowHeight();
		setTypeName("dragon::Renderer");
		enableSignals();
		connectSignal(ostd::tBuiltinSignals::WindowResized);
		validate();
	}

	void Renderer::handleSignal(ostd::tSignal& signal)
	{
		if (isInvalid()) return;
		if (signal.ID == ostd::tBuiltinSignals::WindowResized)
		{
			m_pixels = ostd::Utils::resizeArray<uint32_t>(m_pixels, m_parent->getWindowWidth() * m_parent->getWindowHeight());
			SDL_DestroyTexture(m_texture);
			m_texture = SDL_CreateTexture(m_parent->getSDLRenderer(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, m_parent->getWindowWidth(), m_parent->getWindowHeight());
			m_windowWidth = m_parent->getWindowWidth();
			m_windowHeight = m_parent->getWindowHeight();
			updateBuffer();
			displayBuffer();  
		}
	}

	void Renderer::updateBuffer(void)
	{
		if (isInvalid()) return;
		SDL_UpdateTexture(m_texture, NULL, m_pixels, m_windowWidth * 4);
	}
	
	void Renderer::displayBuffer(void)
	{
		if (isInvalid()) return;
		SDL_Rect rect { 0, 0, m_windowWidth, m_windowHeight };
		SDL_RenderCopy(m_parent->getSDLRenderer(), m_texture, NULL, &rect);
	}

	void Renderer::clear(const ostd::Color& color)
	{
		if (isInvalid()) return;
		for (int32_t y = 0; y < m_windowHeight; y++)
		{
			for (uint32_t x = 0; x < m_windowWidth; x++)
			{
				int32_t index = CONVERT_2D_1D(x, y, m_windowWidth);
				m_pixels[index] = color.asInteger(ostd::Color::eColorFormat::ARGB);
			}
		}
	}
}