#pragma once

#include "Common.hpp"

namespace dscript
{
	struct Type;
	struct VarInfo;
	struct FuncInfo;

	// A type as written in the source (resolved to a Type by the code generator, so types
	// can be used before they are declared).
	struct TypeSyntax
	{
		enum class Kind { Named, Pointer, Array, Function };
		Kind kind { Kind::Named };
		SourcePos pos;
		std::string name;					// Named
		TypeSyntax* base { nullptr };		// Pointer / Array; Function: the return type (nullptr: void)
		struct Node* count { nullptr };		// Array (a constant expression; nullptr: [], the size of the value)
		std::vector<TypeSyntax*> params;	// Function
	};

	enum class NodeKind
	{
		// Declarations
		Program, StructDecl, EnumDecl, EnumMember, ConstDecl, AliasDecl, VarDecl, FuncDecl,
		// Statements
		Block, If, While, DoWhile, For, Return, Break, Continue, ExprStmt, LowBlock, Assert, DebugPrint,
		// Expressions
		IntLit, StrLit, BoolLit, Null, Ident, Unary, Binary, Assign, Ternary, Call, Index,
		Member, Cast, SizeofType, SizeofExpr, PreInc, PreDec, PostInc, PostDec, InitList
	};

	// One node type for the whole tree: the fields used depend on the kind (see Parser.cpp).
	struct Node
	{
		NodeKind kind { NodeKind::Program };
		SourcePos pos;
		SourcePos namePos;				// declarations, parameters, enum members, Member: where the name is
		SourcePos endPos;				// Block: where its '}' is
		std::string name;				// identifiers, operators, member names
		std::string text;				// string literals, low block text; Ident of a local const / static: the name as written
		int64_t value { 0 };			// integer literals; Member: 1 for '->'; FuncDecl: 1 if interrupt
		TypeSyntax* typeSyntax { nullptr };
		Node* a { nullptr };
		Node* b { nullptr };
		Node* c { nullptr };
		Node* d { nullptr };
		std::vector<Node*> list;		// Program / Block / struct fields / params / call arguments / ...
		bool isPrivate { false };		// a struct field declared priv (methods: FuncDecl flag 16)

		// Filled in by the code generator.
		Type* type { nullptr };
		bool lvalue { false };
		bool readOnly { false };		// in a constant array (assignments are errors)
		VarInfo* var { nullptr };		// Ident naming a variable
		FuncInfo* func { nullptr };		// Ident / Call naming a function
		bool isConst { false };			// a compile-time integer constant
		bool constPtr { false };		// a constant pointer value (a field of a struct constant), in constValue
		int64_t constValue { 0 };
	};
}
