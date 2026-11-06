#pragma once

#include "Types.hpp"
#include "Index.hpp"
#include <functional>
#include <map>
#include <set>

namespace dscript
{
	struct VarInfo
	{
		enum class Storage { Global, Local, Param };
		std::string name;
		Type* type { nullptr };
		Storage storage { Storage::Global };
		int32_t offset { 0 };		// Local: from FP; Param: argument index
		std::string symbol;			// Global: the dasm data symbol (without '$')
		bool readOnly { false };	// a constant array (const Name: T[N] = ...)
		SourcePos pos;
		SourcePos defPos;			// where the name is declared
		bool param { false };		// a parameter (also those of an expanded inline sub)
		bool isStatic { false };	// a static variable / constant array of a sub (<sub>::<name>)
		const Node* decl { nullptr };	// globals: the declaration
	};

	struct FuncInfo
	{
		std::string name;
		std::string label;			// the dasm label
		Type* type { nullptr };		// a Function type
		std::vector<std::string> paramNames;
		std::vector<Node*> defaults;	// default values (constant expressions) or nullptr
		size_t requiredParams { 0 };
		Node* decl { nullptr };
		bool interrupt { false };
		bool isInline { false };		// inline sub: its body is generated at every direct call
		bool addressTaken { false };	// used as a value (a sub pointer): an inline sub also needs real code
		bool defined { false };
	};

	struct CodeGenOptions
	{
		bool raw { false };			// bare metal: ends with hlt instead of ret
		bool rom { false };			// the program runs from ROM: everything stored in it is read-only
		bool noInline { false };	// inline subs are compiled as normal subs (for debugging)
		bool noAsserts { false };	// NDEBUG: assert(...) is only checked, no code
		std::vector<std::string> directives;	// .load / .fixed / .header lines for raw mode
		// For the language server: errors are collected (errors()) instead of stopping at the
		// first one (each declaration and sub is checked on its own), no code is returned, and
		// what every name refers to goes in the index.
		bool analysis { false };
		Index* index { nullptr };
	};

	// Checks the program (types, names, overloads) and generates dasm source for it.
	// Generated code layout:
	//   .entry __ds_start; data (globals, string literals); code: __ds_start (calls __ds_init,
	//   which runs the non-constant global initializers, then main, then hlt (raw) or ret),
	//   the top-level low blocks, the subs, the runtime helpers that are used.
	class CodeGen
	{
		public:
			CodeGen(const CodeGenOptions& options);
			std::vector<std::string> generate(Node* program);
			// Analysis mode: the errors found.
			const std::vector<CompileError>& errors(void) const { return m_errors; }

		private:
			// Struct constants: type is the struct, fields holds every scalar field's value by its
			// path ("pos.x"); they have no memory (only their fields are used).
			// String constants: isString, text (they are used as that string literal).
			struct ConstInfo { Node* decl { nullptr }; bool resolving { false }; bool resolved { false }; int64_t value { 0 }; Type* type { nullptr }; std::map<std::string, int64_t> fields; bool isString { false }; std::string text; };
			struct EnumInfo { Node* decl { nullptr }; bool resolving { false }; bool resolved { false }; std::map<std::string, int64_t> values;
				Type* type { nullptr }; };	// type: the enum's integer type (i32 unless given: enum E: u8 { ... })
			struct AliasInfo { Node* decl { nullptr }; bool resolving { false }; Type* type { nullptr }; };
			// deferDepth: the defer frames outside the loop (break / continue run the ones above).
			struct Loop { std::string breakLabel; std::string continueLabel; size_t deferDepth { 0 }; };
			// A defer: its statement, and the variables it sees (those declared before it).
			struct Deferred { Node* body; std::vector<std::map<std::string, VarInfo*>> scopes; };
			typedef std::string Reg;

