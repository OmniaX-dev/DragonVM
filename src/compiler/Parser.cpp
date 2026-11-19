#include "Parser.hpp"
#include "ConditionBuiltins.hpp"
#include <cstdio>
#include <set>
#include <functional>

// Node fields by kind (see AST.hpp):
//   Program:     list = declarations
//   StructDecl:  name, list = fields (VarDecl), value = 1 if nameless (struct { ... } as a type,
//                named __anon_struct_<n> and added at the end of the program)
//   EnumDecl:    name, typeSyntax = the integer type (nullptr: i32), list = members (EnumMember:
//                name, a = value or nullptr)
//   ConstDecl:   name, typeSyntax (optional), a = value (struct constants: an InitList, or nullptr
//                for the fields' defaults)
//   AliasDecl:   name, typeSyntax
//   VarDecl:     name, typeSyntax (optional if initialized), a = initializer (expression / InitList),
//                c = the constructor call of var p: Point(args) (a Call: Member(Ident " ctor", new)(args))
//   FuncDecl:    name, list = params (VarDecl, a = default value), typeSyntax = return type
//                (nullptr: void), a = body (nullptr: declaration only), value = flags: 1 interrupt,
//                2 inline, 4 method (sub in a struct: named <Struct>::<name>, added at the end of
//                the program, with a first parameter this: <Struct>*), 8 static method (no this),
//                16 priv (only the struct's own subs can use it); fields: isPrivate
//   Block:       list = statements
//   If:          a = condition, b = then, c = else
//   While:       a = condition, b = body       DoWhile: a = condition, b = body
//   For:         a = init (VarDecl / ExprStmt), b = condition, c = step, d = body (all optional but d)
//   Return:      a = value       ExprStmt: a       LowBlock: text
//   Assert:      a = condition, b = message (optional), text = the condition as written
//   Defer:       a = the statement run when the block is left
//   atomic { }:  a Block (see atomicBlock)
//   Unary:       name = operator, a          Binary / Assign: name = operator, a, b
//   Ternary:     a ? b : c                   Call: a = callee, list = arguments
//   Index:       a[b]                        Member: a.name / a->name (value = 1)
//   Cast:        typeSyntax, a               SizeofType: typeSyntax    SizeofExpr: a
//   Pre/PostInc/Dec: a                       InitList: list
//
// Namespaces: namespace A { ... } (A::B, nested, reopened anywhere) only names its declarations:
// they are top-level declarations named A::name (shortName: name, scope: A), and a NamespaceDecl
// (name: the full name) for every namespace opened. A struct's nested structs / enums are hidden
// top-level declarations named <Struct>::<name> (owner: the struct). Names used in a declaration
// are looked up from its scope by the code generator (Ident / TypeSyntax not resolved here).
//
// Local constants (const in a sub) and static variables (static var): hidden top-level
// ConstDecl / VarDecl named <sub>::<name> (text = the name as written), added before the
// declaration they are in; in their scope, the name (also $name in low blocks) is renamed.

namespace dscript
{
	static std::string describe(const Token& t);

	Parser::Parser(const std::vector<Token>& tokens) : m_tokens(tokens)
	{
		// The templates' names: the struct / sub names inside every template { ... } (so that
		// Name<...> can be told from a comparison anywhere, also before the template).
		for (size_t i = 0; i < m_tokens.size(); i++)
		{
			if (m_tokens[i].kind != TokenKind::Keyword || m_tokens[i].text != "template")
				continue;
			size_t j = i + 1;
			int parens = 0;
			while (j < m_tokens.size() && !(parens == 0 && m_tokens[j].kind == TokenKind::Punct && m_tokens[j].text == "{"))
			{
				if (m_tokens[j].kind == TokenKind::Punct && m_tokens[j].text == "(") parens++;
				else if (m_tokens[j].kind == TokenKind::Punct && m_tokens[j].text == ")") parens--;
				j++;
			}
			int depth = 0;
			for (; j < m_tokens.size(); j++)
			{
				const Token& t = m_tokens[j];
				if (t.kind == TokenKind::Punct && t.text == "{") depth++;
				else if (t.kind == TokenKind::Punct && t.text == "}" && --depth == 0) break;
				else if (depth == 1 && t.kind == TokenKind::Keyword && (t.text == "struct" || t.text == "sub") && j + 1 < m_tokens.size()
						 && m_tokens[j + 1].kind == TokenKind::Identifier)
					m_templateNames.insert(m_tokens[j + 1].text);
			}
		}
	}

	bool Parser::isTemplateName(const std::string& name) const
	{
		const size_t sep = name.rfind("::");
		return m_templateNames.count(sep == std::string::npos ? name : name.substr(sep + 2)) > 0;
	}

	void Parser::expectCloseAngle(void)
	{
		// (Array<Array<u32>>: the lexer made '>>' of the two; '>=' / '>>=' after the arguments too)
		Token& t = m_tokens[std::min(m_pos, m_tokens.size() - 1)];
		if (t.kind == TokenKind::Punct && (t.text == ">>" || t.text == ">=" || t.text == ">>="))
		{
			t.text = t.text.substr(1);
			t.pos.column++;
			return;
		}
		expectPunct(">", "after the template's arguments");
	}

	// <type or value, ...>: a value is an expression that cannot be a type (a number, a literal,
	// -x, Name.field, f(x), N + 1...: + - * / % at most, a comparison needs parentheses); a name
	// alone is parsed as a type (the code generator tells a constant's name from a type's).
	void Parser::parseTemplateArgs(std::vector<TypeSyntax*>& out)
	{
		expectPunct("<", "");
		while (true)
		{
			const Token& t = peek();
			bool value = t.kind == TokenKind::Integer || t.kind == TokenKind::Float || t.kind == TokenKind::String
				|| (t.kind == TokenKind::Punct && (t.text == "-" || t.text == "!" || t.text == "~" || t.text == "+"))
				|| (t.kind == TokenKind::Keyword && (t.text == "true" || t.text == "false" || t.text == "null" || t.text == "sizeof" || t.text == "cast" || t.text == "not"));
			if (t.kind == TokenKind::Identifier && t.fixedType == nullptr)
			{
				// Name.field / f(...) / N + 1: a value (after a qualified name)
				int i = 1;
				while (peek(i).kind == TokenKind::Punct && peek(i).text == "::" && peek(i + 1).kind == TokenKind::Identifier)
					i += 2;
				const Token& after = peek(i);
				value = after.kind == TokenKind::Punct && (after.text == "." || after.text == "(" || after.text == "+" || after.text == "-" ||
					after.text == "/" || after.text == "%");
			}
			if (value)
			{
				TypeSyntax* v = new TypeSyntax();
				v->kind = TypeSyntax::Kind::Value;
				v->pos = t.pos;
				v->value = parseBinary(9);
				out.push_back(v);
			}
			else
				out.push_back(parseType());
			if (!acceptPunct(","))
				break;
		}
		expectCloseAngle();
	}

	Node* Parser::parseCondition(void)
	{
		const bool outer = m_condition;
		m_condition = true;
		Node* e = parseExpression();
		m_condition = outer;
		return e;
	}

