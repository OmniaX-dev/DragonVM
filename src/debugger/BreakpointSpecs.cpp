#include "BreakpointSpecs.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <set>

namespace dragon
{
	namespace
	{
		// The path's components, without "", "." and the leading ".." (relative paths compare by their end).
		stdvec<std::string> components(const String& path)
		{
			stdvec<std::string> out;
			std::string part;
			const std::string text = path.cpp_str();
			for (size_t i = 0; i <= text.size(); i++)
			{
				if (i == text.size() || text[i] == '/' || text[i] == '\\')
				{
					if (!part.empty() && part != ".")
					{
						if (part == ".." && !out.empty() && out.back() != "..")
							out.pop_back();
						else
							out.push_back(part);
					}
					part.clear();
				}
				else
					part += text[i];
			}
			while (!out.empty() && out.front() == "..")
				out.erase(out.begin());
			return out;
		}

		bool isIdentifier(const std::string& s)
		{
			if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_'))
				return false;
			for (char c : s)
			{
				if (!(std::isalnum((unsigned char)c) || c == '_'))
					return false;
			}
			return true;
		}

		// A table and whether it is in memory (its addresses count) or a DRX program not loaded.
		struct tTable { const DisassemblyTable* table; bool loaded; };

		stdvec<tTable> allTables(void)
		{
			stdvec<tTable> tables;
			std::set<u64> loadedIds;
			for (const DisassemblyTable* t : DisassemblyLoader::getTables())
			{
				tables.push_back({ t, true });
				if (t->getDrxId() != 0)
					loadedIds.insert(t->getDrxId());
			}
			for (const auto& [id, table] : DisassemblyLoader::getDrxTables())
			{
				if (loadedIds.count(id) == 0)
					tables.push_back({ table.get(), false });
			}
			return tables;
		}

		String programName(const DisassemblyTable& table)
		{
			if (table.getName() != "")
				return table.getName();
			return String(std::filesystem::path(table.getFilePath().cpp_str()).filename().string());
		}

		void addPending(BreakSpec::tResult& result, const DisassemblyTable& table)
		{
			const String name = programName(table);
			if (std::find(result.pending.begin(), result.pending.end(), name) == result.pending.end())
				result.pending.push_back(name);
		}

		// The table's source files that are one of `paths`.
		std::set<i32> localFiles(const DisassemblyTable& table, const stdvec<String>& paths)
		{
			std::set<i32> files;
			const auto& sources = table.getSources();
			for (i32 i = 0; i < cast<i32>(sources.size()); i++)
			{
				for (const auto& p : paths)
				{
					if (sources[i].path == p || BreakSpec::samePath(sources[i].path, p))
					{
						files.insert(i);
						break;
					}
				}
			}
			return files;
		}

		// The first line in [from, to] with code in those files (inlined copies only: inlinedOnly);
		// max() if none.
		i32 firstLineWithCode(const DisassemblyTable& table, const std::set<i32>& files, i32 from, i32 to, bool inlinedOnly)
		{
			i32 line = std::numeric_limits<i32>::max();
			for (const auto& c : table.getCodeTable())
			{
				if (files.count(c.srcFile) && c.srcLine >= from && c.srcLine <= to && c.srcLine < line && (!inlinedOnly || c.inlineDepth > 0))
					line = c.srcLine;
			}
			return line;
		}

		// Where the line starts in the table (each copy: inlined subs).
		void lineAddresses(const DisassemblyTable& table, const std::set<i32>& files, i32 line, bool inlinedOnly, std::set<u32>& out)
		{
			bool any = false;
			for (const auto& c : table.getCodeTable())
			{
				if (files.count(c.srcFile) && c.srcLine == line && c.statement && (!inlinedOnly || c.inlineDepth > 0))
				{
					out.insert(c.addr);
					any = true;
				}
			}
			if (any)
				return;
			for (const auto& c : table.getCodeTable())
			{
				if (files.count(c.srcFile) && c.srcLine == line && (!inlinedOnly || c.inlineDepth > 0))
				{
					out.insert(c.addr);
					return;
				}
			}
		}
	}

