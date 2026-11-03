#include "CodeGen.hpp"
#include <cstdio>
#include <algorithm>

// Registers used by the generated code:
//   R1..R16    expression temporaries (a stack: alloc() / release())
//   R17..R20   scratch for the code generator (copies, store-backs)
//   S1, S2     the $variables of low blocks
//   ACC        comparisons; RV return values; FP locals (FP + offset); PP parameters (PP - 4 * i)
// Every call saves and restores R1..R20 (the CPU's call frame), so temporaries survive calls.

namespace dscript
{
	static const int TempRegisters = 16;
	static const char* ScratchCounter = "R17";
	static const char* ScratchDst = "R18";
	static const char* ScratchSrc = "R19";
	static const char* ScratchAddr = "R20";
	static const char* LowScratch[2] = { "reg(0x07)", "reg(0x08)" };

	static uint32_t alignUp(uint32_t value, uint32_t align) { return (value + align - 1) / align * align; }

	// The first low block in a sub's body (nullptr: none).
	static const Node* findLow(const Node* n)
	{
		if (n == nullptr)
			return nullptr;
		if (n->kind == NodeKind::LowBlock)
			return n;
		for (const Node* c : { n->a, n->b, n->c, n->d })
		{
			if (const Node* low = findLow(c))
				return low;
		}
		for (const Node* c : n->list)
		{
			if (const Node* low = findLow(c))
				return low;
		}
		return nullptr;
	}

	// The dasm symbol of a global: local constants and statics are named <sub>::<name>.
	static std::string dataSymbol(const std::string& name)
	{
		if (name.find(':') == std::string::npos)
			return name;
		std::string s = "__static_";
		for (char c : name)
			s += (c == ':' ? '_' : c);
		return s;
	}

	// An identifier in a low block ($name): letters, digits, _ and the :: of local constants /
	// statics (<sub>::<name>).
	static size_t lowNameEnd(const std::string& line, size_t j)
	{
		while (j < line.size())
		{
			if (std::isalnum((unsigned char)line[j]) || line[j] == '_')
				j++;
			else if (line[j] == ':' && j + 2 < line.size() && line[j + 1] == ':' && (std::isalnum((unsigned char)line[j + 2]) || line[j + 2] == '_'))
				j += 2;
			else
				break;
		}
		return j;
	}

	// The name of a local constant / static as written (hidden name: <sub>::<name>[::<n>]).
	// Hidden names: <sub>::<name>[::<n>] (local constants and statics; <sub> can be a method,
	// <Struct>::<method>) and <Struct>::<method>. The parts, and the name without the <n>.
	static std::vector<std::string> nameParts(const std::string& name)
	{
		std::vector<std::string> parts;
		size_t start = 0;
		while (true)
		{
			size_t p = name.find("::", start);
			parts.push_back(name.substr(start, p == std::string::npos ? std::string::npos : p - start));
			if (p == std::string::npos)
				break;
			start = p + 2;
		}
		if (parts.size() > 2 && !parts.back().empty() && std::all_of(parts.back().begin(), parts.back().end(), ::isdigit))
			parts.pop_back();
		return parts;
	}

	static std::string shownName(const std::string& name)
	{
		return nameParts(name).back();
	}

	// "in sub X" for local constants / statics ("" otherwise).
	static std::string ownerOf(const std::string& name)
	{
		std::vector<std::string> parts = nameParts(name);
		if (parts.size() < 2)
			return "";
		parts.pop_back();
		if (parts[0].rfind("__lambda_", 0) == 0)
			return "in a lambda";
		return parts.size() == 2 ? "in method " + parts[0] + "." + parts[1] : "in sub " + parts[0];
	}

	// The dasm label of a sub (methods: <Struct>::<name>).
	static std::string codeLabel(const std::string& name)
	{
		if (name.find(':') == std::string::npos)
			return name;
		std::string s = "__method_";
		for (char c : name)
			s += (c == ':' ? '_' : c);
		return s;
	}

	static std::string comment(const std::vector<std::string>& notes)
	{
		std::string s;
		for (const auto& n : notes)
		{
			if (!n.empty())
				s += (s.empty() ? "  // " : ", ") + n;
		}
		return s;
	}

	static std::string quoted(const std::string& text)
	{
		std::string s = "\"";
		for (unsigned char c : text)
		{
			if (c == '\n') s += "\\n";
			else if (c == '\t') s += "\\t";
			else if (c == '\r') s += "\\r";
			else if (c == '"') s += "\\\"";
			else if (c == '\\') s += "\\\\";
			else if (c < 32 || c >= 127)
			{
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\x%02X", c);
				s += buf;
			}
			else
				s += (char)c;
		}
		return s + "\"";
	}

	static std::string number(int64_t value)
	{
		std::string s = std::to_string(value);
		if (value < 0 || value > 9)
		{
			char buf[16];
			std::snprintf(buf, sizeof(buf), " (0x%X)", (uint32_t)value);
			s += buf;
		}
		return s;
	}

	static std::string bytes(uint32_t size)
	{
		return std::to_string(size) + (size == 1 ? " byte" : " bytes");
	}

	// Does the sub (FuncDecl) have a parameter or a local variable with this name?
	static bool declaresName(const Node* n, const std::string& name, bool top = true)
	{
		if (n == nullptr)
			return false;
		if (top)
		{
			for (const Node* p : n->list)
			{
				if (p->name == name)
					return true;
			}
			return declaresName(n->a, name, false);
		}
		if (n->kind == NodeKind::VarDecl && n->name == name)
			return true;
		for (const Node* c : { n->a, n->b, n->c, n->d })
		{
			if (declaresName(c, name, false))
				return true;
		}
		for (const Node* c : n->list)
		{
			if (declaresName(c, name, false))
				return true;
		}
		return false;
	}

	static bool isPowerOfTwo(uint32_t v, int& shift)
	{
		if (v == 0 || (v & (v - 1)) != 0)
			return false;
		shift = 0;
		while ((1u << shift) != v)
			shift++;
		return true;
	}

	// The value as stored in a variable of type t.
	static int64_t wrapTo(int64_t value, Type* t)
	{
		switch (t->kind)
		{
			case Type::Kind::Bool: return value != 0;
			case Type::Kind::I8: return (int8_t)value;
			case Type::Kind::U8: return (uint8_t)value;
			case Type::Kind::I16: return (int16_t)value;
			case Type::Kind::U16: return (uint16_t)value;
			case Type::Kind::I32: return (int32_t)value;
			default: return (uint32_t)value;
		}
	}

	CodeGen::CodeGen(const CodeGenOptions& options) : m_options(options)
	{
	}

	std::string CodeGen::imm(int64_t value)
	{
		char buf[16];
		std::snprintf(buf, sizeof(buf), "0x%08X", (uint32_t)value);
		return buf;
	}

	// ===================================== Declarations =====================================
	void CodeGen::declareName(const std::string& name, const SourcePos& pos, const std::string& what)
	{
		if (m_names.count(name) > 0)
			error(pos, "'" + name + "' is already declared (as " + m_names[name] + ")");
		if (m_types.builtin(name) != nullptr)
			error(pos, "'" + name + "' is a builtin type name");
		m_names[name] = what;
	}

	void CodeGen::collect(Node* program)
	{
		for (Node* d : program->list)
		{
			attempt([&] {
				switch (d->kind)
				{
					case NodeKind::StructDecl:
					{
						declareName(d->name, d->pos, "a struct");
						StructInfo* info = new StructInfo();
						info->name = d->name;
						info->decl = d;
						info->anonymous = d->value != 0;
						info->pos = d->pos;
						m_structs[d->name] = info;
					}
					break;
					case NodeKind::EnumDecl:
						declareName(d->name, d->pos, "an enum");
						m_enums[d->name].decl = d;
					break;
					case NodeKind::AliasDecl:
						declareName(d->name, d->pos, "an alias");
						m_aliases[d->name].decl = d;
					break;
					case NodeKind::ConstDecl:
						if (d->typeSyntax != nullptr && d->typeSyntax->kind == TypeSyntax::Kind::Array)
						{
							// const Name: T[N] = ...: read-only data in the program.
							declareName(d->name, d->pos, "a constant array");
							VarInfo* v = new VarInfo();
							v->name = d->name;
							v->storage = VarInfo::Storage::Global;
							v->symbol = dataSymbol(d->name);
							v->pos = d->pos;
							v->defPos = d->namePos;
							v->readOnly = true;
							m_globals[d->name] = v;
							m_globalDecls.push_back(d);
							break;
						}
						declareName(d->name, d->pos, "a constant");
						m_consts[d->name].decl = d;
					break;
					case NodeKind::VarDecl:
					{
						declareName(d->name, d->pos, "a global variable");
						VarInfo* v = new VarInfo();
						v->name = d->name;
						v->storage = VarInfo::Storage::Global;
						v->symbol = dataSymbol(d->name);
						v->pos = d->pos;
						v->defPos = d->namePos;
						m_globals[d->name] = v;
						m_globalDecls.push_back(d);
					}
					break;
					case NodeKind::FuncDecl:
					{
						if (m_funcs.count(d->name) == 0)
							declareName(d->name, d->pos, "a sub");
						FuncInfo* f = new FuncInfo();
						f->name = d->name;
						if (d->name.rfind("__lambda_", 0) == 0)
							f->name = "the lambda at " + posString(d->pos);
						f->decl = d;
						f->interrupt = (d->value & 1) != 0;
						f->isInline = (d->value & 2) != 0;
						f->defined = d->a != nullptr;
						m_funcs[d->name].push_back(f);
						m_funcList.push_back(f);
					}
					break;
					case NodeKind::LowBlock:
						m_topLows.push_back(d);
					break;
					default:
						error(d->pos, "unexpected declaration");
				}
			});
		}
	}

	bool CodeGen::isTypeName(const std::string& name)
	{
		return m_types.builtin(name) != nullptr || m_structs.count(name) > 0 || m_aliases.count(name) > 0 || m_enums.count(name) > 0;
	}

	Type* CodeGen::resolveType(TypeSyntax* ts)
	{
		switch (ts->kind)
		{
			case TypeSyntax::Kind::Named:
			{
				if (Type* t = m_types.builtin(ts->name))
				{
					if (m_options.index != nullptr)
						indexName(ts->pos, ts->name, {}, ts->name + comment({ "builtin type" + (t->name() != ts->name ? ": " + t->name() : std::string()),
							t->kind == Type::Kind::Void ? "" : bytes(t->size()) }));
					return t;
				}
				if (m_structs.count(ts->name) > 0)
				{
					StructInfo* info = m_structs[ts->name];
					if (m_options.index != nullptr)
						indexName(ts->pos, info->anonymous ? "struct" : ts->name, info->decl->namePos, hoverStruct(info));
					return m_types.structType(info);
				}
				if (m_enums.count(ts->name) > 0)
				{
					if (m_options.index != nullptr)
						indexName(ts->pos, ts->name, m_enums[ts->name].decl->namePos, hoverEnum(ts->name));
					return m_types.i32();
				}
				if (m_aliases.count(ts->name) > 0)
				{
					if (m_options.index != nullptr)
						indexName(ts->pos, ts->name, m_aliases[ts->name].decl->namePos, "alias " + ts->name);
					AliasInfo& a = m_aliases[ts->name];
					if (a.type == nullptr)
					{
						if (a.resolving)
							error(ts->pos, "alias '" + ts->name + "' refers to itself");
						a.resolving = true;
						a.type = resolveType(a.decl->typeSyntax);
						a.resolving = false;
					}
					return a.type;
				}
				error(ts->pos, "unknown type: " + ts->name);
			}
			case TypeSyntax::Kind::Pointer:
				return m_types.pointer(resolveType(ts->base));
			case TypeSyntax::Kind::Array:
			{
				if (ts->count == nullptr)
					error(ts->pos, "the array needs a size ([] is only allowed for the outer array of a variable or constant with a value)");
				Type* base = resolveType(ts->base);
				if (base->kind == Type::Kind::Void)
					error(ts->pos, "arrays of void are not allowed");
				if (base->kind == Type::Kind::Struct)
					resolveStruct(base->structInfo, ts->pos);
				int64_t count = constExpr(ts->count, "an array size");
				if (count <= 0 || count > 0x0FFFFFFF)
					error(ts->pos, "invalid array size: " + std::to_string(count));
				return m_types.array(base, (uint32_t)count);
			}
			case TypeSyntax::Kind::Function:
			{
				std::vector<Type*> params;
				for (TypeSyntax* p : ts->params)
				{
					Type* t = resolveType(p);
					if (!t->isScalar())
						error(p->pos, "sub parameters must be integers or pointers (not " + t->name() + ")");
					params.push_back(t);
				}
				Type* ret = ts->base ? resolveType(ts->base) : m_types.voidType();
				if (ret->kind != Type::Kind::Void && !ret->isScalar())
					error(ts->pos, "subs return integers, pointers or void (not " + ret->name() + ")");
				return m_types.function(params, ret);
			}
		}
		return nullptr;
	}

	void CodeGen::resolveStruct(StructInfo* info, const SourcePos& usePos)
	{
		if (info->complete)
			return;
		if (info->resolving)
			error(usePos, (info->anonymous ? info->displayName() : "struct '" + info->name + "'") + " contains itself (use a pointer)");
		info->resolving = true;
		uint32_t offset = 0;
		for (Node* f : info->decl->list)
		{
			if (info->field(f->name) != nullptr)
				error(f->pos, "duplicate field '" + f->name + "' in " + (info->anonymous ? info->displayName() : "struct " + info->name));
			if (m_funcs.count(info->name + "::" + f->name) > 0)
				error(f->namePos, "struct " + info->name + " has a field and a method named '" + f->name + "'");
			StructField field;
			field.name = f->name;
			field.pos = f->pos;
			field.namePos = f->namePos;
			field.type = resolveType(f->typeSyntax);
			if (field.type->kind == Type::Kind::Void)
				error(f->pos, "fields cannot be void");
			if (field.type->kind == Type::Kind::Struct)
				resolveStruct(field.type->structInfo, f->pos);
			field.offset = offset;
			field.init = f->a;
			offset += field.type->size();
			info->fields.push_back(field);
		}
		info->size = offset;
		info->complete = true;
		info->resolving = false;
		// Default values: constants of the field's type.
		for (auto& field : info->fields)
		{
			if (field.init == nullptr)
				continue;
			foldStringConst(field.init);
			if (!field.type->isScalar())
				error(field.pos, "only integer and pointer fields can have a default value");
			if (field.type->isFunction() && field.init->kind == NodeKind::Ident && m_funcs.count(field.init->name) > 0)
			{
				// A sub: checked once the subs have their types (resolveSubDefaults).
				m_subDefaults.push_back(&field);
				continue;
			}
			if (field.init->kind == NodeKind::StrLit && field.type->isPointer())
				continue;	// set at run time
			check(field.init, field.type);
			requireConvertible(field.init, field.type, "for the default value of field '" + field.name + "'");
			if (!field.init->isConst && !field.init->constPtr)
				error(field.init->pos, "default values must be constants");
		}
	}

	int64_t CodeGen::constExpr(Node* e, const std::string& what)
	{
		check(e);
		if (!e->isConst || !(e->type->isInteger() || e->type->kind == Type::Kind::Null))
			error(e->pos, what + " must be an integer constant");
		return e->constValue;
	}