	// template(T1, optional T2, ...) where <condition> { structs and subs }: the parameters, the
	// condition (parsed now) and the body's tokens (parsed for each instance); each struct / sub
	// in it is an entity, found with its tokens. In a struct's body (owner): only subs (methods).
	Node* Parser::parseTemplate(Node* owner)
	{
		const size_t start = m_pos;
		Node* n = make(NodeKind::TemplateDecl, next().pos);
		TemplateDef* def = new TemplateDef();
		n->tmpl = def;
		def->pos = n->pos;
		def->scope = m_scope;
		def->owner = owner;
		def->external = m_external;
		n->owner = owner;
		n->scope = m_scope;
		n->external = m_external;
		expectPunct("(", "after 'template' (template(T) { ... })");
		bool optional = false;
		while (true)
		{
			if (peek().kind == TokenKind::Identifier && peek().text == "optional" && peek(1).kind == TokenKind::Identifier)
			{
				if (optional)
					error(peek().pos, "'optional' is written once: the parameters after it are optional too");
				optional = true;
				next();
			}
			const std::string param = expectIdentifier("(a template parameter)");
			for (const auto& p : def->params)
			{
				if (p == param)
					error(lastPos(), "template parameter '" + param + "' is there twice");
			}
			def->params.push_back(param);
			// N: type (a value parameter); = default (a type, or a value): optional.
			def->paramTypes.push_back(acceptPunct(":") ? parseType() : nullptr);
			def->defaultTypes.push_back(nullptr);
			def->defaultValues.push_back(nullptr);
			if (acceptPunct("="))
			{
				if (def->paramTypes.back() != nullptr)
					def->defaultValues.back() = parseTernary();
				else
					def->defaultTypes.back() = parseType();
				optional = true;
			}
			if (!optional)
				def->required = def->params.size();		// (after a default, the others are optional too)
			if (!acceptPunct(","))
				break;
		}
		expectPunct(")", "after the template's parameters");
		if (peek().kind == TokenKind::Identifier && peek().text == "where")
		{
			next();
			const size_t from = m_pos;
			def->where = parseCondition();
			// The parts of a top-level 'and' (to say which one is false).
			std::string part;
			int depth = 0;
			for (size_t i = from; i < m_pos; i++)
			{
				const Token& t = m_tokens[i];
				if (t.kind == TokenKind::Punct && (t.text == "(" || t.text == "[")) depth++;
				if (t.kind == TokenKind::Punct && (t.text == ")" || t.text == "]")) depth--;
				if (depth == 0 && ((t.kind == TokenKind::Keyword && t.text == "and") || (t.kind == TokenKind::Punct && t.text == "&&")))
				{
					def->whereParts.push_back(part);
					part.clear();
					continue;
				}
				// (as written: no space inside parentheses, before ',' or a call's '(')
				const std::string text = (t.kind == TokenKind::String ? "\"" + t.text + "\"" : t.text);
				const bool tight = part.empty() || part.back() == '(' || part.back() == '[' || part.back() == '!'
					|| (t.kind == TokenKind::Punct && (t.text == ")" || t.text == "]" || t.text == ","))
					|| (t.kind == TokenKind::Punct && t.text == "(" && i > from && (m_tokens[i - 1].kind == TokenKind::Identifier || m_tokens[i - 1].kind == TokenKind::Keyword));
				part += (tight ? "" : " ") + text;
			}
			def->whereParts.push_back(part);
		}
		const SourcePos open = peek().pos;
		expectPunct("{", "to start the template's body");
		const size_t bodyStart = m_pos;
		int depth = 1;
		while (true)
		{
			if (peek().kind == TokenKind::End)
				error(open, "'}' missing at the end of the template");
			if (isPunct("{")) depth++;
			else if (isPunct("}") && --depth == 0) break;
			next();
		}
		def->tokens.assign(m_tokens.begin() + (std::ptrdiff_t)bodyStart, m_tokens.begin() + (std::ptrdiff_t)m_pos);
		next();		// }
		acceptPunct(";");
		def->source.assign(m_tokens.begin() + (std::ptrdiff_t)start, m_tokens.begin() + (std::ptrdiff_t)m_pos);
		// The entities: [priv | inline | static | atomic | readonly | constexpr | consteval ...] sub
		// name ... / struct name { ... }; around them, if constexpr (condition) { ... } else { ... }
		// (their guards: conditions on the parameters); static_assert(condition, "message").
		const auto& tk = def->tokens;
		auto l_is = [&](size_t i, TokenKind kind, const char* text) { return i < tk.size() && tk[i].kind == kind && tk[i].text == text; };
		// The token after the one that closes tk[i] ('(' / '{').
		auto l_close = [&](size_t i) -> size_t {
			const std::string open = tk[i].text, close = open == "(" ? ")" : "}";
			int d = 0;
			for (; i < tk.size(); i++)
			{
				if (tk[i].kind == TokenKind::Punct && tk[i].text == open) d++;
				else if (tk[i].kind == TokenKind::Punct && tk[i].text == close && --d == 0) return i + 1;
			}
			error(open == "(" ? tk.back().pos : tk.back().pos, "'" + close + "' missing in the template");
		};
		// An expression of the tokens [from, to) (a condition: its built-ins take types).
		auto l_condition = [&](size_t from, size_t to) -> Node* {
			std::vector<Token> part(tk.begin() + (std::ptrdiff_t)from, tk.begin() + (std::ptrdiff_t)to);
			Token end;
			end.kind = TokenKind::End;
			end.pos = to < tk.size() ? tk[to].pos : tk.back().pos;
			part.push_back(end);
			Parser sub(part);
			sub.addTemplateNames(m_templateNames);
			Node* e = sub.parseCondition();
			if (sub.peek().kind != TokenKind::End)
				error(sub.peek().pos, "unexpected " + describe(sub.peek()) + " in the condition");
			return e;
		};
		std::function<void(size_t&, size_t, const std::vector<Guard>&)> l_item;
		auto l_scan = [&](size_t i, size_t end, const std::vector<Guard>& guards) {
			while (i < end)
				l_item(i, end, guards);
		};
		l_item = [&](size_t& i, size_t end, const std::vector<Guard>& guards) {
			if (l_is(i, TokenKind::Keyword, "if") && l_is(i + 1, TokenKind::Keyword, "constexpr") && l_is(i + 2, TokenKind::Punct, "("))
			{
				const size_t afterCond = l_close(i + 2);
				Node* cond = l_condition(i + 3, afterCond - 1);
				if (!l_is(afterCond, TokenKind::Punct, "{"))
					error(afterCond < tk.size() ? tk[afterCond].pos : open, "'{' expected after the if constexpr's condition (around structs and subs)");
				const size_t afterBody = l_close(afterCond);
				std::vector<Guard> inner = guards;
				inner.push_back({ cond, true });
				l_scan(afterCond + 1, afterBody - 1, inner);
				i = afterBody;
				if (l_is(i, TokenKind::Keyword, "else"))
				{
					inner.back().expected = false;
					i++;
					if (l_is(i, TokenKind::Keyword, "if"))
						l_item(i, end, inner);		// else if constexpr (...)
					else if (l_is(i, TokenKind::Punct, "{"))
					{
						const size_t afterElse = l_close(i);
						l_scan(i + 1, afterElse - 1, inner);
						i = afterElse;
					}
					else
						error(i < tk.size() ? tk[i].pos : open, "'{' or 'if constexpr' expected after else");
				}
				return;
			}
			if (l_is(i, TokenKind::Keyword, "static_assert"))
			{
				TemplateAssert as;
				as.pos = tk[i].pos;
				as.guards = guards;
				if (!l_is(i + 1, TokenKind::Punct, "("))
					error(tk[i].pos, "'(' expected after static_assert");
				const size_t after = l_close(i + 1);
				// condition [, "message"]
				size_t comma = after - 1;
				int d = 0;
				for (size_t k = i + 2; k < after - 1; k++)
				{
					if (tk[k].kind == TokenKind::Punct && (tk[k].text == "(" || tk[k].text == "[")) d++;
					if (tk[k].kind == TokenKind::Punct && (tk[k].text == ")" || tk[k].text == "]")) d--;
					if (d == 0 && tk[k].kind == TokenKind::Punct && tk[k].text == ",") { comma = k; break; }
				}
				as.condition = l_condition(i + 2, comma);
				for (size_t k = i + 2; k < comma; k++)
					as.text += (k > i + 2 ? " " : "") + (tk[k].kind == TokenKind::String ? "\"" + tk[k].text + "\"" : tk[k].text);
				if (comma < after - 1)
				{
					if (comma + 2 != after - 1 || tk[comma + 1].kind != TokenKind::String)
						error(tk[comma].pos, "static_assert's message is a string (static_assert(condition, \"message\"))");
					as.message = tk[comma + 1].text;
				}
				def->asserts.push_back(as);
				i = after;
				if (!l_is(i, TokenKind::Punct, ";"))
					error(i < tk.size() ? tk[i].pos : open, "';' expected after static_assert(...)");
				i++;
				return;
			}
			TemplateEntity e;
			e.first = i;
			e.guards = guards;
			while (i < tk.size() && tk[i].kind == TokenKind::Keyword && (tk[i].text == "priv" || tk[i].text == "inline" || tk[i].text == "static" || tk[i].text == "atomic"
				|| tk[i].text == "readonly" || tk[i].text == "constexpr" || tk[i].text == "consteval"))
				i++;
			if (!(l_is(i, TokenKind::Keyword, "sub") || (l_is(i, TokenKind::Keyword, "struct") && owner == nullptr)) || i + 1 >= tk.size() || tk[i + 1].kind != TokenKind::Identifier)
				error(i < tk.size() ? tk[i].pos : open, owner != nullptr ? "a template in a struct holds methods (sub ...)" :
					"a template holds structs and subs (struct ... / sub ...; around them, if constexpr (...) { ... }; static_assert(...)), found " + describe(i < tk.size() ? tk[i] : peek()));
			e.isStruct = tk[i].text == "struct";
			e.nameToken = i + 1;
			e.name = tk[i + 1].text;
			i += 2;
			// To its end: a struct's '}' (and ';'), a sub's ';' (a declaration) or the '}' of its body.
			int parens = 0;
			while (i < end && !(parens == 0 && tk[i].kind == TokenKind::Punct && (tk[i].text == "{" || tk[i].text == ";")))
			{
				if (tk[i].kind == TokenKind::Punct && tk[i].text == "(") parens++;
				else if (tk[i].kind == TokenKind::Punct && tk[i].text == ")") parens--;
				i++;
			}
			if (l_is(i, TokenKind::Punct, "{"))
				i = l_close(i) - 1;
			i++;
			if (e.isStruct && l_is(i, TokenKind::Punct, ";"))
				i++;
			e.last = std::min(i, tk.size());
			def->entities.push_back(e);
			m_templateNames.insert(e.name);
		};
		l_scan(0, tk.size(), {});
		if (def->entities.empty())
			error(open, "an empty template");
		return n;
	}

	std::vector<Node*> Parser::parseInstance(const TemplateDef& def, size_t entity, Node* owner)
	{
		m_scope = def.scope;
		m_namespace = owner != nullptr ? owner->scope : def.scope;
		m_external = 0;
		std::vector<Node*> out;
		if (owner != nullptr)
		{
			// A method: priv / static / inline / atomic, then the sub.
			int flags = 0;
			while (true)
			{
				if ((flags & 8) == 0 && acceptKeyword("static")) flags |= 8;
				else if ((flags & 2) == 0 && acceptKeyword("inline")) flags |= 2;
				else if ((flags & 16) == 0 && acceptKeyword("priv")) flags |= 16;
				else if ((flags & 32) == 0 && acceptKeyword("atomic")) flags |= 32;
				else if ((flags & 64) == 0 && acceptKeyword("readonly")) flags |= 64;
				else if ((flags & 384) == 0 && acceptKeyword("constexpr")) flags |= 128;
				else if ((flags & 384) == 0 && acceptKeyword("consteval")) flags |= 256;
				else break;
			}
			m_scope = owner->name;
			m_lambdas.push_back(parseFunction(flags, owner));
		}
		else
		{
			const bool priv = acceptKeyword("priv");
			Node* d = parseDeclaration();
			d->isPrivate = d->isPrivate || priv;
			for (Node* h : m_hoisted)
				out.push_back(h);
			m_hoisted.clear();
			out.push_back(d);
		}
		(void)entity;
		for (auto* list : { &m_hoisted, &m_anonStructs, &m_nested, &m_lambdas })
		{
			out.insert(out.end(), list->begin(), list->end());
			list->clear();
		}
		return out;
	}

	const Token& Parser::peek(int offset) const
	{
		size_t i = m_pos + offset;
		if (i >= m_tokens.size())
			return m_tokens.back();
		return m_tokens[i];
	}

	const Token& Parser::next(void)
	{
		const Token& t = peek();
		if (m_pos < m_tokens.size() - 1)
			m_pos++;
		return t;
	}

	bool Parser::isPunct(const std::string& p, int offset) const
	{
		return peek(offset).kind == TokenKind::Punct && peek(offset).text == p;
	}

	bool Parser::isKeyword(const std::string& k, int offset) const
	{
		return peek(offset).kind == TokenKind::Keyword && peek(offset).text == k;
	}

	bool Parser::acceptPunct(const std::string& p)
	{
		if (!isPunct(p))
			return false;
		next();
		return true;
	}

	bool Parser::acceptKeyword(const std::string& k)
	{
		if (!isKeyword(k))
			return false;
		next();
		return true;
	}

	static std::string describe(const Token& t)
	{
		switch (t.kind)
		{
			case TokenKind::End: return "end of file";
			case TokenKind::Integer: return "number";
			case TokenKind::Float: return "number";
			case TokenKind::String: return "string";
			case TokenKind::LowBlock: return "low block";
			case TokenKind::Directive: return "directive";
			default: return "'" + t.text + "'";
		}
	}

	const Token& Parser::expectPunct(const std::string& p, const std::string& context)
	{
		if (!isPunct(p))
			error(peek().pos, "'" + p + "' expected " + context + ", found " + describe(peek()));
		return next();
	}