	BreakSpec::eKind BreakSpec::kind(const String& spec)
	{
		const std::string s = spec.new_trim().cpp_str();
		if (s.empty() || s[0] == '$')
			return eKind::None;
		const size_t colon = s.rfind(':');
		if (colon != std::string::npos && colon > 0 && colon + 1 < s.size() && s[colon - 1] != ':')
		{
			const std::string number = s.substr(colon + 1);
			if (std::all_of(number.begin(), number.end(), [](unsigned char c) { return std::isdigit(c); }))
				return eKind::SourceLine;
		}
		size_t start = 0;
		while (true)
		{
			const size_t sep = s.find("::", start);
			if (!isIdentifier(s.substr(start, sep == std::string::npos ? std::string::npos : sep - start)))
				return eKind::None;
			if (sep == std::string::npos)
				return eKind::Sub;
			start = sep + 2;
		}
	}

	bool BreakSpec::pathMatches(const String& path, const String& pattern)
	{
		const auto p = components(pattern);
		const auto q = components(path);
		if (p.empty() || p.size() > q.size())
			return false;
		for (size_t k = 0; k < p.size(); k++)
		{
			const std::string& want = p[p.size() - 1 - k];
			const std::string& have = q[q.size() - 1 - k];
			if (want != have && !(k == 0 && have == want + ".ds"))
				return false;
		}
		return true;
	}

	bool BreakSpec::samePath(const String& a, const String& b)
	{
		if (a == b)
			return true;
		const auto x = components(a);
		const auto y = components(b);
		if (x.empty() || y.empty())
			return false;
		const bool absA = a.cpp_str().rfind("/", 0) == 0, absB = b.cpp_str().rfind("/", 0) == 0;
		if (absA && absB)
			return x == y;
		const auto& shorter = (x.size() <= y.size()) ? x : y;
		const auto& longer = (x.size() <= y.size()) ? y : x;
		// (A relative path of a single name is too little to say.)
		if (shorter.size() < 2)
			return false;
		return std::equal(shorter.rbegin(), shorter.rend(), longer.rbegin());
	}

	stdvec<String> BreakSpec::shortestDistinct(const stdvec<String>& paths)
	{
		stdvec<String> out;
		for (size_t i = 0; i < paths.size(); i++)
		{
			const auto mine = components(paths[i]);
			size_t k = 1;
			for (; k < mine.size(); k++)
			{
				bool unique = true;
				for (size_t j = 0; j < paths.size() && unique; j++)
				{
					if (j == i)
						continue;
					const auto other = components(paths[j]);
					unique = !(other.size() >= k && std::equal(mine.end() - (std::ptrdiff_t)k, mine.end(), other.end() - (std::ptrdiff_t)k));
				}
				if (unique)
					break;
			}
			std::string s;
			for (size_t c = mine.size() - std::min(k, mine.size()); c < mine.size(); c++)
				s += (s.empty() ? "" : "/") + mine[c];
			out.push_back(String(s));
		}
		return out;
	}