	int64_t CodeGen::resolveConst(const std::string& name, const SourcePos& usePos)
	{
		ConstInfo& c = m_consts[name];
		if (c.resolved)
			return c.value;
		if (c.resolving)
			error(usePos, "constant '" + name + "' depends on itself");
		c.resolving = true;
		Type* t = c.decl->typeSyntax ? resolveType(c.decl->typeSyntax) : nullptr;
		if (c.decl->a != nullptr)
			foldStringConst(c.decl->a);
		if (c.decl->a != nullptr && c.decl->a->kind == NodeKind::StrLit)
		{
			// A string constant: used as that string literal (stored once in the program).
			Type* str = m_types.pointer(m_types.u8());
			if (t != nullptr && !(t->kind == Type::Kind::Pointer && t->base->size() == 1 && t->base->isInteger()))
				error(c.decl->pos, "a string constant is a string (u8* / i8*), not " + t->name());
			c.isString = true;
			c.text = c.decl->a->text;
			c.type = (t != nullptr ? t : str);
			c.resolved = true;
			c.resolving = false;
			return 0;
		}
		if (t != nullptr && t->kind == Type::Kind::Array)
			error(c.decl->pos, "constant arrays need their array type written in the declaration (const " + name + ": T[N] = ...)");
		if (t != nullptr && t->kind == Type::Kind::Struct)
		{
			resolveStruct(t->structInfo, c.decl->pos);
			constStructValues(t->structInfo, c.decl->a, "", c, name);
			c.type = t;
			c.resolved = true;
			c.resolving = false;
			return 0;
		}
		if (c.decl->a == nullptr)
			error(c.decl->pos, "constant '" + name + "' needs a value (only struct constants can use their fields' defaults)");
		if (c.decl->a->kind == NodeKind::InitList)
			error(c.decl->a->pos, "{ ... } is the value of struct constants only");
		int64_t value = constExpr(c.decl->a, "the value of constant '" + name + "'");
		if (t == nullptr)
			t = promote(c.decl->a->type);
		if (!t->isInteger())
			error(c.decl->pos, "constants are integers");
		c.value = wrapTo(value, t);
		c.type = t;
		c.resolved = true;
		c.resolving = false;
		return c.value;
	}

	void CodeGen::constStructValues(StructInfo* info, Node* init, const std::string& prefix, ConstInfo& c, const std::string& constName)
	{
		if (init != nullptr && init->kind != NodeKind::InitList)
			error(init->pos, "a struct constant's value is a { ... } list (or nothing: the fields' defaults)");
		if (init != nullptr && init->list.size() > info->fields.size())
			error(init->list[info->fields.size()]->pos, "too many values for " + (info->anonymous ? info->displayName() : "struct " + info->name));
		for (size_t i = 0; i < info->fields.size(); i++)
		{
			const StructField& field = info->fields[i];
			const std::string path = prefix + field.name;
			Node* v = (init != nullptr && i < init->list.size()) ? init->list[i] : nullptr;
			if (field.type->kind == Type::Kind::Struct)
			{
				resolveStruct(field.type->structInfo, field.pos);
				constStructValues(field.type->structInfo, v, path + ".", c, constName);
				continue;
			}
			if (!field.type->isScalar())
				error(v ? v->pos : c.decl->pos, "constant '" + constName + "': field '" + path + "' is " + field.type->name()
					+ " (struct constants can only have integer, pointer and struct fields)");
			if (v == nullptr)
				v = field.init;
			int64_t value = 0;
			if (v != nullptr)
			{
				foldStringConst(v);
				if (v->kind == NodeKind::InitList)
					error(v->pos, "constant '" + constName + "': field '" + path + "' is not a struct");
				if (v->kind == NodeKind::StrLit)
					error(v->pos, "constant '" + constName + "': field '" + path + "' cannot be a string (a struct constant has no memory)");
				check(v, field.type);
				requireConvertible(v, field.type, "for field '" + path + "' of constant '" + constName + "'");
				if (!v->isConst && !v->constPtr)
					error(v->pos, "constant '" + constName + "': the value of field '" + path + "' must be a constant");
				value = v->constValue;
			}
			c.fields[path] = wrapTo(value, field.type);
		}
	}

	bool CodeGen::sizeofConstStruct(Node* e, uint32_t& size)
	{
		Type* st = nullptr;
		std::string path;
		if (constStructRef(e, st, path) == nullptr)
			return false;
		size = st->size();
		return true;
	}

	void CodeGen::foldStringConst(Node* e)
	{
		// The name of a string constant -> that string literal.
		if (e == nullptr || e->kind != NodeKind::Ident || lookupVar(e->name) != nullptr || m_consts.count(e->name) == 0)
			return;
		resolveConst(e->name, e->pos);
		const ConstInfo& c = m_consts[e->name];
		if (!c.isString)
			return;
		if (m_options.index != nullptr)
			indexName(e->pos, (e->text.empty() ? e->name : e->text), c.decl->namePos, hoverConst(e->name));
		e->kind = NodeKind::StrLit;
		e->text = c.text;
	}

	void CodeGen::inferArraySize(TypeSyntax* ts, Node* init, const SourcePos& pos)
	{
		// T[] = value: the size of the value ({ ... }: its count; a string: its length + 1).
		if (ts == nullptr || ts->kind != TypeSyntax::Kind::Array || ts->count != nullptr)
			return;
		if (init == nullptr)
			error(pos, "an array without a size ([]) needs a value (its size is the value's)");
		foldStringConst(init);
		int64_t count = 0;
		if (init->kind == NodeKind::InitList)
			count = (int64_t)init->list.size();
		else if (init->kind == NodeKind::StrLit)
			count = (int64_t)init->text.size() + 1;
		else
			error(init->pos, "an array without a size ([]) is initialized with { ... } or a string");
		if (count == 0)
			error(init->pos, "an array without a size ([]) needs at least one value");
		Node* n = new Node();
		n->kind = NodeKind::IntLit;
		n->pos = ts->pos;
		n->value = count;
		ts->count = n;
	}

	CodeGen::ConstInfo* CodeGen::constStructRef(Node* e, Type*& structType, std::string& path)
	{
		// e names a struct constant (Name) or a struct field of one (Name.field...).
		if (e->kind == NodeKind::Ident)
		{
			if (lookupVar(e->name) != nullptr || m_consts.count(e->name) == 0)
				return nullptr;
			resolveConst(e->name, e->pos);
			ConstInfo& c = m_consts[e->name];
			if (c.type == nullptr || c.type->kind != Type::Kind::Struct)
				return nullptr;
			if (m_options.index != nullptr)
				indexName(e->pos, (e->text.empty() ? e->name : e->text), c.decl->namePos, hoverConst(e->name));
			structType = c.type;
			path.clear();
			return &c;
		}
		if (e->kind == NodeKind::Member && e->value == 0)
		{
			ConstInfo* c = constStructRef(e->a, structType, path);
			if (c == nullptr)
				return nullptr;
			const StructField* f = structType->structInfo->field(e->name);
			if (f == nullptr)
				error(e->namePos, (structType->structInfo->anonymous ? structType->structInfo->displayName() : "struct " + structType->structInfo->name) + " has no field '" + e->name + "'");
			if (f->type->kind != Type::Kind::Struct)
				return nullptr;
			if (m_options.index != nullptr)
				indexName(e->namePos, e->name, f->namePos, hoverField(structType->structInfo, *f));
			structType = f->type;
			path += e->name + ".";
			return c;
		}
		return nullptr;
	}

	void CodeGen::resolveEnum(EnumInfo& info)
	{
		if (info.resolved)
			return;
		if (info.resolving)
			error(info.decl->pos, "enum '" + info.decl->name + "' depends on itself");
		info.resolving = true;
		int64_t next = 0;
		for (Node* m : info.decl->list)
		{
			if (info.values.count(m->name) > 0)
				error(m->pos, "duplicate enum member: " + m->name);
			if (m->a)
				next = constExpr(m->a, "an enum value");
			info.values[m->name] = (int32_t)next;
			next++;
		}
		info.resolved = true;
		info.resolving = false;
	}

	std::string CodeGen::mangle(Type* t)
	{
		switch (t->kind)
		{
			case Type::Kind::Pointer: return mangle(t->base) + "p";
			case Type::Kind::Array: return mangle(t->base) + "a" + std::to_string(t->count);
			case Type::Kind::Struct: return "S" + t->structInfo->name;
			case Type::Kind::Function:
			{
				std::string s = "F";
				for (Type* p : t->params)
					s += mangle(p);
				return s + "r" + mangle(t->base);
			}
			default: return t->name();
		}
	}

	void CodeGen::resolveFunction(FuncInfo* f)
	{
		Node* d = f->decl;
		std::vector<Type*> params;
		bool seenDefault = false;
		for (Node* p : d->list)
		{
			Type* t = resolveType(p->typeSyntax);
			if (!t->isScalar())
			{
				if (t->kind == Type::Kind::Struct)
					error(p->pos, "structs are passed by pointer: " + p->name + ": " + t->name() + "*");
				if (t->kind == Type::Kind::Array)
					error(p->pos, "arrays are passed by pointer: " + p->name + ": " + t->base->name() + "*");
				error(p->pos, "invalid parameter type: " + t->name());
			}
			for (const auto& n : f->paramNames)
			{
				if (n == p->name)
					error(p->pos, "duplicate parameter: " + p->name);
			}
			params.push_back(t);
			f->paramNames.push_back(p->name);
			f->defaults.push_back(p->a);
			if (p->a)
				seenDefault = true;
			else if (seenDefault)
				error(p->pos, "parameters after one with a default value need a default value too");
			else
				f->requiredParams++;
		}
		Type* ret = d->typeSyntax ? resolveType(d->typeSyntax) : m_types.voidType();
		if (ret->kind != Type::Kind::Void && !ret->isScalar())
			error(d->pos, "subs return integers, pointers or void (return structs through a pointer parameter)");
		f->type = m_types.function(params, ret);
		if (f->interrupt && (!params.empty() || ret->kind != Type::Kind::Void))
			error(d->pos, "interrupt subs take no parameters and return void");
		if (f->isInline && f->name == "main")
			error(d->pos, "main cannot be inline");
		if (f->isInline && d->a)
		{
			if (const Node* low = findLow(d->a))
				error(low->pos, "inline subs cannot contain low blocks (the code is copied into every caller, "
					"and low code needs a real call frame): use a normal sub");
		}
		for (size_t i = 0; i < f->defaults.size(); i++)
		{
			if (f->defaults[i] == nullptr)
				continue;
			check(f->defaults[i], params[i]);
			requireConvertible(f->defaults[i], params[i], "for the default value of '" + f->paramNames[i] + "'");
			foldStringConst(f->defaults[i]);
			if (!f->defaults[i]->isConst && f->defaults[i]->kind != NodeKind::StrLit)
				error(f->defaults[i]->pos, "default values must be constants");
		}
	}

	// ======================================= Checking =======================================
	VarInfo* CodeGen::lookupVar(const std::string& name)
	{
		for (int i = (int)m_scopes.size() - 1; i >= 0; i--)
		{
			auto it = m_scopes[i].find(name);
			if (it != m_scopes[i].end())
				return it->second;
		}
		auto it = m_globals.find(name);
		if (it != m_globals.end() && it->second->type != nullptr)
			return it->second;
		return nullptr;
	}

	Type* CodeGen::decay(Type* t)
	{
		if (t != nullptr && t->kind == Type::Kind::Array)
			return m_types.pointer(t->base);
		return t;
	}

	Type* CodeGen::promote(Type* t)
	{
		if (t->kind == Type::Kind::U32)
			return t;
		if (t->isInteger())
			return m_types.i32();
		return t;
	}

	Type* CodeGen::arithmeticType(Type* a, Type* b)
	{
		if (promote(a)->kind == Type::Kind::U32 || promote(b)->kind == Type::Kind::U32)
			return m_types.u32();
		return m_types.i32();
	}

	FuncInfo* CodeGen::functionForType(const std::vector<FuncInfo*>& funcs, Type* fnType)
	{
		for (FuncInfo* f : funcs)
		{
			if (f->type == fnType)
				return f;
		}
		return nullptr;
	}

	// -1: not convertible; 0: same type; higher: worse (for choosing an overload).
	int CodeGen::conversionCost(Node* from, Type* to)
	{
		Type* f = from->type;
		if (f == nullptr)
		{
			// An overloaded sub name: fine if one of them has exactly this type.
			if (from->kind == NodeKind::Ident || (from->kind == NodeKind::Unary && from->name == "&"))
			{
				Node* id = (from->kind == NodeKind::Ident ? from : from->a);
				if (to->kind == Type::Kind::Function && m_funcs.count(id->name) > 0 && functionForType(m_funcs[id->name], to))
					return 0;
				if (to->isVoidPointer())
					return -1;
			}
			return -1;
		}
		if (f == to)
			return 0;
		if (to->kind == Type::Kind::Void)
			return -1;
		if (f->kind == Type::Kind::Array && to->kind == Type::Kind::Pointer)
		{
			if (to->base == f->base) return 1;
			if (to->isVoidPointer()) return 2;
			return -1;
		}
		if (f->isInteger() && to->isInteger())
		{
			if (to->kind == Type::Kind::Bool) return 3;
			if (f->kind == Type::Kind::Bool) return 2;
			if (to->size() > f->size() && to->isSigned() == f->isSigned()) return 1;
			if (to->size() > f->size() && to->isSigned()) return 2;
			return 3;
		}
		if (f->kind == Type::Kind::Null && (to->isPointer() || to->isFunction()))
			return 1;
		if (f->isInteger() && from->isConst && from->constValue == 0 && (to->isPointer() || to->isFunction()))
			return 2;
		if (f->kind == Type::Kind::Pointer && to->kind == Type::Kind::Pointer)
		{
			if (to->isVoidPointer() || f->isVoidPointer())
				return 1;
			return -1;
		}
		return -1;
	}

	void CodeGen::requireConvertible(Node* from, Type* to, const std::string& context)
	{
		if (conversionCost(from, to) >= 0)
			return;
		std::string fromName = (from->type ? from->type->name() : "overloaded sub '" + (from->kind == NodeKind::Ident ? from->name : from->a->name) + "'");
		std::string hint = "";
		if (from->type && to->isPointer() && from->type->isInteger())
			hint = " (use cast<" + to->name() + ">(...))";
		else if (from->type && to->isInteger() && (from->type->isPointer() || from->type->isFunction()))
			hint = " (use cast<" + to->name() + ">(...))";
		else if (from->type && from->type->isPointer() && to->isPointer())
			hint = " (use cast<" + to->name() + ">(...))";
		error(from->pos, "cannot convert " + fromName + " to " + to->name() + " " + context + hint);
	}

	FuncInfo* CodeGen::resolveOverload(const std::string& name, const std::vector<Node*>& args, const SourcePos& pos)
	{
		const auto& funcs = m_funcs[name];
		FuncInfo* best = nullptr;
		int bestCost = 0x7FFFFFFF;
		bool ambiguous = false;
		for (FuncInfo* f : funcs)
		{
			const auto& params = f->type->params;
			if (args.size() > params.size() || args.size() < f->requiredParams)
				continue;
			int cost = 0;
			bool ok = true;
			for (size_t i = 0; i < args.size() && ok; i++)
			{
				int c = conversionCost(args[i], params[i]);
				if (c < 0)
					ok = false;
				else
					cost += c;
			}
			if (!ok)
				continue;
			if (cost < bestCost)
			{
				best = f;
				bestCost = cost;
				ambiguous = false;
			}
			else if (cost == bestCost)
				ambiguous = true;
		}
		std::string argTypes = "(";
		for (size_t i = 0; i < args.size(); i++)
			argTypes += (i > 0 ? ", " : "") + (args[i]->type ? args[i]->type->name() : std::string("sub"));
		argTypes += ")";
		if (best == nullptr)
		{
			std::string msg = "no sub '" + name + "' takes " + argTypes;
			if (funcs.size() > 0)
			{
				msg += "; candidates:";
				for (FuncInfo* f : funcs)
					msg += "\n    " + name + f->type->name().substr(3) + " (" + posString(f->decl->pos) + ")";
			}
			error(pos, msg);
		}
		if (ambiguous)
			error(pos, "ambiguous call to overloaded sub '" + name + "' with " + argTypes + " (use casts)");
		return best;
	}

