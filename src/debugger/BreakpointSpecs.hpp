#pragma once

#include "DisassemblyLoader.hpp"

namespace dragon
{
	// Breakpoints written as a source line ("drake/entry.ds:37", "entry.ds:37") or a sub
	// ("dr::fs::FileSystem::info", "FileSystem::info", "info"), found in the programs'
	// disassembly. The loaded programs (the fixed ones and the DRX programs loaded so far) give
	// the addresses; the DRX programs not loaded (yet) only tell that it is there: a pending
	// breakpoint, which gets its addresses when one of them is loaded (the debugger resolves the
	// breakpoints again whenever the programs in memory change).
	//
	// Files: the path's last components ("drake/entry.ds", "entry.ds", ".ds" may be left out);
	// several different files matching is an error (with the shortest paths that tell them
	// apart). Subs: the full name or its last parts ("::" separated); several different subs
	// matching is an error, overloads (the same name) all get a breakpoint. A sub in a library is
	// in every program linked with it: each copy gets one (as each copy of an inlined line does).
	class BreakSpec
	{
		public: enum class eKind { None = 0, SourceLine, Sub };
		public: struct tResult
		{
			bool valid { false };
			String error { "" };
			stdvec<u32> addresses;			// in the loaded programs (sorted, each once)
			String where { "" };			// what it resolved to: "drake/entry.ds:37", "dr::fs::FileSystem::info"
			i32 line { 0 };					// source lines: the line used (the next one with code)
			stdvec<String> pending;			// DRX programs not loaded that have it
		};

		public:
			static eKind kind(const String& spec);
			static tResult resolve(const String& spec);
			// The path ends with the pattern's components (the file name also without ".ds").
			static bool pathMatches(const String& path, const String& pattern);
			// The same file written two ways (a relative path the end of the other).
			static bool samePath(const String& a, const String& b);
			// For each path, its shortest end that no other path has.
			static stdvec<String> shortestDistinct(const stdvec<String>& paths);
	};
}
