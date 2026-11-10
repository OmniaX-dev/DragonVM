#include "Lexer.hpp"
#include <cctype>
#include <iostream>

namespace dscript
{
	std::string posString(const SourcePos& pos)
	{
		return pos.file + ":" + std::to_string(pos.line) + ":" + std::to_string(pos.column);
	}

	void error(const SourcePos& pos, const std::string& message)
	{
		throw CompileError { pos, message };
	}

	static WarningHandler s_warningHandler = nullptr;

	void setWarningHandler(WarningHandler handler)
	{
		s_warningHandler = handler;
	}

	void warning(const SourcePos& pos, const std::string& message)
	{
		if (s_warningHandler != nullptr)
		{
			s_warningHandler(pos, message);
			return;
		}
		std::cerr << posString(pos) << ": warning: " << message << "\n";
	}

	static const char* s_keywords[] = {
		"var", "const", "sub", "struct", "enum", "alias", "return", "if", "else", "while", "do", "for",
		"break", "continue", "null", "true", "false", "cast", "sizeof", "low", "interrupt", "inline", "static", "priv",
		"assert", "debug_print", "namespace", "defer", "atomic", "and", "or", "not", "void", nullptr
	};

	bool Lexer::isKeyword(const std::string& word)
	{
		for (int i = 0; s_keywords[i] != nullptr; i++)
		{
			if (word == s_keywords[i])
				return true;
		}
		return false;
	}

	Lexer::Lexer(const std::string& source, const std::string& fileName, int firstLine)
		: m_source(source), m_file(fileName), m_line(firstLine)
	{
	}

	char Lexer::peek(int offset) const
	{
		if (m_pos + offset >= m_source.size())
			return '\0';
		return m_source[m_pos + offset];
	}

	char Lexer::advance(void)
	{
		char c = peek();
		m_pos++;
		if (c == '\n')
		{
			m_line++;
			m_column = 1;
		}
		else
			m_column++;
		return c;
	}

	bool Lexer::atLineStart(void) const
	{
		// Only blanks between the start of the line and the current position.
		size_t i = m_pos;
		while (i > 0)
		{
			char c = m_source[i - 1];
			if (c == '\n')
				return true;
			if (c != ' ' && c != '\t' && c != '\r')
				return false;
			i--;
		}
		return true;
	}

	Token Lexer::makeToken(TokenKind kind) const
	{
		Token t;
		t.kind = kind;
		t.pos = { m_file, m_tokLine, m_tokColumn };
		return t;
	}

	void Lexer::skipBlanksAndComments(void)
	{
		while (true)
		{
			char c = peek();
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
				advance();
			else if (c == '/' && peek(1) == '/')
			{
				while (peek() != '\n' && peek() != '\0')
					advance();
			}
			else if (c == '/' && peek(1) == '*')
			{
				SourcePos start { m_file, m_line, m_column };
				advance();
				advance();
				while (!(peek() == '*' && peek(1) == '/'))
				{
					if (peek() == '\0')
						error(start, "unterminated comment");
					advance();
				}
				advance();
				advance();
			}
			else
				return;
		}
	}

	std::vector<Token> Lexer::tokenize(void)
	{
		std::vector<Token> tokens;
		while (true)
		{
			skipBlanksAndComments();
			m_tokLine = m_line;
			m_tokColumn = m_column;
			char c = peek();
			if (c == '\0')
				break;
			if (c == '#' && atLineStart())
			{
				tokens.push_back(lexDirective());
				m_lastWasLow = false;
				continue;
			}
			if (m_lastWasLow && c == '{')
			{
				tokens.push_back(lexLowBlock());
				m_lastWasLow = false;
				continue;
			}
			m_lastWasLow = false;
			if (std::isalpha((unsigned char)c) || c == '_')
			{
				Token t = makeToken(TokenKind::Identifier);
				while (std::isalnum((unsigned char)peek()) || peek() == '_')
					t.text += advance();
				if (isKeyword(t.text))
					t.kind = TokenKind::Keyword;
				m_lastWasLow = (t.text == "low");
				tokens.push_back(t);
			}
			else if (std::isdigit((unsigned char)c))
				tokens.push_back(lexNumber());
			else if (c == '"')
				tokens.push_back(lexString());
			else if (c == '\'')
				tokens.push_back(lexChar());
			else
				tokens.push_back(lexPunct());
		}
		Token end = makeToken(TokenKind::End);
		end.pos = { m_file, m_line, m_column };
		tokens.push_back(end);
		return tokens;
	}