	void CodeGen::check(Node* e, Type* expected)
	{
		e->isConst = false;
		e->constPtr = false;
		e->lvalue = false;
		e->readOnly = false;
		switch (e->kind)
		{
			case NodeKind::IntLit:
				e->type = (e->name == "char") ? m_types.u8() : (e->value > 0x7FFFFFFF ? m_types.u32() : m_types.i32());
				e->isConst = true;
				e->constValue = e->value;
			break;
			case NodeKind::StrLit:
				e->type = m_types.pointer(m_types.u8());
			break;
			case NodeKind::BoolLit:
				e->type = m_types.boolType();
				e->isConst = true;
				e->constValue = e->value;
			break;
			case NodeKind::Null:
				e->type = m_types.nullType();
				e->isConst = true;
				e->constValue = 0;
			break;
			case NodeKind::Ident: checkIdent(e, expected); break;
			case NodeKind::Unary:
				if (e->name == "&" && e->a->kind == NodeKind::Ident && lookupVar(e->a->name) == nullptr && m_funcs.count(e->a->name) > 0)
				{
					checkIdent(e->a, expected);	// &sub: the sub's address (overloads: picked by the expected type)
					e->type = e->a->type;
					break;
				}
				checkUnary(e);
			break;
			case NodeKind::Binary: checkBinary(e); break;
			case NodeKind::Assign: checkAssign(e); break;
			case NodeKind::Call: checkCall(e); break;
			case NodeKind::Member: checkMember(e); break;
			case NodeKind::Ternary:
			{
				check(e->a);
				if (!decay(e->a->type)->isScalar())
					error(e->a->pos, "the condition must be an integer or a pointer");
				check(e->b, expected);
				check(e->c, expected);
				Type* tb = decay(e->b->type);
				Type* tc = decay(e->c->type);
				if (tb == nullptr || tc == nullptr)
					error(e->pos, "ambiguous overloaded sub in ?:");
				if (tb->isInteger() && tc->isInteger())
					e->type = (tb == tc ? tb : arithmeticType(tb, tc));
				else if (tb == tc && tb->isScalar())
					e->type = tb;
				else if (tb->isPointer() && tc->kind == Type::Kind::Null)
					e->type = tb;
				else if (tc->isPointer() && tb->kind == Type::Kind::Null)
					e->type = tc;
				else if ((tb->isFunction() && tc->kind == Type::Kind::Null) || (tc->isFunction() && tb->kind == Type::Kind::Null))
					e->type = tb->isFunction() ? tb : tc;
				else
					error(e->pos, "the two values of ?: have different types: " + tb->name() + " and " + tc->name());
				if (e->a->isConst && e->b->isConst && e->c->isConst)
				{
					e->isConst = true;
					e->constValue = wrapTo(e->a->constValue ? e->b->constValue : e->c->constValue, e->type);
				}
			}
			break;
			case NodeKind::Index:
			{
				check(e->a);
				check(e->b);
				Type* ta = decay(e->a->type);
				if (ta == nullptr || ta->kind != Type::Kind::Pointer)
					error(e->a->pos, "only arrays and pointers can be indexed");
				if (ta->base->kind == Type::Kind::Void)
					error(e->a->pos, "cannot index a void* (cast it first)");
				if (!e->b->type || !e->b->type->isInteger())
					error(e->b->pos, "the index must be an integer");
				e->type = ta->base;
				e->lvalue = true;
				e->readOnly = e->a->readOnly && e->a->type->kind == Type::Kind::Array;
			}
			break;
			case NodeKind::Cast:
			{
				Type* to = resolveType(e->typeSyntax);
				check(e->a, to->isFunction() ? to : nullptr);
				Type* from = decay(e->a->type);
				if (from == nullptr)
					error(e->a->pos, "ambiguous overloaded sub in cast");
				bool ok = (to->kind == Type::Kind::Void) || (from->isScalar() && to->isScalar());
				if (!ok)
					error(e->pos, "cannot cast " + from->name() + " to " + to->name());
				e->type = to;
				if (e->a->isConst && to->isInteger())
				{
					e->isConst = true;
					e->constValue = wrapTo(e->a->constValue, to);
				}
				else if ((e->a->isConst || e->a->constPtr) && to->isPointer())
				{
					e->constPtr = true;		// cast<T*>(constant): a constant address
					e->constValue = (uint32_t)e->a->constValue;
				}
			}
			break;
			case NodeKind::SizeofType:
			{
				Type* t = resolveType(e->typeSyntax);
				if (t->kind == Type::Kind::Struct)
					resolveStruct(t->structInfo, e->pos);
				if (t->size() == 0)
					error(e->pos, "sizeof(void)");
				e->type = m_types.u32();
				e->isConst = true;
				e->constValue = t->size();
			}
			break;
			case NodeKind::SizeofExpr:
			{
				uint32_t size = 0;
				if (e->a->kind == NodeKind::Ident && lookupVar(e->a->name) == nullptr && isTypeName(e->a->name))
				{
					TypeSyntax ts;
					ts.name = e->a->name;
					ts.pos = e->a->pos;
					Type* t = resolveType(&ts);
					if (t->kind == Type::Kind::Struct)
						resolveStruct(t->structInfo, e->pos);
					size = t->size();
				}
				else if (sizeofConstStruct(e->a, size))
					;	// a struct constant (or a struct field of one): its type's size
				else
				{
					check(e->a);
					if (e->a->type == nullptr || e->a->type->size() == 0)
						error(e->pos, "sizeof needs a value with a size");
					size = e->a->type->size();
				}
				e->type = m_types.u32();
				e->isConst = true;
				e->constValue = size;
			}
			break;
			case NodeKind::PreInc: case NodeKind::PreDec: case NodeKind::PostInc: case NodeKind::PostDec:
			{
				check(e->a);
				if (!e->a->lvalue || e->a->type->kind == Type::Kind::Array)
					error(e->pos, "++ and -- need a variable");
				if (e->a->readOnly)
					error(e->pos, "cannot change a constant array");
				if (!e->a->type->isInteger() && e->a->type->kind != Type::Kind::Pointer)
					error(e->pos, "++ and -- work on integers and pointers");
				e->type = e->a->type;
			}
			break;
			case NodeKind::InitList:
				error(e->pos, "{ ... } lists are only allowed to initialize variables");
			default:
				error(e->pos, "expression expected");
		}
	}

	void CodeGen::checkIdent(Node* e, Type* expected)
	{
		e->var = nullptr;
		e->func = nullptr;
		if (VarInfo* v = lookupVar(e->name))
		{
			if (m_options.index != nullptr && !m_discarding)
				indexName(e->pos, (e->text.empty() ? e->name : e->text), v->defPos, hoverVar(v));
			e->var = v;
			e->type = v->type;
			e->lvalue = true;
			e->readOnly = v->readOnly;
			return;
		}
		if (m_consts.count(e->name) > 0)
		{
			e->constValue = resolveConst(e->name, e->pos);
			if (m_options.index != nullptr)
				indexName(e->pos, (e->text.empty() ? e->name : e->text), m_consts[e->name].decl->namePos, hoverConst(e->name));
			if (m_consts[e->name].isString)
			{
				Type* t = m_consts[e->name].type;
				foldStringConst(e);
				check(e);
				e->type = t;
				return;
			}
			if (m_consts[e->name].type->kind == Type::Kind::Struct)
				error(e->pos, "'" + e->name + "' is a struct constant: it has no address, use its fields (" + e->name + ".field)");
			e->type = m_consts[e->name].type;
			e->isConst = true;
			return;
		}
		if (m_funcs.count(e->name) > 0)
		{
			const auto& funcs = m_funcs[e->name];
			if (funcs.size() == 1)
				e->func = funcs[0];
			else if (expected && expected->isFunction())
				e->func = functionForType(funcs, expected);
			e->type = e->func ? e->func->type : nullptr;
			if (e->func && !m_discarding)
				e->func->addressTaken = true;
			if (m_options.index != nullptr)
			{
				// Overloaded and not chosen by the expected type: all of them.
				std::string hover;
				for (FuncInfo* f : funcs)
				{
					if (e->func == nullptr || e->func == f)
						hover += (hover.empty() ? "" : "\n") + hoverFunc(f);
				}
				indexName(e->pos, e->text.empty() ? e->name : e->text, (e->func ? e->func : funcs[0])->decl->namePos, hover);
			}
			return;
		}
		if (m_func && m_inline.empty())
		{
			// A lambda using a local variable of the sub it is written in.
			for (Node* outer = m_func->decl->d; outer != nullptr; outer = outer->d)
			{
				if (declaresName(outer, e->name))
					error(e->pos, "lambdas cannot use the local variables or parameters of the sub around them ('" + e->name +
						"'): pass it as a parameter (or use a global)");
			}
		}
		if (m_globals.count(e->name) > 0)
		{
			if (m_inDeclarations)
				error(e->pos, "'" + e->name + "' is a variable, not a constant");
			error(e->pos, "global '" + e->name + "' is used in its own declaration");
		}
		if (isTypeName(e->name))
			error(e->pos, "'" + e->name + "' is a type, not a value");
		error(e->pos, "unknown name: " + e->name);
	}

	void CodeGen::checkUnary(Node* e)
	{
		const std::string& op = e->name;
		if (op == "&")
		{
			if (e->a->kind == NodeKind::Ident && lookupVar(e->a->name) == nullptr && m_funcs.count(e->a->name) > 0)
			{
				check(e->a);	// a sub: its address (the same as the sub name)
				e->type = e->a->type;
				return;
			}
			check(e->a);
			if (!e->a->lvalue)
				error(e->pos, "& needs a variable (or something stored in memory)");
			e->type = m_types.pointer(e->a->type);
			return;
		}
		check(e->a);
		Type* t = decay(e->a->type);
		if (t == nullptr)
			error(e->a->pos, "ambiguous overloaded sub");
		if (op == "*")
		{
			if (t->kind == Type::Kind::Null || t->kind != Type::Kind::Pointer)
				error(e->pos, "* needs a pointer (found " + t->name() + ")");
			if (t->base->kind == Type::Kind::Void)
				error(e->pos, "cannot dereference a void* (cast it first)");
			e->type = t->base;
			e->lvalue = true;
			return;
		}
		if (op == "!")
		{
			if (!t->isScalar())
				error(e->pos, "! needs an integer or a pointer");
			e->type = m_types.boolType();
			if (e->a->isConst)
			{
				e->isConst = true;
				e->constValue = e->a->constValue == 0;
			}
			return;
		}
		if (!t->isInteger())
			error(e->pos, op + " needs an integer (found " + t->name() + ")");
		e->type = promote(t);
		if (e->a->isConst)
		{
			e->isConst = true;
			e->constValue = wrapTo(op == "-" ? -e->a->constValue : ~e->a->constValue, e->type);
		}
	}

	static bool isComparison(const std::string& op)
	{
		return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
	}

	void CodeGen::checkBinary(Node* e)
	{
		check(e->a);
		check(e->b);
		const std::string& op = e->name;
		Type* ta = decay(e->a->type);
		Type* tb = decay(e->b->type);
		if (ta == nullptr || tb == nullptr)
			error(e->pos, "ambiguous overloaded sub");
		if (op == "&&" || op == "||")
		{
			if (!ta->isScalar() || !tb->isScalar())
				error(e->pos, op + " needs integers or pointers");
			e->type = m_types.boolType();
			if (e->a->isConst && e->b->isConst)
			{
				e->isConst = true;
				e->constValue = (op == "&&") ? (e->a->constValue && e->b->constValue) : (e->a->constValue || e->b->constValue);
			}
			return;
		}
		if (isComparison(op))
		{
			bool ok = (ta->isInteger() && tb->isInteger());
			if (ta->isPointer() && tb->isPointer())
				ok = (ta == tb || ta->kind == Type::Kind::Null || tb->kind == Type::Kind::Null || ta->isVoidPointer() || tb->isVoidPointer());
			if ((ta->isFunction() || tb->isFunction()) && (op == "==" || op == "!="))
				ok = (ta == tb || ta->kind == Type::Kind::Null || tb->kind == Type::Kind::Null);
			if ((ta->isPointer() && tb->isInteger() && e->b->isConst && e->b->constValue == 0) || (tb->isPointer() && ta->isInteger() && e->a->isConst && e->a->constValue == 0))
				ok = true;
			if (!ok)
				error(e->pos, "cannot compare " + ta->name() + " and " + tb->name());
			e->type = m_types.boolType();
			if (e->a->isConst && e->b->isConst && ta->isInteger() && tb->isInteger())
			{
				bool isUnsigned = arithmeticType(ta, tb)->kind == Type::Kind::U32;
				int64_t x = isUnsigned ? (int64_t)(uint32_t)e->a->constValue : (int64_t)(int32_t)e->a->constValue;
				int64_t y = isUnsigned ? (int64_t)(uint32_t)e->b->constValue : (int64_t)(int32_t)e->b->constValue;
				int64_t r = 0;
				if (op == "==") r = x == y; else if (op == "!=") r = x != y; else if (op == "<") r = x < y;
				else if (op == "<=") r = x <= y; else if (op == ">") r = x > y; else r = x >= y;
				e->isConst = true;
				e->constValue = r;
			}
			return;
		}
		// Pointer arithmetic.
		if (op == "+" && ta->kind == Type::Kind::Pointer && tb->isInteger()) { e->type = ta; return; }
		if (op == "+" && tb->kind == Type::Kind::Pointer && ta->isInteger()) { e->type = tb; return; }
		if (op == "-" && ta->kind == Type::Kind::Pointer && tb->isInteger()) { e->type = ta; return; }
		if (op == "-" && ta->kind == Type::Kind::Pointer && tb->kind == Type::Kind::Pointer)
		{
			if (ta != tb)
				error(e->pos, "cannot subtract " + tb->name() + " from " + ta->name());
			e->type = m_types.i32();
			return;
		}
		if (!ta->isInteger() || !tb->isInteger())
			error(e->pos, "'" + op + "' needs integers (found " + ta->name() + " and " + tb->name() + ")");
		e->type = (op == "<<" || op == ">>") ? promote(ta) : arithmeticType(ta, tb);
		if (e->a->isConst && e->b->isConst)
		{
			bool isUnsigned = e->type->kind == Type::Kind::U32;
			int64_t x = isUnsigned ? (int64_t)(uint32_t)e->a->constValue : (int64_t)(int32_t)e->a->constValue;
			int64_t y = isUnsigned ? (int64_t)(uint32_t)e->b->constValue : (int64_t)(int32_t)e->b->constValue;
			int64_t r = 0;
			if ((op == "/" || op == "%") && y == 0)
				error(e->pos, "division by zero");
			if (op == "+") r = x + y; else if (op == "-") r = x - y; else if (op == "*") r = x * y;
			else if (op == "/") r = x / y; else if (op == "%") r = x % y; else if (op == "&") r = x & y;
			else if (op == "|") r = x | y; else if (op == "^") r = x ^ y;
			else if (op == "<<") r = (int64_t)((uint32_t)x << (y & 31));
			else if (op == ">>") r = isUnsigned ? (int64_t)((uint32_t)x >> (y & 31)) : (int64_t)((int32_t)x >> (y & 31));
			e->isConst = true;
			e->constValue = wrapTo(r, e->type);
		}
	}

