#include "RuntimeWindow.hpp"
#include "DragonRuntime.hpp"
#include <ostd/utils/Time.hpp>

namespace dragon
{
	void RuntimeWindow::create(const String& title, const String& fontPath)
	{
		m_fontPath = fontPath;
		initialize(hw::VirtualDisplay::ScreenWidth, hw::VirtualDisplay::ScreenHeight, title);
	}

	void RuntimeWindow::onInitialize(void)
	{
		// The runtime drives this window one step per CPU cycle: waiting for events would
		// stall the CPU.
		enableBlockingEvents(false);
		setClearColor({ 0, 0, 0 });

		addWidget(m_display);
		m_display.setAnchorPreset(ogfx::gui::eAnchorPreset::FullRect);
		m_display.initialize(m_fontPath);
		m_display.enableCRTEffect(DragonRuntime::machine_config.crt_effect);
		m_display.enableBloomEffect(DragonRuntime::machine_config.bloom_effect);
	}

	void RuntimeWindow::__main_loop(void)
	{
		m_display.processSignal();
		// Events, updates and drawing run on gui::Window's own timers (60 fps); checking them
		// at most every millisecond keeps the per-cycle cost down at high clock rates.
		const auto now = std::chrono::steady_clock::now();
		if (now - m_lastWindowStep < std::chrono::milliseconds(1))
			return;
		m_lastWindowStep = now;
		handle_events();
		m_fixedUpdateTimer.update();
		m_mainLoopTimer.update();
	}

	void RuntimeWindow::runMachine(void)
	{
		auto& cpu = DragonRuntime::cpu;
		auto& disk = DragonRuntime::vDiskInterface;
		bool running = true;
		u8 screenRedrawRate = DragonRuntime::vCMOS.read8(data::CMOSRegisters::ScreenRedrawRate);
		f64 cycleUPS = (DragonRuntime::machine_config.fixed_clock ? DragonRuntime::machine_config.clock_rate_sec : -1.0);
		ostd::StepTimer cycleTimer(cycleUPS, [&](f64 dt) {
			mainLoop();
			running = cpu.execute() && isRunning();
			disk.cycleStep();
		});
		ostd::StepTimer screenTimer(screenRedrawRate, [&](f64 dt) {
			m_display.redrawScreen();
		});
		while (running || disk.isBusy())
		{
			cycleTimer.update();
			screenTimer.update();
			if (dragon::data::ErrorHandler::hasError())
			{
				DragonRuntime::processErrors();
				break;
			}
		}
	}
}