	const Token& Parser::expectKeyword(const std::string& k, const std::string& context)
	{
		if (!isKeyword(k))
			error(peek().pos, "'" + k + "' expected " + context + ", found " + describe(peek()));
		return next();
	}

	std::string Parser::expectIdentifier(const std::string& context)
	{
		if (peek().kind != TokenKind::Identifier)
			error(peek().pos, "name expected " + context + ", found " + describe(peek()));
		return next().text;
	}

	const SourcePos& Parser::lastPos(void) const
	{
		return m_tokens[m_pos - 1].pos;
	}

	Node* Parser::make(NodeKind kind, const SourcePos& pos)
	{
		Node* n = new Node();
		n->kind = kind;
		n->pos = pos;
		n->external = m_external;
		// (in an if constexpr around declarations: they exist only if its condition says so)
		n->guard = m_guard;
		n->guardElse = m_guardElse;
		return n;
	}

	// ===================================== Declarations =====================================
	Node* Parser::parseProgram(void)
	{
		Node* program = make(NodeKind::Program, peek().pos);
		while (peek().kind != TokenKind::End)
			parseTopLevel(program->list);
		for (Node* s : m_anonStructs)
			program->list.push_back(s);
		m_anonStructs.clear();
		for (Node* s : m_nested)
			program->list.push_back(s);
		m_nested.clear();
		for (Node* l : m_lambdas)
			program->list.push_back(l);
		m_lambdas.clear();
		return program;
	}

	void Parser::parseTopLevel(std::vector<Node*>& out)
	{
		// A library's declarations (the preprocessor puts them there for #import).
		if (peek().kind == TokenKind::Identifier && peek().text == "__dlib_extern" && peek(1).kind == TokenKind::Integer && m_external == 0)
		{
			next();
			m_external = (int)next().value + 1;
			expectPunct("{", "after __dlib_extern");
			while (!acceptPunct("}"))
			{
				if (peek().kind == TokenKind::End)
					error(peek().pos, "'}' missing at the end of a library's declarations");
				parseTopLevel(out);
			}
			m_external = 0;
			return;
		}
		if (isKeyword("namespace"))
		{
			parseNamespace(out);
			return;
		}
		if (isKeyword("if") && isKeyword("constexpr", 1))
		{
			parseDeclIf(out, nullptr);
			return;
		}
		if (isKeyword("static_assert"))
		{
			out.push_back(parseStaticAssert());
			return;
		}
		Node* d = parseDeclaration();
		for (Node* h : m_hoisted)
			out.push_back(h);
		m_hoisted.clear();
		out.push_back(d);
	}

	void Parser::parseNamespace(std::vector<Node*>& out)
	{
		const SourcePos pos = next().pos;
		const SourcePos namePos = peek().pos;
		bool global = false;
		const std::string name = parseQualifiedName("after 'namespace'", global);
		if (global)
			error(namePos, "a namespace is named from where it is (namespace A::B, not ::A)");
		const std::string outerNamespace = m_namespace;
		const std::string outerScope = m_scope;
		// namespace A::B { ... }: opens A, then A::B.
		size_t start = 0;
		std::vector<Node*> opened;
		while (true)
		{
			const size_t sep = name.find("::", start);
			const std::string part = name.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
			Node* n = make(NodeKind::NamespaceDecl, pos);
			opened.push_back(n);
			n->namePos = namePos;
			n->shortName = part;
			n->scope = m_namespace;
			m_namespace = m_namespace.empty() ? part : m_namespace + "::" + part;
			n->name = m_namespace;
			out.push_back(n);
			if (sep == std::string::npos)
				break;
			start = sep + 2;
		}
		m_scope = m_namespace;
		expectPunct("{", "to start the namespace");
		while (!isPunct("}"))
		{
			if (peek().kind == TokenKind::End)
				error(pos, "'}' missing at the end of namespace " + m_namespace);
			parseTopLevel(out);
		}
		for (Node* n : opened)
			n->endPos = peek().pos;
		next();
		acceptPunct(";");
		m_namespace = outerNamespace;
		m_scope = outerScope;
	}

	// if constexpr (condition) { declarations } [else { ... } | else if constexpr ...] at the top
	// level, in a namespace, or in a struct's body (structDecl): the declarations get it as their
	// guard (the code generator keeps those whose conditions say so).
	void Parser::parseDeclIf(std::vector<Node*>& out, Node* structDecl)
	{
		Node* n = make(NodeKind::DeclIf, next().pos);
		next();		// constexpr
		n->value = -1;
		n->scope = m_scope;
		expectPunct("(", "after 'if constexpr'");
		n->a = parseCondition();
		expectPunct(")", "after the condition");
		Node* const outerGuard = m_guard;
		const bool outerElse = m_guardElse;
		m_guard = n;
		m_guardElse = false;
		auto l_body = [&] {
			const SourcePos open = peek().pos;
			expectPunct("{", "after the if constexpr's condition (around declarations)");
			while (!acceptPunct("}"))
			{
				if (peek().kind == TokenKind::End)
					error(open, "'}' missing at the end of the if constexpr");
				if (structDecl != nullptr)
					parseStructMember(structDecl);
				else
					parseTopLevel(out);
			}
		};
		l_body();
		if (acceptKeyword("else"))
		{
			m_guardElse = true;
			if (isKeyword("if") && isKeyword("constexpr", 1))
				parseDeclIf(out, structDecl);
			else
				l_body();
		}
		m_guard = outerGuard;
		m_guardElse = outerElse;
	}

	// static_assert(condition[, "message"]);: checked when compiling (no code).
	Node* Parser::parseStaticAssert(void)
	{
		Node* n = make(NodeKind::StaticAssert, next().pos);
		n->scope = m_scope;
		expectPunct("(", "after 'static_assert'");
		const size_t from = m_pos;
		n->a = parseCondition();
		n->text = tokenText(from, m_pos);
		if (acceptPunct(","))
		{
			if (peek().kind != TokenKind::String)
				error(peek().pos, "static_assert's message is a string (static_assert(condition, \"message\"))");
			n->name = next().text;
		}
		expectPunct(")", "after static_assert(condition[, message])");
		expectPunct(";", "after static_assert(...)");
		return n;
	}

	std::string Parser::parseQualifiedName(const std::string& context, bool& global)
	{
		global = acceptPunct("::");
		std::string name = expectIdentifier(context);
		while (isPunct("::") && peek(1).kind == TokenKind::Identifier)
		{
			next();
			name += "::" + next().text;
		}
		return name;
	}

	void Parser::nameDeclaration(Node* d)
	{
		d->shortName = d->name;
		d->scope = m_scope;
		if (!m_scope.empty())
			d->name = m_scope + "::" + d->name;
	}

	Node* Parser::parseDeclaration(void)
	{
		if (isKeyword("template")) return parseTemplate(nullptr);
		if (isKeyword("struct")) return parseStruct();
		if (isKeyword("enum")) return parseEnum();
		if (isKeyword("const") || (isKeyword("constexpr") && peek(1).kind == TokenKind::Identifier))
		{
			Node* c = parseConst();
			nameDeclaration(c);
			return c;
		}
		if (isKeyword("constinit"))
		{
			Node* v = parseVarDecl();
			expectPunct(";", "after the variable declaration");
			nameDeclaration(v);
			return v;
		}
		if (m_external != 0 && isKeyword("readonly") && isKeyword("var", 1))
		{
			// (A library's interface: a constant variable of the library, set when it starts.)
			next();
			Node* v = parseVarDecl();
			v->constKind = 1;
			expectPunct(";", "after the variable declaration");
			nameDeclaration(v);
			return v;
		}
		if (isKeyword("alias"))
		{
			Node* a = parseAlias();
			nameDeclaration(a);
			return a;
		}
		if (isKeyword("low"))
		{
			Node* l = parseLow();
			l->scope = m_scope;
			return l;
		}
		if (isKeyword("var") || isKeyword("auto"))
		{
			Node* v = parseVarDecl();
			expectPunct(";", "after the variable declaration");
			nameDeclaration(v);
			return v;
		}
		if (isKeyword("sub")) return parseFunction(0);
		if (isKeyword("constexpr") || isKeyword("consteval"))
		{
			// constexpr / consteval [inline] sub
			const int ck = next().text == "constexpr" ? 128 : 256;
			const int inl = acceptKeyword("inline") ? 2 : 0;
			if (!isKeyword("sub"))
				error(peek().pos, "'sub' expected after '" + m_tokens[m_pos - 1].text + "', found " + describe(peek()));
			return parseFunction(ck | inl);
		}
		if (isKeyword("readonly"))
			error(peek().pos, "'readonly' is for the fields and subs of a struct");
		if (isKeyword("static"))
			error(peek().pos, "'static' is for the local variables and constants of a sub (a global already exists for the whole program)");
		if (isKeyword("priv"))
			error(peek().pos, "'priv' is for the fields and subs of a struct");
		if (acceptKeyword("atomic"))
		{
			if (isKeyword("inline"))
				error(peek().pos, "inline subs cannot be atomic (they are copied into their callers: use a normal sub)");
			const bool interrupt = acceptKeyword("interrupt");
			if (!isKeyword("sub"))
				error(peek().pos, "'sub' expected after 'atomic'");
			return parseFunction(32 | (interrupt ? 1 : 0));
		}
		if (acceptKeyword("interrupt"))
		{
			if (isKeyword("atomic"))
				error(peek().pos, "write 'atomic interrupt sub'");
			if (!isKeyword("sub"))
				error(peek().pos, "'sub' expected after 'interrupt'");
			return parseFunction(1);
		}
		if (acceptKeyword("inline"))
		{
			if (isKeyword("atomic"))
				error(peek().pos, "inline subs cannot be atomic (they are copied into their callers: use a normal sub)");
			if (isKeyword("interrupt"))
				error(peek().pos, "interrupt subs cannot be inline (they are entered by an interrupt, not called)");
			int ck = 0;
			if (acceptKeyword("constexpr"))
				ck = 128;
			else if (acceptKeyword("consteval"))
				ck = 256;
			if (!isKeyword("sub"))
				error(peek().pos, "'sub' expected after '" + m_tokens[m_pos - 1].text + "'");
			return parseFunction(2 | ck);
		}
		error(peek().pos, "declaration expected (var, sub, inline sub, struct, enum, const, alias, namespace or low), found " + describe(peek()));
	}

	Node* Parser::parseStruct(const Node* owner)
	{
		Node* n = make(NodeKind::StructDecl, next().pos);
		n->name = expectIdentifier("after 'struct'");
		n->namePos = lastPos();
		nameDeclaration(n);
		n->owner = const_cast<Node*>(owner);
		parseStructFields(n);
		acceptPunct(";");
		return n;
	}

