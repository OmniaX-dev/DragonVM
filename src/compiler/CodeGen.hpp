#pragma once

#include "Types.hpp"
#include "Common.hpp"
#include "Index.hpp"
#include "../tools/DLIB.hpp"
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
		bool readOnly { false };	// a constant array (const Name: T[N] = ...): in the program's read-only data
		// const (a constant variable: changing it, or through a pointer to it, is an error); 3:
		// constinit (a global whose value must be known when compiling).
		bool isConst { false };
		int constKind { 0 };
		bool localRuntime { false };	// a local constant array whose values are not constants: in its sub, per call
		bool ct { false };			// a variable of a sub being evaluated when compiling (offset: its address there)
		bool ctValue { false };		// a struct value computed when compiling (read-only data used as a call's result: a value, not a variable)
		bool addressUsed { false };	// the generated code uses its address (read-only data made when needed: only then in the program)
		SourcePos pos;
		SourcePos defPos;			// where the name is declared
		bool param { false };		// a parameter (also those of an expanded inline sub)
		bool isStatic { false };	// a static variable / constant array of a sub (<sub>::<name>)
		bool external { false };	// in a library (#import): only declared here
		const Node* decl { nullptr };	// globals: the declaration
		// -O: a local / parameter kept in a register (its memory slot is not used), or "".
		std::string reg;
		int optId { -1 };			// -O: its index in the sub's variables (the same in both passes)
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
		bool external { false };		// in a library (#import): its code is there (not the inline ones)
		// A template's instance: generated only if something uses it (called, or its address taken).
		bool instance { false };
		bool used { false };
		bool generated { false };
		bool resolving { false };		// (a constexpr sub resolved early, for a constant)
		std::string instanceNote;		// " (in Array<u32>, instantiated at file:line)": added to its errors
		// An instance's signature that doesn't work for these types (a method nothing calls must not
		// make the struct unusable): the error, raised if something uses it.
		bool broken { false };
		CompileError brokenError;
	};

	struct CodeGenOptions
	{
		bool raw { false };			// bare metal: ends with hlt instead of ret
		bool rom { false };			// the program runs from ROM: everything stored in it is read-only
		bool noInline { false };	// inline subs are compiled as normal subs (for debugging)
		bool noAsserts { false };	// NDEBUG: assert(...) is only checked, no code
		// -O: scalar locals and parameters whose address is never taken are kept in the registers
		// a sub's expressions leave free, and a peephole pass removes the copies that leaves.
		bool optimize { false };
		std::vector<std::string> directives;	// .load / .fixed / .header lines for raw mode
		// For the language server: errors are collected (errors()) instead of stopping at the
		// first one (each declaration and sub is checked on its own), no code is returned, and
		// what every name refers to goes in the index.
		bool analysis { false };
		Index* index { nullptr };
		// A library (dsc --lib): no main; __ds_lib_init initializes its globals.
		bool library { false };
		// The libraries the program imports: __ds_start calls their __ds_lib_init first (as
		// __dlib_init_<n>, which the linker connects), in this order.
		int libraries { 0 };
		// A shared library (dsc --lib --shared, a DRL): its code is loaded once for every process,
		// so it never uses the address of a writable global directly. Its writable globals are
		// together between $__ds_rw_start and $__ds_rw_end (every process gets its own copy), and
		// their addresses come from the running process's table:
		//   mov R, [$__ds_libtable]           the table (the system word LibTable points at it)
		//   movo R, *R, dword $__ds_slot      this library's bias (its slot: 4 * slot)
		//   addip R, $__dsrw__<symbol>        + the global's offset in the library
		// (another library's writable global: $__dsslot__<symbol> / $__dsoff__<symbol>). The
		// linker (dsc) and the loader fill those in; see other/info/dynamic-linking.md.
		bool shared { false };
	};

	// Checks the program (types, names, overloads) and generates dasm source for it.
	// Generated code layout:
	//   .entry __ds_start; data (globals, string literals); code: __ds_start (calls __ds_init,
	//   which runs the non-constant global initializers, then main, then hlt (raw) or ret),
	//   the top-level low blocks, the subs, the runtime helpers that are used.
	struct ConditionEval;

	class CodeGen
	{
		friend struct ConditionEval;

		public:
			CodeGen(const CodeGenOptions& options);
			std::vector<std::string> generate(Node* program);
			// Analysis mode: the errors found.
			const std::vector<CompileError>& errors(void) const { return m_errors; }
			// A library (after generate()): the symbols the programs that import it can use (its
			// subs and globals), and its declarations as DScript source (lines: where each line
			// of it comes from).
			std::vector<std::string> exports(void) const;
			std::string interface(std::vector<dragon::dlib::LineInfo>& lines);
			// For the debugger (the disassembly's { SUBS }): a line per sub with code here (inline
			// subs too: their copies), "<name>\t<label>\t<parameter types>\t<inline 0/1>\t<file>\t<line>\t<last line>".
			std::vector<std::string> subTable(void) const;
			// The labels where a library's code can be cut into pieces for the linker (which keeps
			// only the pieces a program uses): __ds_lib_init (the first code) and every sub that
			// follows code ending with ret (another sub), in the order of the code. A sub right
			// after a top-level low block is not one (the low code could run into it).
			const std::vector<std::string>& pieceStarts(void) const { return m_pieceStarts; }

		private:
			// Struct constants: type is the struct, fields holds every scalar field's value by its
			// path ("pos.x"); they have no memory (only their fields are used).
			// String constants: isString, text (they are used as that string literal).
			// runtime: a const whose value is not a compile-time constant: a constant variable (a global
			// in storage; a local constant: per call, in its sub). storage: also the read-only copy of
			// a struct constant used as a value (its address taken, passed, a method called on it).
			struct ConstInfo { Node* decl { nullptr }; bool resolving { false }; bool resolved { false }; int64_t value { 0 }; Type* type { nullptr }; std::map<std::string, int64_t> fields; bool isString { false }; std::string text;
				bool runtime { false }; VarInfo* storage { nullptr }; };
			struct EnumInfo { Node* decl { nullptr }; bool resolving { false }; bool resolved { false }; std::map<std::string, int64_t> values;
				Type* type { nullptr }; };	// type: the enum's integer type (i32 unless given: enum E: u8 { ... })
			struct AliasInfo { Node* decl { nullptr }; bool resolving { false }; Type* type { nullptr }; };
			// deferDepth: the defer frames outside the loop (break / continue run the ones above).
			struct Loop { std::string breakLabel; std::string continueLabel; size_t deferDepth { 0 }; };
			// A defer: its statement, and the variables it sees (those declared before it).
			struct Deferred { Node* body; std::vector<std::map<std::string, VarInfo*>> scopes; int atomic; };
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
			std::string qualifyThroughAlias(const std::string& written, const std::string& scope);
			std::string aliasedScope(const std::string& full);
			int m_aliasDepth { 0 };		// qualifyThroughAlias (an alias naming itself)
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
			// A const (not constexpr): a compile-time constant if its value is one, otherwise a constant
			// variable (after the declarations: a global gets its storage and type here).
			void decideConst(const std::string& name);
			int64_t resolveConstValue(const std::string& name);
			void typeGlobal(Node* d, VarInfo* v);
			// A struct constant as a value: its read-only copy in the program's data.
			VarInfo* constStorage(const std::string& name);
			// const fields (in the struct, or in its struct fields / arrays of structs): a whole
			// struct cannot be assigned. which: the first one's path.
			bool hasConstFields(Type* t, std::string* which = nullptr);
			// The code being checked is the constructor (new) of this struct.
			bool constructing(const StructInfo* info);
			// Two pointers to the same type (const or not).
			static bool samePointee(const Type* a, const Type* b) { return a->kind == Type::Kind::Pointer && b->kind == Type::Kind::Pointer && a->base == b->base; }
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
			void checkVarArg(Node* arg);
			Type* varArgType(Type* t);
			uint32_t varArgTypeCode(Type* t);
			void rewriteMethodCall(Node* e);
			bool insideStruct(const StructInfo* info);
			void checkFieldAccess(const StructInfo* info, const StructField* f, const SourcePos& pos);
			void checkSubAccess(FuncInfo* f, const SourcePos& pos);
			void resolveSubDefaults(void);
			void checkMember(Node* e, bool placeWanted = false);
			bool m_placeWanted { false };	// the next check is of what & takes the address of
			Type* decay(Type* t);
			Type* promote(Type* t);
			Type* arithmeticType(Type* a, Type* b);
			int conversionCost(Node* from, Type* to);
			void requireConvertible(Node* from, Type* to, const std::string& context);
			FuncInfo* resolveOverload(const std::string& name, const std::vector<Node*>& args, const SourcePos& pos, const std::vector<FuncInfo*>* candidates = nullptr);
			// The best overload (nullptr: none; ambiguous: two as good), without errors.
			FuncInfo* bestOverload(const std::vector<FuncInfo*>& funcs, const std::vector<Node*>& args, bool& ambiguous);
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
			// discard: the value isn't used (an expression statement, a for loop's step).
			Reg genAssign(Node* e, bool discard = false);
			Reg genIncDec(Node* e, bool discard = false);
			// An expression whose value isn't used.
			void genDiscard(Node* e);
			// Where a scalar lvalue is: *base, or *base + offset (an immediate, "dword 0x...", or a
			// register). base is FP / PP for locals and parameters (not allocated).
			struct Place
			{
				Reg base;
				bool ownBase { true };
				std::string offset;		// "" (none), "dword <imm>" or a register
				Reg offsetReg;			// the offset's register, if it is one (allocated after base)
				bool isReg { false };	// a register variable (base is its register)
			};
			Place genPlace(Node* e);
			void loadPlace(const Reg& dst, const Place& p, Type* t);
			// value: a register or an immediate.
			void storePlace(const Place& p, const std::string& value, Type* t);
			void releasePlace(const Place& p);
			// The value of an assignment (in v, the newest register) as the expression's result:
			// v itself, or moved into the place's base register.
			Reg placeResult(const Place& p, const Reg& v);
			// A constant value as the immediate a store of type t writes ("" if it can't be one).
			std::string constImmediate(Node* value, Type* t);
			// An argument as an immediate / label (constants, string literals, subs), or "".
			std::string argImmediate(Node* arg, Type* param);
			Reg genCall(Node* e);
			Reg genInlineCall(Node* e, FuncInfo* f);
			Reg genLogicalValue(Node* e);
			void genLow(Node* low, bool topLevel);
			void load(const Reg& dst, const Reg& addr, Type* t);
			void store(const Reg& addr, const Reg& value, Type* t);
			// fromMemory: a register variable's memory slot (its parameter value, at the start).
			void loadVar(const Reg& dst, VarInfo* v, bool fromMemory = false);
			// taken: -O pass 1, the variable can't be in a register.
			void varAddress(const Reg& dst, VarInfo* v, bool taken = true);
			// A shared library's writable global (its address comes from the process's table).
			bool isSharedData(const VarInfo* v) const;
			// A for-in loop as the for loop it means (built once, kept in s->list).
			Node* forInLoop(Node* s);

			// ===== Classes and interfaces =====
			// A class's layout (its base's fields, its vtable pointers): the offset of its own fields.
			uint32_t classLayout(StructInfo* info);
			void resolveFriends(StructInfo* info);
			// A class without a constructor gets new() if it needs one (its vtable pointers, its base's
			// constructor without arguments).
			void implicitConstructor(StructInfo* info);
			// Is `to` from, or a class / interface it extends (offset: where it is in a from; depth:
			// how many steps up)?
			bool derives(const StructInfo* from, const StructInfo* to, uint32_t* offset = nullptr, int* depth = nullptr);
			// A pointer conversion between related classes: the offset added (false: not one).
			bool pointerAdjust(Type* from, Type* to, int64_t& delta);
			// The struct of a method (its decl's owner), or nullptr.
			StructInfo* structOfMethod(const FuncInfo* f);
			// The methods named `name` a struct has: its own, then those it inherits (from its base
			// classes and its interfaces) that it does not override. Constructors: only its own.
			std::vector<FuncInfo*> methodsOf(StructInfo* info, const std::string& name);
			// X::m with X a struct: X (and m), or nullptr.
			StructInfo* qualifierStruct(const std::string& name, std::string* member = nullptr);
			// A static field / constant a class has, maybe inherited: its full name, or "".
			std::string memberName(StructInfo* info, const std::string& name);
			static bool sameSignature(const FuncInfo* a, const FuncInfo* b);
			static bool isVirtualSub(const FuncInfo* f) { return (f->decl->value & (512 | 1024 | 2048)) != 0; }
			// The table of a class / interface (slots, overrides, what an abstract class lacks).
			void buildVtables(StructInfo* info);
			int slotOf(StructInfo* info, const FuncInfo* f);
			// The sub a class calls for an interface's sub (the nearest in its classes), and its class.
			FuncInfo* finalOverrider(StructInfo* info, const FuncInfo* m, StructInfo** where);
			// The label of a class's table for the vtable pointer at `offset` (the table is emitted).
			std::string vtableLabel(StructInfo* info, uint32_t offset);
			std::string typeinfoLabel(StructInfo* info);
			void emitVtables(StructInfo* info);
			std::set<StructInfo*> m_vtableRequests;
			std::vector<StructInfo*> m_vtableQueue;
			size_t m_vtablesEmitted { 0 };
			std::set<StructInfo*> m_typeinfos;
			// The vtable pointers of a value of type t at addr + offset.
			void storeVptrs(const Reg& addr, Type* t, int32_t offset = 0);
			// A struct value copied into new storage (its vtable pointers: its type's).
			void copyValue(const Reg& dst, const Reg& src, Type* t);
			// A class's constructor: its base's constructor (implicit: the one without arguments) and
			// its vtable pointers, added to its body once.
			void prepareConstructor(FuncInfo* f);
			std::set<Node*> m_preparedCtors;
			// Is the code being checked in a class derived from info (prot)?
			bool insideDerived(const StructInfo* info);
			// An interface, an abstract class: only pointers to it.
			void requireValueType(Type* t, const SourcePos& pos, const std::string& what);
			void collectDefaults(Type* type, int32_t offset, std::vector<std::pair<int32_t, const StructField*>>& out, std::vector<std::pair<int32_t, std::string>>* vptrs);

			// ===== Templates =====
			// A template's struct / sub (by full name: Ns::Array, Struct::map): the templates that
			// declare it (several: overloads chosen by their where).
			struct TemplateCandidate { TemplateDef* def; size_t entity; };
			std::map<std::string, std::vector<TemplateCandidate>> m_templates;
			std::set<std::string> m_templateShortNames;
			std::map<std::string, std::string> m_instances;		// Ns::Array<u32> -> the instance (the same name)
			// An instance -> its template and arguments (to deduce T from an Array<T> parameter).
			struct InstanceOrigin { std::string templ; std::vector<Type*> args; };
			std::map<std::string, InstanceOrigin> m_instanceOrigin;
			std::map<std::pair<const TemplateDef*, size_t>, Node*> m_genericHeads;	// a sub entity parsed as is (its parameters' types, for deduction)
			Type* m_notGiven { nullptr };		// an optional parameter that was not given
			int m_instanceDepth { 0 };
			size_t m_globalsTyped { 0 }, m_globalsGenerated { 0 };
			int32_t m_initLocalMin { 4 };
			typedef std::map<std::string, Type*> Bindings;
			void registerTemplate(Node* d);
			// A template's argument: a type, or a value (a value-arg Type: Array<u32, 16>, Box<CONFIG>).
			Type* templateArgument(TypeSyntax* a, const Bindings* bindings = nullptr);
			// A value argument from a compile-time constant (of its own type; paramType: converted to it).
			Type* valueArgument(Node* e, Type* paramType, const Bindings* bindings);
			Type* makeValueArg(Type* t, int64_t value, const std::vector<uint8_t>* bytes, const SourcePos& pos);
			int m_valueArgCount { 0 };
			// if constexpr around declarations: does d exist (its guards' conditions say so)?
			bool guardHolds(Node* d);
			void applyGuards(Node* program);
			void filterMembers(Node* structDecl);
			void checkStaticAssert(Node* s, const Bindings* bindings);
			std::vector<Node*> m_staticAsserts;		// top-level ones (checked after the declarations)
			// The instance of the template `name` for these arguments (explicit, then deduced from
			// callArgs for a sub; owner: the struct of a method template): its full name. Instantiates
			// it the first time (its declarations registered; its subs generated if used).
			std::string instantiate(const std::string& name, std::vector<Type*> args, std::vector<Node*>* callArgs, StructInfo* owner,
				const SourcePos& pos, bool wantStruct);
			bool deduce(const TemplateDef* def, size_t entity, std::vector<Type*>& args, std::vector<Node*>& callArgs, bool method, std::string& why);
			bool unify(TypeSyntax* pattern, Type* actual, const TemplateDef* def, std::vector<Type*>& args, bool fromLiteral, std::string& why);
			Node* genericHead(const TemplateDef* def, size_t entity);
			// Name<args> / T (a parameter in an instance) in an expression: the struct / sub it is.
			void resolveTemplateIdent(Node* e);
			void markUsed(FuncInfo* f);
			// A compile-time condition (where, if constexpr): its value. bindings: a template's
			// parameters (nullptr: an optional one not given) while its where is checked.
			int64_t evalCondition(Node* e, const Bindings* bindings);
			Type* conditionType(TypeSyntax* ts, const Bindings* bindings);
			void genGlobalTypes(size_t from);
			void genGlobalValues(size_t from);
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
			int m_atomic { 0 };			// in an atomic block (interrupts disabled: an int would be ignored)
			int m_inDefer { 0 };		// generating a defer's statement (return / break / continue cannot leave it)
			// Inline subs being expanded (innermost last): return jumps to endLabel.
			// resultOffset: a struct result's place in the caller's frame (FP + offset).
			struct InlineCall { FuncInfo* func; std::string endLabel; int32_t resultOffset { 0 }; };
			// Structs by value: a sub returning a struct gets the address of its caller's result
			// (a hidden first argument: the parameters' slots are one further); a struct argument is
			// a copy in the caller's frame, passed by its address. temporary: room in the frame.
			static bool returnsStruct(const Type* fn) { return fn != nullptr && fn->base != nullptr && fn->base->kind == Type::Kind::Struct; }
			int32_t temporary(uint32_t size);
			std::vector<InlineCall> m_inline;
			bool m_discarding { false };	// checking an inline sub whose code is not kept
			// --no-inline: the calls between subs declared inline (caller -> callee, at), so that
			// recursion through them is still an error (it would be when inlining).
			std::map<FuncInfo*, std::vector<std::pair<FuncInfo*, SourcePos>>> m_inlineCalls;
			void checkInlineRecursion(void);
			int m_regDepth { 0 };
			int m_regLimit { 16 };		// temporaries alloc() may use (fewer when -O keeps variables in registers)
			// ===== -O (register variables) =====
			// Each sub is generated twice: pass 1 records its variables (whether their address is
			// taken, how often they are used, in which loop depth, and when they are live) and the
			// temporaries it needs; pass 2 generates it with the chosen variables in registers.
			struct OptVar { bool eligible { true }; bool param { false }; double weight { 0 }; int start { 0 }; int end { 0x7FFFFFFF }; };
			struct OptRetry {};			// pass 2 needed more temporaries than pass 1: no register variables
			int m_optPass { 0 };		// 0: off, 1: recording, 2: register variables
			std::vector<OptVar> m_optVars;
			std::vector<std::string> m_optRegs;	// pass 2: optId -> its register ("" if in memory)
			int m_optTick { 0 };
			int m_optMaxDepth { 0 };
			int m_loopDepth { 0 };
			std::set<int> m_lowRegs;	// registers the sub's low blocks name (never variables)
			bool m_lowOpaque { false };	// a low block names registers in a way not tracked (reg(...))
			void optRegister(VarInfo* v, bool functionParam);
			void optUse(VarInfo* v);
			void optEndScope(const std::map<std::string, VarInfo*>& scope);
			void optAssign(void);
			// Peephole pass over a sub's code (-O): copies into and out of temporaries.
			void peephole(std::vector<std::string>& code);
			int m_stmtDepth { 0 };		// m_regDepth at the start of a statement (> 0 in inline subs)
			std::vector<std::string>* m_out { nullptr };
			Loc m_loc;
			std::map<std::string, int> m_sourceIndex;	// source file -> its .srcfile index
			std::vector<std::string> m_sourcePaths;

			std::vector<std::string> m_data;
			std::vector<std::string> m_dataRW;	// shared library: the writable globals (its rw part)
			std::vector<std::string> m_pieceStarts;
			bool m_subEmitted { false };
			std::vector<std::string> m_code;
			std::vector<std::string> m_initCode;
			std::map<std::string, std::string> m_strings;
			std::set<std::string> m_helpers;
			int m_labelCounter { 0 };
			std::vector<CompileError> m_errors;
			std::vector<const StructField*> m_subDefaults;	// struct fields with a sub as default value
			Node* m_program { nullptr };

			// ===== Compile-time evaluation (constexpr / consteval subs) =====
			// A call whose arguments are constants runs here, in a memory of its own (big endian, like
			// the machine; addresses from CtBase): its value replaces the call (a struct value: read-only
			// data in the program). What it cannot do (memory outside, other subs, low blocks...) makes
			// a constexpr call a normal one, and a consteval call an error.
			struct CtFail { SourcePos pos; std::string why; };
			enum class CtFlow { Normal, Break, Continue, Return };
			std::vector<uint8_t> m_ct;
			int m_ctDepth { 0 };
			int64_t m_ctSteps { 0 };
			std::string m_ctWhy;						// why the last constexpr call was not evaluated
			std::map<VarInfo*, std::vector<uint8_t>> m_ctBlobs;	// read-only data known when compiling (struct constants' copies, values, constant arrays)
			std::map<VarInfo*, uint32_t> m_ctGlobals;	// their copies in the memory (this evaluation)
			std::vector<FuncInfo*> m_ctFuncs;			// the subs being evaluated (innermost last)
			int m_ctBlobCount { 0 };
			std::vector<VarInfo*> m_lazyData;			// read-only data (in m_ctBlobs) put in the program if its address is used
			void ctFold(Node* call, FuncInfo* f);
			bool ctArgument(Node* arg, Type* param, uint32_t& out);
			[[noreturn]] void ctFail(const SourcePos& pos, const std::string& why);
			uint32_t ctAlloc(uint32_t size);
			uint32_t ctString(const std::string& text);
			uint32_t ctBlobAddress(VarInfo* v);
			uint8_t* ctBytes(uint32_t addr, uint32_t size, const SourcePos& pos);
			uint32_t ctLoad(uint32_t addr, Type* t, const SourcePos& pos);
			void ctStore(uint32_t addr, Type* t, uint32_t value, const SourcePos& pos);
			uint32_t ctConvert(uint32_t value, Type* from, Type* to);
			uint32_t ctCall(FuncInfo* f, const std::vector<uint32_t>& args, const SourcePos& pos);
			CtFlow ctStatement(Node* s, uint32_t& result);
			void ctLocal(Node* s);
			void ctInit(uint32_t addr, Type* type, Node* init);
			uint32_t ctEval(Node* e);
			uint32_t ctAddr(Node* e);
			uint32_t ctArith(const std::string& op, uint32_t a, Type* ta, uint32_t b, Type* tb, Type* result, const SourcePos& pos);
			// Read-only data with these bytes (a struct value computed when compiling).
			VarInfo* ctBlob(Type* t, const std::vector<uint8_t>& bytes);
			// A struct constant's fields from its bytes (constexpr P: Point = make(...)).
			void ctFields(StructInfo* info, const std::vector<uint8_t>& bytes, uint32_t offset, const std::string& prefix, ConstInfo& c);

			// ===== library interfaces =====
			std::string interfaceType(Type* t);
			std::string interfaceValue(Node* init, Type* t);
			std::string interfaceNumber(int64_t value, Type* t);
	};
}
