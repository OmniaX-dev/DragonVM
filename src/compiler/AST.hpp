#pragma once

#include "Common.hpp"
#include "Lexer.hpp"

namespace dscript
{
	struct Type;
	struct VarInfo;
	struct FuncInfo;

	// A type as written in the source (resolved to a Type by the code generator, so types
	// can be used before they are declared).
	struct TypeSyntax
	{
		// Value: a template's value argument (Array<u32, 16>): the expression in value.
		enum class Kind { Named, Pointer, Array, Function, Value };
		Kind kind { Kind::Named };
		SourcePos pos;
		std::string name;					// Named (the full name once the code generator resolved it)
		std::string written;				// Named: the name as written (empty: name)
		bool resolved { false };			// Named: name is already the full name
		TypeSyntax* base { nullptr };		// Pointer / Array; Function: the return type (nullptr: void)
		bool constTarget { false };			// Pointer: const T* (what it points at is constant)
		struct Node* count { nullptr };		// Array (a constant expression; nullptr: [], the size of the value)
		std::vector<TypeSyntax*> params;	// Function
		int variadic { 0 };					// Function: 1 ... (more arguments), 2 ...typed (with their types)
		std::vector<TypeSyntax*> args;		// Named: a template's arguments (Array<u32>)
		Type* fixed { nullptr };			// a template parameter in an instance: the type it stands for (name: the parameter)
		struct Node* value { nullptr };		// Value
	};

	// template(T1, optional T2) where (condition) { structs and subs }: kept as tokens, each struct /
	// sub instantiated on use with the parameters replaced by types (CodeGen::instantiate).
	// An if constexpr (condition) around declarations: true (else: false) for them to exist.
	struct Guard { struct Node* condition { nullptr }; bool expected { true }; };
	struct TemplateEntity
	{
		std::string name;					// as declared (Array)
		bool isStruct { false };
		size_t first { 0 }, last { 0 };		// its tokens in TemplateDef::tokens: [first, last)
		size_t nameToken { 0 };				// the token of its name
		std::vector<Guard> guards;			// the template body's if constexpr around it (outer first)
	};
	// static_assert(condition, message) in a template's body: checked for each instance.
	struct TemplateAssert { struct Node* condition { nullptr }; std::string message; std::string text; std::vector<Guard> guards; SourcePos pos; };
	struct TemplateDef
	{
		std::vector<std::string> params;
		// A value parameter's type (N: u32; nullptr: a type parameter), and the defaults (a type
		// parameter's, a value parameter's: a parameter with a default is optional).
		std::vector<TypeSyntax*> paramTypes;
		std::vector<TypeSyntax*> defaultTypes;
		std::vector<struct Node*> defaultValues;
		std::vector<TemplateAssert> asserts;
		size_t required { 0 };				// the parameters before 'optional'
		struct Node* where { nullptr };		// nullptr: none
		std::vector<std::string> whereParts;	// the source of each part of a top-level 'and' (for errors)
		std::vector<Token> tokens;			// the body (inside the braces)
		std::vector<Token> source;			// the whole template (a library's interface writes it)
		std::vector<TemplateEntity> entities;
		std::string scope;					// where it is (a namespace, or the struct of a method template)
		struct Node* owner { nullptr };		// the struct of a method template
		SourcePos pos;
		int external { 0 };					// from a library's interface
	};

	enum class NodeKind
	{
		// Declarations
		Program, NamespaceDecl, StructDecl, EnumDecl, EnumMember, ConstDecl, AliasDecl, VarDecl, FuncDecl, TemplateDecl,
		// if constexpr (a) around declarations (their guard; value: -1 not decided, 0 / 1), static_assert(a, text)
		DeclIf, StaticAssert,
		// Statements
		Block, If, While, DoWhile, For, ForIn, Return, Break, Continue, ExprStmt, LowBlock, Assert, DebugPrint, Defer,
		// Expressions
		IntLit, FloatLit, StrLit, BoolLit, Null, Ident, Unary, Binary, Assign, Ternary, Call, Index,
		Member, Cast, SizeofType, SizeofExpr, PreInc, PreDec, PostInc, PostDec, InitList, TypeArg
	};