	void CodeGen::checkAssign(Node* e)
	{
		check(e->a);
		Type* t = e->a->type;
		if (!e->a->lvalue)
			error(e->a->pos, "cannot assign to this (not a variable)");
		if (e->a->readOnly)
			error(e->a->pos, "cannot assign to a constant array");
		if (t->kind == Type::Kind::Array)
			error(e->pos, "cannot assign to an array (assign its elements)");
		if (e->name == "=")
		{
			check(e->b, t);
			requireConvertible(e->b, t, "in the assignment");
			e->type = (t->kind == Type::Kind::Struct) ? m_types.voidType() : t;
			return;
		}
		check(e->b);
		Type* tb = decay(e->b->type);
		if (tb == nullptr || !tb->isInteger())
			error(e->b->pos, "'" + e->name + "' needs an integer value");
		bool pointerOk = (e->name == "+=" || e->name == "-=") && t->kind == Type::Kind::Pointer;
		if (!t->isInteger() && !pointerOk)
			error(e->pos, "'" + e->name + "' needs an integer variable");
		if (e->name == "/=" || e->name == "%=")
		{
			if (e->b->isConst && e->b->constValue == 0)
				error(e->pos, "division by zero");
		}
		e->type = t;
	}

	// Fields whose default value is a sub (var f: sub(i32) -> i32 = twice;): stored as its
	// address (the label), so they are checked after the subs (and their overloads) are known.
	void CodeGen::resolveSubDefaults(void)
	{
		for (const StructField* f : m_subDefaults)
		{
			attempt([&] {
				check(f->init, f->type);
				requireConvertible(f->init, f->type, "for the default value of field '" + f->name + "'");
				if (f->init->func == nullptr)
					error(f->init->pos, "which '" + f->init->name + "' (it is overloaded)?");
			});
		}
	}

	// x.method(args) / p->method(args): Struct::method(&x / p, args) (the call is rewritten).
	void CodeGen::rewriteMethodCall(Node* e)
	{
		Node* callee = e->a;
		Node* obj = callee->a;
		// Enum.Member and Constant.field are not methods.
		if (obj->kind == NodeKind::Ident && lookupVar(obj->name) == nullptr && (m_enums.count(obj->name) > 0 || m_consts.count(obj->name) > 0))
			return;
		check(obj);
		Type* t = obj->type;
		if (t == nullptr)
			return;
		if (callee->value == 1)
		{
			t = decay(t);
			if (t->kind != Type::Kind::Pointer || t->base->kind != Type::Kind::Struct)
				return;
			t = t->base;
		}
		else if (t->kind != Type::Kind::Struct)
			return;
		StructInfo* info = t->structInfo;
		resolveStruct(info, callee->pos);
		if (info->field(callee->name) != nullptr)
			return;		// a sub pointer field
		const std::string method = info->name + "::" + callee->name;
		if (m_funcs.count(method) == 0)
			error(callee->namePos, (info->anonymous ? info->displayName() : "struct " + info->name) + " has no field or method '" + callee->name + "'");
		Node* self = obj;
		if (callee->value == 0)
		{
			self = new Node();
			self->kind = NodeKind::Unary;
			self->name = "&";
			self->pos = obj->pos;
			self->a = obj;
		}
		Node* id = new Node();
		id->kind = NodeKind::Ident;
		id->name = method;
		id->text = callee->name;
		id->pos = callee->namePos;
		e->a = id;
		e->list.insert(e->list.begin(), self);
	}

	void CodeGen::checkCall(Node* e)
	{
		if (e->a->kind == NodeKind::Member)
			rewriteMethodCall(e);
		Node* callee = e->a;
		e->func = nullptr;
		if (callee->kind == NodeKind::Ident && lookupVar(callee->name) == nullptr && m_funcs.count(callee->name) > 0)
		{
			// The subs get their types after the declarations (constants, enum values, array
			// sizes, default values): a call there is not a constant.
			for (FuncInfo* f : m_funcs[callee->name])
			{
				if (f->type == nullptr)
					error(e->pos, "a sub call is not a constant");
			}
			for (Node* arg : e->list)
				check(arg);
			FuncInfo* f = resolveOverload(callee->name, e->list, e->pos);
			if (f->interrupt)
				error(e->pos, "interrupt subs cannot be called (they end with rti)");
			for (size_t i = 0; i < e->list.size(); i++)
			{
				check(e->list[i], f->type->params[i]);
				requireConvertible(e->list[i], f->type->params[i], "for parameter '" + f->paramNames[i] + "' of " + f->name);
			}
			e->func = f;
			e->type = f->type->base;
			if (m_options.index != nullptr)
				indexName(callee->pos, callee->text.empty() ? callee->name : callee->text, f->decl->namePos, hoverFunc(f));
			return;
		}
		check(callee);
		Type* t = callee->type;
		if (t != nullptr && t->kind == Type::Kind::Pointer && t->base->isFunction())
			t = t->base;
		if (t == nullptr || !t->isFunction())
			error(callee->pos, "this is not a sub (or a sub pointer)");
		if (e->list.size() != t->params.size())
			error(e->pos, "this sub takes " + std::to_string(t->params.size()) + " arguments, not " + std::to_string(e->list.size()));
		for (size_t i = 0; i < e->list.size(); i++)
		{
			check(e->list[i], t->params[i]);
			requireConvertible(e->list[i], t->params[i], "for argument " + std::to_string(i + 1));
		}
		e->type = t->base;
	}

	void CodeGen::checkMember(Node* e)
	{
		// Enum.Member
		if (e->value == 0 && e->a->kind == NodeKind::Ident && lookupVar(e->a->name) == nullptr && m_enums.count(e->a->name) > 0)
		{
			EnumInfo& info = m_enums[e->a->name];
			resolveEnum(info);
			if (m_options.index != nullptr)
				indexName(e->a->pos, e->a->name, info.decl->namePos, hoverEnum(e->a->name));
			if (info.values.count(e->name) == 0)
				error(e->pos, "enum " + e->a->name + " has no member '" + e->name + "'");
			if (m_options.index != nullptr)
			{
				for (const Node* m : info.decl->list)
				{
					if (m->name == e->name)
						indexName(e->namePos, e->name, m->namePos, e->a->name + "." + e->name + " = " + std::to_string(info.values[e->name]));
				}
			}
			e->type = m_types.i32();
			e->isConst = true;
			e->constValue = info.values[e->name];
			return;
		}
		// Constant.field: the field's value.
		if (e->value == 0)
		{
			Type* st = nullptr;
			std::string path;
			if (ConstInfo* c = constStructRef(e->a, st, path))
			{
				const StructField* f = st->structInfo->field(e->name);
				if (f == nullptr)
					error(e->namePos, (st->structInfo->anonymous ? st->structInfo->displayName() : "struct " + st->structInfo->name) + " has no field '" + e->name + "'");
				if (m_options.index != nullptr)
					indexName(e->namePos, e->name, f->namePos, hoverField(st->structInfo, *f));
				if (f->type->kind == Type::Kind::Struct)
					error(e->pos, "'" + e->name + "' is a struct field of a constant: it has no address, use its fields");
				e->type = f->type;
				e->constValue = c->fields[path + e->name];
				if (f->type->isInteger())
					e->isConst = true;
				else
					e->constPtr = true;	// pointers and sub pointers
				return;
			}
		}
		check(e->a);
		Type* t = e->a->type;
		if (t == nullptr)
			error(e->a->pos, "not a struct");
		if (e->value == 1)
		{
			t = decay(t);
			if (t->kind != Type::Kind::Pointer || t->base->kind != Type::Kind::Struct)
				error(e->pos, "-> needs a pointer to a struct (found " + t->name() + ")");
			t = t->base;
		}
		else if (t->kind != Type::Kind::Struct)
		{
			if (t->kind == Type::Kind::Pointer && t->base->kind == Type::Kind::Struct)
				error(e->pos, "'" + e->a->name + "' is a pointer: use -> to access its fields");
			error(e->pos, ". needs a struct (found " + t->name() + ")");
		}
		resolveStruct(t->structInfo, e->pos);
		const StructField* f = t->structInfo->field(e->name);
		if (f == nullptr)
			error(e->namePos, (t->structInfo->anonymous ? t->structInfo->displayName() : "struct " + t->structInfo->name) + " has no field '" + e->name + "'");
		if (m_options.index != nullptr && !m_discarding)
			indexName(e->namePos, e->name, f->namePos, hoverField(t->structInfo, *f));
		e->type = f->type;
		e->lvalue = (e->value == 1) || e->a->lvalue;
		e->readOnly = (e->value == 0) && e->a->readOnly;
	}

	// ==================================== Code generation ===================================
	void CodeGen::emit(const std::string& line)
	{
		m_out->push_back("\t" + line);
	}

	void CodeGen::label(const std::string& name)
	{
		m_out->push_back(name + ":");
	}

	std::string CodeGen::newLabel(void)
	{
		return "__L" + std::to_string(m_labelCounter++);
	}

	CodeGen::Reg CodeGen::alloc(const SourcePos& pos)
	{
		if (m_regDepth >= TempRegisters)
			error(pos, "expression too complex (more than 16 temporary values): split it");
		m_regDepth++;
		return "R" + std::to_string(m_regDepth);
	}

	void CodeGen::release(const Reg& r)
	{
		if (r.empty())
			return;
		if (r != "R" + std::to_string(m_regDepth))
			error({ "<compiler>", 0, 0 }, "internal error: registers released out of order (" + r + ")");
		m_regDepth--;
	}

	std::string CodeGen::stringLiteral(const std::string& text)
	{
		auto it = m_strings.find(text);
		if (it != m_strings.end())
			return it->second;
		std::string sym = "__S" + std::to_string(m_strings.size());
		m_strings[text] = sym;
		return sym;
	}

	void CodeGen::addImmediate(const Reg& r, int64_t value)
	{
		if ((uint32_t)value != 0)
			emit("addip " + r + ", " + imm(value));
	}

	void CodeGen::scale(const Reg& r, uint32_t size)
	{
		int shift = 0;
		if (size == 1)
			return;
		if (isPowerOfTwo(size, shift))
			emit("lsh " + r + ", " + std::to_string(shift));
		else
			emit("mulip " + r + ", " + imm(size));
	}

	void CodeGen::normalize(const Reg& r, Type* t)
	{
		switch (t->kind)
		{
			case Type::Kind::I8: emit("lsh " + r + ", 24"); emit("rsh " + r + ", 24"); break;
			case Type::Kind::U8: emit("andip " + r + ", 0x000000FF"); break;
			case Type::Kind::I16: emit("lsh " + r + ", 16"); emit("rsh " + r + ", 16"); break;
			case Type::Kind::U16: emit("andip " + r + ", 0x0000FFFF"); break;
			case Type::Kind::Bool:
			{
				std::string l = newLabel();
				emit("mov ACC, " + r);
				emit("mov " + r + ", 0");
				emit("jeq $" + l + ", 0");
				emit("mov " + r + ", 1");
				label(l);
			}
			break;
			default: break;
		}
	}

	void CodeGen::convert(const Reg& r, Type* from, Type* to)
	{
		if (from == to || !to->isInteger() || from == nullptr)
			return;
		const Type::Kind f = from->kind;
		switch (to->kind)
		{
			case Type::Kind::Bool: if (f != Type::Kind::Bool) normalize(r, to); break;
			case Type::Kind::I8: if (f != Type::Kind::Bool) normalize(r, to); break;
			case Type::Kind::U8: if (f != Type::Kind::Bool) normalize(r, to); break;
			case Type::Kind::I16: if (f != Type::Kind::Bool && f != Type::Kind::I8 && f != Type::Kind::U8) normalize(r, to); break;
			case Type::Kind::U16: if (f != Type::Kind::Bool && f != Type::Kind::U8) normalize(r, to); break;
			default: break;
		}
	}

	void CodeGen::load(const Reg& dst, const Reg& addr, Type* t)
	{
		if (!t->isScalar())
		{
			if (dst != addr)
				emit("mov " + dst + ", " + addr);
			return;
		}
		switch (t->size())
		{
			case 1: emit("movb " + dst + ", *" + addr); break;
			case 2: emit("movw " + dst + ", *" + addr); break;
			default: emit("mov " + dst + ", *" + addr); break;
		}
		if (t->kind == Type::Kind::I8 || t->kind == Type::Kind::I16)
			normalize(dst, t);
	}

	void CodeGen::store(const Reg& addr, const Reg& value, Type* t)
	{
		switch (t->size())
		{
			case 1: emit("movb *" + addr + ", " + value); break;
			case 2: emit("movw *" + addr + ", " + value); break;
			case 4: emit("mov *" + addr + ", " + value); break;
			default: copyMemory(addr, value, t->size()); break;
		}
	}

	void CodeGen::varAddress(const Reg& dst, VarInfo* v)
	{
		switch (v->storage)
		{
			case VarInfo::Storage::Global:
				emit("mov " + dst + ", $" + v->symbol);
			break;
			case VarInfo::Storage::Local:
				emit("mov " + dst + ", FP");
				addImmediate(dst, v->offset);
			break;
			case VarInfo::Storage::Param:
				// Arguments are 32-bit slots (big endian): a smaller value is in the slot's last bytes.
				emit("mov " + dst + ", PP");
				addImmediate(dst, -4 * v->offset + (4 - (int64_t)v->type->size()));
			break;
		}
	}

	void CodeGen::loadVar(const Reg& dst, VarInfo* v)
	{
		Type* t = v->type;
		if (!t->isScalar())
		{
			varAddress(dst, v);
			return;
		}
		const char* sizeSuffix = (t->size() == 1 ? "b" : (t->size() == 2 ? "w" : ""));
		if (v->storage == VarInfo::Storage::Global)
			emit(std::string("mov") + sizeSuffix + " " + dst + ", [$" + v->symbol + "]");
		else
		{
			const char* base = (v->storage == VarInfo::Storage::Local ? "FP" : "PP");
			int64_t offset = (v->storage == VarInfo::Storage::Local ? v->offset : -4 * v->offset + (4 - (int64_t)t->size()));
			emit(std::string("mov") + sizeSuffix + "o " + dst + ", *" + base + ", dword " + imm(offset));
		}
		if (t->kind == Type::Kind::I8 || t->kind == Type::Kind::I16)
			normalize(dst, t);
	}

	void CodeGen::copyMemory(const Reg& dst, const Reg& src, uint32_t size)
	{
		if (size == 0)
			return;
		emit("mov " + std::string(ScratchDst) + ", " + dst);
		emit("mov " + std::string(ScratchSrc) + ", " + src);
		const uint32_t words = size / 4;
		const std::string d = ScratchDst, s = ScratchSrc;
		if (words > 8)
		{
			std::string loop = newLabel();
			emit("mov " + std::string(ScratchCounter) + ", " + imm(words));
			label(loop);
			emit("mov *" + d + ", *" + s);
			emit("addip " + d + ", 4");
			emit("addip " + s + ", 4");
			emit("subip " + std::string(ScratchCounter) + ", 1");
			emit("mov ACC, " + std::string(ScratchCounter));
			emit("jne $" + loop + ", 0");
		}
		else
		{
			for (uint32_t i = 0; i < words; i++)
			{
				emit("mov *" + d + ", *" + s);
				emit("addip " + d + ", 4");
				emit("addip " + s + ", 4");
			}
		}
		for (uint32_t i = 0; i < size % 4; i++)
		{
			emit("movb *" + d + ", *" + s);
			emit("addip " + d + ", 1");
			emit("addip " + s + ", 1");
		}
	}

