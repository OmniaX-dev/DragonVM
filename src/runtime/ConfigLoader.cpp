#include "ConfigLoader.hpp"
#include <ostd/File.hpp>
#include <ostd/Utils.hpp>
#include "../hardware/CPUExtensions.hpp"

namespace dragon
{
	const tMachineConfig MachineConfigLoader::loadConfig(const ostd::String& configFilePath)
	{
		tMachineConfig config;
		ostd::TextFileBuffer file(configFilePath.cpp_str());
		if (!file.exists()) return config; //TODO: Error
		auto lines = file.getLines();
		for (auto& line : lines)
		{
			ostd::String lineEdit = line;
			if (!lineEdit.contains("=")) continue; //TODO: Warning
			auto tokens = lineEdit.tokenize("=");
			if (tokens.count() != 2) continue; //TODO: Warning
			lineEdit = tokens.next();
			lineEdit = lineEdit.toLower();
			if (lineEdit == "disks")
			{
				lineEdit = tokens.next();
				tokens = lineEdit.tokenize(",");
				if (tokens.count() == 0) continue; //TODO: Warning
				int32_t disk_nr = 0;
				while (tokens.hasNext())
				{
					lineEdit = tokens.next();
					config.vdisk_paths[disk_nr++] = lineEdit;
				}
			}
			else if (lineEdit == "cpuext")
			{
				lineEdit = tokens.next();
				tokens = lineEdit.tokenize(",");
				if (tokens.count() == 0) continue; //TODO: Warning
				int32_t ext_nr = 0;
				while (tokens.hasNext())
				{
					if (ext_nr >= 16) break; //TODO: Warning
					lineEdit = tokens.next();
					if (lineEdit == "extmov")
					{
						config.cpuext_list[ext_nr++] = new hw::cpuext::ExtMov;
					}
					else if (lineEdit == "extalu")
					{
						config.cpuext_list[ext_nr++] = new hw::cpuext::ExtAlu;
					}
					else continue; //TODO: Warning
				}
			}
			else if (lineEdit == "bios")
			{
				lineEdit = tokens.next();
				config.bios_path = lineEdit;
			}
			else if (lineEdit == "cmos")
			{
				lineEdit = tokens.next();
				config.cmos_path = lineEdit;
			}
			else if (lineEdit == "singlecolor_foreground")
			{
				lineEdit = tokens.next();
				config.singleColor_foreground.set(lineEdit);
			}
			else if (lineEdit == "singlecolor_background")
			{
				lineEdit = tokens.next();
				config.singleColor_background.set(lineEdit);
			}
			else if (lineEdit == "clock_rate_sec")
			{
				lineEdit = tokens.next();
				lineEdit.trim().toLower();
				if (!lineEdit.isNumeric()) continue; //TODO: Error
				config.clock_rate_sec = lineEdit.toInt();
			}
			else if (lineEdit == "fixed_clock")
			{
				lineEdit = tokens.next();
				lineEdit.trim().toLower();
				if (lineEdit == "true")
					config.fixed_clock = true;
				else if (lineEdit == "false")
					config.fixed_clock = false;
				else continue; //TODO: Error
			}
			else if (lineEdit == "memory_extension_pages")
			{
				lineEdit = tokens.next();
				lineEdit.trim().toLower();
				if (!lineEdit.isNumeric()) continue; //TODO: Error
				config.memory_extension_pages = lineEdit.toInt();
				
				//TODO: Warnings 
				if (config.memory_extension_pages < 0)
					config.memory_extension_pages = 0;
				if (config.memory_extension_pages > data::DefaultValues::MaxMemoryExtensionPages)
					config.memory_extension_pages = data::DefaultValues::MaxMemoryExtensionPages;
			}
			else if (lineEdit == "16color_palette")
			{
				lineEdit = tokens.next();
				lineEdit.trim().toLower();
				if (!lineEdit.isNumeric()) continue; //TODO: Error
				config.text16_palette = lineEdit.toInt();
			}
			else if (lineEdit == "screen_redraw_rate_per_second")
			{
				lineEdit = tokens.next();
				lineEdit.trim().toLower();
				if (!lineEdit.isNumeric()) continue; //TODO: Error
				config.screen_redraw_rate_per_second = lineEdit.toInt();
			}
			else continue; //TODO: Warning
		}
		return validate_machine_config(config);
	}

	const tMachineConfig& MachineConfigLoader::validate_machine_config(tMachineConfig& config) //TODO: Implement config validation
	{
		config.m_valid = true;
		return config;
	}
}