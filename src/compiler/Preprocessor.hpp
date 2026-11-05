#pragma once

#include "Lexer.hpp"
#include "Index.hpp"
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

			// For the language server: the text of files open in the editor (by canonical path),
			// used instead of what is on the disk; the #includes go in the index.
			static void setOverlay(const std::map<std::string, std::string>* files);
			void setIndex(Index* index) { m_index = index; }
			// Every file read (canonical paths).
			const std::set<std::string>& files(void) const { return m_files; }
			inline bool isDefined(const std::string& name) const { return m_defines.count(name) > 0; }

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
			std::set<std::string> m_files;
			Index* m_index { nullptr };
	};
}