	void CodeGen::zeroMemory(const Reg& dst, uint32_t size)
	{
		if (size == 0)
			return;
		const std::string d = ScratchDst;
		emit("mov " + d + ", " + dst);
		const uint32_t words = size / 4;
		if (words > 8)
		{
			std::string loop = newLabel();
			emit("mov " + std::string(ScratchCounter) + ", " + imm(words));
			label(loop);
			emit("mov *" + d + ", 0");
			emit("addip " + d + ", 4");
			emit("subip " + std::string(ScratchCounter) + ", 1");
			emit("mov ACC, " + std::string(ScratchCounter));
			emit("jne $" + loop + ", 0");
		}
		else
		{
			for (uint32_t i = 0; i < words; i++)
			{
				emit("mov *" + d + ", 0");
				emit("addip " + d + ", 4");
			}
		}
		for (uint32_t i = 0; i < size % 4; i++)
		{
			emit("movb *" + d + ", 0");
			emit("addip " + d + ", 1");
		}
	}

	CodeGen::Reg CodeGen::gen(Node* e)
	{
		if ((e->isConst && e->type && (e->type->isInteger() || e->type->kind == Type::Kind::Null)) || e->constPtr)
		{
			Reg r = alloc(e->pos);
			emit("mov " + r + ", " + imm(e->constValue));
			return r;
		}
		switch (e->kind)
		{
			case NodeKind::StrLit:
			{
				Reg r = alloc(e->pos);
				emit("mov " + r + ", $" + stringLiteral(e->text));
				return r;
			}
			case NodeKind::Ident:
			{
				Reg r = alloc(e->pos);
				if (e->var)
					loadVar(r, e->var);
				else if (e->func)
					emit("mov " + r + ", $" + e->func->label);
				else
					error(e->pos, "ambiguous overloaded sub '" + e->name + "' (the context doesn't tell which one)");
				return r;
			}
			case NodeKind::Unary:
			{
				const std::string& op = e->name;
				if (op == "&")
				{
					if (e->a->func || (e->a->kind == NodeKind::Ident && !e->a->var))
						return gen(e->a);
					return genAddr(e->a);
				}
				Reg r = gen(e->a);
				if (op == "-")
					emit("neg " + r);
				else if (op == "~")
					emit("notip " + r);
				else if (op == "!")
				{
					std::string l = newLabel();
					emit("mov ACC, " + r);
					emit("mov " + r + ", 0");
					emit("jne $" + l + ", 0");
					emit("mov " + r + ", 1");
					label(l);
				}
				else if (op == "*")
					load(r, r, e->type);
				return r;
			}
			case NodeKind::Binary: return genBinary(e);
			case NodeKind::Assign: return genAssign(e);
			case NodeKind::Call: return genCall(e);
			case NodeKind::Ternary:
			{
				Reg r = alloc(e->pos);
				std::string lElse = newLabel(), lEnd = newLabel();
				genBranch(e->a, lElse, false);
				Reg v = gen(e->b);
				convert(v, decay(e->b->type), e->type);
				emit("mov " + r + ", " + v);
				release(v);
				emit("jmp $" + lEnd);
				label(lElse);
				v = gen(e->c);
				convert(v, decay(e->c->type), e->type);
				emit("mov " + r + ", " + v);
				release(v);
				label(lEnd);
				return r;
			}
			case NodeKind::Index:
			case NodeKind::Member:
			{
				Reg r = genAddr(e);
				load(r, r, e->type);
				return r;
			}
			case NodeKind::Cast:
			{
				if (e->type->kind == Type::Kind::Void)
				{
					Reg r = gen(e->a);
					return r;
				}
				Reg r = gen(e->a);
				convert(r, decay(e->a->type), e->type);
				return r;
			}
			case NodeKind::PreInc: case NodeKind::PreDec: case NodeKind::PostInc: case NodeKind::PostDec:
				return genIncDec(e);
			default:
				error(e->pos, "cannot generate code for this expression");
		}
	}

	CodeGen::Reg CodeGen::genAddr(Node* e)
	{
		switch (e->kind)
		{
			case NodeKind::Ident:
			{
				Reg r = alloc(e->pos);
				varAddress(r, e->var);
				return r;
			}
			case NodeKind::Unary:
				if (e->name == "*")
					return gen(e->a);
			break;
			case NodeKind::Index:
			{
				Reg base = (e->a->type->kind == Type::Kind::Array) ? genAddr(e->a) : gen(e->a);
				const uint32_t size = e->type->size();
				if (e->b->isConst)
					addImmediate(base, e->b->constValue * size);
				else
				{
					Reg i = gen(e->b);
					scale(i, size);
					emit("addip " + base + ", " + i);
					release(i);
				}
				return base;
			}
			case NodeKind::Member:
			{
				Type* st = (e->value == 1) ? decay(e->a->type)->base : e->a->type;
				Reg r = (e->value == 1) ? gen(e->a) : genAddr(e->a);
				addImmediate(r, st->structInfo->field(e->name)->offset);
				return r;
			}
			default: break;
		}
		error(e->pos, "this has no address");
	}

	// For "a OP b": the jump taken when the comparison is true / false. The CPU compares the
	// operand with ACC (jgr L, x: jump if x > ACC), so ACC holds a and the operand is b.
	static const char* jumpFor(const std::string& op, bool whenTrue)
	{
		if (op == "==") return whenTrue ? "jeq" : "jne";
		if (op == "!=") return whenTrue ? "jne" : "jeq";
		if (op == "<") return whenTrue ? "jgr" : "jle";		// b > a
		if (op == "<=") return whenTrue ? "jge" : "jls";	// b >= a
		if (op == ">") return whenTrue ? "jls" : "jge";		// b < a
		return whenTrue ? "jle" : "jgr";					// >=: b <= a
	}

	void CodeGen::genBranch(Node* e, const std::string& target, bool jumpIfTrue)
	{
		if (e->isConst)
		{
			if ((e->constValue != 0) == jumpIfTrue)
				emit("jmp $" + target);
			return;
		}
		if (e->kind == NodeKind::Unary && e->name == "!")
		{
			genBranch(e->a, target, !jumpIfTrue);
			return;
		}
		if (e->kind == NodeKind::Binary && (e->name == "&&" || e->name == "||"))
		{
			const bool isAnd = (e->name == "&&");
			if (isAnd != jumpIfTrue)
			{
				// && jumping when false, || jumping when true: either side decides.
				genBranch(e->a, target, jumpIfTrue);
				genBranch(e->b, target, jumpIfTrue);
			}
			else
			{
				std::string skip = newLabel();
				genBranch(e->a, skip, !jumpIfTrue);
				genBranch(e->b, target, jumpIfTrue);
				label(skip);
			}
			return;
		}
		if (e->kind == NodeKind::Binary && isComparison(e->name))
		{
			Type* ta = decay(e->a->type);
			Type* tb = decay(e->b->type);
			const bool isUnsigned = (ta->isInteger() && tb->isInteger()) ? arithmeticType(ta, tb)->kind == Type::Kind::U32 : true;
			Reg a = gen(e->a);
			std::string operand;
			Reg b;
			if (e->b->isConst)
				operand = imm(isUnsigned ? (e->b->constValue ^ 0x80000000) : e->b->constValue);
			else
			{
				b = gen(e->b);
				if (isUnsigned)
					emit("xorip " + b + ", 0x80000000");
				operand = b;
			}
			// Unsigned: flipping the sign bits makes the signed comparison of the CPU work.
			if (isUnsigned)
				emit("xorip " + a + ", 0x80000000");
			emit("mov ACC, " + a);
			emit(std::string(jumpFor(e->name, jumpIfTrue)) + " $" + target + ", " + operand);
			release(b);
			release(a);
			return;
		}
		Reg r = gen(e);
		emit("mov ACC, " + r);
		emit(std::string(jumpIfTrue ? "jne" : "jeq") + " $" + target + ", 0");
		release(r);
	}

	CodeGen::Reg CodeGen::genLogicalValue(Node* e)
	{
		Reg r = alloc(e->pos);
		std::string lFalse = newLabel(), lEnd = newLabel();
		genBranch(e, lFalse, false);
		emit("mov " + r + ", 1");
		emit("jmp $" + lEnd);
		label(lFalse);
		emit("mov " + r + ", 0");
		label(lEnd);
		return r;
	}

	CodeGen::Reg CodeGen::genBinary(Node* e)
	{
		const std::string& op = e->name;
		if (op == "&&" || op == "||" || isComparison(op))
			return genLogicalValue(e);
		Type* ta = decay(e->a->type);
		Type* tb = decay(e->b->type);
		// Pointer arithmetic.
		if (ta->kind == Type::Kind::Pointer || tb->kind == Type::Kind::Pointer)
		{
			if (ta->kind == Type::Kind::Pointer && tb->kind == Type::Kind::Pointer)
			{
				Reg a = gen(e->a);
				Reg b = gen(e->b);
				emit("subip " + a + ", " + b);
				release(b);
				const uint32_t size = std::max<uint32_t>(1, ta->base->size());
				int shift = 0;
				if (size > 1)
				{
					if (isPowerOfTwo(size, shift))
						emit("rsh " + a + ", " + std::to_string(shift));
					else
						emit("divip " + a + ", " + imm(size));
				}
				return a;
			}
			Node* ptr = (ta->kind == Type::Kind::Pointer) ? e->a : e->b;
			Node* idx = (ptr == e->a) ? e->b : e->a;
			const uint32_t size = std::max<uint32_t>(1, decay(ptr->type)->base->size());
			Reg p = gen(e->a);
			if (ptr == e->a && idx->isConst)
			{
				addImmediate(p, (op == "-" ? -idx->constValue : idx->constValue) * size);
				return p;
			}
			Reg i = gen(e->b);
			if (ptr == e->a)
			{
				scale(i, size);
				emit((op == "-" ? "subip " : "addip ") + p + ", " + i);
			}
			else
			{
				// int + pointer: the result goes in the first register.
				scale(p, size);
				emit("addip " + p + ", " + i);
			}
			release(i);
			return p;
		}
		const bool isUnsigned = e->type->kind == Type::Kind::U32;
		Reg a = gen(e->a);
		Reg b;
		std::string operand;
		if (e->b->isConst)
			operand = imm(e->b->constValue);
		else
		{
			b = gen(e->b);
			operand = b;
		}
		if (op == "+") emit("addip " + a + ", " + operand);
		else if (op == "-") emit("subip " + a + ", " + operand);
		else if (op == "*") emit("mulip " + a + ", " + operand);
		else if (op == "/" || op == "%")
		{
			emit((isUnsigned ? "divipu " : "divip ") + a + ", " + operand);
			if (op == "%")
				emit("mov " + a + ", RV");
		}
		else if (op == "&") emit("andip " + a + ", " + operand);
		else if (op == "|") emit("orip " + a + ", " + operand);
		else if (op == "^") emit("xorip " + a + ", " + operand);
		else if (op == "<<") emit("lsh " + a + ", " + (e->b->isConst ? std::to_string(e->b->constValue & 31) : operand));
		else if (op == ">>")
		{
			if (!isUnsigned)
				emit("rsh " + a + ", " + (e->b->isConst ? std::to_string(e->b->constValue & 31) : operand));
			else if (e->b->isConst)
			{
				const int n = (int)(e->b->constValue & 31);
				if (n > 0)
				{
					emit("rsh " + a + ", " + std::to_string(n));
					emit("andip " + a + ", " + imm(0xFFFFFFFFu >> n));
				}
			}
			else
			{
				// Logical shift right: a runtime helper.
				m_helpers.insert("__ds_shru");
				emit("push " + a);
				emit("push " + b);
				emit("push 2");
				emit("call $__ds_shru");
				emit("mov " + a + ", RV");
			}
		}
		release(b);
		return a;
	}

	CodeGen::Reg CodeGen::genAssign(Node* e)
	{
		Type* t = e->a->type;
		if (t->kind == Type::Kind::Struct)
		{
			Reg d = genAddr(e->a);
			Reg s = gen(e->b);
			copyMemory(d, s, t->size());
			release(s);
			release(d);
			return "";
		}
		Reg d = genAddr(e->a);
		if (e->name == "=")
		{
			Reg v = gen(e->b);
			convert(v, decay(e->b->type), t);
			store(d, v, t);
			emit("mov " + d + ", " + v);
			release(v);
			return d;
		}
		const std::string op = e->name.substr(0, e->name.size() - 1);
		Reg v = alloc(e->pos);
		load(v, d, t);
		Reg x;
		std::string operand;
		if (e->b->isConst)
			operand = imm(e->b->constValue);
		else
		{
			x = gen(e->b);
			operand = x;
		}
		if (t->kind == Type::Kind::Pointer)
		{
			const uint32_t size = std::max<uint32_t>(1, t->base->size());
			if (e->b->isConst)
				operand = imm(e->b->constValue * size);
			else
				scale(x, size);
			emit((op == "-" ? "subip " : "addip ") + v + ", " + operand);
		}
		else
		{
			const bool isUnsigned = arithmeticType(t, decay(e->b->type))->kind == Type::Kind::U32;
			if (op == "+") emit("addip " + v + ", " + operand);
			else if (op == "-") emit("subip " + v + ", " + operand);
			else if (op == "*") emit("mulip " + v + ", " + operand);
			else if (op == "/" || op == "%")
			{
				emit((isUnsigned ? "divipu " : "divip ") + v + ", " + operand);
				if (op == "%")
					emit("mov " + v + ", RV");
			}
			else if (op == "&") emit("andip " + v + ", " + operand);
			else if (op == "|") emit("orip " + v + ", " + operand);
			else if (op == "^") emit("xorip " + v + ", " + operand);
			else if (op == "<<") emit("lsh " + v + ", " + (e->b->isConst ? std::to_string(e->b->constValue & 31) : operand));
			else if (op == ">>")
			{
				const bool logical = promote(t)->kind == Type::Kind::U32;
				if (!logical)
					emit("rsh " + v + ", " + (e->b->isConst ? std::to_string(e->b->constValue & 31) : operand));
				else if (e->b->isConst)
				{
					const int n = (int)(e->b->constValue & 31);
					if (n > 0)
					{
						emit("rsh " + v + ", " + std::to_string(n));
						emit("andip " + v + ", " + imm(0xFFFFFFFFu >> n));
					}
				}
				else
				{
					m_helpers.insert("__ds_shru");
					emit("push " + v);
					emit("push " + x);
					emit("push 2");
					emit("call $__ds_shru");
					emit("mov " + v + ", RV");
				}
			}
			normalize(v, t);
		}
		release(x);
		store(d, v, t);
		emit("mov " + d + ", " + v);
		release(v);
		return d;
	}

	CodeGen::Reg CodeGen::genIncDec(Node* e)
	{
		Type* t = e->type;
		const bool inc = (e->kind == NodeKind::PreInc || e->kind == NodeKind::PostInc);
		const bool post = (e->kind == NodeKind::PostInc || e->kind == NodeKind::PostDec);
		const int64_t step = (t->kind == Type::Kind::Pointer) ? std::max<uint32_t>(1, t->base->size()) : 1;
		Reg d = genAddr(e->a);
		Reg v = alloc(e->pos);
		load(v, d, t);
		Reg n = alloc(e->pos);
		emit("mov " + n + ", " + v);
		addImmediate(n, inc ? step : -step);
		if (t->isInteger())
			normalize(n, t);
		store(d, n, t);
		emit("mov " + d + ", " + (post ? v : n));
		release(n);
		release(v);
		return d;
	}

