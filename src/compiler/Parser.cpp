#include "Parser.hpp"

// Node fields by kind (see AST.hpp):
//   Program:     list = declarations
//   StructDecl:  name, list = fields (VarDecl), value = 1 if nameless (struct { ... } as a type,
//                named __anon_struct_<n> and added at the end of the program)
//   EnumDecl:    name, list = members (EnumMember: name, a = value or nullptr)
//   ConstDecl:   name, typeSyntax (optional), a = value (struct constants: an InitList, or nullptr
//                for the fields' defaults)
//   AliasDecl:   name, typeSyntax
//   VarDecl:     name, typeSyntax (optional if initialized), a = initializer (expression / InitList)
//   FuncDecl:    name, list = params (VarDecl, a = default value), typeSyntax = return type
//                (nullptr: void), a = body (nullptr: declaration only), value = 1 if interrupt
//   Block:       list = statements
//   If:          a = condition, b = then, c = else
//   While:       a = condition, b = body       DoWhile: a = condition, b = body
//   For:         a = init (VarDecl / ExprStmt), b = condition, c = step, d = body (all optional but d)
//   Return:      a = value       ExprStmt: a       LowBlock: text
//   Unary:       name = operator, a          Binary / Assign: name = operator, a, b
//   Ternary:     a ? b : c                   Call: a = callee, list = arguments
//   Index:       a[b]                        Member: a.name / a->name (value = 1)
//   Cast:        typeSyntax, a               SizeofType: typeSyntax    SizeofExpr: a
//   Pre/PostInc/Dec: a                       InitList: list

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

	Node* Parser::make(NodeKind kind, const SourcePos& pos)
	{
		Node* n = new Node();
		n->kind = kind;
		n->pos = pos;
		return n;
	}

	// ===================================== Declarations =====================================
	Node* Parser::parseProgram(void)
	{
		Node* program = make(NodeKind::Program, peek().pos);
		while (peek().kind != TokenKind::End)
			program->list.push_back(parseDeclaration());
		for (Node* s : m_anonStructs)
			program->list.push_back(s);
		m_anonStructs.clear();
		for (Node* l : m_lambdas)
			program->list.push_back(l);
		m_lambdas.clear();
		return program;
	}

	Node* Parser::parseDeclaration(void)
	{
		if (isKeyword("struct")) return parseStruct();
		if (isKeyword("enum")) return parseEnum();
		if (isKeyword("const")) return parseConst();
		if (isKeyword("alias")) return parseAlias();
		if (isKeyword("low")) return parseLow();
		if (isKeyword("var"))
		{
			Node* v = parseVarDecl();
			expectPunct(";", "after the variable declaration");
			return v;
		}
		if (isKeyword("sub")) return parseFunction(0);
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
		error(peek().pos, "declaration expected (var, sub, inline sub, struct, enum, const, alias or low), found " + describe(peek()));
	}

	Node* Parser::parseStruct(void)
	{
		Node* n = make(NodeKind::StructDecl, next().pos);
		n->name = expectIdentifier("after 'struct'");
		parseStructFields(n);
		acceptPunct(";");
		return n;
	}

	void Parser::parseStructFields(Node* decl)
	{
		expectPunct("{", "to start the struct");
		while (!acceptPunct("}"))
		{
			if (!isKeyword("var"))
				error(peek().pos, "struct fields are declared with 'var', found " + describe(peek()));
			Node* field = parseVarDecl(false);
			expectPunct(";", "after the struct field");
			decl->list.push_back(field);
		}
	}

	Node* Parser::parseEnum(void)
	{
		Node* n = make(NodeKind::EnumDecl, next().pos);
		n->name = expectIdentifier("after 'enum'");
		expectPunct("{", "to start the enum");
		while (!acceptPunct("}"))
		{
			Node* m = make(NodeKind::EnumMember, peek().pos);
			m->name = expectIdentifier("(enum member)");
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
		if (acceptPunct(":"))
			n->typeSyntax = parseType();
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
	Node* Parser::parseFunction(int flags)
	{
		Node* n = make(NodeKind::FuncDecl, next().pos);
		n->value = flags;
		n->name = expectIdentifier("after 'sub'");
		expectPunct("(", "after the sub name");
		parseParams(n);
		if (acceptPunct("->"))
			n->typeSyntax = parseType();
		if (acceptPunct(";"))
			return n;
		Node* outer = m_currentFunc;
		m_currentFunc = n;
		n->a = parseBlock();
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
		expectPunct("(", "after 'sub' (a lambda: sub(var x: i32) -> i32 { ... })");
		parseParams(n);
		if (acceptPunct("->"))
			n->typeSyntax = parseType();
		if (!isPunct("{"))
			error(peek().pos, "'{' expected: a lambda needs a body (sub(...) -> type { ... })");
		n->d = m_currentFunc;
		Node* outer = m_currentFunc;
		m_currentFunc = n;
		n->a = parseBlock();
		m_currentFunc = outer;
		m_lambdas.push_back(n);
		Node* id = make(NodeKind::Ident, n->pos);
		id->name = n->name;
		return id;
	}

	Node* Parser::parseLow(void)
	{
		Node* n = make(NodeKind::LowBlock, next().pos);
		if (peek().kind != TokenKind::LowBlock)
			error(peek().pos, "'{' expected after 'low'");
		const Token& t = next();
		n->text = t.text;
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
			decl->value = 1;
			if (!isPunct("{"))
				error(peek().pos, "'{' expected after 'struct' in a type (struct { fields }), found " + describe(peek()));
			parseStructFields(decl);
			m_anonStructs.push_back(decl);
			t->name = decl->name;
		}
		else if (acceptKeyword("void"))
			t->name = "void";
		else
			t->name = expectIdentifier("(type)");
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
		while (!acceptPunct("}"))
		{
			if (peek().kind == TokenKind::End)
				error(n->pos, "'}' missing at the end of this block");
			n->list.push_back(parseStatement());
		}
		return n;
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
			return v;
		}
		if (isKeyword("low"))
			return parseLow();
		if (acceptKeyword("if"))
		{
			Node* n = make(NodeKind::If, pos);
			expectPunct("(", "after 'if'");
			n->a = parseExpression();
			expectPunct(")", "after the condition");
			n->b = parseStatement();
			if (acceptKeyword("else"))
				n->c = parseStatement();
			return n;
		}
		if (acceptKeyword("while"))
		{
			Node* n = make(NodeKind::While, pos);
			expectPunct("(", "after 'while'");
			n->a = parseExpression();
			expectPunct(")", "after the condition");
			n->b = parseStatement();
			return n;
		}
		if (acceptKeyword("do"))
		{
			Node* n = make(NodeKind::DoWhile, pos);
			n->b = parseStatement();
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
			if (isKeyword("var"))
				n->a = parseVarDecl();
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
		if ((isKeyword("sub") && peek(1).kind == TokenKind::Identifier) || isKeyword("inline") || isKeyword("struct") || isKeyword("enum"))
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
		if (t.kind == TokenKind::Identifier)
		{
			Node* n = make(NodeKind::Ident, t.pos);
			n->name = t.text;
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
