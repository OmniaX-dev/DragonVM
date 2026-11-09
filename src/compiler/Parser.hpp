#pragma once

#include "Lexer.hpp"
#include "AST.hpp"
#include <map>

namespace dscript
{
	class Parser
	{
		public:
			Parser(const std::vector<Token>& tokens);
			Node* parseProgram(void);

		private:
			const Token& peek(int offset = 0) const;
			const Token& next(void);
			bool isPunct(const std::string& p, int offset = 0) const;
			bool isKeyword(const std::string& k, int offset = 0) const;
			bool acceptPunct(const std::string& p);
			bool acceptKeyword(const std::string& k);
			const Token& expectPunct(const std::string& p, const std::string& context);
			const Token& expectKeyword(const std::string& k, const std::string& context);
			std::string expectIdentifier(const std::string& context);
			Node* make(NodeKind kind, const SourcePos& pos);
			const SourcePos& lastPos(void) const;	// of the token just read

			// One top-level declaration (a namespace: all of its declarations) into `out`.
			void parseTopLevel(std::vector<Node*>& out);
			void parseNamespace(std::vector<Node*>& out);
			// A name with its scopes: A::B::c (leading :: for the global scope: global = true).
			std::string parseQualifiedName(const std::string& context, bool& global);
			// The declaration's full name, shortName and scope (in the current scope).
			void nameDeclaration(Node* d);
			Node* parseDeclaration(void);
			Node* parseStruct(const Node* owner = nullptr);
			void parseStructFields(Node* decl);
			Node* parseEnum(const Node* owner = nullptr);
			Node* parseConst(void);
			Node* parseAlias(void);
			Node* parseVarDecl(bool allowNoType = true);
			Node* parseFunction(int flags, const Node* owner = nullptr);
			void parseParams(Node* fn);
			Node* parseLambda(void);
			Node* parseLow(void);
			TypeSyntax* parseType(void);
			bool startsType(void) const;
			bool pointerTypeAhead(void) const;
			Node* parseInitializer(void);

			Node* parseStatement(void);
			Node* parseBlock(void);
			Node* parseLocalStatic(const SourcePos& pos);
			// A defer as the body of if / while / ...: it needs a block of its own (it is run when
			// a block is left).
			void requireBlockFor(const Node* body, const std::string& what);
			std::string tokenText(size_t from, size_t to) const;	// the tokens [from, to) as source text

			// Local scopes (inside subs): what each name means there.
			void pushScope(void);
			void popScope(void);
			void declareLocal(const std::string& name, const std::string& hidden = "");
			std::string localName(const std::string& name, const SourcePos& pos, bool* found = nullptr);
			std::string renameLow(const std::string& text);

			Node* parseExpression(void);
			Node* parseAssignment(void);
			Node* parseTernary(void);
			Node* parseBinary(int minPrec);
			Node* parseUnary(void);
			Node* parsePostfix(Node* expr);
			Node* parsePrimary(void);

		private:
			std::vector<Token> m_tokens;
			size_t m_pos { 0 };
			// Nameless structs (struct { ... } used as a type): hidden top-level StructDecls.
			std::vector<Node*> m_anonStructs;
			int m_anonCount { 0 };
			// Lambdas: hidden top-level FuncDecls.
			std::vector<Node*> m_lambdas;
			int m_lambdaCount { 0 };
			Node* m_currentFunc { nullptr };	// the sub being parsed (for lambdas)
			// Local constants and static variables: hidden top-level declarations named
			// <sub>::<name> (added before the declaration they are in); the scopes map a local name
			// to that hidden name ("": a local variable or parameter, which hides outer names).
			std::vector<Node*> m_hoisted;
			std::map<std::string, int> m_hoistedCount;
			std::vector<std::map<std::string, std::string>> m_scopes;
			int m_hoistScope { -1 };
			// The namespace being parsed (Bios::Text16, "" at the global scope), and the scope of the
			// declarations being parsed (the namespace, or a struct's full name in its body).
			std::string m_namespace;
			std::string m_scope;
			// Nested structs / enums (in a struct): hidden top-level declarations.
			int m_external { 0 };			// in a library's declarations (__dlib_extern <n> { ... }): n + 1
			std::vector<Node*> m_nested;	// in the value of a local const / static: the scopes below this one are not usable
	};
}