	// One member of a struct's body (fields, methods, nested types, constants, method templates, if
	// constexpr around members, static_assert).
	void Parser::parseStructMember(Node* decl)
	{
		if (isKeyword("if") && isKeyword("constexpr", 1))
		{
			std::vector<Node*> unused;
			parseDeclIf(unused, decl);
			return;
		}
		if (isKeyword("static_assert"))
		{
			decl->list.push_back(parseStaticAssert());		// (checked with the struct's layout)
			return;
		}
		// priv var / priv sub: only the struct's own subs can use it. readonly var: everyone
		// can read it, only the struct's own subs can change it.
		const bool priv = acceptKeyword("priv");
		if (isKeyword("const"))
		{
			// const name: T [= default];: set when the struct is built (its default value,
			// { ... }, or the constructor), then never changed.
			if (isKeyword("var", 1) || isKeyword("auto", 1))
				error(peek(1).pos, "'const " + peek(1).text + "': const takes the place of var (const name: type)");
			Node* field = parseVarDecl(false);
			field->isPrivate = priv;
			field->shortName = field->name;
			field->scope = m_scope;
			expectPunct(";", "after the struct field");
			decl->list.push_back(field);
			return;
		}
		if (isKeyword("constexpr") && peek(1).kind == TokenKind::Identifier)
		{
			// constexpr NAME = value;: a constant of the struct (Struct::NAME, Struct.NAME,
			// x.NAME, NAME in its subs): no memory, the same for every instance.
			if (decl->value != 0)
				error(peek().pos, "constants in a struct need a named struct (struct Name { ... })");
			Node* c = parseConst();
			nameDeclaration(c);
			c->owner = decl;
			c->isPrivate = priv;
			m_nested.push_back(c);
			return;
		}
		if (isKeyword("constinit"))
			error(peek().pos, "constinit is for globals (a field gets its value when its struct is built: a default value, or const for one that never changes)");
		if (isKeyword("readonly") && !isKeyword("var", 1) && !isKeyword("const", 1))
			;	// readonly sub: a method (below)
		else if (isKeyword("readonly"))
		{
			const Token& ro = next();
			if (priv || isKeyword("priv"))
				error(ro.pos, "a field is either priv or readonly: priv already keeps others from changing it (and from reading it)");
			if (decl->value != 0)
				error(ro.pos, "readonly fields need a named struct (struct Name { ... }): only its subs can change them");
			if (isKeyword("const"))
				error(peek().pos, "a field is either readonly or const: const already keeps everyone (also the struct's subs) from changing it");
			Node* field = parseVarDecl(false);
			field->isReadonly = true;
			field->shortName = field->name;
			field->scope = m_scope;
			expectPunct(";", "after the struct field");
			decl->list.push_back(field);
			return;
		}
		if (isKeyword("struct") || isKeyword("enum"))
		{
			// A nested struct / enum: <Struct>::<name> (priv: only usable in the struct).
			if (decl->value != 0)
				error(peek().pos, "nested types need a named struct (struct Name { ... }), not a nameless one");
			if (isKeyword("struct") && !(peek(1).kind == TokenKind::Identifier))
				error(peek(1).pos, "a nested struct needs a name (struct Name { ... }); use struct { ... } as the type of a field for a nameless one");
			Node* nested = isKeyword("struct") ? parseStruct(decl) : parseEnum(decl);
			nested->isPrivate = priv;
			m_nested.push_back(nested);
			return;
		}
		if (isKeyword("static") && isKeyword("var", 1))
			error(peek(1).pos, "structs have no static fields: use a global (or a constant) next to the struct");
		if (isKeyword("template"))
		{
			// Method templates: hidden top-level template declarations, owned by the struct.
			if (priv)
				error(peek().pos, "priv goes on the template's subs (template(T) { priv sub ... })");
			if (decl->value != 0)
				error(peek().pos, "method templates need a named struct (struct Name { ... })");
			m_lambdas.push_back(parseTemplate(decl));
			return;
		}
		if (isKeyword("sub") || isKeyword("inline") || isKeyword("static") || isKeyword("priv") || isKeyword("atomic") || isKeyword("readonly")
			|| isKeyword("constexpr") || isKeyword("consteval"))
		{
			// A method: a hidden top-level sub (added at the end of the program). static: no
			// this, called as Struct::name(...) / Struct.name(...).
			if (decl->value != 0)
				error(peek().pos, "methods need a named struct (struct Name { ... }), not a nameless one");
			// priv, static and inline, in any order
			int flags = priv ? 16 : 0;
			while (true)
			{
				if ((flags & 8) == 0 && acceptKeyword("static"))
					flags |= 8;
				else if ((flags & 2) == 0 && acceptKeyword("inline"))
					flags |= 2;
				else if ((flags & 16) == 0 && acceptKeyword("priv"))
					flags |= 16;
				else if ((flags & 32) == 0 && acceptKeyword("atomic"))
					flags |= 32;
				else if ((flags & 64) == 0 && acceptKeyword("readonly"))
					flags |= 64;
				else if ((flags & 384) == 0 && acceptKeyword("constexpr"))
					flags |= 128;
				else if ((flags & 384) == 0 && acceptKeyword("consteval"))
					flags |= 256;
				else
					break;
			}
			if ((flags & 34) == 34)
				error(peek().pos, "inline subs cannot be atomic (they are copied into their callers: use a normal sub)");
			if ((flags & 72) == 72)
				error(peek().pos, "a static sub has no this: readonly is for the subs that work on the struct (it makes this a const pointer)");
			if (!isKeyword("sub"))
				error(peek().pos, "'sub' expected after '" + m_tokens[m_pos - 1].text + "', found " + describe(peek()));
			Node* outerFunc = m_currentFunc;
			auto outerScopes = std::move(m_scopes);
			m_scopes.clear();
			m_lambdas.push_back(parseFunction(flags, decl));
			m_scopes = std::move(outerScopes);
			m_currentFunc = outerFunc;
			return;
		}
		if (isKeyword("interrupt"))
			error(peek().pos, "interrupt subs cannot be methods (they take no parameters)");
		if (isKeyword("auto"))
			error(peek().pos, "struct fields need a type: declare them with 'var' (auto is for variables with a value)");
		if (!isKeyword("var"))
			error(peek().pos, "struct fields are declared with 'var' (methods with 'sub'), found " + describe(peek()));
		Node* field = parseVarDecl(false);
		field->isPrivate = priv;
		field->shortName = field->name;
		field->scope = m_scope;
		expectPunct(";", "after the struct field");
		decl->list.push_back(field);
	}

	void Parser::parseStructFields(Node* decl)
	{
		expectPunct("{", "to start the struct");
		// The names in a named struct are looked up from it first (its nested types).
		const std::string outerScope = m_scope;
		if (decl->value == 0)
			m_scope = decl->name;
		while (!acceptPunct("}"))
			parseStructMember(decl);
		m_scope = outerScope;
	}

	Node* Parser::parseEnum(const Node* owner)
	{
		Node* n = make(NodeKind::EnumDecl, next().pos);
		n->name = expectIdentifier("after 'enum'");
		n->namePos = lastPos();
		nameDeclaration(n);
		n->owner = const_cast<Node*>(owner);
		if (acceptPunct(":"))
			n->typeSyntax = parseType();
		expectPunct("{", "to start the enum");
		while (!acceptPunct("}"))
		{
			Node* m = make(NodeKind::EnumMember, peek().pos);
			m->name = expectIdentifier("(enum member)");
			m->namePos = lastPos();
			if (acceptPunct("="))
				m->a = parseTernary();
			n->list.push_back(m);
			if (!acceptPunct(","))
			{
				expectPunct("}", "after the enum members");
				break;
			}
		}
		acceptPunct(";");
		return n;
	}

	// const NAME[: T] = value; / constexpr NAME[: T] = value;: a ConstDecl (constKind 1: a
	// compile-time constant if the value is one, otherwise a constant variable; 2: it must be a
	// compile-time constant). const p: T(args);: a constant variable built by its constructor (a
	// VarDecl, constKind 1).
	Node* Parser::parseConst(void)
	{
		const size_t start = m_pos;
		const Token& keyword = next();
		const bool isConstexpr = keyword.text == "constexpr";
		if (isKeyword("var") || isKeyword("auto"))
			error(peek().pos, "'" + keyword.text + " " + peek().text + "': " + keyword.text + " takes the place of var (" + keyword.text + " name = ...)");
		Node* n = make(NodeKind::ConstDecl, keyword.pos);
		n->constKind = isConstexpr ? 2 : 1;
		n->name = expectIdentifier("after '" + keyword.text + "'");
		n->namePos = lastPos();
		if (acceptPunct(":"))
			n->typeSyntax = parseType();
		if (n->typeSyntax != nullptr && isPunct("("))
		{
			if (isConstexpr)
				error(peek().pos, "a constexpr value is computed when compiling: a constructor runs when the program does (use const)");
			m_pos = start;
			Node* v = parseVarDecl(true);
			expectPunct(";", "after the constant");
			return v;
		}
		// Struct constants can omit the value (the fields' defaults) or use { ... }.
		if (n->typeSyntax != nullptr && isPunct(";"))
		{
			next();
			return n;
		}
		expectPunct("=", "(a constant needs a value)");
		n->a = isPunct("{") ? parseInitializer() : parseTernary();
		expectPunct(";", "after the constant");
		return n;
	}

	Node* Parser::parseAlias(void)
	{
		Node* n = make(NodeKind::AliasDecl, next().pos);
		n->name = expectIdentifier("after 'alias'");
		n->namePos = lastPos();
		expectPunct("=", "in the alias");
		n->typeSyntax = parseType();
		expectPunct(";", "after the alias");
		return n;
	}

	// A for-in loop (after 'for ('): auto x in, auto& x in, or var x: T in. 'in' is not a keyword:
	// only there it means something.
	bool Parser::isForIn(void)
	{
		auto l_in = [&](size_t k) { return peek(k).kind == TokenKind::Identifier && peek(k).text == "in"; };
		if (isKeyword("auto"))
			return (peek(1).kind == TokenKind::Identifier && l_in(2)) || (isPunct("&", 1) && peek(2).kind == TokenKind::Identifier && l_in(3));
		if (!isKeyword("var") || peek(1).kind != TokenKind::Identifier || !isPunct(":", 2))
			return false;
		// var x: <type> in: the type is parsed, then the parser goes back.
		const size_t saved = m_pos;
		next(); next(); next();
		parseType();
		const bool in = l_in(0);
		m_pos = saved;
		return in;
	}

