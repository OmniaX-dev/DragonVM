#include "ConfigLoader.hpp"
#include <ostd/File.hpp>
#include <ostd/Utils.hpp>

namespace dragon
{
	const tMachineConfig MachineConfigLoader::loadConfig(const ostd::String& configFilePath)
	{
		tMachineConfig config;
		ostd::TextFileBuffer file(configFilePath);
		if (!file.exists()) return config;
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