		private:
			// ===== analysis (language server) =====
			bool attempt(const std::function<void()>& fn);
			void indexName(const SourcePos& pos, const std::string& written, const SourcePos& def, const std::string& hover);
			void indexDeclarations(void);
			// Completion: a local / parameter (visible until the end of its scope), and the rest.
			void completeLocal(VarInfo* v);
			void indexCompletion(Node* program);
			void indexLow(const SourcePos& pos, const std::string& written, const std::string& name);
			std::string hoverVar(VarInfo* v);
			std::string hoverFunc(FuncInfo* f);
			std::string hoverConst(const std::string& name);
			std::string hoverStruct(StructInfo* info);
			std::string hoverField(StructInfo* info, const StructField& f);
			std::string hoverEnum(const std::string& name);
			std::string hoverAlias(const std::string& name);
			std::string hoverType(const std::string& name, const SourcePos& usePos, SourcePos& def);

			// ===== names and scopes =====
			// The full name `written` refers to from `scope` (the innermost scope first; in a struct,
			// only its nested types), or `written` if nothing has it.
			std::string qualify(const std::string& written, const std::string& scope);
			bool isDeclared(const std::string& name);
			// priv nested types: only usable in their struct.
			void checkTypeAccess(const std::string& full, const std::string& scope, const SourcePos& pos);
			// Every name in the declarations -> its full name (before anything is checked).
			void resolveNames(Node* program);
			void resolveNames(Node* n, const std::string& scope);
			void resolveTypeNames(TypeSyntax* ts, const std::string& scope);

			// ===== declarations =====
			void collect(Node* program);
			void declareName(const std::string& name, const SourcePos& pos, const std::string& what);
			Type* resolveType(TypeSyntax* ts);
			void resolveStruct(StructInfo* info, const SourcePos& usePos);
			int64_t resolveConst(const std::string& name, const SourcePos& usePos);
			void resolveEnum(EnumInfo& info);
			Type* enumType(EnumInfo& info);
			void resolveFunction(FuncInfo* f);
			int64_t constExpr(Node* e, const std::string& what);
			void constStructValues(StructInfo* info, Node* init, const std::string& prefix, ConstInfo& c, const std::string& constName);
			ConstInfo* constStructRef(Node* e, Type*& structType, std::string& path);
			bool sizeofConstStruct(Node* e, uint32_t& size);
			void foldStringConst(Node* e);
			void inferArraySize(TypeSyntax* ts, Node* init, const SourcePos& pos);
			std::string mangle(Type* t);

			// ===== checking =====
			void check(Node* e, Type* expected = nullptr);
			void checkIdent(Node* e, Type* expected);
			void checkUnary(Node* e);
			void checkBinary(Node* e);
			void checkAssign(Node* e);
			void checkCall(Node* e);
			void rewriteMethodCall(Node* e);
			bool insideStruct(const StructInfo* info);
			void checkFieldAccess(const StructInfo* info, const StructField* f, const SourcePos& pos);
			void checkSubAccess(FuncInfo* f, const SourcePos& pos);
			void resolveSubDefaults(void);
			void checkMember(Node* e);
			Type* decay(Type* t);
			Type* promote(Type* t);
			Type* arithmeticType(Type* a, Type* b);
			int conversionCost(Node* from, Type* to);
			void requireConvertible(Node* from, Type* to, const std::string& context);
			FuncInfo* resolveOverload(const std::string& name, const std::vector<Node*>& args, const SourcePos& pos);
			FuncInfo* functionForType(const std::vector<FuncInfo*>& funcs, Type* fnType);
			VarInfo* lookupVar(const std::string& name);
			bool isTypeName(const std::string& name);