	// Inline sub call: the arguments go into new locals of the caller, the body is generated
	// here (seeing only its parameters and the globals), return jumps to the end, with the
	// value in RV (as a real call).
	CodeGen::Reg CodeGen::genInlineCall(Node* e, FuncInfo* f)
	{
		for (const auto& c : m_inline)
		{
			if (c.func == f)
				error(e->pos, "inline sub '" + f->name + "' calls itself (directly or through another inline sub): "
					"inline subs cannot be recursive");
		}
		if (m_func == f)
			error(e->pos, "inline sub '" + f->name + "' calls itself: inline subs cannot be recursive");
		const auto& params = f->type->params;
		std::map<std::string, VarInfo*> paramScope;
		for (size_t i = 0; i < params.size(); i++)
		{
			VarInfo* v = new VarInfo();
			v->name = f->paramNames[i];
			v->type = params[i];
			v->storage = VarInfo::Storage::Local;
			v->pos = f->decl->list[i]->pos;
			v->defPos = f->decl->list[i]->namePos;
			v->param = true;
			if (m_options.index != nullptr && !m_discarding)
				indexName(v->defPos, v->name, v->defPos, hoverVar(v));
			m_localCursor -= 4;
			v->offset = m_localCursor;
			m_localMin = std::min(m_localMin, m_localCursor);
			Reg r;
			if (i < e->list.size())
			{
				r = gen(e->list[i]);
				convert(r, decay(e->list[i]->type), params[i]);
			}
			else
			{
				Node* def = f->defaults[i];
				r = alloc(e->pos);
				if (def->kind == NodeKind::StrLit)
					emit("mov " + r + ", $" + stringLiteral(def->text));
				else
					emit("mov " + r + ", " + imm(wrapTo(def->constValue, params[i])));
			}
			Reg a = alloc(e->pos);
			varAddress(a, v);
			store(a, r, params[i]);
			release(a);
			release(r);
			paramScope[v->name] = v;
		}
		// The body sees its parameters (and the globals), not the caller's variables or loops.
		auto scopes = std::move(m_scopes);
		auto loops = std::move(m_loops);
		m_scopes.clear();
		m_scopes.push_back(std::move(paramScope));
		m_loops.clear();
		m_inline.push_back({ f, newLabel() });
		const int stmtDepth = m_stmtDepth;
		m_stmtDepth = m_regDepth;	// the caller's temporaries stay in their registers
		Node* body = f->decl->a;
		for (Node* s : body->list)
			genStatement(s);
		const bool endsWithReturn = !body->list.empty() && body->list.back()->kind == NodeKind::Return;
		if (!endsWithReturn && f->type->base->kind != Type::Kind::Void)
			emit("mov RV, 0");
		label(m_inline.back().endLabel);
		m_inline.pop_back();
		m_stmtDepth = stmtDepth;
		m_scopes = std::move(scopes);
		m_loops = std::move(loops);
		if (f->type->base->kind == Type::Kind::Void)
			return "";
		Reg r = alloc(e->pos);
		emit("mov " + r + ", RV");
		return r;
	}

	CodeGen::Reg CodeGen::genCall(Node* e)
	{
		FuncInfo* f = e->func;
		if (f && f->isInline)
			return genInlineCall(e, f);
		Type* fnType = nullptr;
		Reg fr;
		if (f == nullptr)
		{
			fr = gen(e->a);
			fnType = e->a->type;
			if (fnType->kind == Type::Kind::Pointer)
			{
				load(fr, fr, fnType->base);
				fnType = fnType->base;
			}
		}
		else
			fnType = f->type;
		const auto& params = fnType->params;
		for (size_t i = 0; i < e->list.size(); i++)
		{
			Reg r = gen(e->list[i]);
			convert(r, decay(e->list[i]->type), params[i]);
			emit("push " + r);
			release(r);
		}
		for (size_t i = e->list.size(); i < params.size(); i++)
		{
			Node* def = f->defaults[i];
			if (def->kind == NodeKind::StrLit)
				emit("push $" + stringLiteral(def->text));
			else
				emit("push " + imm(wrapTo(def->constValue, params[i])));
		}
		emit("push " + std::to_string(params.size()));
		if (f)
			emit("call $" + f->label);
		else
			emit("call " + fr);
		if (fnType->base->kind == Type::Kind::Void)
		{
			release(fr);
			return "";
		}
		if (fr.empty())
			fr = alloc(e->pos);
		emit("mov " + fr + ", RV");
		return fr;
	}

	// ====================================== Statements ======================================
	void CodeGen::genInit(Reg addr, Type* type, Node* init, int32_t offset)
	{
		// addr holds the variable's address; the value at addr + offset is initialized.
		foldStringConst(init);
		if (type->kind == Type::Kind::Array && init->kind == NodeKind::StrLit)
		{
			if (type->base->size() != 1)
				error(init->pos, "strings can only initialize u8 / i8 arrays");
			if (init->text.size() + 1 > type->count && init->text.size() != type->count)
				error(init->pos, "the string is longer than the array (" + std::to_string(type->count) + " bytes)");
			Reg d = alloc(init->pos);
			emit("mov " + d + ", " + addr);
			addImmediate(d, offset);
			Reg s = alloc(init->pos);
			emit("mov " + s + ", $" + stringLiteral(init->text));
			copyMemory(d, s, std::min<uint32_t>((uint32_t)init->text.size() + 1, type->count));
			release(s);
			release(d);
			return;
		}
		if (init->kind == NodeKind::InitList)
		{
			if (type->kind == Type::Kind::Array)
			{
				if (init->list.size() > type->count)
					error(init->pos, "too many values for " + type->name());
				for (size_t i = 0; i < init->list.size(); i++)
					genInit(addr, type->base, init->list[i], offset + (int32_t)(i * type->base->size()));
				return;
			}
			if (type->kind == Type::Kind::Struct)
			{
				const auto& fields = type->structInfo->fields;
				if (init->list.size() > fields.size())
					error(init->pos, "too many values for struct " + type->name());
				for (size_t i = 0; i < init->list.size(); i++)
					genInit(addr, fields[i].type, init->list[i], offset + (int32_t)fields[i].offset);
				return;
			}
			error(init->pos, "{ ... } initializes arrays and structs, not " + type->name());
		}
		check(init, type);
		requireConvertible(init, type, "in the initialization");
		if (type->kind == Type::Kind::Struct)
		{
			Reg d = alloc(init->pos);
			emit("mov " + d + ", " + addr);
			addImmediate(d, offset);
			Reg s = gen(init);
			copyMemory(d, s, type->size());
			release(s);
			release(d);
			return;
		}
		if (type->kind == Type::Kind::Array)
			error(init->pos, "arrays are initialized with { ... } or a string");
		Reg v = gen(init);
		convert(v, decay(init->type), type);
		Reg d = alloc(init->pos);
		emit("mov " + d + ", " + addr);
		addImmediate(d, offset);
		store(d, v, type);
		release(d);
		release(v);
	}

	// The struct's default field values (the memory was zeroed).
	static void collectDefaults(Type* type, int32_t offset, std::vector<std::pair<int32_t, const StructField*>>& out)
	{
		if (type->kind == Type::Kind::Struct)
		{
			for (const auto& f : type->structInfo->fields)
			{
				if (f.init)
					out.push_back({ offset + (int32_t)f.offset, &f });
				else
					collectDefaults(f.type, offset + (int32_t)f.offset, out);
			}
		}
		else if (type->kind == Type::Kind::Array && type->base->kind == Type::Kind::Struct)
		{
			for (uint32_t i = 0; i < type->count; i++)
				collectDefaults(type->base, offset + (int32_t)(i * type->base->size()), out);
		}
	}

	void CodeGen::genLocalVar(Node* s)
	{
		if (m_scopes.back().count(s->name) > 0)
			error(s->pos, "'" + s->name + "' is already declared in this block");
		Type* t = nullptr;
		inferArraySize(s->typeSyntax, s->a, s->pos);
		if (s->typeSyntax)
			t = resolveType(s->typeSyntax);
		else
		{
			check(s->a);
			t = decay(s->a->type);
			if (t == nullptr || t->kind == Type::Kind::Null || t->kind == Type::Kind::Void)
				error(s->pos, "cannot tell the type of '" + s->name + "' (write var " + s->name + ": <type>)");
			if (t->isInteger() && s->a->kind == NodeKind::IntLit)
				t = m_types.i32();
		}
		if (t->kind == Type::Kind::Void)
			error(s->pos, "variables cannot be void");
		if (t->kind == Type::Kind::Struct)
			resolveStruct(t->structInfo, s->pos);
		VarInfo* v = new VarInfo();
		v->name = s->name;
		v->type = t;
		v->storage = VarInfo::Storage::Local;
		v->pos = s->pos;
		v->defPos = s->namePos;
		if (m_options.index != nullptr)
			indexName(v->defPos, v->name, v->defPos, hoverVar(v));
		m_localCursor -= (int32_t)alignUp(std::max<uint32_t>(t->size(), 1), 4);
		v->offset = m_localCursor;
		m_localMin = std::min(m_localMin, m_localCursor);

		const bool aggregate = (t->kind == Type::Kind::Struct || t->kind == Type::Kind::Array);
		std::vector<std::pair<int32_t, const StructField*>> defaults;
		collectDefaults(t, 0, defaults);
		if (aggregate && (s->a == nullptr || s->a->kind == NodeKind::InitList))
		{
			if (s->a != nullptr || !defaults.empty() || t->kind == Type::Kind::Struct)
			{
				Reg a = alloc(s->pos);
				varAddress(a, v);
				zeroMemory(a, t->size());
				for (const auto& d : defaults)
				{
					Reg val = alloc(s->pos);
					if (d.second->init->kind == NodeKind::StrLit)
						emit("mov " + val + ", $" + stringLiteral(d.second->init->text));
					else if (d.second->init->kind == NodeKind::Ident && d.second->init->func != nullptr)
						emit("mov " + val + ", $" + d.second->init->func->label);
					else
						emit("mov " + val + ", " + imm(wrapTo(d.second->init->constValue, d.second->type)));
					Reg p = alloc(s->pos);
					emit("mov " + p + ", " + a);
					addImmediate(p, d.first);
					store(p, val, d.second->type);
					release(p);
					release(val);
				}
				if (s->a)
					genInit(a, t, s->a, 0);
				release(a);
			}
		}
		else if (s->a)
		{
			Reg a = alloc(s->pos);
			varAddress(a, v);
			genInit(a, t, s->a, 0);
			release(a);
		}
		m_scopes.back()[s->name] = v;
	}

	void CodeGen::genStatement(Node* s)
	{
		switch (s->kind)
		{
			case NodeKind::Block:
				m_scopes.push_back({});
				for (Node* st : s->list)
					genStatement(st);
				m_scopes.pop_back();
			break;
			case NodeKind::VarDecl:
				genLocalVar(s);
			break;
			case NodeKind::ExprStmt:
			{
				check(s->a);
				if (s->a->kind == NodeKind::Ident && s->a->func == nullptr && s->a->var == nullptr && m_funcs.count(s->a->name))
					error(s->pos, "'" + s->a->name + "' is a sub: call it with " + s->a->name + "()");
				Reg r = gen(s->a);
				release(r);
			}
			break;
			case NodeKind::If:
			{
				check(s->a);
				if (!decay(s->a->type)->isScalar())
					error(s->a->pos, "the condition must be an integer or a pointer");
				std::string lElse = newLabel(), lEnd = newLabel();
				genBranch(s->a, s->c ? lElse : lEnd, false);
				genStatement(s->b);
				if (s->c)
				{
					emit("jmp $" + lEnd);
					label(lElse);
					genStatement(s->c);
				}
				label(lEnd);
			}
			break;
			case NodeKind::While:
			{
				check(s->a);
				std::string lStart = newLabel(), lEnd = newLabel();
				label(lStart);
				genBranch(s->a, lEnd, false);
				m_loops.push_back({ lEnd, lStart });
				genStatement(s->b);
				m_loops.pop_back();
				emit("jmp $" + lStart);
				label(lEnd);
			}
			break;
			case NodeKind::DoWhile:
			{
				std::string lStart = newLabel(), lCont = newLabel(), lEnd = newLabel();
				label(lStart);
				m_loops.push_back({ lEnd, lCont });
				genStatement(s->b);
				m_loops.pop_back();
				label(lCont);
				check(s->a);
				genBranch(s->a, lStart, true);
				label(lEnd);
			}
			break;
			case NodeKind::For:
			{
				m_scopes.push_back({});
				if (s->a)
					genStatement(s->a);
				std::string lStart = newLabel(), lCont = newLabel(), lEnd = newLabel();
				label(lStart);
				if (s->b)
				{
					check(s->b);
					genBranch(s->b, lEnd, false);
				}
				m_loops.push_back({ lEnd, lCont });
				genStatement(s->d);
				m_loops.pop_back();
				label(lCont);
				if (s->c)
				{
					check(s->c);
					release(gen(s->c));
				}
				emit("jmp $" + lStart);
				label(lEnd);
				m_scopes.pop_back();
			}
			break;
			case NodeKind::Return:
			{
				FuncInfo* fn = m_inline.empty() ? m_func : m_inline.back().func;
				Type* ret = fn->type->base;
				if (s->a)
				{
					if (ret->kind == Type::Kind::Void)
						error(s->pos, fn->name + " returns void");
					check(s->a, ret);
					requireConvertible(s->a, ret, "in the return value");
					Reg r = gen(s->a);
					convert(r, decay(s->a->type), ret);
					emit("mov RV, " + r);
					release(r);
				}
				else if (ret->kind != Type::Kind::Void)
					error(s->pos, fn->name + " must return a value (" + ret->name() + ")");
				if (!m_inline.empty())
					emit("jmp $" + m_inline.back().endLabel);
				else
					emit(m_func->interrupt ? "rti" : "ret");
			}
			break;
			case NodeKind::Break:
				if (m_loops.empty())
					error(s->pos, "break outside of a loop");
				emit("jmp $" + m_loops.back().breakLabel);
			break;
			case NodeKind::Continue:
				if (m_loops.empty())
					error(s->pos, "continue outside of a loop");
				emit("jmp $" + m_loops.back().continueLabel);
			break;
			case NodeKind::LowBlock:
				genLow(s, false);
			break;
			default:
				error(s->pos, "statement expected");
		}
		if (m_regDepth != m_stmtDepth)
			error(s->pos, "internal error: temporaries left after a statement");
	}

