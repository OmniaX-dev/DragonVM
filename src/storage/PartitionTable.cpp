#include "PartitionTable.hpp"
#include "DPT.hpp"

#include <algorithm>

namespace dragon
{
	namespace storage
	{
		//PartitionScheme
		String PartitionScheme::flagsToString(u32 flags) const
		{
			String str = "";
			for (const auto& flag : getFlags())
			{
				if (((flags >> flag.bit) & 1) == 0)
					continue;
				if (str != "")
					str.add(",");
				str.add(flag.name);
			}
			return str;
		}

		stdvec<String> PartitionScheme::validate(const tPartitionTable& table, u64 diskSize) const
		{
			stdvec<String> errors;
			const auto& parts = table.partitions;
			if (diskSize < getMinDiskSize())
				errors.push_back(String("The disk is too small for a ").add(getName()).add(" table (at least ").add(formatSize(getMinDiskSize())).add(")."));
			if (parts.size() > getMaxPartitions())
				errors.push_back(String("Too many partitions: ").add(cast<u64>(parts.size())).add(", ").add(getName()).add(" allows ").add(getMaxPartitions()).add("."));
			const u64 first = getFirstUsableAddress(diskSize);
			const u64 last = getLastUsableAddress(diskSize);
			u32 knownFlags = 0;
			for (const auto& flag : getFlags())
				knownFlags |= (1u << flag.bit);
			for (size_t i = 0; i < parts.size(); i++)
			{
				const tPartition& part = parts[i];
				const String name = String("Partition ").add(cast<u64>(i + 1));
				if (part.size == 0)
					errors.push_back(String(name).add(": the size is 0."));
				if (part.start < first)
					errors.push_back(String(name).add(": starts before ").add(String::getHexStr(first, true, 4)).add(" (the first usable address)."));
				if (part.end() > last || part.end() < part.start)
					errors.push_back(String(name).add(": ends at ").add(String::getHexStr(part.end(), true, 4)).add(", past the end of the usable space (").add(String::getHexStr(last, true, 4)).add(")."));
				if (part.label.len() > getMaxLabelLength())
					errors.push_back(String(name).add(": the label is too long (").add(cast<u64>(part.label.len())).add(" bytes, max ").add(getMaxLabelLength()).add(")."));
				if ((part.flags & ~knownFlags) != 0)
					errors.push_back(String(name).add(": unknown flags (").add(String::getHexStr(part.flags & ~knownFlags, true, 2)).add(")."));
				for (const auto& area : getReservedAreas(diskSize))
				{
					if (part.start < area.start + area.size && area.start < part.end())
						errors.push_back(String(name).add(": overlaps the ").add(area.name).add(" area."));
				}
			}
			// Overlaps, in address order.
			stdvec<size_t> order(parts.size());
			for (size_t i = 0; i < order.size(); i++)
				order[i] = i;
			std::sort(order.begin(), order.end(), [&parts](size_t a, size_t b) { return parts[a].start < parts[b].start; });
			for (size_t i = 1; i < order.size(); i++)
			{
				const tPartition& prev = parts[order[i - 1]];
				const tPartition& curr = parts[order[i]];
				if (curr.start < prev.end())
					errors.push_back(String("Partitions ").add(cast<u64>(std::min(order[i - 1], order[i]) + 1)).add(" and ").add(cast<u64>(std::max(order[i - 1], order[i]) + 1)).add(" overlap."));
			}
			return errors;
		}




		//PartitionSchemes
		stdvec<std::unique_ptr<PartitionScheme>>& PartitionSchemes::schemes(void)
		{
			static stdvec<std::unique_ptr<PartitionScheme>> s_schemes = [](void) {
				stdvec<std::unique_ptr<PartitionScheme>> list;
				list.push_back(std::make_unique<DPTScheme>());
				return list;
			}();
			return s_schemes;
		}

		const stdvec<std::unique_ptr<PartitionScheme>>& PartitionSchemes::list(void)
		{
			return schemes();
		}

		const PartitionScheme* PartitionSchemes::find(const String& name)
		{
			for (const auto& scheme : schemes())
			{
				if (scheme->getName() == name)
					return scheme.get();
			}
			return nullptr;
		}

		const PartitionScheme* PartitionSchemes::detect(Disk& disk)
		{
			for (const auto& scheme : schemes())
			{
				if (scheme->detect(disk))
					return scheme.get();
			}
			return nullptr;
		}

		void PartitionSchemes::add(std::unique_ptr<PartitionScheme> scheme)
		{
			schemes().push_back(std::move(scheme));
		}
	}
}