			// ===== code generation =====
			void genFunction(FuncInfo* f);
			void genGlobals(void);
			void genStatement(Node* s);
			void genLocalVar(Node* s);
			// var v: Struct(args): calls Struct::new(&v, args).
			void genConstructor(VarInfo* v, Node* decl);
			// The defers of the frames from `downTo` up (the innermost first, in reverse order).
			void genDefers(size_t downTo);
			bool hasDefers(size_t downTo) const;
			void genDeferred(const Deferred& d);
			void genInit(Reg addr, Type* type, Node* init, int32_t offset);
			void genBranch(Node* e, const std::string& label, bool jumpIfTrue);
			Reg gen(Node* e);
			Reg genAddr(Node* e);
			Reg genBinary(Node* e);
			Reg genAssign(Node* e);
			Reg genIncDec(Node* e);
			Reg genCall(Node* e);
			Reg genInlineCall(Node* e, FuncInfo* f);
			Reg genLogicalValue(Node* e);
			void genLow(Node* low, bool topLevel);
			void load(const Reg& dst, const Reg& addr, Type* t);
			void store(const Reg& addr, const Reg& value, Type* t);
			void loadVar(const Reg& dst, VarInfo* v);
			void varAddress(const Reg& dst, VarInfo* v);
			void convert(const Reg& r, Type* from, Type* to);
			void normalize(const Reg& r, Type* t);
			void copyMemory(const Reg& dst, const Reg& src, uint32_t size);
			void zeroMemory(const Reg& dst, uint32_t size);
			void addImmediate(const Reg& r, int64_t value);
			void scale(const Reg& r, uint32_t size);
			Reg alloc(const SourcePos& pos);
			void release(const Reg& r);
			void emit(const std::string& line);
			void label(const std::string& name);
			// Source markers for the debugger (.loc <file> <line> <inline depth>): the next
			// instructions come from there. noLoc(): from nowhere (generated code).
			struct Loc { int file { -1 }; int line { 0 }; int depth { 0 }; };
			void loc(const SourcePos& pos);
			void loc(const Loc& l, bool statement = true);
			void noLoc(void);
			int sourceIndex(const std::string& file);
			std::string newLabel(void);
			std::string stringLiteral(const std::string& text);
			static std::string imm(int64_t value);

		private:
			CodeGenOptions m_options;
			TypeTable m_types;
			std::map<std::string, std::string> m_names;		// every global name -> what it is (namespaces too)
			std::map<std::string, std::string> m_privTypes;	// priv nested struct / enum -> its struct
			std::map<std::string, StructInfo*> m_structs;
			std::map<std::string, EnumInfo> m_enums;
			std::map<std::string, AliasInfo> m_aliases;
			std::map<std::string, ConstInfo> m_consts;
			bool m_inDeclarations { false };	// resolving the declarations (only constants are known)
			std::map<std::string, VarInfo*> m_globals;
			std::vector<Node*> m_globalDecls;
			std::map<std::string, std::vector<FuncInfo*>> m_funcs;
			std::vector<FuncInfo*> m_funcList;
			std::vector<Node*> m_topLows;

			// The sub being generated.
			FuncInfo* m_func { nullptr };
			std::vector<std::map<std::string, VarInfo*>> m_scopes;
			std::vector<SourcePos> m_scopeEnds;		// analysis: where the scopes being generated end
			int32_t m_localCursor { 4 };
			int32_t m_localMin { 4 };
			std::vector<Loop> m_loops;
			// defer: a frame per block being generated (the sub's body is the first one).
			std::vector<std::vector<Deferred>> m_defers;
			int m_inDefer { 0 };		// generating a defer's statement (return / break / continue cannot leave it)
			// Inline subs being expanded (innermost last): return jumps to endLabel.
			struct InlineCall { FuncInfo* func; std::string endLabel; };
			std::vector<InlineCall> m_inline;
			bool m_discarding { false };	// checking an inline sub whose code is not kept
			// --no-inline: the calls between subs declared inline (caller -> callee, at), so that
			// recursion through them is still an error (it would be when inlining).
			std::map<FuncInfo*, std::vector<std::pair<FuncInfo*, SourcePos>>> m_inlineCalls;
			void checkInlineRecursion(void);
			int m_regDepth { 0 };
			int m_stmtDepth { 0 };		// m_regDepth at the start of a statement (> 0 in inline subs)
			std::vector<std::string>* m_out { nullptr };
			Loc m_loc;
			std::map<std::string, int> m_sourceIndex;	// source file -> its .srcfile index
			std::vector<std::string> m_sourcePaths;

			std::vector<std::string> m_data;
			std::vector<std::string> m_code;
			std::vector<std::string> m_initCode;
			std::map<std::string, std::string> m_strings;
			std::set<std::string> m_helpers;
			int m_labelCounter { 0 };
			std::vector<CompileError> m_errors;
			std::vector<const StructField*> m_subDefaults;	// struct fields with a sub as default value
	};
}
