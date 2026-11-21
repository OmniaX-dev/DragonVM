#pragma once

#include "AST.hpp"

namespace dscript
{
	struct StructInfo;

	// Types are interned: two equal types are the same Type object.
	struct Type
	{
		enum class Kind { Void, Bool, I8, U8, I16, U16, I32, U32, F32, Null, Pointer, Array, Struct, Function };
		Kind kind { Kind::Void };
		Type* base { nullptr };				// Pointer, Array: element type; Function: return type
		uint32_t count { 0 };				// Array
		StructInfo* structInfo { nullptr };	// Struct
		std::vector<Type*> params;			// Function
		int variadic { 0 };					// Function: 1 ... (more arguments), 2 ...typed
		uint32_t owned { 0 };				// Function: bit i, parameter i is own (the sub destroys it, not the caller)
		// Pointer: what it points at is constant (const T*: reading it is fine, changing it through
		// the pointer is an error). Values have no const of their own: only what a pointer points at.
		bool constTarget { false };
		// Pointer: a reference (ref T / const ref T: a parameter or a result; its uses are what it
		// refers to). It is passed as a pointer.
		bool isRef { false };
		// A template's value argument (not a type: Array<u32, 16>): its value is the compile-time
		// constant valueConst (of type base); name() is how it is written in the instance's name.
		bool valueArg { false };
		std::string valueConst;
		std::string valueText;

		bool isInteger(void) const { return kind >= Kind::Bool && kind <= Kind::U32; }
		bool isSigned(void) const { return kind == Kind::I8 || kind == Kind::I16 || kind == Kind::I32; }
		bool isFloat(void) const { return kind == Kind::F32; }
		// Integers and floats: what arithmetic and comparisons of numbers work on.
		bool isArithmetic(void) const { return isInteger() || isFloat(); }
		bool isPointer(void) const { return kind == Kind::Pointer || kind == Kind::Null; }
		bool isFunction(void) const { return kind == Kind::Function; }
		// Fits in a register (integers, floats, pointers, function pointers).
		bool isScalar(void) const { return isInteger() || isFloat() || isPointer() || isFunction(); }
		bool isVoidPointer(void) const { return kind == Kind::Pointer && base->kind == Kind::Void; }
		bool isConstPointer(void) const { return kind == Kind::Pointer && constTarget; }
		bool isReference(void) const { return kind == Kind::Pointer && isRef; }
		uint32_t size(void) const;
		// brief: nameless structs as "struct { ... }" (without where they are declared)
		std::string name(bool brief = false) const;
	};

	struct StructField
	{
		std::string name;
		Type* type { nullptr };
		uint32_t offset { 0 };
		Node* init { nullptr };		// default value (constant expression), or nullptr
		SourcePos pos;
		SourcePos namePos;
		bool isPrivate { false };	// priv var: only the struct's own subs can use it
		bool isReadonly { false };	// readonly var: everyone reads it, only the struct's own subs change it
		bool isConst { false };		// const: set when the struct is built (default, { ... }, in new), then never changed
		bool isProtected { false };	// prot var: the class, the classes derived from it (and its friends) use it
		const StructInfo* owner { nullptr };	// a field a class inherits: the struct / class that declares it (nullptr: this one)
	};

	struct StructInfo
	{
		std::string name;
		std::vector<StructField> fields;
		uint32_t size { 0 };
		bool complete { false };	// layout computed
		bool resolving { false };
		Node* decl { nullptr };
		bool anonymous { false };	// struct { ... } used as a type (name is a hidden one)
		SourcePos pos;

		// ===== classes and interfaces =====
		// A class's fields start with those of the class it extends (base, at baseOffset: 0, or 4
		// when the class adds the vtable pointer its base has not), copied with their offsets in the
		// whole object (owner: where they are declared). An interface is only a vtable pointer.
		int kind { 0 };				// 0 struct, 1 class, 2 interface
		bool isAbstract { false };
		bool isFinal { false };
		StructInfo* base { nullptr };
		uint32_t baseOffset { 0 };
		std::vector<StructInfo*> interfaces;	// impl (an interface: the one it extends)
		bool dynamic { false };		// a vtable pointer at offset 0 (a class with virtual subs, an interface)
		// Every interface the class can be used as, and where its vtable pointer is in the object.
		struct IfaceSlot { StructInfo* iface; uint32_t offset; };
		std::vector<IfaceSlot> ifaceSlots;
		// The vtable pointers in the object: their offset, and whose table layout they follow (the
		// class itself at 0, or an interface).
		struct Vptr { uint32_t offset; StructInfo* view; };
		std::vector<Vptr> vptrs;
		// The table (built once the subs have their types): a slot per virtual sub (the base's
		// first), the sub that introduced it and the one this class calls (nullptr: pure).
		struct VSlot { FuncInfo* decl; FuncInfo* impl; };
		std::vector<VSlot> vslots;
		bool vtablesBuilt { false };
		bool vtablesBuilding { false };
		std::vector<std::string> friendClasses;	// full names
		std::vector<std::string> friendSubs;
		bool friendsResolved { false };
		bool isClass(void) const { return kind == 1; }
		bool isInterface(void) const { return kind == 2; }
		// Can't be a value (only pointed at): an interface, an abstract class.
		bool onlyPointer(void) const { return kind == 2 || isAbstract; }

		const StructField* field(const std::string& name) const;
		// The name for messages ("struct { ... } (file:line)" for nameless structs).
		std::string displayName(void) const;
	};

	class TypeTable
	{
		public:
			TypeTable(void);
			Type* voidType(void) { return m_void; }
			Type* boolType(void) { return m_bool; }
			Type* i8(void) { return m_i8; }
			Type* u8(void) { return m_u8; }
			Type* i16(void) { return m_i16; }
			Type* u16(void) { return m_u16; }
			Type* i32(void) { return m_i32; }
			Type* u32(void) { return m_u32; }
			Type* f32(void) { return m_f32; }
			Type* nullType(void) { return m_null; }
			Type* pointer(Type* base, bool constTarget = false, bool isRef = false);
			// A template's value argument (interned by its type and text).
			Type* valueArg(Type* type, const std::string& text, const std::string& constName);
			Type* findValueArg(Type* type, const std::string& text);
			Type* array(Type* base, uint32_t count);
			Type* function(const std::vector<Type*>& params, Type* ret, int variadic = 0, uint32_t owned = 0);
			Type* structType(StructInfo* info);
			// The builtin type named `name` (i32, u8, string, ...), or nullptr.
			Type* builtin(const std::string& name);

		private:
			Type* make(Type::Kind kind);

		private:
			std::vector<Type*> m_types;
			Type* m_void; Type* m_bool; Type* m_i8; Type* m_u8; Type* m_i16; Type* m_u16; Type* m_i32; Type* m_u32; Type* m_f32; Type* m_null;
	};
}