	// ======================================= Low blocks =====================================
	// Each line is copied as is, except the $names of DScript things:
	//   $sub          the sub's label (its address)        $CONST / $Enum.Member   the value
	//   $var          the variable's value, in a scratch register (S1 / S2) loaded before the
	//                 line; if it is the first operand (the destination), it is stored back after
	//                 the line. Arrays and structs give their address.
	//   &$var         the variable's address               [$var] or *$var   memory at the value
	// Other $names (labels and data of the low code) are left alone.
	void CodeGen::genLow(Node* low, bool topLevel)
	{
		std::string text = low->text;
		size_t start = 0;
		int lineNr = low->pos.line;
		bool switchedSection = false;
		while (start <= text.size())
		{
			size_t end = text.find('\n', start);
			if (end == std::string::npos)
				end = text.size();
			std::string line = text.substr(start, end - start);
			SourcePos pos { low->pos.file, lineNr, 1 };
			start = end + 1;
			lineNr++;
			size_t a = line.find_first_not_of(" \t\r");
			if (a == std::string::npos)
				continue;
			line = line.substr(a);
			while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
				line.pop_back();
			if (line[0] == '.')
				switchedSection = true;

			struct Sub { VarInfo* var; int form; std::string reg; };	// form: 0 value, 1 address
			std::vector<Sub> subs;
			std::string out;
			std::vector<VarInfo*> storeBack;
			// The first operand, to know the destination.
			std::string firstOperand;
			{
				size_t sp = line.find_first_of(" \t");
				if (sp != std::string::npos)
				{
					std::string ops = line.substr(sp + 1);
					size_t comma = ops.find(',');
					firstOperand = ops.substr(0, comma);
					size_t f = firstOperand.find_first_not_of(" \t"), l = firstOperand.find_last_not_of(" \t");
					firstOperand = (f == std::string::npos) ? "" : firstOperand.substr(f, l - f + 1);
				}
			}
			auto l_scratch = [&](VarInfo* v, int form) -> std::string {
				for (const auto& s : subs)
				{
					if (s.var == v && s.form == form)
						return s.reg;
				}
				if (subs.size() >= 2)
					error(pos, "at most 2 DScript variables per low line");
				std::string reg = LowScratch[subs.size()];
				subs.push_back({ v, form, reg });
				return reg;
			};
			size_t i = 0;
			while (i < line.size())
			{
				if (line[i] != '$')
				{
					out += line[i++];
					continue;
				}
				size_t j = lowNameEnd(line, i + 1);
				std::string name = line.substr(i + 1, j - i - 1);
				std::string member;
				if (j < line.size() && line[j] == '.' && m_enums.count(name) > 0)
				{
					size_t k = j + 1;
					while (k < line.size() && (std::isalnum((unsigned char)line[k]) || line[k] == '_'))
						k++;
					member = line.substr(j + 1, k - j - 1);
					j = k;
				}
				if (m_options.index != nullptr && !name.empty())
				{
					// The column of the name in the source (the line was trimmed; the first line of
					// the block starts after its '{').
					const int base = (pos.line == low->pos.line ? low->pos.column + 1 : 1) + (int)a;
					indexLow({ pos.file, pos.line, base + (int)i + 1 }, name);
				}
				VarInfo* v = name.empty() ? nullptr : lookupVar(name);
				// $Constant.field.field...: a field of a struct constant (its value).
				if (v == nullptr && member.empty() && m_consts.count(name) > 0 && j < line.size() && line[j] == '.'
					&& (resolveConst(name, pos), m_consts[name].type->kind == Type::Kind::Struct))
				{
					ConstInfo& c = m_consts[name];
					Type* st = c.type;
					std::string path;
					while (j < line.size() && line[j] == '.' && st != nullptr && st->kind == Type::Kind::Struct)
					{
						size_t k = j + 1;
						while (k < line.size() && (std::isalnum((unsigned char)line[k]) || line[k] == '_'))
							k++;
						std::string fieldName = line.substr(j + 1, k - j - 1);
						const StructField* f = st->structInfo->field(fieldName);
						if (f == nullptr)
							error(pos, "$" + name + ": " + (st->structInfo->anonymous ? st->structInfo->displayName() : "struct " + st->structInfo->name) + " has no field '" + fieldName + "'");
						path += (path.empty() ? "" : ".") + fieldName;
						st = f->type;
						j = k;
					}
					if (st->kind == Type::Kind::Struct)
						error(pos, "$" + name + "." + path + " is a struct: use its fields");
					out += imm(c.fields[path]);
					i = j;
					continue;
				}
				if (v != nullptr && (topLevel ? v->storage == VarInfo::Storage::Global : true))
				{
					bool address = (!out.empty() && out.back() == '&');
					bool deref = (!out.empty() && (out.back() == '*' || out.back() == '['));
					bool bracket = (!out.empty() && out.back() == '[');
					if (address || deref)
						out.pop_back();
					if (bracket)
					{
						if (j >= line.size() || line[j] != ']')
							error(pos, "[$" + name + "] expected");
						j++;
					}
					if (deref && !v->type->isScalar())
						error(pos, "$" + name + " is not a pointer");
					const bool aggregate = !v->type->isScalar();
					std::string reg = l_scratch(v, (address || aggregate) ? 1 : 0);
					out += (deref ? "*" : "") + reg;
					if (!address && !deref && !aggregate && line.substr(i, j - i) == firstOperand)
						storeBack.push_back(v);
				}
				else if (!member.empty())
				{
					EnumInfo& info = m_enums[name];
					resolveEnum(info);
					if (info.values.count(member) == 0)
						error(pos, "enum " + name + " has no member '" + member + "'");
					out += imm(info.values[member]);
				}
				else if (m_consts.count(name) > 0)
				{
					const int64_t value = resolveConst(name, pos);
					if (m_consts[name].isString)
					{
						out += "$" + stringLiteral(m_consts[name].text);	// the string's address
						i = j;
						continue;
					}
					if (m_consts[name].type->kind == Type::Kind::Struct)
						error(pos, "$" + name + " is a struct constant: it has no address, use its fields ($" + name + ".field)");
					out += imm(value);
				}
				else if (m_funcs.count(name) > 0 && lookupVar(name) == nullptr)
				{
					if (m_funcs[name].size() > 1)
						error(pos, "$" + name + " is an overloaded sub: take its address in DScript (var f: sub(...) -> ... = " + name + ";) and use $f");
					out += "$" + m_funcs[name][0]->label;
				}
				else
					out += line.substr(i, j - i);
				i = j;
			}
			for (const auto& s : subs)
			{
				if (s.form == 1)
					varAddress(s.reg, s.var);
				else
					loadVar(s.reg, s.var);
			}
			emit(out);
			for (VarInfo* v : storeBack)
			{
				std::string reg;
				for (const auto& s : subs)
				{
					if (s.var == v && s.form == 0)
						reg = s.reg;
				}
				if (v->storage == VarInfo::Storage::Global)
				{
					const char* suffix = (v->type->size() == 1 ? "b" : (v->type->size() == 2 ? "w" : ""));
					emit(std::string("mov") + suffix + " [$" + v->symbol + "], " + reg);
				}
				else
				{
					emit(std::string("push ") + ScratchAddr);
					varAddress(ScratchAddr, v);
					store(ScratchAddr, reg, v->type);
					emit(std::string("pop ") + ScratchAddr);
				}
			}
		}
		if (switchedSection)
			m_out->push_back(".code");
	}

	// ======================================== Program ========================================
	void CodeGen::genFunction(FuncInfo* f)
	{
		std::vector<std::string> body;
		m_out = &body;
		m_func = f;
		m_scopes.clear();
		m_scopes.push_back({});
		for (size_t i = 0; i < f->paramNames.size(); i++)
		{
			VarInfo* v = new VarInfo();
			v->name = f->paramNames[i];
			v->type = f->type->params[i];
			v->storage = VarInfo::Storage::Param;
			v->offset = (int32_t)i;
			v->pos = f->decl->list[i]->pos;
			v->defPos = f->decl->list[i]->namePos;
			v->param = true;
			if (m_options.index != nullptr)
				indexName(v->defPos, v->name, v->defPos, hoverVar(v));
			m_scopes.back()[v->name] = v;
		}
		m_localCursor = 4;
		m_localMin = 4;
		m_loops.clear();
		m_regDepth = 0;
		Node* bodyNode = f->decl->a;
		for (Node* s : bodyNode->list)
			genStatement(s);
		const bool endsWithReturn = !bodyNode->list.empty() && bodyNode->list.back()->kind == NodeKind::Return;
		if (!endsWithReturn)
		{
			if (f->type->base->kind != Type::Kind::Void)
				emit("mov RV, 0");
			emit(f->interrupt ? "rti" : "ret");
		}
		m_out = &m_code;
		m_code.push_back("");
		label(f->label);
		if (m_localMin < 4)
		{
			// Room for the locals: they are at FP + offset (offsets <= 0).
			emit(std::string("mov ") + ScratchCounter + ", SP");
			emit(std::string("subip ") + ScratchCounter + ", " + imm(4 - m_localMin));
			emit(std::string("mov SP, ") + ScratchCounter);
		}
		m_code.insert(m_code.end(), body.begin(), body.end());
		m_scopes.clear();
		m_func = nullptr;
	}

	// The constant bytes of a global (big endian); false if a part needs code at run time.
	static void putBytes(std::vector<uint8_t>& bytes, uint32_t offset, uint32_t size, int64_t value)
	{
		for (uint32_t i = 0; i < size; i++)
			bytes[offset + i] = (uint8_t)((uint64_t)value >> (8 * (size - 1 - i)));
	}

	void CodeGen::genGlobals(void)
	{
		// Types first (subs can use any global).
		for (Node* d : m_globalDecls)
		{
			const bool ok = attempt([&] {
				VarInfo* v = m_globals[d->name];
				if (m_options.rom && !v->readOnly && d->name.find("::") != std::string::npos)
					error(d->pos, "--rom: '" + d->text + "' is a static variable (stored in the program, like a global), but the program is in ROM, "
						"where nothing can change: use const for read-only values, and keep what changes in RAM (locals, or a pointer to a RAM address)");
				if (m_options.rom && !v->readOnly)
					error(d->pos, "--rom: '" + d->name + "' is a global variable, but the program (and its data) is in ROM, "
						"where nothing can change: use const for read-only values, and keep what changes in RAM (locals, or a pointer to a RAM address)");
				Type* t = nullptr;
				inferArraySize(d->typeSyntax, d->a, d->pos);
				if (d->typeSyntax)
					t = resolveType(d->typeSyntax);
				else
				{
					check(d->a);
					t = decay(d->a->type);
					if (t == nullptr || t->kind == Type::Kind::Null || t->kind == Type::Kind::Void)
						error(d->pos, "cannot tell the type of '" + d->name + "' (write var " + d->name + ": <type>)");
					if (t->isInteger() && d->a->kind == NodeKind::IntLit)
						t = m_types.i32();
				}
				if (t->kind == Type::Kind::Void)
					error(d->pos, "variables cannot be void");
				if (t->kind == Type::Kind::Struct)
					resolveStruct(t->structInfo, d->pos);
				v->type = t;
			});
			if (!ok)
				m_globals[d->name]->type = m_types.i32();
		}
		// Values: constants go in the data, the rest is done by __ds_init (which has locals
		// when an initializer calls an inline sub).
		m_out = &m_initCode;
		m_localCursor = 4;
		m_localMin = 4;
		for (Node* d : m_globalDecls)
		{
			attempt([&] {
				VarInfo* v = m_globals[d->name];
				Type* t = v->type;
				std::vector<uint8_t> bytes(t->size(), 0);
				struct Pending { uint32_t offset; Type* type; Node* init; const StructField* field; };
				std::vector<Pending> pending;
				// Pointers to string literals: stored as the literal's address ($symbol in the data).
				std::map<uint32_t, std::string> refs;
				std::vector<std::pair<int32_t, const StructField*>> defaults;
				collectDefaults(t, 0, defaults);
				for (const auto& def : defaults)
				{
					if (def.second->init->kind == NodeKind::StrLit)
						refs[(uint32_t)def.first] = stringLiteral(def.second->init->text);
					else if (def.second->init->kind == NodeKind::Ident && def.second->init->func != nullptr)
						refs[(uint32_t)def.first] = def.second->init->func->label;
					else
						putBytes(bytes, def.first, def.second->type->size(), def.second->init->constValue);
				}
				// Walk the initializer.
				std::vector<Pending> work;
				if (d->a)
					work.push_back({ 0, t, d->a, nullptr });
				while (!work.empty())
				{
					Pending p = work.back();
					work.pop_back();
					Node* init = p.init;
					foldStringConst(init);
					if (init->kind == NodeKind::InitList)
					{
						if (p.type->kind == Type::Kind::Array)
						{
							if (init->list.size() > p.type->count)
								error(init->pos, "too many values for " + p.type->name());
							for (size_t i = 0; i < init->list.size(); i++)
								work.push_back({ p.offset + (uint32_t)i * p.type->base->size(), p.type->base, init->list[i], nullptr });
						}
						else if (p.type->kind == Type::Kind::Struct)
						{
							const auto& fields = p.type->structInfo->fields;
							if (init->list.size() > fields.size())
								error(init->pos, "too many values for struct " + p.type->name());
							for (size_t i = 0; i < init->list.size(); i++)
								work.push_back({ p.offset + fields[i].offset, fields[i].type, init->list[i], nullptr });
						}
						else
							error(init->pos, "{ ... } initializes arrays and structs, not " + p.type->name());
						continue;
					}
					if (p.type->kind == Type::Kind::Array && init->kind == NodeKind::StrLit)
					{
						if (p.type->base->size() != 1)
							error(init->pos, "strings can only initialize u8 / i8 arrays");
						if (init->text.size() > p.type->count)
							error(init->pos, "the string is longer than the array (" + std::to_string(p.type->count) + " bytes)");
						for (size_t i = 0; i < init->text.size(); i++)
							bytes[p.offset + i] = (uint8_t)init->text[i];
						continue;
					}
					check(init, p.type);
					requireConvertible(init, p.type, "in the initialization of '" + d->name + "'");
					if (p.type->kind == Type::Kind::Struct || p.type->kind == Type::Kind::Array)
						error(init->pos, "global structs and arrays are initialized with { ... }");
					if (init->kind == NodeKind::StrLit)
					{
						refs[p.offset] = stringLiteral(init->text);
						continue;
					}
					refs.erase(p.offset);	// replaces a default
					if (init->isConst || init->constPtr)
						putBytes(bytes, p.offset, p.type->size(), init->isConst ? wrapTo(init->constValue, p.type) : init->constValue);
					else
					{
						if (v->readOnly)
							error(init->pos, "the values of a constant array must be constants or strings ('" + d->name + "' is stored in the program)");
						pending.push_back(p);
					}
				}
				// The data line.
				bool allZero = refs.empty();
				for (uint8_t b : bytes)
					allZero = allZero && b == 0;
				std::string line = "\t$" + v->symbol;
				if (allZero && bytes.size() > 1)
					line += ":" + std::to_string(bytes.size());
				else
				{
					line += " ";
					for (size_t i = 0; i < bytes.size(); i++)
					{
						if (i > 0)
							line += ", ";
						auto ref = refs.find((uint32_t)i);
						if (ref != refs.end())
						{
							line += "$" + ref->second;
							i += 3;
							continue;
						}
						char buf[8];
						std::snprintf(buf, sizeof(buf), "0x%02X", bytes[i]);
						line += buf;
					}
				}
				m_data.push_back(line);
				// Run time initializations.
				for (const auto& p : pending)
				{
					Reg val = alloc(p.init->pos);
					if (p.init->kind == NodeKind::StrLit)
						emit("mov " + val + ", $" + stringLiteral(p.init->text));
					else
					{
						release(val);
						val = gen(p.init);
						convert(val, decay(p.init->type), p.type);
					}
					Reg addr = alloc(p.init->pos);
					emit("mov " + addr + ", $" + v->symbol);
					addImmediate(addr, p.offset);
					store(addr, val, p.type);
					release(addr);
					release(val);
				}
			});
		}
	}

	// ===================================== Analysis =====================================
	// Runs fn; in analysis mode an error is recorded (false is returned) and the generator is
	// reset, so the next declaration or sub can be checked.
	bool CodeGen::attempt(const std::function<void()>& fn)
	{
		if (!m_options.analysis)
		{
			fn();
			return true;
		}
		try
		{
			fn();
			return true;
		}
		catch (const CompileError& e)
		{
			m_errors.push_back(e);
			m_out = &m_code;
			m_scopes.clear();
			m_func = nullptr;
			m_inline.clear();
			m_discarding = false;
			m_regDepth = 0;
			m_stmtDepth = 0;
			m_loops.clear();
			return false;
		}
	}

	void CodeGen::indexName(const SourcePos& pos, const std::string& written, const SourcePos& def, const std::string& hover)
	{
		if (m_options.index != nullptr)
			m_options.index->add(pos, (int)written.size(), def, hover);
	}

