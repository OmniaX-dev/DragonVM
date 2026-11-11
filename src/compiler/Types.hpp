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
			Type* pointer(Type* base);
			Type* array(Type* base, uint32_t count);
			Type* function(const std::vector<Type*>& params, Type* ret);
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
