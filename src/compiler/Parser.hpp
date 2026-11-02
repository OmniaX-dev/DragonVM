#pragma once

#include "Lexer.hpp"
#include "AST.hpp"

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

			Node* parseDeclaration(void);
			Node* parseStruct(void);
			void parseStructFields(Node* decl);
			Node* parseEnum(void);
			Node* parseConst(void);
			Node* parseAlias(void);
			Node* parseVarDecl(bool allowNoType = true);
			Node* parseFunction(bool interrupt);
			Node* parseLow(void);
			TypeSyntax* parseType(void);
			bool startsType(void) const;
			Node* parseInitializer(void);

			Node* parseStatement(void);
			Node* parseBlock(void);

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
	};
}
