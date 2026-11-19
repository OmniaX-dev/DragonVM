#pragma once

// The built-ins of compile-time conditions (a template's where, if constexpr): is_integer(T),
// is_one_of(T, u8, u32), has_method(T, push), ... The parser needs to know which arguments are
// types (parsed as types: T*, Array<u32>) and which are names; the code generator evaluates them
// (its table, in CodeGen.cpp, adds what each one computes: one entry per built-in).

#include <string>

namespace dscript
{
	struct ConditionBuiltin
	{
		const char* name;
		// One letter per argument: T a type, N a name (an identifier or a string); a final '*'
		// repeats the letter before it (one or more).
		const char* args;
		const char* help;
	};

	// nullptr: not a built-in.
	const ConditionBuiltin* findConditionBuiltin(const std::string& name);
}
