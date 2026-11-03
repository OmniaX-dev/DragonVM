#include "Types.hpp"

namespace dscript
{
	uint32_t Type::size(void) const
	{
		switch (kind)
		{
			case Kind::Void: return 0;
			case Kind::Bool: case Kind::I8: case Kind::U8: return 1;
			case Kind::I16: case Kind::U16: return 2;
			case Kind::I32: case Kind::U32: case Kind::Null: case Kind::Pointer: case Kind::Function: return 4;
			case Kind::Array: return base->size() * count;
			case Kind::Struct: return structInfo->size;
		}
		return 0;
	}

	std::string Type::name(bool brief) const
	{
		switch (kind)
		{
			case Kind::Void: return "void";
			case Kind::Bool: return "bool";
			case Kind::I8: return "i8";
			case Kind::U8: return "u8";
			case Kind::I16: return "i16";
			case Kind::U16: return "u16";
			case Kind::I32: return "i32";
			case Kind::U32: return "u32";
			case Kind::Null: return "null";
			case Kind::Pointer: return (base->kind == Kind::Function ? "(" + base->name(brief) + ")" : base->name(brief)) + "*";
			case Kind::Array: return base->name(brief) + "[" + std::to_string(count) + "]";
			case Kind::Struct: return (brief && structInfo->anonymous) ? "struct { ... }" : structInfo->displayName();
			case Kind::Function:
			{
				std::string s = "sub(";
				for (size_t i = 0; i < params.size(); i++)
					s += (i > 0 ? ", " : "") + params[i]->name(brief);
				return s + ") -> " + base->name(brief);
			}
		}
		return "?";
	}

	std::string StructInfo::displayName(void) const
	{
		if (!anonymous)
			return name;
		return "struct { ... } (" + pos.file + ":" + std::to_string(pos.line) + ")";
	}

	const StructField* StructInfo::field(const std::string& fieldName) const
	{
		for (const auto& f : fields)
		{
			if (f.name == fieldName)
				return &f;
		}
		return nullptr;
	}

	TypeTable::TypeTable(void)
	{
		m_void = make(Type::Kind::Void);
		m_bool = make(Type::Kind::Bool);
		m_i8 = make(Type::Kind::I8);
		m_u8 = make(Type::Kind::U8);
		m_i16 = make(Type::Kind::I16);
		m_u16 = make(Type::Kind::U16);
		m_i32 = make(Type::Kind::I32);
		m_u32 = make(Type::Kind::U32);
		m_null = make(Type::Kind::Null);
	}

	Type* TypeTable::make(Type::Kind kind)
	{
		Type* t = new Type();
		t->kind = kind;
		m_types.push_back(t);
		return t;
	}

	Type* TypeTable::pointer(Type* base)
	{
		for (Type* t : m_types)
		{
			if (t->kind == Type::Kind::Pointer && t->base == base)
				return t;
		}
		Type* t = make(Type::Kind::Pointer);
		t->base = base;
		return t;
	}

	Type* TypeTable::array(Type* base, uint32_t count)
	{
		for (Type* t : m_types)
		{
			if (t->kind == Type::Kind::Array && t->base == base && t->count == count)
				return t;
		}
		Type* t = make(Type::Kind::Array);
		t->base = base;
		t->count = count;
		return t;
	}

	Type* TypeTable::function(const std::vector<Type*>& params, Type* ret)
	{
		for (Type* t : m_types)
		{
			if (t->kind == Type::Kind::Function && t->base == ret && t->params == params)
				return t;
		}
		Type* t = make(Type::Kind::Function);
		t->base = ret;
		t->params = params;
		return t;
	}

	Type* TypeTable::structType(StructInfo* info)
	{
		for (Type* t : m_types)
		{
			if (t->kind == Type::Kind::Struct && t->structInfo == info)
				return t;
		}
		Type* t = make(Type::Kind::Struct);
		t->structInfo = info;
		return t;
	}

	Type* TypeTable::builtin(const std::string& name)
	{
		if (name == "void") return m_void;
		if (name == "bool") return m_bool;
		if (name == "i8") return m_i8;
		if (name == "u8" || name == "char") return m_u8;
		if (name == "i16") return m_i16;
		if (name == "u16") return m_u16;
		if (name == "i32" || name == "int") return m_i32;
		if (name == "u32" || name == "uint") return m_u32;
		if (name == "string") return pointer(m_u8);
		return nullptr;
	}
}
