#pragma once

#include "Lexer.hpp"
#include "AST.hpp"
#include <map>
#include <set>

namespace dscript
{
	class Parser
	{
		public:
			Parser(const std::vector<Token>& tokens);
			Node* parseProgram(void);
			// The names of the templates' structs and subs (found in the tokens before parsing: then
			// Name<...> is a template's arguments, not a comparison).
			const std::set<std::string>& templateNames(void) const { return m_templateNames; }
			void addTemplateNames(const std::set<std::string>& names) { m_templateNames.insert(names.begin(), names.end()); }
			// An instance (the parser's tokens: one entity of def, its parameters replaced by types and
			// its name by the instance's): its declarations (the struct and its methods, or the sub,
			// and what they hide at the top level). owner: the struct of a method template.
			std::vector<Node*> parseInstance(const TemplateDef& def, size_t entity, Node* owner);

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
			void parseStructMember(Node* decl);
			int parseVirtualFlag(int flags);
			void checkVirtualFlags(int flags, const Token& name);
			void parseDeclIf(std::vector<Node*>& out, Node* structDecl);
			Node* parseStaticAssert(void);
			Node* m_guard { nullptr };		// the if constexpr around the declarations being parsed
			bool m_guardElse { false };
			Node* parseEnum(const Node* owner = nullptr);
			Node* parseConst(void);
			Node* parseAlias(void);
			// for (auto x in r) / (auto& x in r) / (var x: T in r) / (... in a..b), after 'for ('.
			bool isForIn(void);
			Node* parseForIn(const SourcePos& pos);
			int m_forInCount { 0 };
			Node* parseVarDecl(bool allowNoType = true);
			bool isRefDecl(size_t k = 0);		// ref x = / const ref x = (k: where it would start)
			Node* parseRefDecl(void);
			Node* parseFunction(int flags, const Node* owner = nullptr);
			std::string parseOperatorName(void);
			bool acceptVariadicTyped(void);
			void parseParams(Node* fn);
			bool acceptOwn(void);
			Node* parseLambda(void);
			Node* parseLow(void);
			// template(T1, optional T2) where (condition) { ... } (owner: in a struct's body).
			Node* parseTemplate(Node* owner);
			bool isTemplateName(const std::string& name) const;
			// <type, ...> after a template's name; the closing '>' (also the first one of '>>').
			void parseTemplateArgs(std::vector<TypeSyntax*>& out);
			void expectCloseAngle(void);
			// A compile-time condition (where, if constexpr): calls to its built-ins take types.
			Node* parseCondition(void);
			TypeSyntax* parseType(void);
			bool startsType(void) const;
			bool pointerTypeAhead(void) const;
			Node* parseInitializer(void);

			Node* parseStatement(void);
			void endStatement(const Node* expr, const std::string& context);
			Node* parseSwitch(void);
			Node* parseBlock(void);
			Node* atomicBlock(Node* body, const SourcePos& pos);
			Node* parseLocalStatic(const SourcePos& pos, bool isStatic);
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
			bool renameLocalAlias(std::string& name);

			Node* parseExpression(void);
			Node* parseAssignment(void);
			Node* parseTernary(void);
			Node* parseBinary(int minPrec);
			Node* parseUnary(void);
			Node* parsePostfix(Node* expr);
			Node* parseCatch(Node* expr);
			Node* parseSwitchValue(void);
			Node* parsePrimary(void);

		private:
			std::set<std::string> m_templateNames;
			bool m_condition { false };		// parsing a compile-time condition
			std::vector<Token> m_tokens;
			size_t m_pos { 0 };
			// Nameless structs (struct { ... } used as a type): hidden top-level StructDecls.
			std::vector<Node*> m_anonStructs;
			int m_anonCount { 0 };
			// Lambdas: hidden top-level FuncDecls.
			std::vector<Node*> m_lambdas;
			int m_lambdaCount { 0 };
			int m_atomicCount { 0 };
			Node* m_currentFunc { nullptr };	// the sub being parsed (for lambdas)
			// Local constants and static variables: hidden top-level declarations named
			// <sub>::<name> (added before the declaration they are in); the scopes map a local name
			// to that hidden name ("": a local variable or parameter, which hides outer names).
			std::vector<Node*> m_hoisted;
			std::map<std::string, int> m_hoistedCount;
			std::vector<std::map<std::string, std::string>> m_scopes;
			std::set<std::string> m_localAliases;	// the hidden names of the local aliases
			int m_hoistScope { -1 };
			// A local const's value being parsed: does it use the sub's variables (then it is a
			// constant variable of the sub, not a hidden declaration)?
			bool m_probeLocals { false };
			bool m_usedLocal { false };
			// The namespace being parsed (Bios::Text16, "" at the global scope), and the scope of the
			// declarations being parsed (the namespace, or a struct's full name in its body).
			std::string m_namespace;
			std::string m_scope;
			// Nested structs / enums (in a struct): hidden top-level declarations.
			int m_external { 0 };			// in a library's declarations (__dlib_extern <n> { ... }): n + 1
			std::vector<Node*> m_nested;	// in the value of a local const / static: the scopes below this one are not usable
	};
}
