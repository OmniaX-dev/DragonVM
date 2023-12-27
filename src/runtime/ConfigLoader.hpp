#pragma once

#include <ostd/Types.hpp>
#include <map>

namespace dragon
{
	struct tMachineConfig
	{
		std::map<int32_t, ostd::String> vdisk_paths;
		ostd::String bios_path;
		ostd::String cmos_path;
		
		inline bool isValid(void) const { return m_valid; }

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