	// for (auto x in range) body: value = 1 for auto& (x points at the element), a = the variable
	// (a VarDecl: its type for var x: T), b = the range (or the start of a..b), d = the end of
	// a..b (nullptr: not a number range), c = the body, text = the loop's number (its hidden
	// variables). The code generator turns it into a for loop (CodeGen::forInLoop).
	Node* Parser::parseForIn(const SourcePos& pos)
	{
		Node* n = make(NodeKind::ForIn, pos);
		Node* v = make(NodeKind::VarDecl, peek().pos);
		if (acceptKeyword("auto"))
		{
			if (acceptPunct("&"))
				n->value = 1;
			v->name = expectIdentifier("after 'auto'");
		}
		else
		{
			expectKeyword("var", "");
			v->name = expectIdentifier("after 'var'");
			expectPunct(":", "after the variable's name");
			v->typeSyntax = parseType();
		}
		v->namePos = lastPos();
		next();		// in
		n->b = parseExpression();
		if (acceptPunct(".."))
		{
			n->d = parseExpression();
			if (n->value == 1)
				error(v->namePos, "auto& points at the elements of a range: a range of numbers (a..b) has none (auto " + v->name + " in ...)");
		}
		expectPunct(")", "after the range");
		n->a = v;
		declareLocal(v->name);
		n->text = std::to_string(++m_forInCount);
		n->c = parseStatement();
		requireBlockFor(n->c, "for");
		return n;
	}

	Node* Parser::parseVarDecl(bool allowNoType)
	{
		Node* n = make(NodeKind::VarDecl, peek().pos);
		// auto: the type comes from the value (as var without a type), and saying it is an error.
		// const (a variable that never changes after its value), constinit (a global whose value
		// is known when compiling): like var.
		const bool isAuto = acceptKeyword("auto");
		std::string keyword = "auto";
		if (!isAuto)
		{
			if (acceptKeyword("const"))
				n->constKind = 1, keyword = "const";
			else if (acceptKeyword("constinit"))
				n->constKind = 3, keyword = "constinit";
			else
				expectKeyword("var", ""), keyword = "var";
		}
		n->name = expectIdentifier("after '" + keyword + "'");
		n->namePos = lastPos();
		if (isAuto && isPunct(":"))
			error(peek().pos, "'auto' takes its type from the value: remove ': <type>' (or declare '" + n->name + "' with var)");
		if (acceptPunct(":"))
			n->typeSyntax = parseType();
		if (n->typeSyntax != nullptr && isPunct("("))
		{
			// var p: Point(args): the struct's constructor (sub new) on it, after its defaults.
			if (!allowNoType)
				error(peek().pos, "struct fields cannot call a constructor (call it in a sub, or use a default value)");
			const SourcePos at = next().pos;
			Node* call = make(NodeKind::Call, at);
			Node* member = make(NodeKind::Member, at);
			member->namePos = at;
			member->name = "new";
			member->a = make(NodeKind::Ident, n->namePos);
			member->a->name = " ctor";		// the variable (bound by the code generator)
			member->a->text = n->name;
			member->a->resolved = true;
			call->a = member;
			while (!acceptPunct(")"))
			{
				call->list.push_back(parseAssignment());
				if (!acceptPunct(","))
				{
					expectPunct(")", "after the constructor's arguments");
					break;
				}
			}
			n->c = call;
			if (isPunct("="))
				error(peek().pos, "'" + n->name + "' is built by its constructor: it cannot also have a value");
		}
		if (acceptPunct("="))
			n->a = parseInitializer();
		if (isAuto && n->a == nullptr)
			error(n->pos, "'auto " + n->name + "' needs a value to take its type from (auto " + n->name + " = ...;)");
		if (isAuto && n->a->kind == NodeKind::InitList)
			error(n->a->pos, "'auto' can't take a type from { ... }: declare '" + n->name + "' with var and its type");
		if (n->typeSyntax == nullptr && (n->a == nullptr || !allowNoType || n->a->kind == NodeKind::InitList))
			error(n->pos, "the type of '" + n->name + "' is needed (" + keyword + " " + n->name + ": <type>)");
		return n;
	}

	Node* Parser::parseInitializer(void)
	{
		if (!isPunct("{"))
			return parseAssignment();
		Node* n = make(NodeKind::InitList, next().pos);
		while (!acceptPunct("}"))
		{
			n->list.push_back(parseInitializer());
			if (!acceptPunct(","))
			{
				expectPunct("}", "after the initializer list");
				break;
			}
		}
		return n;
	}

	// FuncDecl flags (value): 1 interrupt, 2 inline, 4 method, 8 static method, 16 priv, 32 atomic
	// (the body is an atomic block). owner:
	// the struct of a method.
	Node* Parser::parseFunction(int flags, const Node* owner)
	{
		Node* n = make(NodeKind::FuncDecl, next().pos);
		n->value = flags | (owner != nullptr ? 4 : 0);
		n->constKind = (flags & 128) != 0 ? 2 : (flags & 256) != 0 ? 3 : 0;
		if ((flags & 64) != 0 && owner == nullptr)
			error(n->pos, "readonly is for the subs of a struct (this becomes a const pointer)");
		if (n->constKind != 0 && (flags & 33) != 0)
			error(n->pos, std::string((flags & 1) != 0 ? "interrupt" : "atomic") + " subs cannot be " + (n->constKind == 2 ? "constexpr" : "consteval") +
				" (they are never evaluated when compiling)");
		n->name = expectIdentifier("after 'sub'");
		n->namePos = lastPos();
		// Methods: <Struct>::<name> (the scope is the struct's); the others: in the namespace.
		nameDeclaration(n);
		n->owner = const_cast<Node*>(owner);
		expectPunct("(", "after the sub name");
		parseParams(n);
		if (owner != nullptr && (flags & 8) == 0)
		{
			// The hidden first parameter: this: <Struct>*.
			Node* self = make(NodeKind::VarDecl, n->namePos);
			self->name = "this";
			self->namePos = n->namePos;
			TypeSyntax* named = new TypeSyntax();
			named->pos = n->namePos;
			named->name = owner->name;
			named->written = owner->shortName;
			named->resolved = true;
			self->typeSyntax = new TypeSyntax();
			self->typeSyntax->kind = TypeSyntax::Kind::Pointer;
			self->typeSyntax->pos = n->namePos;
			self->typeSyntax->base = named;
			self->typeSyntax->constTarget = (flags & 64) != 0;	// readonly sub: this is a const pointer
			n->list.insert(n->list.begin(), self);
		}
		if (acceptPunct("->"))
			n->typeSyntax = parseType();
		if (owner != nullptr && !isPunct("{") && m_external == 0)
			error(peek().pos, "a method needs its body here, in the struct ('{' expected)");
		if (acceptPunct(";"))
			return n;
		Node* outer = m_currentFunc;
		m_currentFunc = n;
		pushScope();
		for (Node* p : n->list)
			declareLocal(p->name);
		const size_t bodyStart = m_pos;
		n->a = parseBlock();
		if ((flags & 32) != 0)
			n->a = atomicBlock(n->a, n->pos);
		if ((flags & (2 | 128 | 256)) != 0)		// (inline, constexpr, consteval: the body is in a library's interface)
			n->bodyTokens.assign(m_tokens.begin() + (std::ptrdiff_t)bodyStart, m_tokens.begin() + (std::ptrdiff_t)m_pos);
		popScope();
		m_currentFunc = outer;
		return n;
	}

	// After '...': 'typed' (written right after it: ...typed).
	bool Parser::acceptVariadicTyped(void)
	{
		const Token& t = peek();
		if (t.kind != TokenKind::Identifier || t.text != "typed")
			return false;
		next();
		return true;
	}

	void Parser::parseParams(Node* n)
	{
		if (isKeyword("void") && isPunct(")", 1))
			next();
		while (!acceptPunct(")"))
		{
			if (acceptPunct("..."))
			{
				// ... (more arguments, of any type) or ...typed (with their types): the last "parameter".
				n->variadic = acceptVariadicTyped() ? 2 : 1;
				expectPunct(")", "after '...' (it ends the parameters)");
				break;
			}
			Node* p = make(NodeKind::VarDecl, peek().pos);
			if (isKeyword("auto"))
				error(peek().pos, "parameters need a type (auto is for variables with a value): var name: type");
			if (isKeyword("constexpr") || isKeyword("constinit") || isKeyword("consteval"))
				error(peek().pos, "parameters are var or const (" + peek().text + " is for constants and subs)");
			if (acceptKeyword("const"))
				p->constKind = 1;		// the sub cannot change it
			else
				acceptKeyword("var");
			p->pos = peek().pos;
			p->name = expectIdentifier("(parameter name)");
			p->namePos = lastPos();
			expectPunct(":", "after the parameter name (parameters need a type)");
			p->typeSyntax = parseType();
			if (acceptPunct("="))
				p->a = parseTernary();
			n->list.push_back(p);
			if (!acceptPunct(","))
			{
				expectPunct(")", "after the parameters");
				break;
			}
		}
	}

	// A lambda: sub(params) -> type { body }, or interrupt sub() { body }. It becomes a hidden
	// top-level sub (d: the sub it is written in, for the error about its local variables);
	// the expression is its name.
	Node* Parser::parseLambda(void)
	{
		const bool interrupt = acceptKeyword("interrupt");
		Node* n = make(NodeKind::FuncDecl, next().pos);
		n->value = interrupt ? 1 : 0;
		n->name = "__lambda_" + std::to_string(m_lambdaCount++);
		n->shortName = n->name;
		n->scope = m_scope;
		n->namePos = n->pos;	// 'sub'
		expectPunct("(", "after 'sub' (a lambda: sub(var x: i32) -> i32 { ... })");
		parseParams(n);
		if (acceptPunct("->"))
			n->typeSyntax = parseType();
		if (!isPunct("{"))
			error(peek().pos, "'{' expected: a lambda needs a body (sub(...) -> type { ... })");
		n->d = m_currentFunc;
		Node* outer = m_currentFunc;
		m_currentFunc = n;
		pushScope();
		for (Node* p : n->list)
			declareLocal(p->name);
		n->a = parseBlock();
		popScope();
		m_currentFunc = outer;
		m_lambdas.push_back(n);
		Node* id = make(NodeKind::Ident, n->pos);
		id->name = n->name;
		id->resolved = true;
		return id;
	}

	Node* Parser::parseLow(void)
	{
		Node* n = make(NodeKind::LowBlock, next().pos);
		if (peek().kind != TokenKind::LowBlock)
			error(peek().pos, "'{' expected after 'low'");
		const Token& t = next();
		n->text = renameLow(t.text);
		n->pos = t.pos;
		return n;
	}

