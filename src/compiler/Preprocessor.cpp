#include "Preprocessor.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cctype>

namespace dscript
{
	static const std::map<std::string, std::string>* s_overlay = nullptr;

	void Preprocessor::setOverlay(const std::map<std::string, std::string>* files)
	{
		s_overlay = files;
	}

	static bool readFile(const std::string& path, std::string& out)
	{
		if (s_overlay != nullptr)
		{
			std::error_code ec;
			auto it = s_overlay->find(std::filesystem::weakly_canonical(path, ec).string());
			if (it != s_overlay->end())
			{
				out = it->second;
				return true;
			}
		}
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		std::stringstream ss;
		ss << file.rdbuf();
		out = ss.str();
		return true;
	}

	static std::string trim(const std::string& s)
	{
		size_t a = 0, b = s.size();
		while (a < b && std::isspace((unsigned char)s[a])) a++;
		while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
		return s.substr(a, b - a);
	}

	static bool isIdentifier(const std::string& s)
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

	Preprocessor::Preprocessor(const std::vector<std::string>& includeDirs) : m_includeDirs(includeDirs)
	{
	}

	void Preprocessor::define(const std::string& name, const std::string& value)
	{
		Lexer lexer(value, "<command line>");
		std::vector<Token> tokens = lexer.tokenize();
		tokens.pop_back();	// End
		m_defines[name] = tokens;
	}

	std::vector<Token> Preprocessor::processFile(const std::string& path)
	{
		std::string source;
		if (!readFile(path, source))
			error({ path, 0, 0 }, "cannot open file");
		std::string canonical = std::filesystem::weakly_canonical(path).string();
		m_files.insert(canonical);
		m_includeStack.push_back(canonical);
		Lexer lexer(source, path);
		std::vector<Token> tokens = lexer.tokenize();
		std::vector<Token> out;
		std::string directory = std::filesystem::path(path).parent_path().string();
		processTokens(tokens, directory, out);
		m_includeStack.pop_back();
		if (m_includeStack.empty())
			out.push_back(tokens.back());	// End
		return out;
	}

	void Preprocessor::processTokens(const std::vector<Token>& tokens, const std::string& directory, std::vector<Token>& out)
	{
		std::vector<Conditional> stack;
		for (const Token& tok : tokens)
		{
			if (tok.kind == TokenKind::End)
				break;
			if (tok.kind == TokenKind::Directive)
			{
				directive(tok, directory, out, stack);
				continue;
			}
			if (!stack.empty() && !stack.back().active)
				continue;
			expand(tok, out, 0);
		}
		if (!stack.empty())
			error(stack.back().pos, "#if without #endif");
	}

	void Preprocessor::expand(const Token& tok, std::vector<Token>& out, int depth)
	{
		if (tok.kind == TokenKind::Identifier && m_defines.count(tok.text) > 0)
		{
			if (depth > 32)
				error(tok.pos, "#define expansion too deep (recursive definition?): " + tok.text);
			for (Token t : m_defines[tok.text])
			{
				t.pos = tok.pos;	// errors point at the use
				expand(t, out, depth + 1);
			}
			return;
		}
		out.push_back(tok);
	}

	std::string Preprocessor::argument(const Token& tok, const std::string& rest, const std::string& name)
	{
		std::string r = trim(rest);
		if (r.size() < 2 || r.front() != '(' || r.back() != ')')
			error(tok.pos, "#" + name + " needs its argument in parentheses: #" + name + "(...)");
		return trim(r.substr(1, r.size() - 2));
	}

	std::string Preprocessor::resolveInclude(const Token& tok, std::string path, const std::string& directory)
	{
		bool systemOnly = false;
		if (path.size() >= 2 && path.front() == '<' && path.back() == '>')
		{
			systemOnly = true;
			path = trim(path.substr(1, path.size() - 2));
		}
		else if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
			path = path.substr(1, path.size() - 2);
		if (!systemOnly)
		{
			std::filesystem::path p = std::filesystem::path(directory) / path;
			if (std::filesystem::exists(p))
				return p.string();
		}
		for (const auto& dir : m_includeDirs)
		{
			std::filesystem::path p = std::filesystem::path(dir) / path;
			if (std::filesystem::exists(p))
				return p.string();
		}
		error(tok.pos, "#include: file not found: " + path);
	}

