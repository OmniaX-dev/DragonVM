#include "Parser.hpp"
#include <cstdio>
#include <set>

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
	Parser::Parser(const std::vector<Token>& tokens) : m_tokens(tokens)
	{
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
		if (isKeyword("struct")) return parseStruct();
		if (isKeyword("enum")) return parseEnum();
		if (isKeyword("const"))
		{
			Node* c = parseConst();
			nameDeclaration(c);
			return c;
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
		if (isKeyword("var"))
		{
			Node* v = parseVarDecl();
			expectPunct(";", "after the variable declaration");
			nameDeclaration(v);
			return v;
		}
		if (isKeyword("sub")) return parseFunction(0);
		if (isKeyword("static"))
			error(peek().pos, "'static' is for the local variables and constants of a sub (a global already exists for the whole program)");
		if (isKeyword("priv"))
			error(peek().pos, "'priv' is for the fields and subs of a struct");
		if (acceptKeyword("interrupt"))
		{
			if (!isKeyword("sub"))
				error(peek().pos, "'sub' expected after 'interrupt'");
			return parseFunction(1);
		}
		if (acceptKeyword("inline"))
		{
			if (isKeyword("interrupt"))
				error(peek().pos, "interrupt subs cannot be inline (they are entered by an interrupt, not called)");
			if (!isKeyword("sub"))
				error(peek().pos, "'sub' expected after 'inline'");
			return parseFunction(2);
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

	void Parser::parseStructFields(Node* decl)
	{
		expectPunct("{", "to start the struct");
		// The names in a named struct are looked up from it first (its nested types).
		const std::string outerScope = m_scope;
		if (decl->value == 0)
			m_scope = decl->name;
		while (!acceptPunct("}"))
		{
			// priv var / priv sub: only the struct's own subs can use it.
			const bool priv = acceptKeyword("priv");
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
				continue;
			}
			if (isKeyword("static") && isKeyword("var", 1))
				error(peek(1).pos, "structs have no static fields: use a global (or a constant) next to the struct");
			if (isKeyword("sub") || isKeyword("inline") || isKeyword("static") || isKeyword("priv"))
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
					else
						break;
				}
				if (!isKeyword("sub"))
					error(peek().pos, "'sub' expected after '" + m_tokens[m_pos - 1].text + "', found " + describe(peek()));
				Node* outerFunc = m_currentFunc;
				auto outerScopes = std::move(m_scopes);
				m_scopes.clear();
				m_lambdas.push_back(parseFunction(flags, decl));
				m_scopes = std::move(outerScopes);
				m_currentFunc = outerFunc;
				continue;
			}
			if (isKeyword("interrupt"))
				error(peek().pos, "interrupt subs cannot be methods (they take no parameters)");
			if (!isKeyword("var"))
				error(peek().pos, "struct fields are declared with 'var' (methods with 'sub'), found " + describe(peek()));
			Node* field = parseVarDecl(false);
			field->isPrivate = priv;
			field->shortName = field->name;
			field->scope = m_scope;
			expectPunct(";", "after the struct field");
			decl->list.push_back(field);
		}
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

	Node* Parser::parseConst(void)
	{
		Node* n = make(NodeKind::ConstDecl, next().pos);
		n->name = expectIdentifier("after 'const'");
		n->namePos = lastPos();
		if (acceptPunct(":"))
			n->typeSyntax = parseType();
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

	Node* Parser::parseVarDecl(bool allowNoType)
	{
		Node* n = make(NodeKind::VarDecl, peek().pos);
		expectKeyword("var", "");
		n->name = expectIdentifier("after 'var'");
		n->namePos = lastPos();
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
		if (n->typeSyntax == nullptr && (n->a == nullptr || !allowNoType || n->a->kind == NodeKind::InitList))
			error(n->pos, "the type of '" + n->name + "' is needed (var " + n->name + ": <type>)");
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

	// FuncDecl flags (value): 1 interrupt, 2 inline.
	// FuncDecl flags (value): 1 interrupt, 2 inline, 4 method, 8 static method, 16 priv. owner:
	// the struct of a method.
	Node* Parser::parseFunction(int flags, const Node* owner)
	{
		Node* n = make(NodeKind::FuncDecl, next().pos);
		n->value = flags | (owner != nullptr ? 4 : 0);
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
		if ((flags & 2) != 0)
			n->bodyTokens.assign(m_tokens.begin() + (std::ptrdiff_t)bodyStart, m_tokens.begin() + (std::ptrdiff_t)m_pos);
		popScope();
		m_currentFunc = outer;
		return n;
	}

	void Parser::parseParams(Node* n)
	{
		if (isKeyword("void") && isPunct(")", 1))
			next();
		while (!acceptPunct(")"))
		{
			Node* p = make(NodeKind::VarDecl, peek().pos);
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
	bool Parser::startsType(void) const
	{
		const Token& t = peek();
		if (t.kind == TokenKind::Keyword)
			return t.text == "void" || t.text == "sub" || t.text == "struct";
		if (t.kind != TokenKind::Identifier)
			return false;
		static const char* names[] = { "i8", "u8", "i16", "u16", "i32", "u32", "bool", "char", "string", "int", "uint", nullptr };
		for (int i = 0; names[i] != nullptr; i++)
		{
			if (t.text == names[i])
				return true;
		}
		return false;
	}

	TypeSyntax* Parser::parseType(void)
	{
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
		else
		{
			bool global = false;
			t->name = parseQualifiedName("(type)", global);
			t->written = (global ? "::" : "") + t->name;
			t->resolved = global;
		}
		while (true)
		{
			if (isPunct("*"))
			{
				TypeSyntax* p = new TypeSyntax();
				p->kind = TypeSyntax::Kind::Pointer;
				p->pos = next().pos;
				p->base = t;
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
	Node* Parser::parseLocalStatic(const SourcePos& pos)
	{
		const int outerHoist = m_hoistScope;
		m_hoistScope = (int)m_scopes.size();
		Node* d = nullptr;
		if (isKeyword("const"))
			d = parseConst();
		else
		{
			d = parseVarDecl();
			expectPunct(";", "after the variable declaration");
		}
		m_hoistScope = outerHoist;
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
		return make(NodeKind::Block, pos);
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
			return t && (t->kind == TokenKind::Identifier || t->kind == TokenKind::Integer || t->kind == TokenKind::String ||
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
		if (isKeyword("var"))
		{
			Node* v = parseVarDecl();
			expectPunct(";", "after the variable declaration");
			declareLocal(v->name);
			return v;
		}
		if (isKeyword("const"))
			return parseLocalStatic(pos);
		if (acceptKeyword("static"))
		{
			if (!isKeyword("var") && !isKeyword("const"))
				error(peek().pos, "'var' or 'const' expected after 'static', found " + describe(peek()));
			return parseLocalStatic(pos);
		}
		if (isKeyword("low"))
			return parseLow();
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
		if (acceptKeyword("defer"))
		{
			// defer statement: run when the block it is in is left (in the reverse order).
			Node* n = make(NodeKind::Defer, pos);
			if (isKeyword("var"))
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
			expectPunct("(", "after 'if'");
			n->a = parseExpression();
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
			Node* n = make(NodeKind::For, pos);
			expectPunct("(", "after 'for'");
			pushScope();
			if (isKeyword("var"))
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
			if (startsType())
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
			n->name = t.isChar ? "char" : "";
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
				return n;
			}
			bool local = false;
			n->name = localName(t.text, t.pos, &local);
			n->resolved = local;
			if (n->name != t.text)
				n->text = t.text;
			next();
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