	// ======================================== Types =========================================
	// A name followed by '*'s and then ')' or '[': a pointer type (as an expression, the '*' would
	// need something after it).
	bool Parser::pointerTypeAhead(void) const
	{
		int i = 0;
		if (peek(i).kind == TokenKind::Punct && peek(i).text == "::")
			i++;
		if (peek(i).kind != TokenKind::Identifier)
			return false;
		i++;
		while (peek(i).kind == TokenKind::Punct && peek(i).text == "::" && peek(i + 1).kind == TokenKind::Identifier)
			i += 2;
		if (peek(i).kind != TokenKind::Punct || peek(i).text != "*")
			return false;
		while (peek(i).kind == TokenKind::Punct && peek(i).text == "*")
			i++;
		return peek(i).kind == TokenKind::Punct && (peek(i).text == ")" || peek(i).text == "[");
	}

	bool Parser::startsType(void) const
	{
		const Token& t = peek();
		if (t.fixedType != nullptr)
			return true;
		if (t.kind == TokenKind::Keyword)
			return t.text == "void" || t.text == "sub" || t.text == "struct" || t.text == "const";
		if (t.kind != TokenKind::Identifier)
			return false;
		static const char* names[] = { "i8", "u8", "i16", "u16", "i32", "u32", "f32", "bool", "char", "string", "int", "uint", "float", nullptr };
		for (int i = 0; names[i] != nullptr; i++)
		{
			if (t.text == names[i])
				return true;
		}
		return false;
	}

	TypeSyntax* Parser::parseType(void)
	{
		// const T*: what the (first) pointer points at is constant. A constant variable is
		// declared with const (const p: T* = ...): in a type, const is only about the target.
		const SourcePos constPos = peek().pos;
		const bool constTarget = acceptKeyword("const");
		if (constTarget && isKeyword("const"))
			error(peek().pos, "const twice");
		TypeSyntax* t = new TypeSyntax();
		t->pos = peek().pos;
		if (acceptPunct("("))
		{
			delete t;
			t = parseType();
			expectPunct(")", "after the type");
		}
		else if (acceptKeyword("sub"))
		{
			t->kind = TypeSyntax::Kind::Function;
			expectPunct("(", "in the sub type: sub(<types>) -> <type>");
			if (isKeyword("void") && isPunct(")", 1))
				next();
			while (!acceptPunct(")"))
			{
				if (acceptPunct("..."))
				{
					t->variadic = acceptVariadicTyped() ? 2 : 1;
					expectPunct(")", "after '...' (it ends the parameter types)");
					break;
				}
				t->params.push_back(parseType());
				if (!acceptPunct(","))
				{
					expectPunct(")", "after the parameter types");
					break;
				}
			}
			if (acceptPunct("->"))
				t->base = parseType();
		}
		else if (isKeyword("struct"))
		{
			// struct { fields }: a nameless struct, declared at the top level under a hidden name.
			Node* decl = make(NodeKind::StructDecl, next().pos);
			decl->name = "__anon_struct_" + std::to_string(++m_anonCount);
			decl->shortName = decl->name;
			decl->scope = m_scope;
			decl->namePos = decl->pos;	// 'struct'
			decl->value = 1;
			if (!isPunct("{"))
				error(peek().pos, "'{' expected after 'struct' in a type (struct { fields }), found " + describe(peek()));
			parseStructFields(decl);
			m_anonStructs.push_back(decl);
			t->name = decl->name;
			t->resolved = true;
		}
		else if (acceptKeyword("void"))
			t->name = "void";
		else if (peek().fixedType != nullptr)
		{
			// A template parameter in an instance: the type it stands for.
			t->fixed = peek().fixedType;
			t->name = next().text;
			t->resolved = true;
		}
		else
		{
			bool global = false;
			t->name = parseQualifiedName("(type)", global);
			t->written = (global ? "::" : "") + t->name;
			t->resolved = global;
			if (isPunct("<") && isTemplateName(t->name))
				parseTemplateArgs(t->args);
		}
		if (constTarget && !isPunct("*"))
			error(constPos, "const in a type is for what a pointer points at (const " + std::string(t->kind == TypeSyntax::Kind::Named ? t->name : "T") +
				"*): a variable, parameter or field that never changes is declared with const (const name: type)");
		bool firstPointer = true;
		while (true)
		{
			if (isPunct("*"))
			{
				TypeSyntax* p = new TypeSyntax();
				p->kind = TypeSyntax::Kind::Pointer;
				p->pos = next().pos;
				p->base = t;
				p->constTarget = constTarget && firstPointer;
				firstPointer = false;
				t = p;
			}
			else if (isPunct("["))
			{
				TypeSyntax* a = new TypeSyntax();
				a->kind = TypeSyntax::Kind::Array;
				a->pos = next().pos;
				a->base = t;
				if (!acceptPunct("]"))	// T[]: the size comes from the value
				{
					a->count = parseTernary();
					expectPunct("]", "after the array size");
				}
				t = a;
			}
			else
				break;
		}
		return t;
	}

	// ====================================== Statements ======================================
	Node* Parser::parseBlock(void)
	{
		Node* n = make(NodeKind::Block, peek().pos);
		expectPunct("{", "to start the block");
		pushScope();
		while (true)
		{
			const SourcePos at = peek().pos;
			if (acceptPunct("}"))
			{
				n->endPos = at;
				break;
			}
			if (peek().kind == TokenKind::End)
				error(n->pos, "'}' missing at the end of this block");
			n->list.push_back(parseStatement());
		}
		popScope();
		return n;
	}

	void Parser::pushScope(void)
	{
		m_scopes.emplace_back();
	}

	void Parser::popScope(void)
	{
		m_scopes.pop_back();
	}

	// atomic { body }: interrupts are disabled in the body, and put back as they were (not just
	// enabled: atomic blocks can be nested) on every way out of it:
	//   { var __atomic_N: u32;
	//     low { mov $__atomic_N, FL }   low { zflg 0 }          (InterruptsEnabled: bit 0 of FL)
	//     defer { the bit of __atomic_N back into FL }
	//     body }                                                (body->atomic: no int in it)
	// The CPU ignores every interrupt while they are disabled, int too.
	Node* Parser::atomicBlock(Node* body, const SourcePos& pos)
	{
		if (m_currentFunc != nullptr && (m_currentFunc->value & 2) != 0)
			error(pos, "atomic cannot be used in inline subs (they are copied into their callers: use a normal sub)");
		const std::string saved = "__atomic_" + std::to_string(m_atomicCount++);
		auto l_low = [&](const std::string& text, const SourcePos& at) {
			Node* low = make(NodeKind::LowBlock, at);
			low->text = text;
			return low;
		};
		Node* block = make(NodeKind::Block, pos);
		block->endPos = body->endPos;
		Node* var = make(NodeKind::VarDecl, pos);
		var->name = saved;
		var->namePos = pos;
		var->typeSyntax = new TypeSyntax();
		var->typeSyntax->pos = pos;
		var->typeSyntax->name = "u32";
		var->typeSyntax->resolved = true;
		block->list.push_back(var);
		block->list.push_back(l_low("mov $" + saved + ", FL", pos));
		block->list.push_back(l_low("zflg 0", pos));
		// FL = (FL & ~1) | (saved & 1), with interrupts still disabled until the last mov. Only the
		// low blocks' scratch registers (S1, S2: reg(0x07), reg(0x08)) and ACC (and / or write
		// their result there): a return value is waiting in a register while the defers run.
		Node* restore = make(NodeKind::Block, body->endPos);
		restore->endPos = body->endPos;
		for (const char* line : { "mov reg(0x08), FL", "and reg(0x08), 0xFFFFFFFE", "mov reg(0x08), ACC", "mov reg(0x07), $SAVED", "and reg(0x07), 1", "or ACC, reg(0x08)", "mov FL, ACC" })
		{
			std::string text = line;
			const size_t at = text.find("SAVED");
			if (at != std::string::npos)
				text.replace(at, 5, saved);
			restore->list.push_back(l_low(text, body->endPos));
		}
		Node* deferred = make(NodeKind::Defer, pos);
		deferred->a = restore;
		block->list.push_back(deferred);
		body->atomic = true;
		block->list.push_back(body);
		return block;
	}

	void Parser::declareLocal(const std::string& name, const std::string& hidden)
	{
		if (!m_scopes.empty())
			m_scopes.back()[name] = hidden;
	}

	// The name an identifier refers to in the current scopes (a local constant / static: its
	// hidden name).
	std::string Parser::localName(const std::string& name, const SourcePos& pos, bool* found)
	{
		for (int i = (int)m_scopes.size() - 1; i >= 0; i--)
		{
			auto it = m_scopes[i].find(name);
			if (it == m_scopes[i].end())
				continue;
			if (found != nullptr)
				*found = true;
			if (!it->second.empty())
				return it->second;
			if (i < m_hoistScope && m_probeLocals)
			{
				m_usedLocal = true;		// (a constant variable of the sub)
				return name;
			}
			if (i < m_hoistScope)
				error(pos, "'" + name + "' is a local variable: the value of a local constant or static variable is set once "
					"(not at every call), so it cannot use the sub's locals or parameters");
			return name;
		}
		return name;
	}

	// $name in a low block, for a local constant / static: its hidden name.
	std::string Parser::renameLow(const std::string& text)
	{
		if (m_scopes.empty())
			return text;
		std::string out;
		size_t i = 0;
		while (i < text.size())
		{
			if (text[i] != '$')
			{
				out += text[i++];
				continue;
			}
			size_t j = i + 1;
			while (j < text.size() && (std::isalnum((unsigned char)text[j]) || text[j] == '_'))
				j++;
			const std::string name = text.substr(i + 1, j - i - 1);
			std::string renamed = name;
			for (int k = (int)m_scopes.size() - 1; k >= 0; k--)
			{
				auto it = m_scopes[k].find(name);
				if (it != m_scopes[k].end())
				{
					if (!it->second.empty())
						renamed = it->second;
					break;
				}
			}
			out += "$" + renamed;
			i = j;
		}
		return out;
	}