	void Preprocessor::directive(const Token& tok, const std::string& directory, std::vector<Token>& out, std::vector<Conditional>& stack)
	{
		std::string text = trim(tok.text);
		size_t n = 0;
		while (n < text.size() && (std::isalnum((unsigned char)text[n]) || text[n] == '_'))
			n++;
		const std::string name = text.substr(0, n);
		const std::string rest = text.substr(n);
		const bool active = stack.empty() || stack.back().active;

		if (name == "if" || name == "ifdef" || name == "ifndef")
		{
			Conditional c { active, false, false, false, tok.pos };
			if (active)
			{
				const std::string arg = argument(tok, rest, name);
				bool value = false;
				if (name == "if")
					value = evaluate(tok, arg) != 0;
				else
					value = (m_defines.count(arg) > 0) == (name == "ifdef");
				c.active = value;
				c.taken = value;
			}
			stack.push_back(c);
			return;
		}
		if (name == "elif")
		{
			if (stack.empty() || stack.back().seenElse)
				error(tok.pos, "#elif without #if");
			Conditional& c = stack.back();
			if (c.parentActive && !c.taken && evaluate(tok, argument(tok, rest, name)) != 0)
			{
				c.active = true;
				c.taken = true;
			}
			else
				c.active = false;
			return;
		}
		if (name == "else")
		{
			if (stack.empty() || stack.back().seenElse)
				error(tok.pos, "#else without #if");
			Conditional& c = stack.back();
			c.seenElse = true;
			c.active = c.parentActive && !c.taken;
			c.taken = true;
			return;
		}
		if (name == "endif")
		{
			if (stack.empty())
				error(tok.pos, "#endif without #if");
			stack.pop_back();
			return;
		}
		if (!active)
			return;

		if (name == "define")
		{
			std::string arg = argument(tok, rest, name);
			std::string defName = arg, value = "";
			size_t eq = arg.find('=');
			if (eq != std::string::npos)
			{
				defName = trim(arg.substr(0, eq));
				value = trim(arg.substr(eq + 1));
			}
			if (!isIdentifier(defName))
				error(tok.pos, "invalid #define name: " + defName);
			if (Lexer::isKeyword(defName))
				error(tok.pos, "cannot #define a keyword: " + defName);
			Lexer lexer(value, tok.pos.file, tok.pos.line);
			std::vector<Token> tokens = lexer.tokenize();
			tokens.pop_back();
			m_defines[defName] = tokens;
		}
		else if (name == "undef")
			m_defines.erase(argument(tok, rest, name));
		else if (name == "include")
		{
			std::string path = resolveInclude(tok, argument(tok, rest, name), directory);
			if (m_index != nullptr)
			{
				const std::string target = std::filesystem::weakly_canonical(path).string();
				m_index->add(tok.pos, (int)tok.text.size() + 1, { target, 1, 1 }, "#include " + target);
			}
			std::string canonical = std::filesystem::weakly_canonical(path).string();
			if (m_onceFiles.count(canonical) > 0)
				return;
			for (const auto& f : m_includeStack)
			{
				if (f == canonical)
					error(tok.pos, "recursive #include: " + path);
			}
			if (m_includeStack.size() > 64)
				error(tok.pos, "#include nesting too deep");
			std::vector<Token> included = processFile(path);
			out.insert(out.end(), included.begin(), included.end());
		}
		else if (name == "once")
			m_onceFiles.insert(m_includeStack.back());
		else if (name == "error")
			error(tok.pos, "#error: " + argument(tok, rest, name));
		else if (name == "warning")
			warning(tok.pos, "#warning: " + argument(tok, rest, name));
		else
			error(tok.pos, "unknown directive: #" + name);
	}

	// ===== #if expressions: integers, defined(NAME), C operators and and/or/not =====
	namespace
	{
		struct ExprReader
		{
			std::vector<Token> toks;
			size_t i { 0 };
			const Token& peek(void) { return toks[i]; }
			bool is(const std::string& p) { return (toks[i].kind == TokenKind::Punct || toks[i].kind == TokenKind::Keyword) && toks[i].text == p; }
			bool accept(const std::string& p) { if (is(p)) { i++; return true; } return false; }
		};

		int64_t parseTernary(ExprReader& r);

