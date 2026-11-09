#include "Preprocessor.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cctype>
#include <algorithm>

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

	// Tokens back to dscript text (for the language server's hover of a define's use).
	static std::string renderTokens(const std::vector<Token>& tokens, size_t from)
	{
		auto escape = [](const std::string& text, char quote) {
			std::string r;
			for (char c : text)
			{
				switch (c)
				{
					case '\n': r += "\\n"; break;
					case '\t': r += "\\t"; break;
					case '\r': r += "\\r"; break;
					case '\0': r += "\\0"; break;
					case '\\': r += "\\\\"; break;
					default:
						if (c == quote)
							r += '\\';
						r += c;
				}
			}
			return r;
		};
		auto isOperand = [](const Token& t) {
			return t.kind == TokenKind::Identifier || t.kind == TokenKind::Integer || t.kind == TokenKind::String
				|| (t.kind == TokenKind::Keyword && t.text != "not" && t.text != "and" && t.text != "or")
				|| (t.kind == TokenKind::Punct && (t.text == ")" || t.text == "]"));
		};
		std::string out;
		for (size_t i = from; i < tokens.size(); i++)
		{
			const Token& t = tokens[i];
			std::string text = t.text;
			if (t.kind == TokenKind::Integer)
				text = t.isChar ? "'" + escape(std::string(1, (char)t.value), '\'') + "'" : std::to_string(t.value);
			else if (t.kind == TokenKind::String)
				text = "\"" + escape(t.text, '"') + "\"";
			else if (t.kind == TokenKind::LowBlock)
				text = "{ ... }";
			if (i > from)
			{
				const Token& prev = tokens[i - 1];
				const std::string& pt = prev.text;
				bool space = true;
				if (prev.kind == TokenKind::Punct && (pt == "(" || pt == "[" || pt == "." || pt == "->" || pt == "::" || pt == "!" || pt == "~"))
					space = false;
				else if (t.kind == TokenKind::Punct && (t.text == ")" || t.text == "]" || t.text == "," || t.text == ";" || t.text == "." || t.text == "->" || t.text == "::"))
					space = false;
				else if (t.kind == TokenKind::Punct && (t.text == "(" || t.text == "[") && isOperand(prev))
					space = false;	// a call, an index
				else if (prev.kind == TokenKind::Punct && (pt == "-" || pt == "*" || pt == "&" || pt == "+"))
				{
					// unary: after an operator, '(' ',' or at the start
					const bool unary = (i - 1 == from) || !isOperand(tokens[i - 2]);
					space = !unary;
				}
				if (space)
					out += ' ';
			}
			out += text;
		}
		return out;
	}

	Preprocessor::Preprocessor(const std::vector<std::string>& includeDirs) : m_includeDirs(includeDirs)
	{
	}

	void Preprocessor::define(const std::string& name, const std::string& value)
	{
		defineFrom({ "<command line>", 0, 0 }, name, value, name + (value.empty() ? "" : " = " + value));
	}

	// nameSpec: NAME or NAME(a, b, ...).
	void Preprocessor::defineFrom(const SourcePos& pos, const std::string& nameSpec, const std::string& value, const std::string& text)
	{
		Define def;
		def.pos = (pos.line > 0) ? pos : SourcePos {  };	// nowhere to go for -D
		def.text = "#define(" + text + ")";
		std::string defName = nameSpec;
		const size_t open = nameSpec.find('(');
		if (open != std::string::npos)
		{
			defName = trim(nameSpec.substr(0, open));
			if (nameSpec.back() != ')')
				error(pos, "#define: ')' expected after the parameters of " + defName);
			def.function = true;
			const std::string list = trim(nameSpec.substr(open + 1, nameSpec.size() - open - 2));
			if (!list.empty())
			{
				size_t start = 0;
				while (true)
				{
					const size_t comma = list.find(',', start);
					const std::string param = trim(list.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
					if (!isIdentifier(param) || Lexer::isKeyword(param))
						error(pos, "invalid #define parameter of " + defName + ": '" + param + "'");
					for (const auto& p : def.params)
					{
						if (p == param)
							error(pos, "#define: duplicate parameter '" + param + "' in " + defName);
					}
					def.params.push_back(param);
					if (comma == std::string::npos)
						break;
					start = comma + 1;
				}
			}
		}
		if (!isIdentifier(defName))
			error(pos, "invalid #define name: " + defName);
		if (Lexer::isKeyword(defName))
			error(pos, "cannot #define a keyword: " + defName);
		Lexer lexer(value, pos.file, pos.line > 0 ? pos.line : 1);
		def.body = lexer.tokenize();
		def.body.pop_back();	// End
		for (const Token& t : def.body)
		{
			if (t.kind == TokenKind::Directive || t.kind == TokenKind::LowBlock)
				error(pos, "#define " + defName + ": a directive or a low block can't be in a define");
		}
		m_defines[defName] = def;
		if (m_index != nullptr)
		{
			CompletionSymbol c;
			c.name = defName;
			c.kind = CompletionSymbol::Define;
			c.hover = def.text;
			c.def = def.pos;
			c.isPrivate = def.function;		// has parameters
			m_index->completion.globals.push_back(c);
		}
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
		// The tokens between two directives are expanded together (a call's arguments can be on
		// several lines), with the defines of that point.
		std::vector<Token> pending;
		auto flush = [&](void) {
			std::vector<std::string> hidden;
			expand(pending, out, nullptr, hidden, 0);
			pending.clear();
		};
		for (const Token& tok : tokens)
		{
			if (tok.kind == TokenKind::End)
				break;
			if (tok.kind == TokenKind::Directive)
			{
				flush();
				directive(tok, directory, out, stack);
				continue;
			}
			if (!stack.empty() && !stack.back().active)
				continue;
			pending.push_back(tok);
		}
		flush();
		if (!stack.empty())
			error(stack.back().pos, "#if without #endif");
	}

	void Preprocessor::expand(const std::vector<Token>& in, std::vector<Token>& out, const SourcePos* usePos, std::vector<std::string>& hidden, int depth)
	{
		if (in.empty())
			return;
		if (depth > 64)
			error(usePos != nullptr ? *usePos : in.front().pos, "#define expansion too deep");
		for (size_t i = 0; i < in.size(); i++)
		{
			const Token& tok = in[i];
			auto it = (tok.kind == TokenKind::Identifier) ? m_defines.find(tok.text) : m_defines.end();
			if (it == m_defines.end() || std::find(hidden.begin(), hidden.end(), tok.text) != hidden.end())
			{
				out.push_back(tok);
				continue;
			}
			const Define& def = it->second;
			const SourcePos pos = (usePos != nullptr) ? *usePos : tok.pos;
			std::vector<Token> replaced;
			if (!def.function)
			{
				for (Token t : def.body)
				{
					t.pos = pos;	// errors point at the use
					replaced.push_back(t);
				}
			}
			else
			{
				// Without '(' after it, the name stays (it isn't a call).
				if (i + 1 >= in.size() || in[i + 1].kind != TokenKind::Punct || in[i + 1].text != "(")
				{
					if (usePos == nullptr && m_index != nullptr)
						m_index->add(tok.pos, (int)tok.text.size(), def.pos, def.text, true);
					out.push_back(tok);
					continue;
				}
				// The arguments: split on the commas outside (), [] and {}.
				std::vector<std::vector<Token>> args(1);
				int nesting = 0;
				size_t j = i + 2;
				for ( ; j < in.size(); j++)
				{
					const Token& t = in[j];
					if (t.kind == TokenKind::Punct)
					{
						if (t.text == "(" || t.text == "[" || t.text == "{")
							nesting++;
						else if (t.text == ")" || t.text == "]" || t.text == "}")
						{
							if (nesting == 0 && t.text == ")")
								break;
							nesting--;
						}
						else if (t.text == "," && nesting == 0)
						{
							args.emplace_back();
							continue;
						}
					}
					args.back().push_back(t);
				}
				if (j >= in.size())
					error(tok.pos, "#define " + tok.text + ": ')' expected after the arguments (a call can't contain a directive)");
				if (args.size() == 1 && args[0].empty() && def.params.size() == 0)
					args.clear();
				if (args.size() != def.params.size())
					error(tok.pos, tok.text + " expects " + std::to_string(def.params.size()) + " argument" + (def.params.size() == 1 ? "" : "s") + ", got " + std::to_string(args.size()));
				for (size_t a = 0; a < args.size(); a++)
				{
					if (args[a].empty() && def.params.size() > 1)
						error(tok.pos, tok.text + ": argument " + std::to_string(a + 1) + " is empty");
				}
				// The arguments are expanded first (keeping their own positions), then put in the body.
				std::vector<std::vector<Token>> expanded(args.size());
				for (size_t a = 0; a < args.size(); a++)
					expand(args[a], expanded[a], usePos, hidden, depth + 1);
				for (Token t : def.body)
				{
					if (t.kind == TokenKind::Identifier)
					{
						auto p = std::find(def.params.begin(), def.params.end(), t.text);
						if (p != def.params.end())
						{
							const auto& arg = expanded[p - def.params.begin()];
							replaced.insert(replaced.end(), arg.begin(), arg.end());
							continue;
						}
					}
					t.pos = pos;
					replaced.push_back(t);
				}
				i = j;	// the ')'
			}
			// Rescanned for more defines (not this one).
			const size_t start = out.size();
			hidden.push_back(tok.text);
			expand(replaced, out, &pos, hidden, depth + 1);
			hidden.pop_back();
			// The hover of a use (in the source, not in another define): the define and what this
			// use becomes.
			if (usePos == nullptr && m_index != nullptr)
			{
				std::string hover = def.text + "\n// expands to:\n";
				hover += (out.size() > start) ? renderTokens(out, start) : "// (nothing)";
				m_index->add(tok.pos, (int)tok.text.size(), def.pos, hover, true);
			}
		}
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

	std::string Preprocessor::resolveImport(const Token& tok, std::string path, const std::string& directory)
	{
		bool libraryDirsOnly = false;
		if (path.size() >= 2 && path.front() == '<' && path.back() == '>')
		{
			libraryDirsOnly = true;
			path = trim(path.substr(1, path.size() - 2));
		}
		else if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
			path = path.substr(1, path.size() - 2);
		if (path.empty())
			error(tok.pos, "#import: no library named");
		if (std::filesystem::path(path).extension() != ".dlib")
			path += ".dlib";
		std::vector<std::string> tried;
		if (!libraryDirsOnly || std::filesystem::path(path).is_absolute())
		{
			std::filesystem::path p = std::filesystem::path(path).is_absolute() ? std::filesystem::path(path) : std::filesystem::path(directory) / path;
			if (std::filesystem::exists(p))
				return p.string();
		}
		for (const auto& dir : m_libraryDirs)
		{
			std::filesystem::path p = std::filesystem::path(dir) / path;
			if (std::filesystem::exists(p))
				return p.string();
		}
		error(tok.pos, "#import: library not found: " + path + (m_libraryDirs.empty() ? " (no library directories: -L <dir>)" : " (searched in the -L directories)"));
	}

	void Preprocessor::importLibrary(const Token& tok, const std::string& path, std::vector<Token>& out)
	{
		const std::string canonical = std::filesystem::weakly_canonical(path).string();
		for (const auto& imp : m_imports)
		{
			if (imp.path == canonical)
				return;
		}
		for (const auto& p : m_importStack)
		{
			if (p == canonical)
				error(tok.pos, "#import: the libraries import each other (" + canonical + ")");
		}
		std::ifstream file(canonical, std::ios::binary);
		if (!file)
			error(tok.pos, "#import: cannot open " + canonical);
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		ImportedLibrary imp;
		imp.path = canonical;
		std::string message;
		if (!dragon::dlib::read(bytes, imp.library, message))
			error(tok.pos, "#import: " + canonical + ": " + message);
		m_files.insert(canonical);
		// Its dependencies first (found from where the library is).
		m_importStack.push_back(canonical);
		const std::string libDir = std::filesystem::path(canonical).parent_path().string();
		for (const auto& dep : imp.library.dependencies)
			importLibrary(tok, resolveImport(tok, dep, libDir), out);
		m_importStack.pop_back();
		// Its declarations, where they come from (the library's sources), without this
		// program's defines.
		const int index = (int)m_imports.size();
		std::map<uint32_t, const dragon::dlib::LineInfo*> lines;
		for (auto& l : imp.library.lines)
		{
			// Recorded relative to the library.
			if (!l.file.empty() && l.file[0] != '<')
				l.file = (std::filesystem::path(canonical).parent_path() / l.file).lexically_normal().string();
			lines[l.line] = &l;
		}
		Lexer lexer(imp.library.interface, canonical);
		std::vector<Token> tokens = lexer.tokenize();
		Token t = tok;
		t.kind = TokenKind::Identifier;
		t.text = "__dlib_extern";
		out.push_back(t);
		t.kind = TokenKind::Integer;
		t.text = std::to_string(index);
		t.value = index;
		out.push_back(t);
		t.kind = TokenKind::Punct;
		t.text = "{";
		out.push_back(t);
		for (Token& x : tokens)
		{
			if (x.kind == TokenKind::End)
				break;
			if (x.kind == TokenKind::Directive)
				error(x.pos, "#import: a directive in a library's declarations");
			auto it = lines.find((uint32_t)x.pos.line);
			if (it != lines.end())
				x.pos = { it->second->file, (int)it->second->sourceLine, std::max(1, x.pos.column + it->second->columnDelta) };
			out.push_back(x);
		}
		t.text = "}";
		out.push_back(t);
		m_imports.push_back(std::move(imp));
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
			std::string nameSpec = arg, value = "";
			size_t eq = arg.find('=');
			if (eq != std::string::npos)
			{
				nameSpec = trim(arg.substr(0, eq));
				value = trim(arg.substr(eq + 1));
			}
			defineFrom(tok.pos, nameSpec, value, arg);
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
		else if (name == "import")
		{
			const std::string written = argument(tok, rest, name);
			const std::string path = resolveImport(tok, written, directory);
			if (std::find(m_importArguments.begin(), m_importArguments.end(), written) == m_importArguments.end())
				m_importArguments.push_back(written);
			if (m_index != nullptr)
				m_index->add(tok.pos, (int)tok.text.size() + 1, {}, "#import " + std::filesystem::weakly_canonical(path).string());
			importLibrary(tok, path, out);
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
		// The columns in the line (the expression is in the directive's text, after the '#').
		const size_t offset = tok.text.find(text);
		if (offset != std::string::npos)
		{
			for (Token& t : raw)
				t.pos.column += tok.pos.column + (int)offset;
		}
		ExprReader r;
		std::vector<Token> pending;
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
				pending.push_back(v);
				continue;
			}
			if (t.kind == TokenKind::End)
				break;
			pending.push_back(t);
		}
		std::vector<std::string> hidden;
		expand(pending, r.toks, nullptr, hidden, 0);
		r.toks.push_back(raw.back());	// End
		int64_t value = parseTernary(r);
		if (r.peek().kind != TokenKind::End)
			error(r.peek().pos, "#if: unexpected '" + r.peek().text + "'");
		return value;
	}
}