	BreakSpec::tResult BreakSpec::resolve(const String& specText)
	{
		tResult result;
		const String spec = specText.new_trim();
		const eKind k = kind(spec);
		const stdvec<tTable> tables = allTables();
		std::set<u32> addresses;
		auto l_finish = [&](void) -> tResult {
			result.addresses.assign(addresses.begin(), addresses.end());
			result.valid = !result.addresses.empty() || !result.pending.empty();
			return result;
		};
		if (k == eKind::SourceLine)
		{
			const String pattern = spec.new_substr(0, spec.lastIndexOf(":")).trim();
			const i32 wanted = cast<i32>(spec.new_substr(spec.lastIndexOf(":") + 1).toInt());
			// The files it can be: the same file written two ways counts once.
			stdvec<stdvec<String>> groups;
			for (const auto& t : tables)
			{
				for (const auto& f : t.table->getSources())
				{
					if (!pathMatches(f.path, pattern))
						continue;
					bool placed = false;
					for (auto& g : groups)
					{
						if (std::any_of(g.begin(), g.end(), [&](const String& p) { return samePath(p, f.path); }))
						{
							if (std::find(g.begin(), g.end(), f.path) == g.end())
								g.push_back(f.path);
							placed = true;
							break;
						}
					}
					if (!placed)
						groups.push_back({ f.path });
				}
			}
			if (groups.empty())
			{
				result.error = String("no source file ").add(pattern).add(" in the programs' disassembly");
				return result;
			}
			if (groups.size() > 1)
			{
				stdvec<String> longest;
				for (const auto& g : groups)
					longest.push_back(*std::max_element(g.begin(), g.end(), [](const String& a, const String& b) { return a.len() < b.len(); }));
				const stdvec<String> names = shortestDistinct(longest);
				result.error = String(pattern).add(" is more than one file: ");
				for (size_t i = 0; i < names.size(); i++)
					result.error.add(i == 0 ? "" : ", ").add(names[i]);
				result.error.add(" (write more of its path)");
				return result;
			}
			const stdvec<String>& paths = groups.front();
			i32 line = std::numeric_limits<i32>::max();
			for (const auto& t : tables)
				line = std::min(line, firstLineWithCode(*t.table, localFiles(*t.table, paths), wanted, std::numeric_limits<i32>::max(), false));
			if (line == std::numeric_limits<i32>::max())
			{
				result.error = String("no code at or after line ").add(wanted).add(" of ").add(pattern);
				return result;
			}
			result.line = line;
			result.where = String(pattern).add(":").add(line);
			for (const auto& t : tables)
			{
				const std::set<i32> files = localFiles(*t.table, paths);
				if (files.empty() || firstLineWithCode(*t.table, files, line, line, false) != line)
					continue;
				if (t.loaded)
					lineAddresses(*t.table, files, line, false, addresses);
				else
					addPending(result, *t.table);
			}
			return l_finish();
		}
		if (k == eKind::Sub)
		{
			const String suffix = String("::").add(spec);
			std::set<std::string> names;
			for (const auto& t : tables)
			{
				for (const auto& sub : t.table->getSubs())
				{
					if (sub.name == spec || sub.name.endsWith(suffix))
						names.insert(sub.name.cpp_str());
				}
			}
			if (names.empty())
			{
				result.error = String("no sub named ").add(spec).add(" in the programs' disassembly");
				return result;
			}
			if (names.count(spec.cpp_str()) > 0)
				names = { spec.cpp_str() };
			if (names.size() > 1)
			{
				result.error = String(spec).add(" is more than one sub: ");
				bool first = true;
				for (const auto& n : names)
				{
					result.error.add(first ? "" : ", ").add(n);
					first = false;
				}
				return result;
			}
			const String name = *names.begin();
			result.where = name;
			std::set<std::string> overloads;
			for (const auto& t : tables)
			{
				bool here = false;
				for (const auto& sub : t.table->getSubs())
				{
					if (sub.name != name)
						continue;
					overloads.insert(sub.params.cpp_str());
					std::set<u32> found;
					if (sub.hasAddress)
						found.insert(sub.address);
					if (sub.isInline)
					{
						// Its copies: the first line of its body with inlined code (in its file,
						// or a file of that name: an inline sub of a library is copied from the
						// library's sources, which may be written another way).
						std::set<i32> files = localFiles(*t.table, { sub.path });
						i32 line = firstLineWithCode(*t.table, files, sub.line, sub.lastLine, true);
						if (line == std::numeric_limits<i32>::max())
						{
							const String fileName = String(std::filesystem::path(sub.path.cpp_str()).filename().string());
							files.clear();
							for (i32 i = 0; i < cast<i32>(t.table->getSources().size()); i++)
							{
								if (t.table->getSources()[i].name == fileName)
									files.insert(i);
							}
							line = firstLineWithCode(*t.table, files, sub.line, sub.lastLine, true);
						}
						if (line != std::numeric_limits<i32>::max())
							lineAddresses(*t.table, files, line, true, found);
					}
					if (found.empty())
						continue;
					here = true;
					if (t.loaded)
						addresses.insert(found.begin(), found.end());
				}
				if (here && !t.loaded)
					addPending(result, *t.table);
			}
			if (overloads.size() > 1)
				result.where.add("  (").add(cast<i32>(overloads.size())).add(" overloads)");
			if (addresses.empty() && result.pending.empty())
			{
				result.error = String(name).add(" has no code in the programs (an inline sub never used?)");
				return result;
			}
			return l_finish();
		}
		result.error = "expected <file>:<line> or a sub name";
		return result;
	}
}