		int64_t parsePrimary(ExprReader& r)
		{
			const Token& t = r.peek();
			if (r.accept("("))
			{
				int64_t v = parseTernary(r);
				if (!r.accept(")"))
					error(t.pos, "#if: ')' expected");
				return v;
			}
			if (t.kind == TokenKind::Integer) { r.i++; return t.value; }
			if (t.kind == TokenKind::Keyword && (t.text == "true" || t.text == "false")) { r.i++; return t.text == "true"; }
			if (t.kind == TokenKind::Identifier) { r.i++; return 0; }	// undefined name
			error(t.pos, "#if: invalid expression");
		}

		int64_t parseUnary(ExprReader& r)
		{
			if (r.accept("-")) return -parseUnary(r);
			if (r.accept("+")) return parseUnary(r);
			if (r.accept("!") || r.accept("not")) return !parseUnary(r);
			if (r.accept("~")) return ~parseUnary(r);
			return parsePrimary(r);
		}

		int precedence(ExprReader& r)
		{
			const Token& t = r.peek();
			if (t.kind != TokenKind::Punct && t.kind != TokenKind::Keyword)
				return -1;
			const std::string& p = t.text;
			if (p == "||" || p == "or") return 1;
			if (p == "&&" || p == "and") return 2;
			if (p == "|") return 3;
			if (p == "^") return 4;
			if (p == "&") return 5;
			if (p == "==" || p == "!=") return 6;
			if (p == "<" || p == "<=" || p == ">" || p == ">=") return 7;
			if (p == "<<" || p == ">>") return 8;
			if (p == "+" || p == "-") return 9;
			if (p == "*" || p == "/" || p == "%") return 10;
			return -1;
		}

		int64_t parseBinary(ExprReader& r, int minPrec)
		{
			int64_t left = parseUnary(r);
			while (true)
			{
				int prec = precedence(r);
				if (prec < minPrec)
					return left;
				Token op = r.peek();
				r.i++;
				int64_t right = parseBinary(r, prec + 1);
				const std::string& p = op.text;
				if ((p == "/" || p == "%") && right == 0)
					error(op.pos, "#if: division by zero");
				if (p == "||" || p == "or") left = left || right;
				else if (p == "&&" || p == "and") left = left && right;
				else if (p == "|") left |= right;
				else if (p == "^") left ^= right;
				else if (p == "&") left &= right;
				else if (p == "==") left = left == right;
				else if (p == "!=") left = left != right;
				else if (p == "<") left = left < right;
				else if (p == "<=") left = left <= right;
				else if (p == ">") left = left > right;
				else if (p == ">=") left = left >= right;
				else if (p == "<<") left <<= (right & 63);
				else if (p == ">>") left >>= (right & 63);
				else if (p == "+") left += right;
				else if (p == "-") left -= right;
				else if (p == "*") left *= right;
				else if (p == "/") left /= right;
				else if (p == "%") left %= right;
			}
		}

		int64_t parseTernary(ExprReader& r)
		{
			int64_t c = parseBinary(r, 1);
			if (r.accept("?"))
			{
				int64_t a = parseTernary(r);
				if (!r.accept(":"))
					error(r.peek().pos, "#if: ':' expected");
				int64_t b = parseTernary(r);
				return c ? a : b;
			}
			return c;
		}
	}

	int64_t Preprocessor::evaluate(const Token& tok, const std::string& text)
	{
		Lexer lexer(text, tok.pos.file, tok.pos.line);
		std::vector<Token> raw = lexer.tokenize();
		ExprReader r;
		// defined(NAME) / defined NAME first, then the defines are expanded.
		for (size_t i = 0; i < raw.size(); i++)
		{
			const Token& t = raw[i];
			if (t.kind == TokenKind::Identifier && t.text == "defined")
			{
				std::string defName;
				if (i + 3 < raw.size() && raw[i + 1].text == "(" && raw[i + 3].text == ")")
				{
					defName = raw[i + 2].text;
					i += 3;
				}
				else if (i + 1 < raw.size() && raw[i + 1].kind == TokenKind::Identifier)
				{
					defName = raw[i + 1].text;
					i += 1;
				}
				else
					error(t.pos, "#if: defined(NAME) expected");
				Token v = t;
				v.kind = TokenKind::Integer;
				v.value = m_defines.count(defName) > 0;
				r.toks.push_back(v);
				continue;
			}
			if (t.kind == TokenKind::End)
			{
				r.toks.push_back(t);
				break;
			}
			expand(t, r.toks, 0);
		}
		int64_t value = parseTernary(r);
		if (r.peek().kind != TokenKind::End)
			error(r.peek().pos, "#if: unexpected '" + r.peek().text + "'");
		return value;
	}
}
