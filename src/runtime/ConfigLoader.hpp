#pragma once

#include <ostd/Color.hpp>
#include <map>
#include "../tools/GlobalData.hpp"

namespace dragon
{
	struct tMachineConfig
	{
		std::map<int32_t, ostd::String> vdisk_paths;
		std::map<int32_t, data::CPUExtension*> cpuext_list;
		int32_t clock_rate_sec { 500 };
		bool fixed_clock { true };
		ostd::String bios_path;
		ostd::String cmos_path;
		ostd::Color singleColor_background;
		ostd::Color singleColor_foreground;
		
		inline bool isValid(void) const { return m_valid; }
		inline void destroy(void) { for (auto& ptr : cpuext_list) delete ptr.second; }

		private:
			bool m_valid { false };

			friend class MachineConfigLoader;
	};

	class MachineConfigLoader
	{
		public:
			static const tMachineConfig loadConfig(const ostd::String& configFilePath);

		private:
			static const tMachineConfig& validate_machine_config(tMachineConfig& config);
	};
}