	// const NAME ... ; / static var NAME ... ; / static const NAME ... ; in a sub: a hidden
	// top-level declaration, and an empty statement in its place.
	// A const in a sub (not static) whose value uses the sub's variables or parameters (or is built
	// by a constructor): a constant variable of the sub, in its place. The others are hidden at the
	// top level too, with a placeholder in their place: if the value is not a compile-time
	// constant, it is computed there, every time (the code generator decides).
	Node* Parser::parseLocalStatic(const SourcePos& pos, bool isStatic)
	{
		const int outerHoist = m_hoistScope;
		m_hoistScope = (int)m_scopes.size();
		const bool localConst = !isStatic && isKeyword("const");
		Node* d = nullptr;
		if (isKeyword("const") || isKeyword("constexpr"))
		{
			const bool outerProbe = m_probeLocals, outerUsed = m_usedLocal;
			m_probeLocals = localConst;
			m_usedLocal = false;
			d = parseConst();
			const bool used = m_usedLocal;
			m_probeLocals = outerProbe;
			m_usedLocal = outerUsed;
			if (localConst && (used || d->kind == NodeKind::VarDecl))
			{
				m_hoistScope = outerHoist;
				d->kind = NodeKind::VarDecl;
				d->constKind = 1;
				declareLocal(d->name);
				return d;
			}
		}
		else
		{
			d = parseVarDecl();
			expectPunct(";", "after the variable declaration");
		}
		m_hoistScope = outerHoist;
		d->hoistKind = localConst ? 1 : 2;
		const std::string base = m_currentFunc->name + "::" + d->name;
		const int n = ++m_hoistedCount[base];
		d->text = d->name;
		d->shortName = d->name;
		d->scope = m_scope;
		d->owner = m_currentFunc;
		const std::string hidden = (n == 1 ? base : base + "::" + std::to_string(n));
		declareLocal(d->name, hidden);
		d->name = hidden;
		m_hoisted.push_back(d);
		if (!localConst)
			return make(NodeKind::Block, pos);
		Node* place = make(NodeKind::VarDecl, pos);
		place->name = hidden;
		place->namePos = d->namePos;
		place->typeSyntax = d->typeSyntax;
		place->a = d->a;
		place->constKind = 1;
		place->hoisted = d;
		return place;
	}

	void Parser::requireBlockFor(const Node* body, const std::string& what)
	{
		if (body != nullptr && body->kind == NodeKind::Defer)
			error(body->pos, "a defer runs when its block is left: put it in a block ({ defer ...; }), not directly after " + what);
	}

	std::string Parser::tokenText(size_t from, size_t to) const
	{
		// Spaces between the tokens, except around the ones that are written without: calls,
		// indexing, members, unary operators and the types in cast<...> / sizeof<...>.
		static const std::set<std::string> noSpaceAfter = { "(", "[", ".", "->", "::", "!", "~" };
		static const std::set<std::string> noSpaceBefore = { ")", "]", ",", ".", "->", "::", "[", "++", "--" };
		auto l_is = [](const Token* t, TokenKind kind, const char* text) { return t && t->kind == kind && t->text == text; };
		auto l_endsValue = [](const Token* t) {	// a token after which an operator is binary
			return t && (t->kind == TokenKind::Identifier || t->kind == TokenKind::Integer || t->kind == TokenKind::Float || t->kind == TokenKind::String ||
				(t->kind == TokenKind::Punct && (t->text == ")" || t->text == "]" || t->text == "++" || t->text == "--")) ||
				(t->kind == TokenKind::Keyword && (t->text == "true" || t->text == "false" || t->text == "null")));
		};
		std::string s;
		const Token* prev = nullptr;
		const Token* prev2 = nullptr;
		int typeDepth = 0;		// inside cast<...> / sizeof<...>
		bool afterType = false;	// just closed one (the ( that follows is tight)
		for (size_t i = from; i < to && i < m_tokens.size(); i++)
		{
			const Token& t = m_tokens[i];
			std::string text = t.text;
			if (t.kind == TokenKind::Integer)
			{
				if (t.isChar)
					text = (t.value >= 0x20 && t.value < 0x7F) ? std::string("'") + (char)t.value + "'" : std::to_string(t.value);
				else if (std::to_string(t.value) != t.text)
				{
					char hex[16];
					std::snprintf(hex, sizeof(hex), "0x%llX", (unsigned long long)t.value);
					text = hex;
				}
			}
			else if (t.kind == TokenKind::String)
				text = "\"" + t.text + "\"";
			bool tight = (prev == nullptr);
			if (prev != nullptr)
			{
				const bool unaryPrev = prev->kind == TokenKind::Punct && (prev->text == "-" || prev->text == "*" || prev->text == "&" ||
					prev->text == "+") && !l_endsValue(prev2);
				tight = (prev->kind == TokenKind::Punct && noSpaceAfter.count(prev->text) > 0) ||
					(t.kind == TokenKind::Punct && noSpaceBefore.count(t.text) > 0) || unaryPrev || typeDepth > 0 ||
					(l_is(&t, TokenKind::Punct, "(") && (l_endsValue(prev) || afterType ||
						l_is(prev, TokenKind::Keyword, "sizeof"))) ||
					(l_is(&t, TokenKind::Punct, "<") && (l_is(prev, TokenKind::Keyword, "cast") || l_is(prev, TokenKind::Keyword, "sizeof")));
			}
			afterType = false;
			if (l_is(&t, TokenKind::Punct, "<") && (l_is(prev, TokenKind::Keyword, "cast") || l_is(prev, TokenKind::Keyword, "sizeof")))
				typeDepth++;
			else if (typeDepth > 0 && l_is(&t, TokenKind::Punct, "<"))
				typeDepth++;
			else if (typeDepth > 0 && (l_is(&t, TokenKind::Punct, ">") || l_is(&t, TokenKind::Punct, ">>")))
			{
				typeDepth -= (t.text == ">>") ? 2 : 1;
				if (typeDepth <= 0)
				{
					typeDepth = 0;
					afterType = true;
				}
			}
			if (!tight)
				s += " ";
			s += text;
			prev2 = prev;
			prev = &t;
		}
		return s;
	}

	Node* Parser::parseStatement(void)
	{
		const SourcePos pos = peek().pos;
		if (isPunct("{"))
			return parseBlock();
		if (acceptPunct(";"))
			return make(NodeKind::Block, pos);
		if (isKeyword("var") || isKeyword("auto"))
		{
			Node* v = parseVarDecl();
			expectPunct(";", "after the variable declaration");
			declareLocal(v->name);
			return v;
		}
		if (isKeyword("const") || isKeyword("constexpr"))
			return parseLocalStatic(pos, false);
		if (isKeyword("constinit"))
			error(peek().pos, "constinit is for globals (a local gets its value every time its declaration runs)");
		if (acceptKeyword("static"))
		{
			if (!isKeyword("var") && !isKeyword("auto") && !isKeyword("const") && !isKeyword("constexpr"))
				error(peek().pos, "'var', 'auto', 'const' or 'constexpr' expected after 'static', found " + describe(peek()));
			return parseLocalStatic(pos, true);
		}
		if (isKeyword("low"))
			return parseLow();
		if (isKeyword("static_assert"))
			return parseStaticAssert();
		if (acceptKeyword("assert"))
		{
			Node* n = make(NodeKind::Assert, pos);
			expectPunct("(", "after 'assert'");
			const size_t from = m_pos;
			n->a = parseExpression();
			n->text = tokenText(from, m_pos);
			if (acceptPunct(","))
				n->b = parseExpression();
			expectPunct(")", "after assert(condition[, message])");
			expectPunct(";", "after assert(...)");
			return n;
		}
		if (acceptKeyword("atomic"))
		{
			if (!isPunct("{"))
				error(peek().pos, "'{' expected after 'atomic' (atomic { ... })");
			return atomicBlock(parseBlock(), pos);
		}
		if (acceptKeyword("defer"))
		{
			// defer statement: run when the block it is in is left (in the reverse order).
			Node* n = make(NodeKind::Defer, pos);
			if (isKeyword("var") || isKeyword("auto"))
				error(peek().pos, "a deferred variable would not be usable: defer a statement or a block");
			n->a = parseStatement();
			requireBlockFor(n->a, "defer");
			return n;
		}
		if (acceptKeyword("debug_print"))
		{
			Node* n = make(NodeKind::DebugPrint, pos);
			expectPunct("(", "after 'debug_print'");
			n->a = parseExpression();
			expectPunct(")", "after debug_print(string)");
			expectPunct(";", "after debug_print(...)");
			return n;
		}
		if (acceptKeyword("if"))
		{
			Node* n = make(NodeKind::If, pos);
			// if constexpr (condition): decided while compiling (only the branch it chooses is
			// generated; its built-ins: see ConditionBuiltins.hpp).
			if (isKeyword("constexpr") && isPunct("(", 1))
			{
				next();
				n->isConstexpr = true;
			}
			expectPunct("(", "after 'if'");
			n->a = n->isConstexpr ? parseCondition() : parseExpression();
			expectPunct(")", "after the condition");
			n->b = parseStatement();
			requireBlockFor(n->b, "if");
			if (acceptKeyword("else"))
			{
				n->c = parseStatement();
				requireBlockFor(n->c, "else");
			}
			return n;
		}
		if (acceptKeyword("while"))
		{
			Node* n = make(NodeKind::While, pos);
			expectPunct("(", "after 'while'");
			n->a = parseExpression();
			expectPunct(")", "after the condition");
			n->b = parseStatement();
			requireBlockFor(n->b, "while");
			return n;
		}
		if (acceptKeyword("do"))
		{
			Node* n = make(NodeKind::DoWhile, pos);
			n->b = parseStatement();
			requireBlockFor(n->b, "do");
			expectKeyword("while", "after the do body");
			expectPunct("(", "after 'while'");
			n->a = parseExpression();
			expectPunct(")", "after the condition");
			expectPunct(";", "after do ... while (...)");
			return n;
		}
		if (acceptKeyword("for"))
		{
			expectPunct("(", "after 'for'");
			pushScope();
			if (isForIn())
			{
				Node* loop = parseForIn(pos);
				popScope();
				return loop;
			}
			Node* n = make(NodeKind::For, pos);
			if (isKeyword("var") || isKeyword("auto"))
			{
				n->a = parseVarDecl();
				declareLocal(n->a->name);
			}
			else if (!isPunct(";"))
			{
				n->a = make(NodeKind::ExprStmt, peek().pos);
				n->a->a = parseExpression();
			}
			expectPunct(";", "after the for initialization");
			if (!isPunct(";"))
				n->b = parseExpression();
			expectPunct(";", "after the for condition");
			if (!isPunct(")"))
				n->c = parseExpression();
			expectPunct(")", "after the for step");
			n->d = parseStatement();
			requireBlockFor(n->d, "for");
			popScope();
			return n;
		}
		if (acceptKeyword("return"))
		{
			Node* n = make(NodeKind::Return, pos);
			if (!isPunct(";"))
				n->a = parseExpression();
			expectPunct(";", "after return");
			return n;
		}
		if (acceptKeyword("break"))
		{
			expectPunct(";", "after break");
			return make(NodeKind::Break, pos);
		}
		if (acceptKeyword("continue"))
		{
			expectPunct(";", "after continue");
			return make(NodeKind::Continue, pos);
		}
		if ((isKeyword("sub") && peek(1).kind == TokenKind::Identifier) || isKeyword("inline") || isKeyword("struct") || isKeyword("enum") || isKeyword("namespace"))
			error(pos, "'" + peek().text + "' declarations are only allowed at the top level");
		Node* n = make(NodeKind::ExprStmt, pos);
		n->a = parseExpression();
		expectPunct(";", "after the statement");
		return n;
	}

