#pragma once

#include "Lexer.hpp"
#include <map>
#include <set>

namespace dscript
{
	// Runs the directives on the tokens of a file (and of the files it includes):
	//   #include(path)            path relative to the including file, then the include dirs
	//   #include(<path>)          the include dirs only
	//   #define(NAME = tokens)    #define(NAME)    #undef(NAME)
	//   #if(expr) #ifdef(NAME) #ifndef(NAME) #elif(expr) #else #endif
	//   #once                     the file is included only once
	//   #error(message) #warning(message)
	// Defined names are replaced by their tokens (recursively) everywhere else. In #if
	// expressions, defined(NAME) tests a name; undefined names are 0.
	class Preprocessor
	{
		public:
			Preprocessor(const std::vector<std::string>& includeDirs);
			void define(const std::string& name, const std::string& value);
			std::vector<Token> processFile(const std::string& path);

		private:
			struct Conditional
			{
				bool parentActive;
				bool active;		// this branch is being emitted
				bool taken;			// a branch of this #if was already taken
				bool seenElse;
				SourcePos pos;
			};

		private:
			void processTokens(const std::vector<Token>& tokens, const std::string& directory, std::vector<Token>& out);
			void directive(const Token& tok, const std::string& directory, std::vector<Token>& out, std::vector<Conditional>& stack);
			std::string resolveInclude(const Token& tok, std::string path, const std::string& directory);
			void expand(const Token& tok, std::vector<Token>& out, int depth);
			int64_t evaluate(const Token& tok, const std::string& text);
			std::string argument(const Token& tok, const std::string& rest, const std::string& name);

		private:
			std::vector<std::string> m_includeDirs;
			std::map<std::string, std::vector<Token>> m_defines;
			std::set<std::string> m_onceFiles;
			std::vector<std::string> m_includeStack;
	};
}
