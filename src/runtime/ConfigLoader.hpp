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
		i32 debug_clock_rate_sec { 0 };		// the clock in the debugger (ddb); 0: clock_rate_sec
		u32 ram_size_kb { data::DefaultValues::DefaultRAMSizeKB };
		u32 disk_block_size { data::DefaultValues::DefaultDiskBlockSize };	// bytes per cycle
		bool fixed_clock { true };
		String bios_path;
		String cmos_path;
		ostd::Color singleColor_background;
		ostd::Color singleColor_foreground;
		u8 text16_palette { 0 };
		u8 screen_redraw_rate_per_second { 10 };
		bool crt_effect { false };
		bool bloom_effect { false };

		// The debugger's clock: debug_clock_rate_sec when set, clock_rate_sec otherwise. The
		// debugger runs far slower than the runtime, and its time is machine time (cycles / this
		// rate), so a rate it can reach keeps its milliseconds close to real ones.
		inline i32 debugClockRate(void) const { return debug_clock_rate_sec > 0 ? debug_clock_rate_sec : clock_rate_sec; }

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