	// ===================================== Expressions ======================================
	Node* Parser::parseExpression(void)
	{
		return parseAssignment();
	}

	Node* Parser::parseAssignment(void)
	{
		Node* left = parseTernary();
		static const char* ops[] = { "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", nullptr };
		for (int i = 0; ops[i] != nullptr; i++)
		{
			if (isPunct(ops[i]))
			{
				Node* n = make(NodeKind::Assign, next().pos);
				n->name = ops[i];
				n->a = left;
				n->b = parseAssignment();
				return n;
			}
		}
		return left;
	}

	Node* Parser::parseTernary(void)
	{
		Node* cond = parseBinary(1);
		if (!isPunct("?"))
			return cond;
		Node* n = make(NodeKind::Ternary, next().pos);
		n->a = cond;
		n->b = parseAssignment();
		expectPunct(":", "in the ?: expression");
		n->c = parseTernary();
		return n;
	}

	static int binaryPrecedence(const Token& t, std::string& op)
	{
		if (t.kind != TokenKind::Punct && t.kind != TokenKind::Keyword)
			return -1;
		op = t.text;
		if (op == "or") op = "||";
		if (op == "and") op = "&&";
		if (op == "||") return 1;
		if (op == "&&") return 2;
		if (op == "|") return 3;
		if (op == "^") return 4;
		if (op == "&") return 5;
		if (op == "==" || op == "!=") return 6;
		if (op == "<" || op == "<=" || op == ">" || op == ">=") return 7;
		if (op == "<<" || op == ">>") return 8;
		if (op == "+" || op == "-") return 9;
		if (op == "*" || op == "/" || op == "%") return 10;
		return -1;
	}

	Node* Parser::parseBinary(int minPrec)
	{
		Node* left = parseUnary();
		while (true)
		{
			std::string op;
			int prec = binaryPrecedence(peek(), op);
			if (prec < minPrec)
				return left;
			Node* n = make(NodeKind::Binary, next().pos);
			n->name = op;
			n->a = left;
			n->b = parseBinary(prec + 1);
			left = n;
		}
	}

	Node* Parser::parseUnary(void)
	{
		const SourcePos pos = peek().pos;
		if (isPunct("-") || isPunct("!") || isPunct("~") || isPunct("*") || isPunct("&") || isPunct("+") || isKeyword("not"))
		{
			Node* n = make(NodeKind::Unary, pos);
			n->name = next().text;
			if (n->name == "not") n->name = "!";
			n->a = parseUnary();
			if (n->name == "+")
				return n->a;
			return n;
		}
		if (acceptPunct("++"))
		{
			Node* n = make(NodeKind::PreInc, pos);
			n->a = parseUnary();
			return n;
		}
		if (acceptPunct("--"))
		{
			Node* n = make(NodeKind::PreDec, pos);
			n->a = parseUnary();
			return n;
		}
		if (acceptKeyword("cast"))
		{
			Node* n = make(NodeKind::Cast, pos);
			expectPunct("<", "after 'cast' (cast<type>(value))");
			n->typeSyntax = parseType();
			expectPunct(">", "after the cast type");
			expectPunct("(", "after cast<type>");
			n->a = parseExpression();
			expectPunct(")", "after the cast value");
			return parsePostfix(n);
		}
		if (acceptKeyword("sizeof"))
		{
			if (acceptPunct("<"))
			{
				Node* n = make(NodeKind::SizeofType, pos);
				n->typeSyntax = parseType();
				expectPunct(">", "after the sizeof type");
				return n;
			}
			expectPunct("(", "after 'sizeof' (sizeof(value or type) or sizeof<type>)");
			Node* n = nullptr;
			if (startsType() || pointerTypeAhead())
			{
				n = make(NodeKind::SizeofType, pos);
				n->typeSyntax = parseType();
			}
			else
			{
				// sizeof(name): a variable or a type (decided by the code generator).
				n = make(NodeKind::SizeofExpr, pos);
				n->a = parseExpression();
			}
			expectPunct(")", "after sizeof(...)");
			return n;
		}
		return parsePostfix(parsePrimary());
	}

	Node* Parser::parsePostfix(Node* expr)
	{
		while (true)
		{
			const SourcePos pos = peek().pos;
			if (acceptPunct("("))
			{
				Node* n = make(NodeKind::Call, pos);
				n->a = expr;
				while (!acceptPunct(")"))
				{
					n->list.push_back(parseAssignment());
					if (!acceptPunct(","))
					{
						expectPunct(")", "after the arguments");
						break;
					}
				}
				expr = n;
			}
			else if (acceptPunct("["))
			{
				Node* n = make(NodeKind::Index, pos);
				n->a = expr;
				n->b = parseExpression();
				expectPunct("]", "after the index");
				expr = n;
			}
			else if (isPunct(".") || isPunct("->"))
			{
				Node* n = make(NodeKind::Member, pos);
				n->value = (next().text == "->") ? 1 : 0;
				n->a = expr;
				n->name = expectIdentifier("(member name)");
				n->namePos = lastPos();
				if (isPunct("<") && isTemplateName(n->name))
					parseTemplateArgs(n->templateArgs);		// x.map<f32>(...)
				expr = n;
			}
			else if (acceptPunct("++"))
			{
				Node* n = make(NodeKind::PostInc, pos);
				n->a = expr;
				expr = n;
			}
			else if (acceptPunct("--"))
			{
				Node* n = make(NodeKind::PostDec, pos);
				n->a = expr;
				expr = n;
			}
			else
				return expr;
		}
	}

	Node* Parser::parsePrimary(void)
	{
		const Token& t = peek();
		if (t.kind == TokenKind::Integer)
		{
			Node* n = make(NodeKind::IntLit, t.pos);
			n->value = t.value;
			n->name = t.isChar ? "char" : t.isUnsigned ? "u" : "";
			next();
			return n;
		}
		if (t.kind == TokenKind::Float)
		{
			Node* n = make(NodeKind::FloatLit, t.pos);
			n->value = t.value;		// the f32's bits
			n->text = t.text;
			next();
			return n;
		}
		if (t.kind == TokenKind::String)
		{
			Node* n = make(NodeKind::StrLit, t.pos);
			n->text = t.text;
			next();
			// "abc" "def": concatenated.
			while (peek().kind == TokenKind::String)
				n->text += next().text;
			return n;
		}
		if (t.kind == TokenKind::Identifier && t.fixedType != nullptr)
		{
			// A template parameter as a value in an instance (T.make(): a static sub of the struct
			// it stands for): the type, in typeSyntax.
			Node* n = make(NodeKind::Ident, t.pos);
			n->name = t.text;
			n->resolved = true;
			n->typeSyntax = new TypeSyntax();
			n->typeSyntax->pos = t.pos;
			n->typeSyntax->name = t.text;
			n->typeSyntax->fixed = t.fixedType;
			n->typeSyntax->resolved = true;
			next();
			return n;
		}
		if (m_condition && t.kind == TokenKind::Identifier && isPunct("(", 1))
		{
			if (const ConditionBuiltin* b = findConditionBuiltin(t.text))
			{
				// A built-in of a condition: its arguments are types (T: parsed as types) or names.
				Node* n = make(NodeKind::Call, t.pos);
				n->a = make(NodeKind::Ident, t.pos);
				n->a->name = t.text;
				n->a->resolved = true;
				next();
				next();		// (
				const std::string spec = b->args;
				size_t k = 0;
				while (!isPunct(")"))
				{
					if (k >= spec.size() || spec[k] == '*' || spec[k] == '?')
						error(peek().pos, std::string(b->name) + " takes " + b->help);
					const char kind = (k + 1 < spec.size() && spec[k + 1] == '*') ? spec[k] : spec[k];
					if (kind == 'T')
					{
						Node* arg = make(NodeKind::TypeArg, peek().pos);
						arg->typeSyntax = parseType();
						n->list.push_back(arg);
					}
					else if (peek().kind == TokenKind::String)
					{
						Node* arg = make(NodeKind::StrLit, peek().pos);
						arg->text = next().text;
						n->list.push_back(arg);
					}
					else
					{
						Node* arg = make(NodeKind::StrLit, peek().pos);
						arg->text = expectIdentifier("(a name: " + std::string(b->name) + " takes " + b->help + ")");
						n->list.push_back(arg);
					}
					if (!(k + 1 < spec.size() && spec[k + 1] == '*'))
						k += (k + 1 < spec.size() && spec[k + 1] == '?') ? 2 : 1;
					if (!acceptPunct(","))
						break;
				}
				expectPunct(")", std::string("after the arguments of ") + b->name);
				// (the count: checked when it is evaluated)
				return n;
			}
		}
		if (t.kind == TokenKind::Identifier || (isPunct("::") && peek(1).kind == TokenKind::Identifier))
		{
			Node* n = make(NodeKind::Ident, t.pos);
			if (isPunct("::") || isPunct("::", 1))
			{
				// A::B::name (a namespace's, a struct's static sub / nested type), ::name (global).
				bool global = false;
				n->name = parseQualifiedName("after '::'", global);
				n->resolved = global;
				if (global)
					n->text = "::" + n->name;
				if (isPunct("<") && isTemplateName(n->name))
					parseTemplateArgs(n->templateArgs);
				return n;
			}
			bool local = false;
			n->name = localName(t.text, t.pos, &local);
			n->resolved = local;
			if (n->name != t.text)
				n->text = t.text;
			next();
			if (!local && isPunct("<") && isTemplateName(n->name))
				parseTemplateArgs(n->templateArgs);		// max<u32>(...), Array<u32>.make()
			return n;
		}
		if (isKeyword("true") || isKeyword("false"))
		{
			Node* n = make(NodeKind::BoolLit, t.pos);
			n->value = (t.text == "true") ? 1 : 0;
			next();
			return n;
		}
		if (acceptKeyword("null"))
			return make(NodeKind::Null, t.pos);
		if (isKeyword("sub") || (isKeyword("interrupt") && isKeyword("sub", 1)))
			return parseLambda();
		if (acceptPunct("("))
		{
			Node* n = parseExpression();
			expectPunct(")", "after the expression");
			return n;
		}
		error(t.pos, "expression expected, found " + describe(t));
	}
}
