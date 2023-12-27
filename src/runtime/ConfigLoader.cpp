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
			ostd::StringEditor lineEdit = line;
			if (!lineEdit.contains("=")) continue; //TODO: Warning
			auto tokens = lineEdit.tokenize("=");
			if (tokens.count() != 2) continue; //TODO: Warning
			lineEdit = tokens.next();
			lineEdit = lineEdit.toLower();
			if (lineEdit.str() == "disks")
			{
				lineEdit = tokens.next();
				tokens = lineEdit.tokenize(",");
				if (tokens.count() == 0) continue; //TODO: Warning
				int32_t disk_nr = 0;
				while (tokens.hasNext())
				{
					lineEdit = tokens.next();
					config.vdisk_paths[disk_nr++] = lineEdit.str();
				}
			}
			else if (lineEdit.str() == "bios")
			{
				lineEdit = tokens.next();
				config.bios_path = lineEdit.str();
			}
			else if (lineEdit.str() == "cmos")
			{
				lineEdit = tokens.next();
				config.cmos_path = lineEdit.str();
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