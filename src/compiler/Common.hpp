#pragma once

// DScript compiler (dsc): common definitions.
//
// The compiler is written in a deliberately plain style (structs, vectors, strings,
// recursive descent, no templates of its own), so that it can one day be ported to
// DScript itself.

#include <string>
#include <vector>
#include <cstdint>

namespace dscript
{
	// A position in a source file (for error messages).
	struct SourcePos
	{
		std::string file;
		int line { 0 };
		int column { 0 };
	};

	// Thrown on the first error; the driver prints it and stops.
	struct CompileError
	{
		SourcePos pos;
		std::string message;
	};

	[[noreturn]] void error(const SourcePos& pos, const std::string& message);
	void warning(const SourcePos& pos, const std::string& message);
	// Where warnings go (default: stderr); nullptr restores the default.
	typedef void (*WarningHandler)(const SourcePos& pos, const std::string& message);
	void setWarningHandler(WarningHandler handler);
	std::string posString(const SourcePos& pos);
}
