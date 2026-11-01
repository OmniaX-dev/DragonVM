#pragma once

#include "Common.hpp"

namespace dscript
{
	enum class TokenKind
	{
		End = 0,
		Identifier,
		Keyword,
		Integer,		// value in Token::value (char literals too)
		String,			// decoded text in Token::text
		Punct,			// operators and separators, text in Token::text
		Directive,		// a whole "#..." line, raw text after '#' in Token::text
		LowBlock		// the raw text of a low { ... } block (without the braces)
	};

	struct Token
	{
		TokenKind kind { TokenKind::End };
		std::string text;
		int64_t value { 0 };
		SourcePos pos;
		bool isChar { false };		// Integer written as a character literal
	};

	// Splits a source text into tokens. Comments are dropped. Directive lines (a '#' as the
	// first non-blank character of a line) become one Directive token; the body of a
	// low { ... } block becomes one LowBlock token.
	class Lexer
	{
		public:
			Lexer(const std::string& source, const std::string& fileName, int firstLine = 1);
			std::vector<Token> tokenize(void);

			static bool isKeyword(const std::string& word);

		private:
			char peek(int offset = 0) const;
			char advance(void);
			bool atLineStart(void) const;
			void skipBlanksAndComments(void);
			Token makeToken(TokenKind kind) const;
			Token lexNumber(void);
			Token lexString(void);
			Token lexChar(void);
			int lexEscape(void);
			Token lexDirective(void);
			Token lexLowBlock(void);
			Token lexPunct(void);

		private:
			std::string m_source;
			std::string m_file;
			size_t m_pos { 0 };
			int m_line { 1 };
			int m_column { 1 };
			int m_tokLine { 1 };
			int m_tokColumn { 1 };
			bool m_lastWasLow { false };
	};
}
