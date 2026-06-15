#pragma once

#include <ostd/data/Color.hpp>
#include <map>
#include "../tools/GlobalData.hpp"

namespace dragon
{
	struct tMachineConfig
	{
		std::map<i32, String> vdisk_paths;
		std::map<i32, data::CPUExtension*> cpuext_list;
		i32 clock_rate_sec { 500 };
		u8 memory_extension_pages { 0 };
		bool fixed_clock { true };
		String bios_path;
		String cmos_path;
		ostd::Color singleColor_background;
		ostd::Color singleColor_foreground;
		u8 text16_palette { 0 };
		u8 screen_redraw_rate_per_second { 10 };

		inline bool isValid(void) const { return m_valid; }
		inline void destroy(void) { for (auto& ptr : cpuext_list) delete ptr.second; }

		private:
			bool m_valid { false };

			friend class MachineConfigLoader;
	};

	class MachineConfigLoader
	{
		public:
			static const tMachineConfig loadConfig(const String& configFilePath);

		private:
			static const tMachineConfig& validate_machine_config(tMachineConfig& config);
	};
}