	// One node type for the whole tree: the fields used depend on the kind (see Parser.cpp).
	struct Node
	{
		NodeKind kind { NodeKind::Program };
		SourcePos pos;
		SourcePos namePos;				// declarations, parameters, enum members, Member: where the name is
		SourcePos endPos;				// Block, NamespaceDecl: where its '}' is
		std::string name;				// identifiers, operators, member names
		std::string text;				// string literals, low block text; Ident of a local const / static: the name as written
		int64_t value { 0 };			// integer literals; Member: 1 for '->'; FuncDecl: 1 if interrupt
		TypeSyntax* typeSyntax { nullptr };
		Node* a { nullptr };
		Node* b { nullptr };
		Node* c { nullptr };
		Node* d { nullptr };
		std::vector<Node*> list;		// Program / Block / struct fields / params / call arguments / ...
		bool isPrivate { false };		// a struct field declared priv (methods: FuncDecl flag 16); a priv nested struct / enum;
										// Member / Ident of a for-in's own call (begin, end, next, get): it may call a priv method
		bool isReadonly { false };		// a struct field declared readonly
		// VarDecl (variables, parameters, fields): 1 const (never changed after its initialization),
		// 3 constinit (a global whose value is known when compiling: no code sets it). ConstDecl: 1
		// const (a compile-time constant if its value is one, otherwise a constant variable), 2
		// constexpr (must be a compile-time constant). FuncDecl: 2 constexpr (evaluated when
		// compiling when its arguments are constants), 3 consteval (always evaluated when compiling).
		int constKind { 0 };
		// ConstDecl / VarDecl hidden at the top level from a sub (text: the name as written): 1 a
		// local constant (const in a sub: per call if it is not a compile-time constant), 2 static.
		// VarDecl in a block with hoisted set: the place of that local constant (hoisted points at it).
		int hoistKind { 0 };
		Node* hoisted { nullptr };
		// Declarations in an if constexpr (at the top level, in a namespace or a struct): the DeclIf
		// (its condition) and the branch they are in (guardElse: its else). A DeclIf has its own.
		Node* guard { nullptr };
		bool guardElse { false };
		int variadic { 0 };				// FuncDecl: 1 if the parameters end with ..., 2 with ...typed
		// Call of a variadic sub's built-in (va_count, va_start, va_types): 1, 2, 3 (0: a normal call).
		int vaBuiltin { 0 };

		// Names and scopes (namespaces, structs).
		// Declarations: name is the full name (Bios::Text16::print), shortName the name as declared
		// (print), scope where the names in the declaration are looked up (Bios::Text16; for the
		// methods, fields and nested types of a struct: the struct's full name).
		std::string shortName;
		std::string scope;
		// Methods, nested structs / enums: the StructDecl they are in. Local constants / statics:
		// the FuncDecl they are in.
		Node* owner { nullptr };
		// Ident: name is already final (a local, a parameter, a renamed local constant / static,
		// ::global); the others are looked up from the scope by the code generator.
		bool resolved { false };
		// Declared in a library's interface (#import): 1 + the library's index. Its subs (but the
		// inline ones) and globals are in the library: only declared here.
		int external { 0 };
		// Inline subs: the tokens of the body (from '{' to '}'), for a library's interface.
		std::vector<Token> bodyTokens;
		// Block: the body of an atomic block (interrupts disabled; see Parser::atomicBlock).
		bool atomic { false };
		// If: if constexpr (only the branch the condition chooses is generated).
		bool isConstexpr { false };
		// Ident / Member: a template's arguments (max<u32>, Array<u32>.make, x.map<f32>).
		std::vector<TypeSyntax*> templateArgs;
		// TemplateDecl: the template. (TypeArg: a type given to a condition's built-in, in typeSyntax.)
		TemplateDef* tmpl { nullptr };

		// Filled in by the code generator.
		Type* type { nullptr };
		bool lvalue { false };
		bool readOnly { false };		// in a constant array, or a readonly field outside its struct (assignments are errors)
		std::string readOnlyWhy;		// readOnly: the error for a change (empty: a constant array)
		VarInfo* var { nullptr };		// Ident naming a variable
		FuncInfo* func { nullptr };		// Ident / Call naming a function
		bool isConst { false };			// a compile-time integer constant
		bool constPtr { false };		// a constant pointer value (a field of a struct constant), in constValue
		int64_t constValue { 0 };
	};
}
