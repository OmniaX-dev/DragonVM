#pragma once

#include "Common.hpp"
#include <map>
#include <tuple>

namespace dscript
{
	// What the language server knows about a program, filled in while it is compiled: every use
	// (and declaration) of a name, where that name is declared and a description of it.
	struct IndexEntry
	{
		SourcePos pos;			// the name in the source
		int length { 0 };
		SourcePos def;			// where it is declared (file empty: builtin, nowhere to go)
		std::string hover;		// dscript code describing it
		bool define { false };	// the use of a #define (the names it expands to don't replace it)
	};

	class Index
	{
		public:
			typedef std::tuple<std::string, int, int> Key;

			void add(const SourcePos& pos, int length, const SourcePos& def, const std::string& hover, bool define = false)
			{
				if (pos.file.empty() || pos.line <= 0 || length <= 0)
					return;
				auto it = m_entries.find(Key(pos.file, pos.line, pos.column));
				if (it != m_entries.end() && it->second.define && !define)
					return;
				m_entries[Key(pos.file, pos.line, pos.column)] = { pos, length, def, hover, define };
			}

			// The entry under file:line:column (1-based), or nullptr.
			const IndexEntry* at(const std::string& file, int line, int column) const
			{
				auto it = m_entries.upper_bound(Key(file, line, column));
				if (it == m_entries.begin())
					return nullptr;
				--it;
				const IndexEntry& e = it->second;
				if (e.pos.file != file || e.pos.line != line || column >= e.pos.column + e.length)
					return nullptr;
				return &e;
			}

			// The hover of the declaration at def (the entry of the declaration itself), if any.
			const IndexEntry* declaration(const SourcePos& def) const
			{
				auto it = m_entries.find(Key(def.file, def.line, def.column));
				return it == m_entries.end() ? nullptr : &it->second;
			}

			// Every entry declared at a key of `hovers` gets that hover (the final description of
			// declarations that were used before they were fully resolved).
			void rehover(const std::map<Key, std::string>& hovers)
			{
				for (auto& e : m_entries)
				{
					auto it = hovers.find(Key(e.second.def.file, e.second.def.line, e.second.def.column));
					if (it != hovers.end())
						e.second.hover = it->second;
				}
			}

			const std::map<Key, IndexEntry>& entries(void) const { return m_entries; }

		private:
			std::map<Key, IndexEntry> m_entries;
	};
}
