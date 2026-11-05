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
	//   #define(NAME(a, b) = tokens)   with parameters: NAME(x, y) is replaced by the tokens,
	//                             a and b by the (expanded) x and y
	//   #if(expr) #ifdef(NAME) #ifndef(NAME) #elif(expr) #else #endif
	//   #once                     the file is included only once
	//   #error(message) #warning(message)
	// Defined names are replaced by their tokens (recursively, but never inside their own
	// expansion) everywhere else. A name with parameters is only replaced when it's followed
	// by '('. In #if expressions, defined(NAME) tests a name; undefined names are 0.
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
			struct Define
			{
				std::vector<Token> body;
				std::vector<std::string> params;
				bool function { false };	// has parameters (maybe none: NAME())
				SourcePos pos;				// the #define
				std::string text;			// for the language server's hover
			};
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
			// Replaces the defines in `in`. `usePos`: where the tokens of a define body go (the
			// outermost use; null at the top level). `hidden`: the defines being expanded.
			void expand(const std::vector<Token>& in, std::vector<Token>& out, const SourcePos* usePos, std::vector<std::string>& hidden, int depth);
			void defineFrom(const SourcePos& pos, const std::string& nameSpec, const std::string& value, const std::string& text);
			int64_t evaluate(const Token& tok, const std::string& text);
			std::string argument(const Token& tok, const std::string& rest, const std::string& name);

		private:
			std::vector<std::string> m_includeDirs;
			std::map<std::string, Define> m_defines;
			std::set<std::string> m_onceFiles;
			std::vector<std::string> m_includeStack;
			std::set<std::string> m_files;
			Index* m_index { nullptr };
	};
}