	Token Lexer::lexNumber(void)
	{
		Token t = makeToken(TokenKind::Integer);
		uint64_t value = 0;
		int base = 10;
		if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X'))
		{
			base = 16;
			advance();
			advance();
		}
		else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B'))
		{
			base = 2;
			advance();
			advance();
		}
		int digits = 0;
		while (true)
		{
			char c = peek();
			int d = -1;
			if (c == '_' && digits > 0) { advance(); continue; }	// 0x0000_FFFF
			if (std::isdigit((unsigned char)c)) d = c - '0';
			else if (base == 16 && std::isxdigit((unsigned char)c)) d = 10 + (std::tolower((unsigned char)c) - 'a');
			if (d < 0 || d >= base)
				break;
			value = value * base + d;
			if (value > 0xFFFFFFFFull)
				error(t.pos, "integer literal too large (32 bits at most)");
			digits++;
			t.text += advance();
		}
		if (digits == 0)
			error(t.pos, "invalid integer literal");
		if (std::isalnum((unsigned char)peek()) || peek() == '.')
			error(t.pos, peek() == '.' ? "floating point numbers are not supported (the machine has no FPU)" : "invalid character in integer literal");
		t.value = (int64_t)value;
		return t;
	}

	int Lexer::lexEscape(void)
	{
		SourcePos pos { m_file, m_line, m_column };
		char c = advance();
		switch (c)
		{
			case 'n': return '\n';
			case 't': return '\t';
			case 'r': return '\r';
			case '0': return 0;
			case 'a': return 7;
			case 'b': return 8;
			case 'e': return 27;
			case '\\': return '\\';
			case '\'': return '\'';
			case '"': return '"';
			case 'x':
			{
				int value = 0;
				for (int i = 0; i < 2; i++)
				{
					char h = advance();
					if (!std::isxdigit((unsigned char)h))
						error(pos, "invalid \\x escape (two hex digits needed)");
					value = value * 16 + (std::isdigit((unsigned char)h) ? h - '0' : 10 + std::tolower((unsigned char)h) - 'a');
				}
				return value;
			}
			default:
				error(pos, std::string("unknown escape sequence: \\") + c);
		}
	}

	Token Lexer::lexString(void)
	{
		Token t = makeToken(TokenKind::String);
		advance();
		while (peek() != '"')
		{
			if (peek() == '\0' || peek() == '\n')
				error(t.pos, "unterminated string literal");
			if (peek() == '\\')
			{
				advance();
				t.text += (char)lexEscape();
			}
			else
				t.text += advance();
		}
		advance();
		return t;
	}

	Token Lexer::lexChar(void)
	{
		Token t = makeToken(TokenKind::Integer);
		t.isChar = true;
		advance();
		if (peek() == '\\')
		{
			advance();
			t.value = lexEscape();
		}
		else if (peek() == '\'' || peek() == '\0' || peek() == '\n')
			error(t.pos, "empty character literal");
		else
			t.value = (unsigned char)advance();
		if (peek() != '\'')
			error(t.pos, "character literal with more than one character");
		advance();
		return t;
	}

	Token Lexer::lexDirective(void)
	{
		Token t = makeToken(TokenKind::Directive);
		advance();	// '#'
		while (peek() != '\n' && peek() != '\0')
		{
			// A comment ends the directive.
			if (peek() == '/' && (peek(1) == '/' || peek(1) == '*'))
				break;
			t.text += advance();
		}
		while (!t.text.empty() && (t.text.back() == ' ' || t.text.back() == '\t' || t.text.back() == '\r'))
			t.text.pop_back();
		return t;
	}

	Token Lexer::lexLowBlock(void)
	{
		Token t = makeToken(TokenKind::LowBlock);
		advance();	// '{'
		t.pos.line = m_line;
		int depth = 1;
		while (true)
		{
			char c = peek();
			if (c == '\0')
				error(t.pos, "unterminated low block");
			if (c == '/' && peek(1) == '/')
			{
				while (peek() != '\n' && peek() != '\0')
					advance();
				continue;
			}
			if (c == '#' && peek(1) == '#')
			{
				// dasm comment
				while (peek() != '\n' && peek() != '\0')
					advance();
				continue;
			}
			if (c == '{') depth++;
			if (c == '}')
			{
				depth--;
				if (depth == 0)
				{
					advance();
					break;
				}
			}
			t.text += advance();
		}
		return t;
	}

	Token Lexer::lexPunct(void)
	{
		Token t = makeToken(TokenKind::Punct);
		static const char* puncts[] = {
			"<<=", ">>=", "->", "::", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
			"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
			"+", "-", "*", "/", "%", "&", "|", "^", "~", "!", "<", ">", "=", "(", ")", "{", "}",
			"[", "]", ";", ":", ",", ".", "?", nullptr
		};
		for (int i = 0; puncts[i] != nullptr; i++)
		{
			const std::string p = puncts[i];
			if (m_source.compare(m_pos, p.size(), p) == 0)
			{
				for (size_t k = 0; k < p.size(); k++)
					advance();
				t.text = p;
				return t;
			}
		}
		error(t.pos, std::string("unexpected character: '") + peek() + "'");
	}
}