	std::string CodeGen::hoverVar(VarInfo* v)
	{
		const std::string type = v->type ? v->type->name(true) : "?";
		const bool hidden = v->name.find("::") != std::string::npos;
		std::string where;
		if (v->param)
			where = "parameter";
		else if (v->storage == VarInfo::Storage::Local)
			where = "local";
		else if (hidden)
			where = (v->readOnly ? "" : "static, ") + ownerOf(v->name);
		else
			where = v->readOnly ? "constant array" : "global";
		const std::string size = (v->type && !v->type->isScalar()) ? bytes(v->type->size()) : "";
		const std::string keyword = v->readOnly ? "const " : (hidden ? "static var " : "var ");
		return keyword + shownName(v->name) + ": " + type + comment({ where, size });
	}

	std::string CodeGen::hoverFunc(FuncInfo* f)
	{
		const bool lambda = f->decl->name.rfind("__lambda_", 0) == 0;
		std::string s;
		if (f->interrupt)
			s += "interrupt ";
		if (f->isInline)
			s += "inline ";
		const bool method = (f->decl->value & 4) != 0;
		const std::vector<std::string> parts = nameParts(f->decl->name);
		s += lambda ? "sub(" : "sub " + (method ? parts[0] + "." + parts[1] : f->decl->name) + "(";
		if (f->type != nullptr)
		{
			for (size_t i = method ? 1 : 0; i < f->type->params.size() && i < f->paramNames.size(); i++)
			{
				s += (i > (method ? 1u : 0u) ? ", " : "") + std::string("var ") + f->paramNames[i] + ": " + f->type->params[i]->name(true);
				const Node* d = i < f->defaults.size() ? f->defaults[i] : nullptr;
				if (d != nullptr)
				{
					const bool isBool = f->type->params[i]->kind == Type::Kind::Bool;
					s += " = " + (d->kind == NodeKind::StrLit ? quoted(d->text) : !d->isConst ? std::string("...")
						: isBool ? std::string(d->constValue ? "true" : "false") : std::to_string(d->constValue));
				}
			}
		}
		s += ")";
		if (f->type != nullptr && f->type->base->kind != Type::Kind::Void)
			s += " -> " + f->type->base->name(true);
		return s + comment({ lambda ? "lambda" : method ? "method of " + parts[0] : "" });
	}

	std::string CodeGen::hoverConst(const std::string& name)
	{
		const ConstInfo& c = m_consts[name];
		const std::string head = "const " + shownName(name);
		const std::string note = comment({ ownerOf(name) });
		if (!c.resolved || c.type == nullptr)
			return head + note;
		if (c.isString)
			return head + " = " + quoted(c.text) + note;
		if (c.type->kind == Type::Kind::Struct)
		{
			// One field per line (in their order), addresses and other big values in hex.
			std::function<std::string(StructInfo*, const std::string&, const std::string&)> l_fields =
				[&](StructInfo* info, const std::string& prefix, const std::string& indent) {
				std::string s = "{\n";
				for (size_t i = 0; i < info->fields.size(); i++)
				{
					const StructField& f = info->fields[i];
					s += indent + "\t" + f.name + " = ";
					if (f.type->kind == Type::Kind::Struct)
						s += l_fields(f.type->structInfo, prefix + f.name + ".", indent + "\t");
					else
					{
						auto it = c.fields.find(prefix + f.name);
						if (it == c.fields.end())
							s += "?";
						else if (it->second < 0 || it->second > 0xFFFF)
						{
							char buf[16];
							std::snprintf(buf, sizeof(buf), "0x%08X", (uint32_t)it->second);
							s += buf;
						}
						else
							s += std::to_string(it->second);
					}
					s += (i + 1 < info->fields.size() ? ",\n" : "\n");
				}
				return s + indent + "}";
			};
			return head + ": " + c.type->name(true) + note + " = " + l_fields(c.type->structInfo, "", "");
		}
		return head + ": " + c.type->name(true) + " = " + number(c.value) + note;
	}

	std::string CodeGen::hoverStruct(StructInfo* info)
	{
		std::string s = info->anonymous ? "struct" : "struct " + info->name;
		if (!info->complete)
			return s;
		s += " {  // " + bytes(info->size) + "\n";
		for (const StructField& f : info->fields)
			s += "\tvar " + f.name + ": " + (f.type ? f.type->name(true) : "?") + ";  // +" + std::to_string(f.offset) + "\n";
		return s + "}";
	}

	std::string CodeGen::hoverField(StructInfo* info, const StructField& f)
	{
		const std::string owner = info->anonymous ? "struct { ... }" : info->name;
		return "var " + f.name + ": " + (f.type ? f.type->name(true) : "?") + comment({ "field of " + owner + " at +" + std::to_string(f.offset),
			f.type ? bytes(f.type->size()) : "" });
	}

	std::string CodeGen::hoverEnum(const std::string& name)
	{
		const EnumInfo& info = m_enums[name];
		std::string s = "enum " + name + " {\n";
		for (size_t i = 0; i < info.decl->list.size(); i++)
		{
			const std::string& member = info.decl->list[i]->name;
			auto it = info.values.find(member);
			s += "\t" + member + " = " + (it != info.values.end() ? std::to_string(it->second) : "?") + (i + 1 < info.decl->list.size() ? ",\n" : "\n");
		}
		return s + "}";
	}

	std::string CodeGen::hoverAlias(const std::string& name)
	{
		const AliasInfo& a = m_aliases[name];
		return "alias " + name + " = " + (a.type ? a.type->name(true) : "?");
	}

	// $name in a low block (pos: where the name is).
	void CodeGen::indexLow(const SourcePos& pos, const std::string& name)
	{
		if (VarInfo* v = lookupVar(name))
			indexName(pos, shownName(name), v->defPos, hoverVar(v));
		else if (m_consts.count(name) > 0)
			indexName(pos, shownName(name), m_consts[name].decl->namePos, hoverConst(name));
		else if (m_enums.count(name) > 0)
			indexName(pos, name, m_enums[name].decl->namePos, hoverEnum(name));
		else if (m_funcs.count(name) > 0 && !m_funcs[name].empty())
			indexName(pos, name, m_funcs[name][0]->decl->namePos, hoverFunc(m_funcs[name][0]));
	}

	// The declarations themselves (hovering their names), and the final description of
	// everything that was used before it was fully resolved.
	void CodeGen::indexDeclarations(void)
	{
		std::map<Index::Key, std::string> hovers;
		auto l_decl = [&](const SourcePos& pos, const std::string& written, const std::string& hover) {
			indexName(pos, written, pos, hover);
			hovers[Index::Key(pos.file, pos.line, pos.column)] = hover;
		};
		for (auto& s : m_structs)
		{
			StructInfo* info = s.second;
			l_decl(info->decl->namePos, info->anonymous ? "struct" : info->name, hoverStruct(info));
			for (const StructField& f : info->fields)
				l_decl(f.namePos, f.name, hoverField(info, f));
		}
		for (auto& e : m_enums)
		{
			l_decl(e.second.decl->namePos, e.first, hoverEnum(e.first));
			for (const Node* m : e.second.decl->list)
			{
				auto it = e.second.values.find(m->name);
				l_decl(m->namePos, m->name, e.first + "." + m->name + " = " + (it != e.second.values.end() ? number(it->second) : "?"));
			}
		}
		for (auto& c : m_consts)
			l_decl(c.second.decl->namePos, shownName(c.first), hoverConst(c.first));
		for (auto& a : m_aliases)
			l_decl(a.second.decl->namePos, a.first, hoverAlias(a.first));
		for (auto& g : m_globals)
		{
			if (g.second->type != nullptr)
				l_decl(g.second->defPos, shownName(g.first), hoverVar(g.second));
		}
		for (FuncInfo* f : m_funcList)
		{
			const bool lambda = f->decl->name.rfind("__lambda_", 0) == 0;
			l_decl(f->decl->namePos, lambda ? "sub" : shownName(f->decl->name), hoverFunc(f));
		}
		m_options.index->rehover(hovers);
	}

	std::vector<std::string> CodeGen::generate(Node* program)
	{
		collect(program);
		// Resolve the declarations (in any order: they can refer to each other). In analysis
		// mode, what fails gets a placeholder so the rest can still be checked.
		m_inDeclarations = true;
		for (auto& s : m_structs)
		{
			if (!attempt([&] { resolveStruct(s.second, s.second->decl->pos); }))
			{
				s.second->resolving = false;
				s.second->complete = true;
			}
		}
		for (auto& e : m_enums)
		{
			if (!attempt([&] { resolveEnum(e.second); }))
			{
				e.second.resolving = false;
				e.second.resolved = true;
			}
		}
		for (auto& c : m_consts)
		{
			if (!attempt([&] { resolveConst(c.first, c.second.decl->pos); }))
			{
				c.second.resolving = false;
				c.second.resolved = true;
				if (c.second.type == nullptr)
					c.second.type = m_types.i32();
			}
		}
		for (auto& a : m_aliases)
		{
			if (!attempt([&] {
				TypeSyntax ts;
				ts.name = a.first;
				ts.pos = a.second.decl->pos;
				resolveType(&ts);
			}))
			{
				a.second.resolving = false;
				if (a.second.type == nullptr)
					a.second.type = m_types.i32();
			}
		}
		for (FuncInfo* f : m_funcList)
		{
			if (!attempt([&] { resolveFunction(f); }))
			{
				// As a sub without parameters (calls to it will report the arguments).
				f->paramNames.clear();
				f->defaults.clear();
				f->requiredParams = 0;
				f->type = m_types.function({}, m_types.voidType());
			}
		}
		m_inDeclarations = false;
		// Overloads: merge declarations with their definition; labels.
		for (auto& entry : m_funcs)
		{
			auto& funcs = entry.second;
			attempt([&] {
				for (size_t i = 0; i < funcs.size(); i++)
				{
					for (size_t j = i + 1; j < funcs.size(); )
					{
						if (funcs[i]->type->params != funcs[j]->type->params)
						{
							j++;
							continue;
						}
						if (funcs[i]->type != funcs[j]->type)
							error(funcs[j]->decl->pos, "'" + entry.first + "' is already declared with the same parameters but another return type");
						if (funcs[i]->defined && funcs[j]->defined)
							error(funcs[j]->decl->pos, "'" + entry.first + "' is already defined with the same parameters (" + posString(funcs[i]->decl->pos) + ")");
						FuncInfo* keep = funcs[i]->defined ? funcs[i] : funcs[j];
						FuncInfo* drop = (keep == funcs[i]) ? funcs[j] : funcs[i];
						for (size_t k = 0; k < keep->defaults.size(); k++)
						{
							if (keep->defaults[k] == nullptr)
								keep->defaults[k] = drop->defaults[k];
						}
						keep->requiredParams = std::min(keep->requiredParams, drop->requiredParams);
						keep->isInline = keep->isInline || drop->isInline;
						funcs[i] = keep;
						funcs.erase(funcs.begin() + j);
						m_funcList.erase(std::find(m_funcList.begin(), m_funcList.end(), drop));
					}
				}
			});
			for (FuncInfo* f : funcs)
			{
				f->label = codeLabel(entry.first);
				if (funcs.size() > 1)
				{
					f->label += "__";
					for (Type* p : f->type->params)
						f->label += "_" + mangle(p);
					if (f->type->params.empty())
						f->label += "_void";
				}
			}
		}
		resolveSubDefaults();
		// Analysis: a file without main is fine (it is included by others).
		FuncInfo* mainFunc = nullptr;
		if (m_funcs.count("main") == 0)
		{
			if (!m_options.analysis)
				error({ program->pos.file, 1, 1 }, "no 'main' sub");
		}
		else
		{
			mainFunc = m_funcs["main"][0];
			attempt([&] {
				if (m_funcs["main"].size() > 1 || !mainFunc->type->params.empty())
					error(mainFunc->decl->pos, "main takes no parameters (sub main() -> i32 or -> void)");
			});
		}

		genGlobals();
		const int32_t initLocalMin = m_localMin;
		for (Node* low : m_topLows)
		{
			std::vector<std::string> lowCode;
			m_out = &lowCode;
			m_scopes.clear();
			attempt([&] { genLow(low, true); });
			m_code.push_back("");
			m_code.insert(m_code.end(), lowCode.begin(), lowCode.end());
		}
		for (FuncInfo* f : m_funcList)
		{
			attempt([&] {
				if (!f->defined)
					error(f->decl->pos, "sub '" + f->name + "' is declared but not defined");
				if (!f->isInline)
					genFunction(f);
			});
		}
		// Inline subs get real code only if they are used as sub pointers (which the generated
		// code can make true for more of them); the others are only checked.
		std::set<FuncInfo*> kept;
		for (bool more = true; more; )
		{
			more = false;
			for (FuncInfo* f : m_funcList)
			{
				if (f->isInline && f->addressTaken && f->defined && kept.insert(f).second)
				{
					attempt([&] { genFunction(f); });
					more = true;
				}
			}
		}
		for (FuncInfo* f : m_funcList)
		{
			if (!f->isInline || kept.count(f) || !f->defined)
				continue;
			const size_t codeSize = m_code.size();
			const auto strings = m_strings;
			const auto helpers = m_helpers;
			m_discarding = true;
			attempt([&] { genFunction(f); });
			m_discarding = false;
			m_code.resize(codeSize);
			m_strings = strings;
			m_helpers = helpers;
		}

		if (m_options.index != nullptr)
			indexDeclarations();
		if (m_options.analysis)
			return {};

		// Assemble the dasm source.
		std::vector<std::string> out;
		out.push_back("## Generated by dsc (DScript compiler) -- do not edit");
		out.push_back(".entry __ds_start");
		for (const auto& d : m_options.directives)
			out.push_back(d);
		out.push_back(".data");
		for (const auto& d : m_data)
			out.push_back(d);
		for (const auto& s : m_strings)
		{
			std::string line = "\t$" + s.second + " ";
			for (size_t i = 0; i < s.first.size(); i++)
			{
				char buf[8];
				std::snprintf(buf, sizeof(buf), "0x%02X, ", (uint8_t)s.first[i]);
				line += buf;
			}
			line += "0x00";
			if (s.first.empty())
				line = "\t$" + s.second + " 0x00";
			out.push_back(line);
		}
		out.push_back(".code");
		out.push_back("__ds_start:");
		if (!m_initCode.empty())
		{
			out.push_back("\tpush 0");
			out.push_back("\tcall $__ds_init");
		}
		out.push_back("\tpush 0");
		out.push_back("\tcall $" + mainFunc->label);
		out.push_back(m_options.raw ? "\thlt" : "\tret");
		if (!m_initCode.empty())
		{
			out.push_back("");
			out.push_back("__ds_init:");
			if (initLocalMin < 4)
			{
				out.push_back(std::string("\tmov ") + ScratchCounter + ", SP");
				out.push_back(std::string("\tsubip ") + ScratchCounter + ", " + imm(4 - initLocalMin));
				out.push_back(std::string("\tmov SP, ") + ScratchCounter);
			}
			out.insert(out.end(), m_initCode.begin(), m_initCode.end());
			out.push_back("\tret");
		}
		out.insert(out.end(), m_code.begin(), m_code.end());
		if (m_helpers.count("__ds_shru") > 0)
		{
			// __ds_shru(value, count): logical shift right.
			const char* helper[] = {
				"", "__ds_shru:",
				"\tmovo R1, *PP, dword 0x00000000",
				"\tmovo R2, *PP, dword 0xFFFFFFFC",
				"\tandip R2, 0x0000001F",
				"\tmov ACC, R2",
				"\tjeq $__ds_shru_end, 0",
				"\trsh R1, 1",
				"\tandip R1, 0x7FFFFFFF",
				"\tsubip R2, 1",
				"\trsh R1, R2",
				"__ds_shru_end:",
				"\tmov RV, R1",
				"\tret", nullptr
			};
			for (int i = 0; helper[i] != nullptr; i++)
				out.push_back(helper[i]);
		}
		return out;
	}
}
