#include "CodeGen.hpp"
#include "Parser.hpp"
#include "ConditionBuiltins.hpp"
#include <cmath>
#include <bit>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>

// Registers used by the generated code:
//   R1..R16    expression temporaries (a stack: alloc() / release())
//   R17..R20   scratch for the code generator (copies, store-backs)
//   S1, S2     the $variables of low blocks
//   ACC        comparisons; RV return values; FP locals (FP + offset); PP parameters (PP - 4 * i)
// Every call saves and restores R1..R20 (the CPU's call frame), so temporaries survive calls.

namespace dscript
{
	// Parameter i of a sub type is own (the sub destroys it, not the caller).
	static bool ownedParam(const Type* fn, size_t i)
	{
		return fn != nullptr && i < 32 && (fn->owned & (1u << i)) != 0;
	}

	static const int TempRegisters = 16;
	static const char* ScratchCounter = "R17";
	static const char* ScratchDst = "R18";
	static const char* ScratchSrc = "R19";
	static const char* ScratchAddr = "R20";
	static const char* LowScratch[2] = { "reg(0x07)", "reg(0x08)" };
	static const char LowMark = '\x01';	// -O: a low block's line (in the sub's code until the peephole pass)

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

	// The dasm symbol of a global: local constants and statics are named <sub>::<name>, the
	// globals of a namespace <namespace>::<name>.
	// A name as a dasm symbol: ':' -> '_', and the characters of template instances
	// (Array<u32*, Point>): < _L, > _R, , _C, * _P, [ ] _A, others _.
	static std::string symbolChars(const std::string& name)
	{
		std::string s;
		for (char c : name)
		{
			if (std::isalnum((unsigned char)c) || c == '_') s += c;
			else if (c == ':') s += '_';
			else if (c == '<') s += "_L";
			else if (c == '>') s += "_R";
			else if (c == ',') s += "_C";
			else if (c == '*') s += "_P";
			else if (c == '[') s += "_A";
			else if (c == ' ' || c == ']') ;
			else if (c == '+') s += "_pl";		// (operators: operator+ and operator- are not the same sub)
			else if (c == '-') s += "_mi";
			else if (c == '/') s += "_sl";
			else if (c == '%') s += "_pc";
			else if (c == '&') s += "_am";
			else if (c == '|') s += "_vb";
			else if (c == '^') s += "_ca";
			else if (c == '=') s += "_eq";
			else if (c == '!') s += "_ex";
			else if (c == '~') s += "_ti";
			else if (c == '(') s += "_op";
			else if (c == ')') s += "_cp";
			else s += '_';
		}
		return s;
	}

	static bool boundReference(const Node* n);
	static SourcePos templateParamPos(const TemplateDef* def, const std::string& name);
	static std::string posKey(const SourcePos& p);

	static bool plainName(const std::string& name) { return std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum((unsigned char)c) || c == '_'; }); }

	static std::string dataSymbol(const std::string& name, bool isStatic)
	{
		if (plainName(name))
			return name;
		return std::string(isStatic ? "__static_" : "__ns_") + symbolChars(name);
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

	// "in sub X" for local constants / statics (their declaration; "" for the others).
	static std::string ownerOf(const Node* decl)
	{
		if (decl == nullptr || decl->text.empty() || decl->owner == nullptr)
			return "";
		const Node* f = decl->owner;
		if (f->name.rfind("__lambda_", 0) == 0)
			return "in a lambda";
		if ((f->value & 4) != 0 && f->owner != nullptr)
			return "in method " + f->owner->name + "." + f->shortName;
		return "in sub " + f->name;
	}

	// The name of a declaration as declared (without its scopes).
	static const std::string& declaredName(const Node* decl)
	{
		return decl->shortName.empty() ? decl->name : decl->shortName;
	}

	// An Ident as written.
	static const std::string& writtenName(const Node* e)
	{
		return e->text.empty() ? e->name : e->text;
	}

	// The dasm label of a sub (methods: <Struct>::<name>; namespaces: <namespace>::<name>).
	static std::string codeLabel(const std::string& name, bool method)
	{
		if (plainName(name))
			return name;
		return std::string(method ? "__method_" : "__ns_") + symbolChars(name);
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

	// ===== f32 constants: their value is their 32 bits =====
	static float f32Of(int64_t bits) { return std::bit_cast<float>((uint32_t)bits); }
	static int64_t bitsOf(float value) { return (int64_t)std::bit_cast<uint32_t>(value); }
	// f32 -> integer as the CPU does it (ftoi / ftou): truncated, saturated, NaN -> 0.
	static int64_t floatToInt(float f, bool isUnsigned)
	{
		if (std::isnan(f)) return 0;
		if (isUnsigned)
		{
			if (f <= 0.0f) return 0;
			if (f >= 4294967296.0f) return 0xFFFFFFFF;
			return (int64_t)(uint32_t)f;
		}
		if (f >= 2147483648.0f) return 0x7FFFFFFF;
		if (f <= -2147483648.0f) return -2147483648LL;
		return (int64_t)(int32_t)f;
	}
	static bool isUnsignedKind(const Type* t)
	{
		return t->kind == Type::Kind::U8 || t->kind == Type::Kind::U16 || t->kind == Type::Kind::U32;
	}
	// A constant of type `from` as a value of type `to` (wrapTo, plus the int <-> f32 conversions).
	static int64_t constConvert(int64_t value, const Type* from, Type* to)
	{
		if (to->isFloat())
		{
			if (from == nullptr || from->isFloat())
				return (uint32_t)value;
			if (from->kind == Type::Kind::U32)
				return bitsOf((float)(uint32_t)value);
			return bitsOf((float)(int32_t)value);
		}
		if (from != nullptr && from->isFloat())
		{
			const float f = f32Of(value);
			if (to->kind == Type::Kind::Bool)
				return f != 0.0f;
			return wrapTo(floatToInt(f, isUnsignedKind(to)), to);
		}
		return wrapTo(value, to);
	}
	// Is a constant of type t true (not 0; for f32: not 0.0 / -0.0)?
	static bool constTruth(int64_t value, const Type* t)
	{
		if (t != nullptr && t->isFloat())
			return f32Of(value) != 0.0f;
		return value != 0;
	}
	// An f32 written so that it reads back as the same f32 (a literal, or 1.0 / 0.0 for the
	// infinities, 0.0 / 0.0 for NaN).
	static std::string floatText(int64_t bits)
	{
		const float f = f32Of(bits);
		if (std::isnan(f)) return "(0.0 / 0.0)";
		if (std::isinf(f)) return f > 0 ? "(1.0 / 0.0)" : "(-1.0 / 0.0)";
		char buf[48];
		std::snprintf(buf, sizeof(buf), "%.9g", (double)f);
		std::string text = buf;
		if (text.find_first_of(".en") == std::string::npos)
			text += ".0";
		return text;
	}

	CodeGen::CodeGen(const CodeGenOptions& options) : m_options(options)
	{
		m_notGiven = new Type();		// (not interned: no type is it)
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
		auto it = m_names.find(name);
		if (it != m_names.end())
		{
			if (what == "a namespace" && it->second == what)
				return;		// reopened
			error(pos, "'" + name + "' is already declared (as " + it->second + ")");
		}
		const size_t sep = name.rfind("::");
		const std::string last = (sep == std::string::npos) ? name : name.substr(sep + 2);
		if (m_types.builtin(last) != nullptr)
			error(pos, "'" + last + "' is a builtin type name");
		m_names[name] = what;
	}

	// ==================================== Names and scopes ===================================
	bool CodeGen::isDeclared(const std::string& name)
	{
		auto it = m_names.find(name);
		return it != m_names.end() && it->second != "a namespace";
	}

	std::string CodeGen::qualify(const std::string& written, const std::string& scope)
	{
		std::string s = scope;
		while (true)
		{
			const std::string candidate = s.empty() ? written : s + "::" + written;
			bool found = isDeclared(candidate);
			if (found && !s.empty() && m_structs.count(s) > 0)
			{
				// In a struct: only its nested types and aliases (and what is in them) and constants, not its subs.
				const std::string first = s + "::" + written.substr(0, written.find("::"));
				found = m_structs.count(first) > 0 || m_enums.count(first) > 0 || m_aliases.count(first) > 0 || m_consts.count(first) > 0 || m_globals.count(first) > 0;	// (and static fields)
			}
			if (found)
				return candidate;
			if (!s.empty() && m_structs.count(s) > 0)
			{
				// A class: what it inherits (its base's constants, static fields, nested types).
				const Node* d = m_structs[s]->decl;
				for (int depth = 0; depth < 32 && d != nullptr && d->typeSyntax != nullptr && d->typeSyntax->kind == TypeSyntax::Kind::Named && d->typeSyntax->resolved; depth++)
				{
					const std::string base = d->typeSyntax->name;
					const std::string first = base + "::" + written.substr(0, written.find("::"));
					if (isDeclared(base + "::" + written) && (m_structs.count(first) > 0 || m_enums.count(first) > 0 || m_aliases.count(first) > 0 || m_consts.count(first) > 0 || m_globals.count(first) > 0))
						return base + "::" + written;
					d = m_structs.count(base) > 0 ? m_structs[base]->decl : nullptr;
				}
			}
			if (s.empty())
				return qualifyThroughAlias(written, scope);
			// The scope around s (its last '::' outside a template's arguments: Ns::Array<Ns::Point>).
			size_t sep = std::string::npos;
			int angles = 0;
			for (size_t i = s.size(); i-- > 1; )
			{
				if (s[i] == '>') angles++;
				else if (s[i] == '<') angles--;
				else if (angles == 0 && s[i] == ':' && s[i - 1] == ':') { sep = i - 1; break; }
			}
			s = (sep == std::string::npos) ? "" : s.substr(0, sep);
		}
	}

	// A::rest where A (or a longer prefix) is an alias of a struct / enum: the struct's name
	// instead (alias Con = Gfx::Console;  Con::Errors.Bad  ->  Gfx::Console::Errors.Bad).
	// written itself if there is no such alias.
	std::string CodeGen::qualifyThroughAlias(const std::string& written, const std::string& scope)
	{
		if (m_aliasDepth > 8)
			return written;
		for (size_t sep = written.find("::", 1); sep != std::string::npos; sep = written.find("::", sep + 2))
		{
			const std::string prefix = qualify(written.substr(0, sep), scope);
			auto it = m_aliases.find(prefix);
			if (it == m_aliases.end())
				continue;
			const std::string target = aliasedScope(prefix);
			if (target == prefix)
				return written;
			m_aliasDepth++;
			const std::string full = qualify(target + written.substr(sep), "");
			m_aliasDepth--;
			return full;
		}
		return written;
	}

	// The struct / enum an alias (full name) names, through aliases of aliases; full itself if
	// it is not such an alias.
	std::string CodeGen::aliasedScope(const std::string& full)
	{
		std::string name = full;
		for (int depth = 0; depth < 8; depth++)
		{
			auto it = m_aliases.find(name);
			if (it == m_aliases.end())
				break;
			const TypeSyntax* ts = it->second.decl->typeSyntax;
			if (ts == nullptr || ts->kind != TypeSyntax::Kind::Named)
				return full;
			if (!ts->args.empty())
			{
				// An instance of a template (alias Heap = BasicHeap<Guard>): its struct.
				AliasInfo& a = it->second;
				if (a.type == nullptr && !a.resolving)
				{
					a.resolving = true;
					a.type = resolveType(a.decl->typeSyntax);
					a.resolving = false;
				}
				if (a.type == nullptr || a.type->kind != Type::Kind::Struct)
					return full;
				name = a.type->structInfo->name;
				continue;
			}
			name = ts->resolved ? ts->name : qualify(ts->name, it->second.decl->scope);
		}
		return (m_structs.count(name) > 0 || m_enums.count(name) > 0) ? name : full;
	}

	void CodeGen::checkTypeAccess(const std::string& full, const std::string& scope, const SourcePos& pos)
	{
		if (m_privTypes.empty())
			return;
		size_t sep = 0;
		while (true)
		{
			sep = full.find("::", sep);
			const std::string prefix = full.substr(0, sep);
			auto it = m_privTypes.find(prefix);
			if (it != m_privTypes.end())
			{
				const std::string& owner = it->second;
				if (scope != owner && scope.rfind(owner + "::", 0) != 0)
					error(pos, "'" + prefix.substr(owner.size() + 2) + "' is private to " + owner + " (priv): only " + owner + " can use it");
			}
			if (sep == std::string::npos)
				break;
			sep += 2;
		}
	}

	void CodeGen::resolveNames(Node* program)
	{
		for (Node* d : program->list)
		{
			if (d->guard != nullptr)
				continue;		// (in an if constexpr: when it is decided)
			attempt([&] {
				if (d->kind == NodeKind::StructDecl)
				{
					// The fields' types are looked up from the struct (its nested types); the
					// classes it extends, its interfaces and friends from where it is.
					resolveTypeNames(d->typeSyntax, d->scope);
					for (TypeSyntax* t : d->impls)
						resolveTypeNames(t, d->scope);
					for (Node* f : d->friends)
					{
						if (f->resolved)
							continue;
						f->text = f->name;
						f->name = f->name.rfind("::", 0) == 0 ? f->name.substr(2) : aliasedScope(qualify(f->name, d->scope));
						f->resolved = true;
					}
					for (Node* f : d->list)
						resolveNames(f, f->scope);
				}
				else if (d->kind != NodeKind::LowBlock && d->kind != NodeKind::NamespaceDecl && d->kind != NodeKind::TemplateDecl)
					resolveNames(d, d->scope);
			});
		}
	}

	void CodeGen::resolveNames(Node* n, const std::string& scope)
	{
		if (n == nullptr)
			return;
		if (n->typeSyntax != nullptr)
			resolveTypeNames(n->typeSyntax, scope);
		if (n->throwsSyntax != nullptr)
			resolveTypeNames(n->throwsSyntax, scope);
		for (TypeSyntax* t : n->templateArgs)
			resolveTypeNames(t, scope);
		if (n->kind == NodeKind::Ident && !n->resolved)
		{
			// (an alias of a struct / enum: that struct / enum, for Enum.Member and Struct.sub())
			const std::string full = aliasedScope(qualify(n->name, scope));
			if (full != n->name)
			{
				if (n->text.empty())
					n->text = n->name;
				n->name = full;
			}
			n->resolved = true;
			n->memberBody = m_memberBody;
			checkTypeAccess(n->name, scope, n->pos);
		}
		if (n->kind == NodeKind::LowBlock)
			return;		// ($names: when it is generated)
		if (n->kind == NodeKind::FuncDecl)
		{
			// A struct's sub: the names in its body may be its struct's members (implicitMember);
			// not those of its parameters' default values, nor of the lambdas in it (no this).
			const bool outer = m_memberBody;
			m_memberBody = false;
			for (Node* c : { n->b, n->c })
				resolveNames(c, scope);
			for (Node* c : n->list)
				resolveNames(c, scope);
			m_memberBody = n->owner != nullptr && n->owner->kind == NodeKind::StructDecl;
			resolveNames(n->a, scope);
			m_memberBody = outer;
			return;
		}
		// (a lambda's d is the sub it is written in: not part of it)
		for (Node* c : { n->a, n->b, n->c, n->kind == NodeKind::FuncDecl ? nullptr : n->d })
			resolveNames(c, scope);
		for (Node* c : n->list)
			resolveNames(c, scope);
	}

	void CodeGen::resolveTypeNames(TypeSyntax* ts, const std::string& scope)
	{
		if (ts == nullptr)
			return;
		switch (ts->kind)
		{
			case TypeSyntax::Kind::Named:
				if (ts->fixed != nullptr)
					break;		// (a template parameter in an instance)
				if (!ts->resolved)
				{
					if (ts->written.empty())
						ts->written = ts->name;
					ts->name = qualify(ts->name, scope);
					ts->resolved = true;
					checkTypeAccess(ts->name, scope, ts->pos);
				}
				for (TypeSyntax* a : ts->args)
					resolveTypeNames(a, scope);
			break;
			case TypeSyntax::Kind::Pointer:
				resolveTypeNames(ts->base, scope);
			break;
			case TypeSyntax::Kind::Array:
				resolveTypeNames(ts->base, scope);
				resolveNames(ts->count, scope);
			break;
			case TypeSyntax::Kind::Function:
				for (TypeSyntax* p : ts->params)
					resolveTypeNames(p, scope);
				resolveTypeNames(ts->base, scope);
				resolveTypeNames(ts->throws, scope);
			break;
			case TypeSyntax::Kind::Value:
			case TypeSyntax::Kind::ValueType:
				resolveNames(ts->value, scope);
			break;
			case TypeSyntax::Kind::BaseType:
				resolveTypeNames(ts->base, scope);
			break;
		}
	}

	void CodeGen::collect(Node* program)
	{
		for (Node* d : program->list)
		{
			if (d->guard != nullptr)
				continue;		// (in an if constexpr: applyGuards)
			attempt([&] {
				switch (d->kind)
				{
					case NodeKind::NamespaceDecl:
						declareName(d->name, d->namePos, "a namespace");
					break;
					case NodeKind::StructDecl:
					{
						declareName(d->name, d->pos, d->classKind == 1 ? "a class" : d->classKind == 2 ? "an interface" : "a struct");
						if (d->isPrivate && d->owner != nullptr)
							m_privTypes[d->name] = d->owner->name;
						StructInfo* info = new StructInfo();
						info->name = d->name;
						info->decl = d;
						info->anonymous = d->value != 0;
						info->pos = d->pos;
						info->kind = d->classKind;
						info->isAbstract = (d->classFlags & 1) != 0;
						info->isFinal = (d->classFlags & 2) != 0;
						m_structs[d->name] = info;
					}
					break;
					case NodeKind::EnumDecl:
						declareName(d->name, d->pos, "an enum");
						if (d->isPrivate && d->owner != nullptr)
							m_privTypes[d->name] = d->owner->name;
						m_enums[d->name].decl = d;
					break;
					case NodeKind::AliasDecl:
						declareName(d->name, d->pos, "an alias");
						if (d->isPrivate && d->owner != nullptr)
							m_privTypes[d->name] = d->owner->name;
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
							v->isStatic = !d->text.empty();
							v->decl = d;
							v->symbol = dataSymbol(d->name, v->isStatic);
							v->pos = d->pos;
							v->defPos = d->namePos;
							v->readOnly = true;
							v->external = d->external != 0;
							m_globals[d->name] = v;
							m_globalDecls.push_back(d);
							break;
						}
						declareName(d->name, d->pos, "a constant");
						m_consts[d->name].decl = d;
						// (A const whose value is not a compile-time constant is a variable: in the
						// globals' order. A local constant: in its sub.)
						if (d->constKind == 1 && d->hoistKind != 1)
							m_globalDecls.push_back(d);
					break;
					case NodeKind::VarDecl:
					{
						declareName(d->name, d->pos, "a global variable");
						VarInfo* v = new VarInfo();
						v->name = d->name;
						v->storage = VarInfo::Storage::Global;
						v->isStatic = !d->text.empty();
						v->decl = d;
						v->symbol = dataSymbol(d->name, v->isStatic);
						v->pos = d->pos;
						v->defPos = d->namePos;
						v->external = d->external != 0;
						v->constKind = d->constKind;
						v->isConst = d->constKind == 1;
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
						f->isInline = (d->value & 2) != 0 && !m_options.noInline;
						f->external = d->external != 0 && (d->a == nullptr || d->constKind != 0);
						f->defined = d->a != nullptr || f->external || (d->value & 1024) != 0;	// (pure: no body)
						m_funcs[d->name].push_back(f);
						m_funcList.push_back(f);
					}
					break;
					case NodeKind::LowBlock:
						m_topLows.push_back(d);
					break;
					case NodeKind::TemplateDecl:
						registerTemplate(d);
					break;
					case NodeKind::StaticAssert:
						m_staticAsserts.push_back(d);	// (checked after the declarations)
					break;
					default:
						error(d->pos, "unexpected declaration");
				}
			});
		}
	}

	// An alias's type (resolved the first time it is used).
	Type* CodeGen::aliasType(const std::string& name, const SourcePos& pos)
	{
		AliasInfo& a = m_aliases[name];
		if (a.type == nullptr)
		{
			if (a.resolving)
				error(pos, "alias '" + name + "' refers to itself");
			a.resolving = true;
			a.type = resolveType(a.decl->typeSyntax);
			a.resolving = false;
		}
		return a.type;
	}

	// Name<args> as a type: a struct template's instance, or the type an alias template's names.
	Type* CodeGen::templateType(const std::string& name, const std::vector<Type*>& args, const SourcePos& pos)
	{
		const std::string inst = instantiate(name, args, nullptr, nullptr, pos, true);
		if (m_structs.count(inst) > 0)
			return m_types.structType(m_structs[inst]);
		return aliasType(inst, pos);
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
				if (ts->fixed != nullptr)
				{
					if (ts->fixed == m_notGiven)
						error(ts->pos, "the template parameter " + ts->name + " is not given here (it is optional: is_given(" + ts->name + ") tells)");
					if (m_options.index != nullptr)
						indexTemplateParam(ts->pos, ts->name);
					return ts->fixed;
				}
				const std::string& written = ts->written.empty() ? ts->name : ts->written;
				if (!ts->args.empty() || m_templates.count(ts->name) > 0)
				{
					// Name<args>: the template's instance (a struct).
					if (ts->args.empty())
						error(ts->pos, written + " is a template: give its types (" + written + "<...>)");
					std::vector<Type*> args;
					for (TypeSyntax* a : ts->args)
						args.push_back(templateArgument(a));
					Type* t = templateType(ts->name, args, ts->pos);
					if (t->kind == Type::Kind::Struct && m_options.index != nullptr && !m_discarding)
						indexName(ts->pos, written, t->structInfo->decl->namePos, hoverStruct(t->structInfo));
					return t;
				}
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
						indexName(ts->pos, info->anonymous ? "struct" : written, info->decl->namePos, hoverStruct(info));
					return m_types.structType(info);
				}
				if (m_enums.count(ts->name) > 0)
				{
					if (m_options.index != nullptr)
						indexName(ts->pos, written, m_enums[ts->name].decl->namePos, hoverEnum(ts->name));
					return enumType(m_enums[ts->name]);
				}
				if (m_aliases.count(ts->name) > 0)
				{
					if (m_options.index != nullptr)
						indexName(ts->pos, written, m_aliases[ts->name].decl->namePos, "alias " + ts->name);
					return aliasType(ts->name, ts->pos);
				}
				if (m_consts.count(ts->name) > 0 && m_consts[ts->name].decl->name.rfind("__tv_", 0) == 0)
					error(ts->pos, "the value " + m_consts[ts->name].decl->text + " where a type is needed (a template parameter given a value: is_value(T) tells, value_type(T) is its type)");
				if (m_consts.count(ts->name) > 0)
					error(ts->pos, "'" + written + "' is a constant, not a type (value_type(" + written + ") is its type)");
				error(ts->pos, "unknown type: " + written);
			}
			case TypeSyntax::Kind::Pointer:
			{
				Type* base = resolveType(ts->base);
				if (ts->isRef && (base->isReference() || base->kind == Type::Kind::Void || base->kind == Type::Kind::Array))
					error(ts->pos, "ref " + base->name() + ": a reference refers to a value (not void, an array or a reference)");
				if (!ts->isRef && base->isReference())
					error(ts->pos, "a pointer to a reference (" + base->name() + "*): use a pointer to the value");
				return m_types.pointer(base, ts->constTarget, ts->isRef);
			}
			case TypeSyntax::Kind::Array:
			{
				if (ts->count == nullptr)
					error(ts->pos, "the array needs a size ([] is only allowed for the outer array of a variable or constant with a value)");
				Type* base = resolveType(ts->base);
				if (base->kind == Type::Kind::Void)
					error(ts->pos, "arrays of void are not allowed");
				rejectReference(base, ts->pos, "array elements");
				if (base->kind == Type::Kind::Struct)
				{
					resolveStruct(base->structInfo, ts->pos);
					requireValueType(base, ts->pos, "an array element");
				}
				int64_t count = constExpr(ts->count, "an array size");
				if (count <= 0 || count > 0x0FFFFFFF)
					error(ts->pos, "invalid array size: " + std::to_string(count));
				return m_types.array(base, (uint32_t)count);
			}
			case TypeSyntax::Kind::Value:
				error(ts->pos, "a value where a type is needed");
			case TypeSyntax::Kind::BaseType:
				return baseType(resolveType(ts->base), ts->absolute, ts->pos);
			case TypeSyntax::Kind::ValueType:
			{
				if (ts->value == nullptr)
					error(ts->pos, "value_type(" + ts->name + "): " + ts->name + " is a type here (" + (ts->fixed ? ts->fixed->name() : "?") + "), not a value");
				check(ts->value);
				Type* t = decay(ts->value->type);
				if (t == nullptr || t->kind == Type::Kind::Void || t->kind == Type::Kind::Null)
					error(ts->pos, "value_type(...) needs a value with a type");
				// (an integer literal: i32, as var x = 5 would be)
				if (t->isInteger() && ts->value->kind == NodeKind::IntLit && ts->value->name != "u" && ts->value->name != "char")
					t = m_types.i32();
				if (m_options.index != nullptr && !m_discarding)
					indexName(ts->pos, "value_type", {}, "value_type(...): " + t->name(true) + comment({ "the type of a value" }));
				return t;
			}
			case TypeSyntax::Kind::Function:
			{
				std::vector<Type*> params;
				for (TypeSyntax* p : ts->params)
				{
					Type* t = resolveType(p);
					if (!t->isScalar() && t->kind != Type::Kind::Struct)
						error(p->pos, "sub parameters must be integers, pointers or structs (not " + t->name() + ")");
					params.push_back(t);
				}
				Type* ret = ts->base ? resolveType(ts->base) : m_types.voidType();
				if (ret->kind != Type::Kind::Void && !ret->isScalar() && ret->kind != Type::Kind::Struct)
					error(ts->pos, "subs return integers, pointers, structs or void (not " + ret->name() + ")");
				std::string errorName;
				Type* errors = ts->throws != nullptr ? errorType(ts->throws, errorName) : nullptr;
				return m_types.function(params, ret, ts->variadic, ts->owned, errors, errorName);
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
		filterMembers(info->decl);
		uint32_t offset = classLayout(info);
		std::vector<Node*> asserts;
		const std::string what = info->anonymous ? info->displayName() : (info->isClass() ? "class " : info->isInterface() ? "interface " : "struct ") + info->name;
		for (Node* f : info->decl->list)
		{
			if (f->kind == NodeKind::StaticAssert)
			{
				asserts.push_back(f);
				continue;
			}
			if (const StructField* same = info->field(f->name))
			{
				if (same->owner != nullptr)
					error(f->namePos, "'" + f->name + "' is a field of " + same->owner->name + " already (" + info->name + " extends it): a class cannot hide what it inherits");
				error(f->pos, "duplicate field '" + f->name + "' in " + what);
			}
			if (m_funcs.count(info->name + "::" + f->name) > 0)
				error(f->namePos, what + " has a field and a method named '" + f->name + "'");
			if (info->base != nullptr && !methodsOf(info->base, f->name).empty())
				error(f->namePos, "'" + f->name + "' is a sub of " + structOfMethod(methodsOf(info->base, f->name)[0])->name + " (" + info->name + " extends it): a field cannot have its name");
			StructField field;
			field.name = f->name;
			field.pos = f->pos;
			field.namePos = f->namePos;
			field.isPrivate = f->isPrivate;
			field.isProtected = f->isProtected;
			field.isReadonly = f->isReadonly;
			field.isConst = f->constKind == 1;
			{
				// (its type is inside the struct: priv constants as array sizes)
				const Node* savedOwner = m_fieldOwner;
				m_fieldOwner = info->decl;
				field.type = resolveType(f->typeSyntax);
				m_fieldOwner = savedOwner;
			}
			if (field.type->kind == Type::Kind::Void)
				error(f->pos, "fields cannot be void");
			rejectReference(field.type, f->pos, "fields");
			requireValueType(field.type, f->pos, "a field");
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
		implicitConstructor(info);
		implicitDestructor(info);
		for (Node* sa : asserts)
			checkStaticAssert(sa, nullptr);		// (sizeof the struct works here)
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
		if (!e->isConst && e->kind == NodeKind::Call && e->func != nullptr && e->func->decl->constKind != 0 && !m_ctWhy.empty())
			error(e->pos, what + " must be a compile-time constant: " + e->func->decl->shortName + "(...) could not be evaluated when compiling (" + m_ctWhy + ")");
		if (!e->isConst && e->kind == NodeKind::Ident && e->var != nullptr && e->var->isConst)
			error(e->pos, what + " must be a compile-time constant: '" + (e->text.empty() ? shownName(e->name) : e->text) +
				"' is computed when the program runs (a const whose value is not a constant: use constexpr values here)");
		if (!e->isConst || !(e->type->isInteger() || e->type->isFloat() || e->type->kind == Type::Kind::Null))
			error(e->pos, what + " must be a constant number");
		return e->constValue;
	}

	int64_t CodeGen::resolveConst(const std::string& name, const SourcePos& usePos)
	{
		ConstInfo& c = m_consts[name];
		if (c.resolved)
			return c.value;
		if (c.runtime)
			error(usePos, "'" + shownName(name) + "' is not a compile-time constant (its value is computed when the program runs): "
				"use constexpr values here");
		if (c.resolving)
			error(usePos, "constant '" + name + "' depends on itself");
		try
		{
			return resolveConstValue(name);
		}
		catch (CompileError&)
		{
			c.resolving = false;
			c.fields.clear();
			c.type = nullptr;
			throw;
		}
	}

	void CodeGen::decideConst(const std::string& name)
	{
		ConstInfo& c = m_consts[name];
		if (c.resolved || c.runtime)
			return;
		if (m_inDeclarations || c.decl->constKind != 1)
		{
			resolveConst(name, c.decl->pos);
			return;
		}
		try
		{
			resolveConst(name, c.decl->pos);
			return;
		}
		catch (CompileError&)
		{
			// Not a compile-time constant: a constant variable (its real errors come back below,
			// as the variable's).
		}
		c.runtime = true;
		Node* d = c.decl;
		if (d->hoistKind == 1)
			return;		// a local constant: computed where it is declared, at every call
		VarInfo* v = new VarInfo();
		v->name = d->name;
		v->storage = VarInfo::Storage::Global;
		v->isStatic = d->hoistKind == 2;
		v->decl = d;
		v->symbol = dataSymbol(d->name, v->isStatic);
		v->pos = d->pos;
		v->defPos = d->namePos;
		v->external = d->external != 0;
		v->isConst = true;
		v->constKind = 1;
		m_globals[d->name] = v;
		typeGlobal(d, v);
	}

	VarInfo* CodeGen::constStorage(const std::string& name)
	{
		ConstInfo& c = m_consts[name];
		if (c.storage != nullptr)
			return c.storage;
		VarInfo* v = new VarInfo();
		v->name = name;
		v->type = c.type;
		v->storage = VarInfo::Storage::Global;
		v->symbol = "__dsconst_" + symbolChars(name);
		v->readOnly = true;
		v->isConst = true;
		v->isStatic = true;
		v->decl = c.decl;
		v->pos = c.decl->pos;
		v->defPos = c.decl->namePos;
		c.storage = v;
		// Its bytes: the fields' values (by path).
		std::vector<uint8_t> bytes(c.type->size(), 0);
		std::function<void(StructInfo*, uint32_t, const std::string&)> l_fields = [&](StructInfo* info, uint32_t offset, const std::string& prefix) {
			for (const StructField& f : info->fields)
			{
				if (f.type->kind == Type::Kind::Struct)
					l_fields(f.type->structInfo, offset + f.offset, prefix + f.name + ".");
				else if (f.type->isScalar())
				{
					auto it = c.fields.find(prefix + f.name);
					const int64_t value = it != c.fields.end() ? it->second : 0;
					for (uint32_t i = 0; i < f.type->size(); i++)
						bytes[offset + f.offset + i] = (uint8_t)((uint64_t)value >> (8 * (f.type->size() - 1 - i)));
				}
			}
		};
		l_fields(c.type->structInfo, 0, "");
		m_ctBlobs[v] = bytes;		// (compile-time evaluation reads it; in the program if code uses it)
		m_lazyData.push_back(v);
		return v;
	}

	bool CodeGen::hasConstFields(Type* t, std::string* which)
	{
		if (t->kind == Type::Kind::Array)
			return hasConstFields(t->base, which);
		if (t->kind != Type::Kind::Struct)
			return false;
		for (const StructField& f : t->structInfo->fields)
		{
			std::string inner;
			if (f.isConst || hasConstFields(f.type, &inner))
			{
				if (which != nullptr)
					*which = f.name + (inner.empty() ? "" : "." + inner);
				return true;
			}
		}
		return false;
	}

	bool CodeGen::constructing(const StructInfo* info)
	{
		FuncInfo* f = m_inline.empty() ? m_func : m_inline.back().func;
		return f != nullptr && f->decl != nullptr && (f->decl->value & 4) != 0 && f->decl->shortName == "new" && f->decl->owner == info->decl;
	}

	int64_t CodeGen::resolveConstValue(const std::string& name)
	{
		ConstInfo& c = m_consts[name];
		c.resolving = true;
		Type* t = c.decl->typeSyntax ? resolveType(c.decl->typeSyntax) : nullptr;
		if (c.decl->a != nullptr)
			foldStringConst(c.decl->a);
		if (c.decl->a != nullptr && c.decl->a->kind == NodeKind::StrLit)
		{
			// A string constant: used as that string literal (stored once in the program).
			Type* str = m_types.pointer(m_types.u8(), true);
			if (t != nullptr && !(t->kind == Type::Kind::Pointer && t->base->size() == 1 && t->base->isInteger() && t->constTarget))
				error(c.decl->pos, "a string constant is a string (const u8* / const i8*: its text is read-only), not " + t->name());
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
			std::function<const StructInfo*(Type*)> l_withTable = [&](Type* ft) -> const StructInfo* {
				if (ft->kind == Type::Kind::Array)
					return l_withTable(ft->base);
				if (ft->kind != Type::Kind::Struct)
					return nullptr;
				if (!ft->structInfo->vptrs.empty())
					return ft->structInfo;
				for (const StructField& f : ft->structInfo->fields)
				{
					if (const StructInfo* w = l_withTable(f.type))
						return w;
				}
				return nullptr;
			};
			if (const StructInfo* w = l_withTable(t))
				error(c.decl->pos, "constant '" + shownName(name) + "': " + w->name + " has virtual subs (or interfaces), its objects get their tables when the program runs "
					"(use a variable)");
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
		if (!t->isInteger() && !t->isFloat())
			error(c.decl->pos, "constants are numbers (integers or f32), strings or structs");
		if (c.decl->a->type->isFloat() && !t->isFloat())
			error(c.decl->a->pos, "cannot convert f32 to " + t->name() + " in the value of constant '" + name + "' (use cast<" + t->name() + ">(...))");
		c.value = constConvert(value, c.decl->a->type, t);
		c.type = t;
		c.resolved = true;
		c.resolving = false;
		return c.value;
	}

	void CodeGen::constStructValues(StructInfo* info, Node* init, const std::string& prefix, ConstInfo& c, const std::string& constName)
	{
		if (init != nullptr && init->kind != NodeKind::InitList && prefix.empty())
		{
			// A struct value computed when compiling (a constexpr sub's): its fields.
			check(init, m_types.structType(info));
			requireConvertible(init, m_types.structType(info), "in the value of constant '" + constName + "'");
			if (init->kind == NodeKind::Ident && init->var != nullptr && m_ctBlobs.count(init->var) > 0)
			{
				ctFields(info, m_ctBlobs[init->var], 0, "", c);
				return;
			}
			if (init->kind == NodeKind::Call && init->func != nullptr && init->func->decl->constKind != 0 && !m_ctWhy.empty())
				error(init->pos, "constant '" + constName + "': " + init->func->decl->shortName + "(...) could not be evaluated when compiling (" + m_ctWhy + ")");
		}
		if (init != nullptr && init->kind != NodeKind::InitList)
			error(init->pos, "a struct constant's value is a { ... } list (or nothing: the fields' defaults)");
		if (init != nullptr && init->list.size() > info->fields.size())
			error(init->list[info->fields.size()]->pos, "too many values for " + (info->anonymous ? info->displayName() : "struct " + info->name));
		for (size_t i = 0; i < info->fields.size(); i++)
		{
			const StructField& field = info->fields[i];
			const std::string path = prefix + field.name;
			Node* v = (init != nullptr && i < init->list.size()) ? init->list[i] : nullptr;
			if (v != nullptr)
				checkFieldAccess(info, &field, v->pos);
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
			c.fields[path] = constConvert(value, v != nullptr ? v->type : nullptr, field.type);
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
		decideConst(e->name);
		if (m_consts[e->name].runtime)
			return;
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
			decideConst(e->name);
			if (m_consts[e->name].runtime)
				return nullptr;
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
			checkFieldAccess(structType->structInfo, f, e->namePos);
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
		Type* t = enumType(info);
		// With a type, the values must fit it; without (i32), any 32 bit value (as before).
		const bool typed = info.decl->typeSyntax != nullptr;
		const int bits = (int)t->size() * 8;
		const int64_t min = typed ? (t->isSigned() ? -(int64_t(1) << (bits - 1)) : 0) : INT32_MIN;
		const int64_t max = typed ? (t->isSigned() ? (int64_t(1) << (bits - 1)) - 1 : (int64_t(1) << bits) - 1) : UINT32_MAX;
		int64_t next = 0;
		for (Node* m : info.decl->list)
		{
			if (info.values.count(m->name) > 0)
				error(m->pos, "duplicate enum member: " + m->name);
			if (m->a)
				next = constExpr(m->a, "an enum value");
			if (next < min || next > max)
				error(m->a ? m->a->pos : m->pos, "enum value " + std::to_string(next) + " of " + info.decl->name + "." + m->name +
					(m->a ? "" : " (the previous one + 1)") + " does not fit " + (typed ? t->name() : "32 bits") +
					" (" + std::to_string(min) + " to " + std::to_string(max) + ")");
			info.values[m->name] = typed ? next : (int32_t)next;
			next++;
		}
		info.resolved = true;
		info.resolving = false;
	}

	Type* CodeGen::enumType(EnumInfo& info)
	{
		if (info.type != nullptr)
			return info.type;
		info.type = m_types.i32();
		if (info.decl->typeSyntax != nullptr)
		{
			Type* t = resolveType(info.decl->typeSyntax);
			if (!t->isInteger() || t->kind == Type::Kind::Bool || t->size() > 4)
				error(info.decl->typeSyntax->pos, "the type of enum " + info.decl->name + " must be an integer type (i8, i16, i32, u8, u16, u32), not " + t->name());
			info.type = t;
		}
		return info.type;
	}

	std::string CodeGen::mangle(Type* t)
	{
		switch (t->kind)
		{
			case Type::Kind::Pointer: return mangle(t->base) + (t->isRef ? (t->constTarget ? "cr" : "r") : (t->constTarget ? "c" : "p"));
			case Type::Kind::Array: return mangle(t->base) + "a" + std::to_string(t->count);
			case Type::Kind::Struct:
			{
				// (A::B -> A__B, Box<u16, 4> -> Box_Lu16_C4_R: only label characters)
				return "S" + symbolChars(t->structInfo->name);
			}
			case Type::Kind::Function:
			{
				std::string s = "F";
				for (size_t i = 0; i < t->params.size(); i++)
					s += std::string(i < 32 && (t->owned & (1u << i)) != 0 ? "O" : "") + mangle(t->params[i]);
				if (t->variadic != 0)
					s += t->variadic == 2 ? "VT" : "V";
				return s + "r" + mangle(t->base) + (t->throws != nullptr ? "X" + t->throwsName : "");
			}
			default: return t->name();
		}
	}

	// throws E: E's integer type and its full name (an enum: its members are the errors).
	Type* CodeGen::errorType(TypeSyntax* ts, std::string& name)
	{
		Type* t = resolveType(ts);
		std::string full = ts->kind == TypeSyntax::Kind::Named && ts->fixed == nullptr ? ts->name : "";
		for (int depth = 0; depth < 16 && m_aliases.count(full) > 0; depth++)
		{
			const Node* decl = m_aliases[full].decl;
			const TypeSyntax* target = decl != nullptr ? decl->typeSyntax : nullptr;
			full = target != nullptr && target->kind == TypeSyntax::Kind::Named && target->fixed == nullptr ? target->name : "";
		}
		if (full.empty() || m_enums.count(full) == 0)
			error(ts->pos, "throws takes an enum (its members are the errors), not " + t->name());
		name = full;
		return t;
	}

	void CodeGen::resolveFunction(FuncInfo* f)
	{
		Node* d = f->decl;
		std::vector<Type*> params;
		bool seenDefault = false;
		uint32_t owned = 0;
		for (Node* p : d->list)
		{
			Type* t = resolveType(p->typeSyntax);
			if (p->owned)
			{
				if (t->isReference())
					error(p->pos, "own " + t->name() + ": a reference refers to the caller's value (own takes the value itself: own " + t->base->name() + ")");
				if (params.size() >= 32)
					error(p->pos, "own: only the first 32 parameters can be own");
				if ((d->value & 1) != 0)
					error(p->pos, "an interrupt sub has no own parameters");
				owned |= 1u << params.size();
			}
			if (t->kind == Type::Kind::Struct)
			{
				resolveStruct(t->structInfo, p->pos);		// (by value: a copy)
				requireValueType(t, p->pos, "a parameter");
			}
			else if (!t->isScalar())
			{
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
			if (p->a != nullptr && t->isReference())
				error(p->a->pos, "a reference parameter has no default value (it refers to what the caller gives)");
			if (p->a)
				seenDefault = true;
			else if (seenDefault)
				error(p->pos, "parameters after one with a default value need a default value too");
			else
				f->requiredParams++;
		}
		Type* ret = d->typeSyntax ? resolveType(d->typeSyntax) : m_types.voidType();
		if (ret->kind == Type::Kind::Struct)
		{
			resolveStruct(ret->structInfo, d->pos);
			requireValueType(ret, d->pos, "a return value");
		}
		else if (ret->kind != Type::Kind::Void && !ret->isScalar())
			error(d->pos, "subs return integers, pointers, structs or void (not " + ret->name() + ")");
		std::string errorName;
		Type* errors = nullptr;
		if (d->throwsSyntax != nullptr)
		{
			errors = errorType(d->throwsSyntax, errorName);
			const std::string& sn = d->shortName;
			const bool isOperator = sn.rfind("operator", 0) == 0 && sn.size() > 8 && !std::isalnum((unsigned char)sn[8]) && sn[8] != '_';
			const char* why = (d->value & 1) != 0 ? "an interrupt sub" : (d->value & 4) != 0 && sn == "new" ? "a constructor (it builds a value: make a static sub that throws)"
				: (d->value & 4) != 0 && sn == "del" ? "a destructor" : isOperator ? "an operator" : f->name == "main" ? "main (it returns the program's result)"
				: d->constKind == 2 ? "a constexpr sub" : d->constKind == 3 ? "a consteval sub" : nullptr;
			if (why != nullptr)
				error(d->throwsSyntax->pos, std::string(why) + " cannot throw");
		}
		if (errors != nullptr && ret->isReference())
			error(d->throwsSyntax->pos, "a sub returning a reference cannot throw (return a pointer: null when there is nothing)");
		f->type = m_types.function(params, ret, d->variadic, owned, errors, errorName);
		if (d->variadic != 0)
		{
			if (seenDefault)
				error(d->namePos, "a sub with ... cannot also have default values (both are about the arguments after the last one given)");
			if ((d->value & 2) != 0)
				error(d->namePos, "inline subs cannot have ... (the extra arguments are read from the call's stack)");
			if (f->name == "main")
				error(d->namePos, "main cannot have ...: it takes no parameters, or (argc, argv)");
		}
		if (f->interrupt && (!params.empty() || d->variadic != 0 || ret->kind != Type::Kind::Void))
			error(d->pos, "interrupt subs take no parameters and return void");
		// An operator: on a struct (a method, its first operand this), or a top-level sub taking one.
		if (d->shortName.rfind("operator", 0) == 0 && d->shortName.size() > 8 && !std::isalnum((unsigned char)d->shortName[8]) && d->shortName[8] != '_')
		{
			std::string op = d->shortName.substr(8);
			if (op.rfind(" cast ", 0) == 0)
				op = " cast";		// (operator cast <type>)
			const bool method = (d->value & 4) != 0;
			if ((d->value & 8) != 0)
				error(d->namePos, "an operator works on a struct: it cannot be static (a top-level sub operator" + op + "(...) takes its operands as parameters)");
			const size_t n = params.size() - (method ? 1 : 0);	// (the operands but this)
			const bool unaryOnly = op == "!" || op == "~" || op == "++" || op == "--" || op == " post++" || op == " post--";
			const bool either = op == "-" || op == "+" || op == "*";		// (unary or binary: * is also a dereference)
			const bool anyCount = op == "()" || op == " cast" || op == "->";
			if (op == "->" && (n != 0 || ret->kind == Type::Kind::Void))
				error(d->namePos, "operator-> takes no parameters and returns what -> goes on with: a pointer to a struct (or a struct with an operator->)");
			if (!method && (op == "()" || op == "[]" || op == "=" || op == " cast" || op == "->"))
				error(d->namePos, "operator" + op + " is a method (in the struct it works on)");
			if (d->variadic != 0 && op != "()")
				error(d->namePos, "an operator takes its operands (not ...)");
			if (op == " cast")
			{
				if (n != 0)
					error(d->namePos, "operator cast takes no parameters (its type is what it returns: sub operator cast() -> T)");
				if (ret->kind == Type::Kind::Void)
					error(d->namePos, "operator cast returns the type it converts to (sub operator cast() -> T)");
			}
			else if (!anyCount)
			{
				const size_t want = unaryOnly ? 0 : 1;		// (as a method)
				const size_t have = n - (method ? 0 : 1);
				if (op == "[]" ? n < 1 : either ? (have > 1) : have != want)
					error(d->namePos, "operator" + op + " takes " + (op == "[]" ? std::string("an index (or more)") : either ? (method ? "no operand (-a) or one (a - b)" : "one operand (-a) or two (a - b)") :
						std::to_string(want + (method ? 0 : 1)) + " operand" + (want + (method ? 0 : 1) == 1 ? "" : "s") + (method ? " besides this" : "")));
			}
			if (!method)
			{
				bool structOperand = false;
				for (Type* p : params)
					structOperand = structOperand || p->kind == Type::Kind::Struct || (p->kind == Type::Kind::Pointer && p->base->kind == Type::Kind::Struct);
				if (!structOperand)
					error(d->namePos, "a top-level operator" + op + " needs a struct operand (a struct, or a pointer to one)");
			}
		}
		// A destructor: sub del() in a struct (virtual by itself in a class with a table).
		if ((d->value & 4) != 0 && d->shortName == "del")
		{
			if ((d->value & 8) != 0 || params.size() != 1 || d->variadic != 0 || ret->kind != Type::Kind::Void)
				error(d->namePos, "a destructor is sub del() (no parameters, no result, not static): it ends the life of this");
			if (d->owner != nullptr && d->owner->classKind == 2)
				error(d->namePos, "interfaces have no destructor");
			if ((d->value & (512 | 1024 | 2048 | 4096)) != 0)
				error(d->namePos, "del is virtual by itself (in a class with a table): no virt, pure, override or final");
			if (d->constKind == 3)
				error(d->namePos, "a destructor cannot be consteval");
			if (m_funcs.count(d->name) > 0 && m_funcs[d->name].size() > 1)
				error(d->namePos, "a struct has one destructor");
		}
		// A constructor: sub new(...) in a struct, on the object being built (this).
		if ((d->value & 4) != 0 && d->shortName == "new")
		{
			if ((d->value & 8) != 0)
				error(d->namePos, "a constructor (new) works on the struct being built (this): it cannot be static");
			if (ret->kind != Type::Kind::Void)
				error(d->namePos, "a constructor (new) returns nothing (sub new(...) { ... }, without -> type)");
		}
		// (On the declaration: also with --no-inline.)
		const bool declaredInline = (d->value & 2) != 0;
		if (declaredInline && f->name == "main")
			error(d->pos, "main cannot be inline");
		if (declaredInline && d->a)
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

	VarInfo* CodeGen::lookupLocal(const std::string& name)
	{
		for (int i = (int)m_scopes.size() - 1; i >= 0; i--)
		{
			auto it = m_scopes[i].find(name);
			if (it != m_scopes[i].end())
				return it->second;
		}
		return nullptr;
	}

	// A name written in the body of a struct's sub (memberBody) that is none of the sub's locals or
	// parameters: the struct's member of that name, if it has one (with what it inherits), before
	// any global, namespace's name or sub of that name (as in C++):
	//   - a field, or a method (call: e is the Call, its callee the name): this->name, checked as if
	//     written so (virtual subs called through the table, priv / readonly / const as usual);
	//   - a static sub: Struct::name (also in the struct's static subs, which have no this).
	// (Static fields and constants: already found from the struct's scope by resolveNames.)
	// true: e (or its callee) is now a Member; false: nothing to do, or a static sub renamed.
	bool CodeGen::implicitMember(Node* e, bool call)
	{
		Node* id = call ? e->a : e;
		if (id->kind != NodeKind::Ident || !id->memberBody)
			return false;
		id->memberBody = false;		// (decided once)
		const std::string written = id->text.empty() ? id->name : id->text;
		if (written.empty() || written[0] == ' ' || written == "this" || written.find(':') != std::string::npos || written.find('.') != std::string::npos
			|| (!call && !id->templateArgs.empty()))
			return false;
		if (lookupLocal(id->name) != nullptr || lookupLocal(written) != nullptr)
			return false;
		VarInfo* self = lookupLocal("this");
		StructInfo* info = nullptr;
		if (self != nullptr && self->type != nullptr && self->type->kind == Type::Kind::Pointer && self->type->base->kind == Type::Kind::Struct)
			info = self->type->base->structInfo;
		else
		{
			self = nullptr;
			info = structOfMethod(m_func);		// (a static sub)
		}
		if (info == nullptr)
			return false;
		resolveStruct(info, id->pos);
		const bool field = info->field(written) != nullptr;
		const std::vector<FuncInfo*> subs = field ? std::vector<FuncInfo*>() : methodsOf(info, written);
		const bool methodTemplate = !field && subs.empty() && m_templates.count(info->name + "::" + written) > 0;
		if (!field && subs.empty() && !methodTemplate)
			return false;
		for (FuncInfo* f : subs)
		{
			if ((f->decl->value & 8) != 0)
			{
				// A static sub: as Struct::name (its overloads, those it inherits, resolved as usual).
				if (id->text.empty())
					id->text = written;
				id->name = f->decl->name;
				id->resolved = true;
				return false;
			}
		}
		if (self == nullptr)
		{
			if (methodTemplate)
				return false;
			error(id->pos, "'" + written + "' is a " + (field ? "field" : "method") + " of " + info->name + ": a static sub has no this (it can't use it)");
		}
		Node* thisNode = new Node();
		thisNode->kind = NodeKind::Ident;
		thisNode->name = "this";
		thisNode->pos = id->pos;
		thisNode->resolved = true;
		Node* m = call ? new Node() : e;
		const SourcePos pos = id->pos;
		const std::vector<TypeSyntax*> templateArgs = id->templateArgs;
		const bool isPrivate = id->isPrivate;
		if (!call)
			*e = Node();
		m->kind = NodeKind::Member;
		m->pos = pos;
		m->namePos = pos;
		m->name = written;
		m->value = 1;						// (->)
		m->a = thisNode;
		m->templateArgs = templateArgs;
		m->isPrivate = isPrivate;
		if (call)
			e->a = m;
		return true;
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
		if (a->isFloat() || b->isFloat())
			return m_types.f32();
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
		const int c = builtinConversionCost(from, to);
		if (c >= 0 || m_noUserConv || from->type == nullptr)
			return c;
		UserConv u;
		if (to->isReference())
			return (to->constTarget && !boundReference(from) && userConversion(from, to->base, u, false, false)) ? u.cost + 1 : -1;
		return userConversion(from, to, u, false, false) ? u.cost : -1;
	}

	int CodeGen::builtinConversionCost(Node* from, Type* to)
	{
		if (to != nullptr && to->isReference())
			return referenceCost(from, to);
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
		// const T* -> T*: no (it would let the target change); T* -> const T*: yes.
		if (f->kind == Type::Kind::Pointer && to->kind == Type::Kind::Pointer && f->constTarget && !to->constTarget)
			return -1;
		// int -> f32: implicit (worse than any int -> int, so overloads prefer the integer
		// ones for integers); f32 -> anything else needs a cast.
		if (f->isInteger() && to->isFloat())
			return 4;
		if (f->isFloat())
			return -1;
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
			if (f->base == to->base)
				return 1;		// (adding const)
			// A class pointer to a pointer to a class it extends / an interface it implements (the
			// nearest is the better).
			int depth = 0;
			if (f->base->kind == Type::Kind::Struct && to->base->kind == Type::Kind::Struct && derives(f->base->structInfo, to->base->structInfo, nullptr, &depth))
				return depth > 1 ? 2 : 1;
			if (to->isVoidPointer() && f->base->kind == Type::Kind::Struct && f->base->structInfo->kind != 0)
				return 3;		// (worse than to a class it extends)
			if (to->isVoidPointer() || f->isVoidPointer())
				return 1;
			return -1;
		}
		return -1;
	}

	void CodeGen::requireConvertible(Node* from, Type* to, const std::string& context)
	{
		if (to->isReference())
		{
			bindReference(from, to, context);		// (its address, or a temporary's)
			return;
		}
		if (builtinConversionCost(from, to) >= 0)
			return;
		UserConv u;
		if (from->type != nullptr && userConversion(from, to, u, false, true))
		{
			// (an implicit constructor / operator cast: from becomes the conversion)
			Node* inner = new Node(*from);
			applyConversion(from, inner, u);
			return;
		}
		std::string fromName = (from->type ? from->type->name() : "overloaded sub '" + (from->kind == NodeKind::Ident ? from->name : from->a->name) + "'");
		std::string hint = "";
		if (from->type && to->isPointer() && from->type->isInteger())
			hint = " (use cast<" + to->name() + ">(...))";
		else if (from->type && to->isInteger() && (from->type->isPointer() || from->type->isFunction()))
			hint = " (use cast<" + to->name() + ">(...))";
		else if (from->type && from->type->isFloat() && to->isInteger())
			hint = " (use cast<" + to->name() + ">(...): it truncates toward zero)";
		else if (from->type && from->type->isConstPointer() && to->kind == Type::Kind::Pointer && !to->constTarget)
			hint = " (it points at a constant: " + to->name() + " would let it change; cast<" + to->name() + ">(...) if it really may)";
		else if (from->type && from->type->isPointer() && to->isPointer())
		{
			int64_t delta = 0;
			if (pointerAdjust(decay(from->type), to, delta))
				hint = " (" + to->base->name() + " is derived from " + decay(from->type)->base->name() + ": static_cast<" + to->name() + ">(...), if it points at one)";
			else
				hint = " (use cast<" + to->name() + ">(...))";
		}
		error(from->pos, "cannot convert " + fromName + " to " + to->name() + " " + context + hint);
	}

	FuncInfo* CodeGen::bestOverload(const std::vector<FuncInfo*>& funcs, const std::vector<Node*>& args, bool& ambiguous)
	{
		FuncInfo* best = nullptr;
		int bestCost = 0x7FFFFFFF;
		ambiguous = false;
		for (FuncInfo* f : funcs)
		{
			if (f->type == nullptr)
				continue;
			const auto& params = f->type->params;
			if ((args.size() > params.size() && f->type->variadic == 0) || args.size() < f->requiredParams)
				continue;
			// ...: worse than a sub that takes the arguments as they are, each extra one too.
			int cost = f->type->variadic != 0 ? 1 + 2 * (int)(args.size() > params.size() ? args.size() - params.size() : 0) : 0;
			bool ok = true;
			for (size_t i = params.size(); i < args.size() && ok; i++)
				ok = args[i]->type != nullptr;
			for (size_t i = 0; i < args.size() && i < params.size() && ok; i++)
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
		return best;
	}

	FuncInfo* CodeGen::resolveOverload(const std::string& name, const std::vector<Node*>& args, const SourcePos& pos, const std::vector<FuncInfo*>* candidates)
	{
		const auto& funcs = candidates != nullptr ? *candidates : m_funcs[name];
		bool ambiguous = false;
		FuncInfo* best = bestOverload(funcs, args, ambiguous);
		std::string argTypes = "(";
		for (size_t i = 0; i < args.size(); i++)
			argTypes += (i > 0 ? ", " : "") + (args[i]->type ? args[i]->type->name() : std::string("sub"));
		argTypes += ")";
		if (best == nullptr && !args.empty() && args[0]->type != nullptr && args[0]->type->isConstPointer() && args[0]->type->base->kind == Type::Kind::Struct)
		{
			// A method that may change its struct, on one that cannot change.
			for (FuncInfo* f : funcs)
			{
				if ((f->decl->value & 12) == 4 && !f->type->params.empty() && f->type->params[0] == m_types.pointer(args[0]->type->base))
				{
					const std::string why = (args[0]->kind == NodeKind::Unary && args[0]->name == "&") ? args[0]->a->readOnlyWhy : "";
					error(pos, "'" + f->decl->shortName + "' is not a readonly sub (it may change the " + args[0]->type->base->name() + "), and " +
						(why.empty() ? "this one is reached through a const pointer" : why) + " (if it does not change it: readonly sub " + f->decl->shortName + "(...))");
				}
			}
		}
		if (best == nullptr && funcs.size() == 1 && funcs[0]->type != nullptr && args.size() <= funcs[0]->type->params.size() && args.size() >= funcs[0]->requiredParams)
		{
			// (one sub: the reference parameter that does not take its argument says why)
			FuncInfo* f = funcs[0];
			for (size_t i = 0; i < args.size(); i++)
			{
				if (f->type->params[i]->isReference() && args[i]->type != nullptr && conversionCost(args[i], f->type->params[i]) < 0)
					bindReference(args[i], f->type->params[i], "for parameter '" + f->paramNames[i] + "' of " + f->name);
			}
		}
		if (best == nullptr)
		{
			std::string msg = "no sub '" + name + "' takes " + argTypes;
			if (funcs.size() > 0)
			{
				msg += "; candidates:";
				for (FuncInfo* f : funcs)
					msg += "\n    " + f->decl->name + f->type->name().substr(3) + " (" + posString(f->decl->pos) + ")";
			}
			error(pos, msg);
		}
		if (ambiguous)
			error(pos, "ambiguous call to overloaded sub '" + name + "' with " + argTypes + " (use casts)");
		return best;
	}

	void CodeGen::check(Node* e, Type* expected)
	{
		const bool placeWanted = m_placeWanted;		// (only for e itself)
		m_placeWanted = false;
		e->isConst = false;
		e->constPtr = false;
		e->lvalue = false;
		e->readOnly = false;
		e->readOnlyWhy.clear();
		switch (e->kind)
		{
			case NodeKind::IntLit:
				// 'c': u8; 123u: u32; otherwise i32 (u32 if it doesn't fit).
				e->type = (e->name == "char") ? m_types.u8() : (e->name == "u" || e->value > 0x7FFFFFFF) ? m_types.u32() : m_types.i32();
				e->isConst = true;
				e->constValue = e->value;
			break;
			case NodeKind::FloatLit:
				e->type = m_types.f32();
				e->isConst = true;
				e->constValue = (uint32_t)e->value;
			break;
			case NodeKind::StrLit:
				e->type = m_types.pointer(m_types.u8(), true);		// (string: its text is read-only)
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
				if (e->name == "&" && e->a->kind == NodeKind::Ident)
					implicitMember(e->a, false);		// (&field: this->field's address; &static_sub)
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
			case NodeKind::Call:
				checkCall(e);
				if (e->kind == NodeKind::Call)
				{
					Type* fn = calleeType(e);
					if (e->handler != nullptr)
						checkHandler(e, expected);
					else if (fn != nullptr && fn->throws != nullptr)
						error(e->pos, (e->func != nullptr ? e->func->name : std::string("this sub")) + " can fail (it throws " + fn->throwsName +
							"): handle its errors: try (they are thrown on) or catch (f(...) catch value, f(...) catch (e) { ... })");
				}
			break;
			case NodeKind::Try:
			case NodeKind::Catch:
			{
				// The call itself, with the Try / Catch as its handler (so that it is still a call:
				// its result is moved, not copied).
				Node* call = e->a;
				const bool isTry = e->kind == NodeKind::Try;
				if (call == nullptr || call->kind != NodeKind::Call || call->handler != nullptr)
					error(e->pos, std::string(isTry ? "try" : "catch") + " is for a call of a sub that throws (" + (isTry ? "try f(...)" : "f(...) catch ...") + ")");
				Node* h = new Node(*e);
				h->a = nullptr;
				*e = *call;
				e->handler = h;
				check(e, expected);
			}
			break;
			case NodeKind::Switch:
				if (e->value != 2)
					error(e->pos, "expression expected");
				checkSwitchValue(e, expected);
			break;
			case NodeKind::Member: checkMember(e, placeWanted); break;
			case NodeKind::Ternary:
			{
				checkCondition(e->a);
				check(e->b, expected);
				check(e->c, expected);
				Type* tb = decay(e->b->type);
				Type* tc = decay(e->c->type);
				if (tb == nullptr || tc == nullptr)
					error(e->pos, "ambiguous overloaded sub in ?:");
				if (tb->isArithmetic() && tc->isArithmetic())
					e->type = (tb == tc ? tb : arithmeticType(tb, tc));
				else if (tb == tc && tb->isScalar())
					e->type = tb;
				else if (samePointee(tb, tc))
					e->type = m_types.pointer(tb->base, true);	// (one of them is const)
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
					Node* chosen = constTruth(e->a->constValue, decay(e->a->type)) ? e->b : e->c;
					e->constValue = constConvert(chosen->constValue, decay(chosen->type), e->type);
				}
			}
			break;
			case NodeKind::Index:
			{
				check(e->a);
				check(e->b);
				if (e->a->type != nullptr && e->a->type->kind == Type::Kind::Struct)
				{
					// a[i]: operator[]; its result a pointer: what it points at (a[i] = x works).
					Type* st = e->a->type;
					Node* call = new Node();
					call->kind = NodeKind::Index;
					call->pos = e->pos;
					if (!userOperator(call, "operator[]", { e->a, e->b }, true))
						error(e->pos, "no operator[] for " + st->name() + " (a sub operator[](...) in the struct)");
					if (call->type != nullptr && call->type->kind == Type::Kind::Pointer && call->type->base->kind != Type::Kind::Void)
					{
						const SourcePos pos = e->pos;
						*e = Node();
						e->kind = NodeKind::Unary;
						e->name = "*";
						e->pos = pos;
						e->a = call;
						check(e);
					}
					else
						*e = *call;
					break;
				}
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
				if (e->readOnly)
					e->readOnlyWhy = e->a->readOnlyWhy;
				if (ta->constTarget)
				{
					e->readOnly = true;
					e->readOnlyWhy = "it points at constants (" + ta->name() + "): they cannot be changed through it";
				}
			}
			break;
			case NodeKind::Cast:
			if (e->value == 1)
			{
				// bitcast<T>(value): the same bits as a T (the same size; structs and arrays too).
				Type* to = resolveType(e->typeSyntax);
				check(e->a);
				Type* from = e->a->type;
				if (from == nullptr)
					error(e->a->pos, "ambiguous overloaded sub in bitcast");
				if (from->kind == Type::Kind::Null)
					from = m_types.pointer(m_types.voidType());
				if (from->kind == Type::Kind::Struct)
					resolveStruct(from->structInfo, e->pos);
				if (to->kind == Type::Kind::Struct)
					resolveStruct(to->structInfo, e->pos);
				if (to->kind == Type::Kind::Void || from->kind == Type::Kind::Void)
					error(e->pos, "bitcast needs a value and a type with a size (not void)");
				if (to->kind == Type::Kind::Array)
					error(e->pos, "bitcast to an array: arrays are not values (bitcast to a struct holding it, or use a pointer)");
				if (to->kind == Type::Kind::Bool)
					error(e->pos, "bitcast to bool: its bits could be other than 0 or 1 (compare instead: value != 0)");
				if (from->size() != to->size())
					error(e->pos, "bitcast<" + to->name() + "> needs a value of the same size: " + from->name() + " is " + std::to_string(from->size()) +
						" byte" + (from->size() == 1 ? "" : "s") + ", " + to->name() + " is " + std::to_string(to->size()));
				e->type = to;
				const bool scalarFrom = from->isArithmetic() || from->isPointer() || from->isFunction();
				if ((e->a->isConst || e->a->constPtr) && scalarFrom && to->isScalar())
				{
					// A constant: its bits, as the other type (f32: its bits are its value).
					const uint64_t mask = to->size() >= 4 ? 0xFFFFFFFFull : ((1ull << (8 * to->size())) - 1);
					const uint64_t bits = (uint64_t)e->a->constValue & mask;
					e->constValue = to->isFloat() ? (int64_t)bits : wrapTo((int64_t)bits, to);
					if (to->isArithmetic())
						e->isConst = true;
					else
						e->constPtr = true;
				}
				break;
			}
			if (e->value == 3)
			{
				// dynamic_cast<T*>(p): p's object's type (from its table) decides: a pointer to its T
				// part, or null. p: a pointer to a class with a table, or to an interface.
				Type* to = resolveType(e->typeSyntax);
				check(e->a);
				Type* from = decay(e->a->type);
				if (from == nullptr)
					error(e->a->pos, "ambiguous overloaded sub in dynamic_cast");
				if (to->kind != Type::Kind::Pointer || to->base->kind != Type::Kind::Struct || to->base->structInfo->kind == 0)
					error(e->pos, "dynamic_cast<" + to->name() + ">: the type is a pointer to a class or an interface");
				e->type = to;
				if (from->kind == Type::Kind::Null)
					break;
				if (from->kind != Type::Kind::Pointer || from->base->kind != Type::Kind::Struct)
					error(e->pos, "dynamic_cast needs a pointer to a class or an interface (not " + from->name() + ")");
				if (from->constTarget && !to->constTarget)
					error(e->pos, "dynamic_cast<" + to->name() + ">: " + from->name() + " points at a constant (dynamic_cast<const " + to->base->name() + "*>(...))");
				StructInfo* fromInfo = from->base->structInfo;
				resolveStruct(fromInfo, e->pos);
				resolveStruct(to->base->structInfo, e->pos);
				if (conversionCost(e->a, to) < 0 && fromInfo->vptrs.empty())
					error(e->pos, "dynamic_cast: " + fromInfo->name + " has no table (no virtual subs, no interfaces): its objects' type is not known when the program runs");
				break;
			}
			if (e->value == 2)
			{
				// static_cast<T>(value): what converts to T anyway, numbers to numbers, and a class
				// pointer to a pointer to a class derived from it (or to the class implementing an
				// interface): the pointer moves to the whole object (unchecked: it must be one).
				Type* to = resolveType(e->typeSyntax);
				check(e->a, to->isFunction() ? to : nullptr);
				if (castConversion(e, to))
					break;
				Type* from = decay(e->a->type);
				if (from == nullptr)
					error(e->a->pos, "ambiguous overloaded sub in static_cast");
				e->type = to;
				int64_t delta = 0;
				if (conversionCost(e->a, to) >= 0)
					;
				else if (from->isArithmetic() && to->isArithmetic())
					;
				else if (pointerAdjust(from, to, delta))
				{
					if (from->constTarget && !to->constTarget)
						error(e->pos, "static_cast<" + to->name() + ">: " + from->name() + " points at a constant (static_cast<const " + to->base->name() + "*>(...))");
				}
				else
					error(e->pos, "static_cast cannot convert " + from->name() + " to " + to->name() + (from->isPointer() && to->isPointer() ?
						" (they are not classes derived from one another: cast<" + to->name() + ">(...) reads the same address as the other type)" : ""));
				if (e->a->isConst && to->isArithmetic())
				{
					e->isConst = true;
					e->constValue = constConvert(e->a->constValue, from, to);
				}
				break;
			}
			{
				Type* to = resolveType(e->typeSyntax);
				check(e->a, to->isFunction() ? to : nullptr);
				if (castConversion(e, to))
					break;		// (a struct's conversion, or a constructor: cast<String>("text"))
				Type* from = decay(e->a->type);
				if (from == nullptr)
					error(e->a->pos, "ambiguous overloaded sub in cast");
				int64_t delta = 0;
				if (pointerAdjust(from, to, delta))
					error(e->pos, "cast<" + to->name() + "> between pointers to classes derived from one another: static_cast<" + to->name() + ">(...) (it finds that part of the object)");
				bool ok = (to->kind == Type::Kind::Void) || (from->isScalar() && to->isScalar());
				// f32 <-> pointers: no meaning (to look at a float's bits: *cast<u32*>(&f)).
				if ((from->isFloat() && !to->isArithmetic() && to->kind != Type::Kind::Void) || (to->isFloat() && !from->isArithmetic()))
					ok = false;
				if (!ok)
					error(e->pos, "cannot cast " + from->name() + " to " + to->name());
				e->type = to;
				if (e->a->isConst && to->isArithmetic())
				{
					e->isConst = true;
					e->constValue = constConvert(e->a->constValue, from, to);
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
				if (e->a->kind == NodeKind::Ident && !e->a->templateArgs.empty() && lookupVar(e->a->name) == nullptr)
					resolveTemplateIdent(e->a);		// sizeof(Box<u32>)
				if (e->a->kind == NodeKind::Ident && lookupVar(e->a->name) == nullptr && isTypeName(e->a->name))
				{
					TypeSyntax ts;
					ts.name = e->a->name;
					ts.written = writtenName(e->a);
					ts.resolved = true;
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
				if (e->a->type != nullptr && e->a->type->kind == Type::Kind::Struct)
				{
					// ++a: operator++, a++: operator post++
					const bool inc = e->kind == NodeKind::PreInc || e->kind == NodeKind::PostInc;
					const bool post = e->kind == NodeKind::PostInc || e->kind == NodeKind::PostDec;
					const std::string op = std::string("operator") + (post ? " post" : "") + (inc ? "++" : "--");
					Type* st = e->a->type;
					if (!userOperator(e, op, { e->a }))
						error(e->pos, "no " + op + " for " + st->name() + (post ? " (a++ is operator post++; ++a is operator++)" : ""));
					break;
				}
				if (!e->a->lvalue || e->a->type->kind == Type::Kind::Array)
					error(e->pos, "++ and -- need a variable");
				if (e->a->readOnly)
					error(e->pos, e->a->readOnlyWhy.empty() ? "cannot change a constant array" : e->a->readOnlyWhy);
				if (!e->a->type->isArithmetic() && e->a->type->kind != Type::Kind::Pointer)
					error(e->pos, "++ and -- work on numbers and pointers");
				e->type = e->a->type;
			}
			break;
			case NodeKind::Materialize:
			{
				// (a value for a const ref parameter: in a temporary)
				check(e->a);
				Type* t = e->a->type;
				e->type = m_types.pointer(t, true);
			}
			break;
			case NodeKind::InPlace:
			{
				check(e->a);
				Type* target = decay(e->a->type)->base;
				for (Node* arg : e->list)
					check(arg, target);
				if (e->value == 0 && e->list.size() == 1 && target->kind != Type::Kind::Struct)
					requireConvertible(e->list[0], target, "in p->new(value)");
				e->type = m_types.voidType();
			}
			break;
			case NodeKind::Construct:
			if (e->value == 2)
			{
				// take(p)
				check(e->a);
				e->type = decay(e->a->type)->base;
				break;
			}
			if (e->a == nullptr)
			{
				// T.new(args): its constructor for the arguments (none, and no constructor: its
				// defaults), on a temporary.
				StructInfo* info = e->methodClass;
				resolveStruct(info, e->pos);
				Type* t = m_types.structType(info);
				requireValueType(t, e->pos, "built");
				for (Node* arg : e->list)
					check(arg);
				e->func = nullptr;
				if (m_funcs.count(info->name + "::new") > 0)
				{
					Node self;
					self.kind = NodeKind::TypeArg;
					self.type = m_types.pointer(t);
					std::vector<Node*> args { &self };
					args.insert(args.end(), e->list.begin(), e->list.end());
					FuncInfo* f = resolveOverload(info->name + "::new", args, e->pos);
					for (size_t i = 0; i < e->list.size(); i++)
					{
						check(e->list[i], f->type->params[i + 1]);
						requireConvertible(e->list[i], f->type->params[i + 1], "for parameter '" + f->paramNames[i + 1] + "' of " + f->name);
					}
					markUsed(f);
					checkSubAccess(f, e->pos);
					e->func = f;
				}
				else if (!e->list.empty())
					error(e->pos, info->name + " has no constructor (sub new(...)): " + info->name + ".new() only gives its default values");
				e->type = t;
				break;
			}
			{
				// (an object built from a value: its constructor, on a temporary)
				check(e->a);
				Type* p = e->func->type->params[1];
				requireConvertible(e->a, p, "for " + e->func->name);
				markUsed(e->func);
				checkSubAccess(e->func, e->pos);
				e->type = m_types.structType(e->methodClass);
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
		if (implicitMember(e, false))
		{
			checkMember(e, false);			// (this->name)
			return;
		}
		resolveTemplateIdent(e);
		if (VarInfo* v = lookupVar(e->name))
		{
			if (v->type != nullptr && v->type->isReference() && (e->callFlags & 16) == 0)
			{
				// A reference parameter: what it refers to (*the pointer).
				Node* raw = new Node(*e);
				raw->callFlags |= 16;
				const SourcePos pos = e->pos;
				*e = Node();
				e->kind = NodeKind::Unary;
				e->name = "*";
				e->pos = pos;
				e->a = raw;
				checkUnary(e);
				return;
			}
			if (m_options.index != nullptr && !m_discarding)
				indexName(e->pos, (e->text.empty() ? e->name : e->text), v->defPos, hoverVar(v));
			e->var = v;
			e->type = v->type;
			e->lvalue = true;
			e->readOnly = v->readOnly;
			if (v->isConst && e->name != " ctor")
			{
				e->readOnly = true;
				e->readOnlyWhy = "'" + (e->text.empty() ? shownName(e->name) : e->text) + "' is a constant (const): it cannot change";
			}
			// A static field: priv (only the struct's subs use it), readonly (only they change it).
			if (v->storage == VarInfo::Storage::Global && v->decl != nullptr && v->decl->owner != nullptr && v->decl->owner->kind == NodeKind::StructDecl
				&& m_structs.count(v->decl->owner->name) > 0 && e->name != " ctor")		// (its own constructor call: allowed)
			{
				const Node* owner = v->decl->owner;
				StructInfo* info = m_structs[owner->name];
				if (v->decl->isPrivate && !insideStruct(info))
					error(e->pos, "'" + v->decl->shortName + "' is private to " + owner->name + " (priv): only the subs of " + owner->name + " can use it");
				if (v->decl->isProtected && !insideDerived(info))
					error(e->pos, "'" + v->decl->shortName + "' is protected (prot): only the subs of " + owner->name + " and of the classes derived from it can use it");
				if (v->decl->isReadonly && !insideStruct(info))
				{
					e->readOnly = true;
					e->readOnlyWhy = "'" + v->decl->shortName + "' is readonly: only the subs of " + owner->name + " can change it";
				}
			}
			if (v->ctValue)
			{
				// A struct value computed when compiling: like a call's result (its address: a copy).
				e->lvalue = false;
				e->readOnly = false;
				e->readOnlyWhy.clear();
			}
			return;
		}
		if (m_consts.count(e->name) > 0)
		{
			const Node* cd = m_consts[e->name].decl;
			if (cd->isPrivate && cd->owner != nullptr && m_structs.count(cd->owner->name) > 0 && !insideStruct(m_structs[cd->owner->name]))
				error(e->pos, "'" + cd->shortName + "' is private to " + cd->owner->name + " (priv): only the subs of " + cd->owner->name + " can use it");
			if (cd->isProtected && cd->owner != nullptr && m_structs.count(cd->owner->name) > 0 && !insideDerived(m_structs[cd->owner->name]))
				error(e->pos, "'" + cd->shortName + "' is protected (prot): only the subs of " + cd->owner->name + " and of the classes derived from it can use it");
			decideConst(e->name);
			if (m_consts[e->name].runtime)
			{
				// (A global one is a variable.)
				if (lookupVar(e->name) != nullptr)
				{
					checkIdent(e, expected);
					return;
				}
				if (m_globals.count(e->name) > 0 && m_globals[e->name]->type == nullptr)
					error(e->pos, "'" + shownName(e->name) + "' is used in its own declaration");
				error(e->pos, "'" + (e->text.empty() ? shownName(e->name) : e->text) + "' is computed when its declaration runs " + ownerOf(m_consts[e->name].decl) +
					": it can only be used after it, in that sub (not in its lambdas, nor where a compile-time constant is needed)");
			}
			e->constValue = resolveConst(e->name, e->pos);
			if (m_options.index != nullptr && e->name.rfind("__tv_", 0) == 0)
				indexTemplateParam(e->pos, "");		// (a template's value parameter)
			else if (m_options.index != nullptr)
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
			{
				// A struct constant as a value: its read-only copy in the program.
				if (m_inDeclarations)
					error(e->pos, "'" + e->name + "' is a struct constant: here only its fields can be used (" + e->name + ".field)");
				VarInfo* v = constStorage(e->name);
				e->var = v;
				e->type = v->type;
				e->lvalue = true;
				e->readOnly = true;
				e->readOnlyWhy = "'" + (e->text.empty() ? shownName(e->name) : e->text) + "' is a constant: it cannot change";
				return;
			}
			e->type = m_consts[e->name].type;
			e->isConst = true;
			return;
		}
		if (m_funcs.count(e->name) > 0)
		{
			const auto& funcs = m_funcs[e->name];
			if (std::all_of(funcs.begin(), funcs.end(), [](FuncInfo* f) { return f->decl->constKind == 3; }))
				error(e->pos, "'" + (e->text.empty() ? e->name : e->text) + "' is consteval: it only exists when compiling (call it; it has no address)");
			if (funcs.size() == 1)
				e->func = funcs[0];
			else if (expected && expected->isFunction())
				e->func = functionForType(funcs, expected);
			markUsed(e->func);
			e->type = e->func ? e->func->type : nullptr;
			if (e->func)
				checkSubAccess(e->func, e->pos);
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
		if (FuncInfo* cur = m_inline.empty() ? m_func : m_inline.back().func)
		{
			// In a class's sub: a constant / static field of a class it extends (a template's base,
			// known once it is instantiated).
			StructInfo* cls = structOfMethod(cur);
			if (cls != nullptr && cls->isClass() && cls->base != nullptr && e->name.find("::") == std::string::npos)
			{
				const std::string full = memberName(cls->base, e->name);
				if (!full.empty())
				{
					if (e->text.empty())
						e->text = e->name;
					e->name = full;
					checkIdent(e, expected);
					return;
				}
			}
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
			m_placeWanted = true;		// (a field of a struct constant: in its read-only copy, not its value)
			check(e->a);
			// A struct returned by a call is in the caller's frame (its result temporary): a method
			// may be called on it (a.copy().add(1)), its address taken.
			const bool structResult = (e->a->kind == NodeKind::Call && e->a->type != nullptr && e->a->type->kind == Type::Kind::Struct)
				|| (e->a->kind == NodeKind::Ident && e->a->var != nullptr && e->a->var->ctValue) || e->a->kind == NodeKind::Construct;
			if (!e->a->lvalue && !structResult)
				error(e->pos, "& needs a variable (or something stored in memory)");
			// Something that cannot change (a constant, a const field, through a const pointer):
			// a const pointer to it. (Constant arrays: as before, their elements' pointers.)
			e->type = m_types.pointer(e->a->type, e->a->readOnly && !e->a->readOnlyWhy.empty());
			return;
		}
		check(e->a);
		Type* t = decay(e->a->type);
		if (t == nullptr)
			error(e->a->pos, "ambiguous overloaded sub");
		if (t->kind == Type::Kind::Struct)
		{
			// (*x on a struct: its operator*, e.g. -> ref T)
			if (userOperator(e, "operator" + op, { e->a }))
				return;
			UserConv u;
			if (op != "!" || !userConversion(e->a, m_types.boolType(), u, false, true))
				error(e->pos, "no operator" + op + " for " + t->name() + " (a sub operator" + op + "() in the struct, or a top-level one)");
			requireConvertible(e->a, m_types.boolType(), "for !");		// (!x: its conversion to bool)
			t = decay(e->a->type);
		}
		if (op == "*")
		{
			if (t->kind == Type::Kind::Null || t->kind != Type::Kind::Pointer)
				error(e->pos, "* needs a pointer (found " + t->name() + ")");
			if (t->base->kind == Type::Kind::Void)
				error(e->pos, "cannot dereference a void* (cast it first)");
			e->type = t->base;
			e->lvalue = true;
			if (t->constTarget)
			{
				e->readOnly = true;
				e->readOnlyWhy = "it points at a constant (" + t->name() + "): it cannot be changed through it";
			}
			return;
		}
		if (op == "!")
		{
			if (!t->isScalar())
				error(e->pos, "! needs a number or a pointer");
			e->type = m_types.boolType();
			if (e->a->isConst)
			{
				e->isConst = true;
				e->constValue = !constTruth(e->a->constValue, t);
			}
			return;
		}
		if (t->isFloat() && op == "-")
		{
			e->type = t;
			if (e->a->isConst)
			{
				e->isConst = true;
				e->constValue = (uint32_t)e->a->constValue ^ 0x80000000u;	// the sign bit
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
		if ((ta->kind == Type::Kind::Struct || tb->kind == Type::Kind::Struct) && op != "&&" && op != "||")
			return binaryOperator(e);		// (a struct's operator)
		if (op == "&&" || op == "||")
		{
			// (a struct operand: its conversion to bool)
			if (ta->kind == Type::Kind::Struct)
				requireConvertible(e->a, m_types.boolType(), "for " + op);
			if (tb->kind == Type::Kind::Struct)
				requireConvertible(e->b, m_types.boolType(), "for " + op);
			ta = decay(e->a->type);
			tb = decay(e->b->type);
		}
		if (op == "&&" || op == "||")
		{
			if (!ta->isScalar() || !tb->isScalar())
				error(e->pos, op + " needs numbers or pointers");
			e->type = m_types.boolType();
			if (e->a->isConst && e->b->isConst)
			{
				e->isConst = true;
				const bool x = constTruth(e->a->constValue, ta), y = constTruth(e->b->constValue, tb);
				e->constValue = (op == "&&") ? (x && y) : (x || y);
			}
			return;
		}
		if (isComparison(op))
		{
			// Pointers to classes derived from one another: the derived one as the other (where
			// that part of its object is).
			if (ta->kind == Type::Kind::Pointer && tb->kind == Type::Kind::Pointer && ta->base != tb->base && ta->base->kind == Type::Kind::Struct && tb->base->kind == Type::Kind::Struct)
			{
				const bool aDerived = derives(ta->base->structInfo, tb->base->structInfo);
				if (aDerived || derives(tb->base->structInfo, ta->base->structInfo))
				{
					Node*& side = aDerived ? e->a : e->b;
					TypeSyntax* ts = new TypeSyntax();
					ts->pos = side->pos;
					ts->fixed = m_types.pointer((aDerived ? tb : ta)->base, ta->constTarget || tb->constTarget);
					ts->name = ts->fixed->name();
					Node* conv = new Node();
					conv->kind = NodeKind::Cast;
					conv->value = 2;
					conv->pos = side->pos;
					conv->typeSyntax = ts;
					conv->a = side;
					side = conv;
					check(side);
					ta = decay(e->a->type);
					tb = decay(e->b->type);
				}
			}
			bool ok = (ta->isArithmetic() && tb->isArithmetic());
			if (ta->isPointer() && tb->isPointer())
				ok = (ta == tb || samePointee(ta, tb) || ta->kind == Type::Kind::Null || tb->kind == Type::Kind::Null || ta->isVoidPointer() || tb->isVoidPointer());
			if ((ta->isFunction() || tb->isFunction()) && (op == "==" || op == "!="))
				ok = (ta == tb || ta->kind == Type::Kind::Null || tb->kind == Type::Kind::Null);
			if ((ta->isPointer() && tb->isInteger() && e->b->isConst && e->b->constValue == 0) || (tb->isPointer() && ta->isInteger() && e->a->isConst && e->a->constValue == 0))
				ok = true;
			if (!ok)
				error(e->pos, "cannot compare " + ta->name() + " and " + tb->name());
			e->type = m_types.boolType();
			if (e->a->isConst && e->b->isConst && (ta->isFloat() || tb->isFloat()))
			{
				const float x = f32Of(constConvert(e->a->constValue, ta, m_types.f32()));
				const float y = f32Of(constConvert(e->b->constValue, tb, m_types.f32()));
				bool r = false;
				if (op == "==") r = x == y; else if (op == "!=") r = x != y; else if (op == "<") r = x < y;
				else if (op == "<=") r = x <= y; else if (op == ">") r = x > y; else r = x >= y;
				e->isConst = true;
				e->constValue = r;
			}
			else if (e->a->isConst && e->b->isConst && ta->isInteger() && tb->isInteger())
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
			if (ta != tb && !samePointee(ta, tb))
				error(e->pos, "cannot subtract " + tb->name() + " from " + ta->name());
			e->type = m_types.i32();
			return;
		}
		if ((ta->isFloat() || tb->isFloat()) && ta->isArithmetic() && tb->isArithmetic())
		{
			if (op != "+" && op != "-" && op != "*" && op != "/")
				error(e->pos, "'" + op + "' needs integers (found " + ta->name() + " and " + tb->name() + ")");
			e->type = m_types.f32();
			if (e->a->isConst && e->b->isConst)
			{
				const float x = f32Of(constConvert(e->a->constValue, ta, e->type));
				const float y = f32Of(constConvert(e->b->constValue, tb, e->type));
				float r = 0.0f;
				if (op == "+") r = x + y; else if (op == "-") r = x - y; else if (op == "*") r = x * y; else r = x / y;
				e->isConst = true;
				e->constValue = bitsOf(r);
			}
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
		if (t != nullptr && t->kind == Type::Kind::Struct && e->name != "=")
		{
			// a += b: operator+= (not derived from + and =)
			check(e->b);
			const std::string op = "operator" + e->name;
			if (!userOperator(e, op, { e->a, e->b }))
				error(e->pos, "no " + op + " for " + t->name() + " and " + (e->b->type ? e->b->type->name() : std::string("?")) + " (a sub " + op + "(...) in the struct, or a top-level one)");
			return;
		}
		if (!e->a->lvalue && e->a->kind == NodeKind::Ident && m_consts.count(e->a->name) > 0)
			error(e->a->pos, "'" + (e->a->text.empty() ? shownName(e->a->name) : e->a->text) + "' is a constant: it cannot change");
		if (!e->a->lvalue)
			error(e->a->pos, "cannot assign to this (not a variable)");
		if (e->a->readOnly)
			error(e->a->pos, e->a->readOnlyWhy.empty() ? "cannot assign to a constant array" : e->a->readOnlyWhy);
		if (t->kind == Type::Kind::Array)
			error(e->pos, "cannot assign to an array (assign its elements)");
		std::string constField;
		if (t->kind == Type::Kind::Struct && hasConstFields(t, &constField))
			error(e->pos, "cannot assign a whole " + t->name() + ": it has a const field (" + constField + "), set only when a struct is built "
				"(initialize a new variable instead)");
		if (e->name == "=" && t->kind == Type::Kind::Struct && m_funcs.count(t->structInfo->name + "::operator=") > 0)
		{
			// a = b: the struct's operator= (for what it takes; b of the same type otherwise: copied)
			check(e->b, t);
			if (userOperator(e, "operator=", { e->a, e->b }, true))
				return;
		}
		if (e->name == "=")
		{
			check(e->b, t);
			requireConvertible(e->b, t, "in the assignment");
			e->type = (t->kind == Type::Kind::Struct) ? m_types.voidType() : t;
			return;
		}
		check(e->b);
		Type* tb = decay(e->b->type);
		if (t->isFloat())
		{
			if (e->name != "+=" && e->name != "-=" && e->name != "*=" && e->name != "/=")
				error(e->pos, "'" + e->name + "' needs an integer variable");
			if (tb == nullptr || !tb->isArithmetic())
				error(e->b->pos, "'" + e->name + "' needs a number");
			e->type = t;
			return;
		}
		if (tb != nullptr && tb->isFloat() && t->isInteger())
			error(e->b->pos, "'" + e->name + "' with an f32 value needs an f32 variable (cast the value: cast<" + t->name() + ">(...))");
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

	// ===== priv =====
	// Is the code being checked part of the struct: one of its subs (methods, static subs), a
	// lambda written in one, or an inline sub of it being expanded?
	bool CodeGen::insideStruct(const StructInfo* info)
	{
		// (a field's type, while the struct is resolved)
		for (const Node* o = m_fieldOwner; o != nullptr; o = o->owner)
		{
			if (o == info->decl)
				return true;
		}
		FuncInfo* f = m_inline.empty() ? m_func : m_inline.back().func;
		for (const Node* d = f ? f->decl : nullptr; d != nullptr; d = d->d)
		{
			// (the subs of a nested struct are inside the structs around it too)
			if ((d->value & 4) == 0)
				continue;
			for (const Node* o = d->owner; o != nullptr; o = o->owner)
			{
				if (o == info->decl)
					return true;
			}
		}
		// Its friends (friend A, f): everything, as its own subs.
		resolveFriends(const_cast<StructInfo*>(info));
		if (info->friendClasses.empty() && info->friendSubs.empty())
			return false;
		for (const Node* d = f ? f->decl : nullptr; d != nullptr; d = d->d)
		{
			if (std::find(info->friendSubs.begin(), info->friendSubs.end(), d->name) != info->friendSubs.end())
				return true;
			if ((d->value & 4) == 0)
				continue;
			for (const Node* o = d->owner; o != nullptr; o = o->owner)
			{
				if (std::find(info->friendClasses.begin(), info->friendClasses.end(), o->name) != info->friendClasses.end())
					return true;
			}
		}
		return false;
	}

	bool CodeGen::insideDerived(const StructInfo* info)
	{
		if (insideStruct(info))
			return true;
		FuncInfo* f = m_inline.empty() ? m_func : m_inline.back().func;
		for (const Node* d = f ? f->decl : nullptr; d != nullptr; d = d->d)
		{
			if ((d->value & 4) == 0)
				continue;
			for (const Node* o = d->owner; o != nullptr; o = o->owner)
			{
				auto it = m_structs.find(o->name);
				if (it != m_structs.end() && derives(it->second, info))
					return true;
			}
		}
		return false;
	}

	void CodeGen::checkFieldAccess(const StructInfo* info, const StructField* f, const SourcePos& pos)
	{
		const StructInfo* owner = f->owner != nullptr ? f->owner : info;
		if (f->isPrivate && !insideStruct(owner))
			error(pos, "'" + f->name + "' is private to " + owner->name + " (priv): only the subs of " + owner->name + " can use it");
		if (f->isProtected && !insideDerived(owner))
			error(pos, "'" + f->name + "' is protected (prot): only the subs of " + owner->name + " and of the classes derived from it can use it");
	}

	void CodeGen::checkSubAccess(FuncInfo* f, const SourcePos& pos)
	{
		if ((f->decl->value & (16 | 8192)) == 0)
			return;
		const Node* owner = f->decl->owner;
		auto it = owner ? m_structs.find(owner->name) : m_structs.end();
		if (it == m_structs.end())
			return;
		if ((f->decl->value & 16) != 0 && !insideStruct(it->second))
			error(pos, "'" + f->decl->shortName + "' is private to " + owner->name + " (priv): only the subs of " + owner->name + " can use it");
		if ((f->decl->value & 8192) != 0 && !insideDerived(it->second))
			error(pos, "'" + f->decl->shortName + "' is protected (prot): only the subs of " + owner->name + " and of the classes derived from it can use it");
	}

	// ======================================== Classes ========================================
	// A class: [vtable pointer (when it adds virtual subs to a base without them)] [the base's
	// fields (and its vtable pointers)] [a vtable pointer per interface it adds] [its fields].
	// An interface: a vtable pointer (its subs' table).
	uint32_t CodeGen::classLayout(StructInfo* info)
	{
		resolveFriends(info);
		if (info->kind == 0)
			return 0;
		Node* d = info->decl;
		auto l_resolve = [&](TypeSyntax* ts) -> StructInfo* {
			Type* t = resolveType(ts);
			if (t->kind != Type::Kind::Struct)
				error(ts->pos, t->name() + " is not a class or an interface");
			if (t->structInfo->resolving)
				error(ts->pos, (info->isInterface() ? "interface " : "class ") + info->name + " extends itself (through " + t->structInfo->name + ")");
			resolveStruct(t->structInfo, ts->pos);
			return t->structInfo;
		};
		auto l_addSlots = [&](StructInfo* iface, uint32_t offset) {
			for (const auto& slot : iface->ifaceSlots)
			{
				const bool known = std::any_of(info->ifaceSlots.begin(), info->ifaceSlots.end(), [&](const StructInfo::IfaceSlot& s) { return s.iface == slot.iface; });
				if (!known)
					info->ifaceSlots.push_back({ slot.iface, offset });
			}
		};
		if (info->kind == 2)
		{
			info->dynamic = true;
			info->vptrs.push_back({ 0, info });
			info->ifaceSlots.push_back({ info, 0 });
			if (d->typeSyntax != nullptr)
			{
				StructInfo* parent = l_resolve(d->typeSyntax);
				if (!parent->isInterface())
					error(d->typeSyntax->pos, "an interface extends an interface (" + parent->name + " is a " + (parent->isClass() ? "class" : "struct") + ")");
				info->interfaces.push_back(parent);
				l_addSlots(parent, 0);
			}
			return 4;
		}
		// Virtual subs of its own (then it has a table)?
		bool ownVirtual = false;
		for (FuncInfo* f : m_funcList)
		{
			if (f->decl->owner == d && (f->decl->value & 4) != 0 && isVirtualSub(f))
				ownVirtual = true;
		}
		StructInfo* base = nullptr;
		if (d->typeSyntax != nullptr)
		{
			base = l_resolve(d->typeSyntax);
			if (base->isInterface())
				error(d->typeSyntax->pos, base->name + " is an interface: a class implements it with impl (class " + d->shortName + " impl " + base->name + ")");
			if (base->isFinal)
				error(d->typeSyntax->pos, base->name + " is a final class: no class extends it");
			if (base->anonymous)
				error(d->typeSyntax->pos, "a class extends a named class or struct");
		}
		info->base = base;
		info->dynamic = ownVirtual || (base != nullptr && base->dynamic);
		uint32_t offset = 0;
		if (info->dynamic && !(base != nullptr && base->dynamic))
		{
			info->vptrs.push_back({ 0, info });
			offset = 4;
		}
		if (base != nullptr)
		{
			info->baseOffset = offset;
			for (const StructField& bf : base->fields)
			{
				StructField f = bf;
				f.offset += offset;
				if (f.owner == nullptr)
					f.owner = base;
				info->fields.push_back(f);
			}
			for (const auto& v : base->vptrs)
				info->vptrs.push_back({ v.offset + offset, v.view->isInterface() ? v.view : info });
			for (const auto& slot : base->ifaceSlots)
				info->ifaceSlots.push_back({ slot.iface, slot.offset + offset });
			offset += base->size;
		}
		for (TypeSyntax* ts : d->impls)
		{
			StructInfo* iface = l_resolve(ts);
			if (!iface->isInterface())
				error(ts->pos, iface->name + " is not an interface (impl takes interfaces; a class extends a class with ex)");
			if (std::find(info->interfaces.begin(), info->interfaces.end(), iface) != info->interfaces.end())
				error(ts->pos, iface->name + " twice");
			info->interfaces.push_back(iface);
			if (std::any_of(info->ifaceSlots.begin(), info->ifaceSlots.end(), [&](const StructInfo::IfaceSlot& s) { return s.iface == iface; }))
				continue;		// (its base implements it already: that table)
			info->vptrs.push_back({ offset, iface });
			l_addSlots(iface, offset);
			offset += 4;
		}
		return offset;
	}

	void CodeGen::resolveFriends(StructInfo* info)
	{
		if (info->friendsResolved)
			return;
		info->friendsResolved = true;
		for (Node* f : info->decl->friends)
		{
			if (m_structs.count(f->name) > 0)
				info->friendClasses.push_back(f->name);
			else if (m_funcs.count(f->name) > 0)
				info->friendSubs.push_back(f->name);
			else
				error(f->pos, "friend " + (f->text.empty() ? f->name : f->text) + ": no struct, class or sub of that name");
		}
	}

	void CodeGen::implicitConstructor(StructInfo* info)
	{
		if (info->kind != 1 || m_funcs.count(info->name + "::new") > 0)
			return;
		bool needed = !info->vptrs.empty();
		if (info->base != nullptr && m_funcs.count(info->base->name + "::new") > 0)
		{
			// (the base's constructor without arguments: what this one calls; none: no constructor,
			// the class's own must call one of the base's)
			bool baseDefault = false;
			for (FuncInfo* b : m_funcs[info->base->name + "::new"])
			{
				size_t required = 0;
				for (Node* p : b->decl->list)
				{
					if (p->name != "this" && p->a == nullptr)
						required++;
				}
				if (required == 0 && b->decl->variadic == 0)
					baseDefault = true;
			}
			needed = baseDefault;
		}
		if (!needed)
			return;
		Node* cls = info->decl;
		Node* d = new Node();
		d->kind = NodeKind::FuncDecl;
		d->pos = cls->namePos;
		d->namePos = cls->namePos;
		d->name = info->name + "::new";
		d->shortName = "new";
		d->scope = info->name;
		d->owner = cls;
		d->value = 4;
		d->external = cls->external;
		Node* self = new Node();
		self->kind = NodeKind::VarDecl;
		self->pos = cls->namePos;
		self->namePos = cls->namePos;
		self->name = "this";
		TypeSyntax* named = new TypeSyntax();
		named->pos = cls->namePos;
		named->name = info->name;
		named->written = cls->shortName;
		named->resolved = true;
		self->typeSyntax = new TypeSyntax();
		self->typeSyntax->kind = TypeSyntax::Kind::Pointer;
		self->typeSyntax->pos = cls->namePos;
		self->typeSyntax->base = named;
		d->list.push_back(self);
		if (cls->external == 0)
		{
			d->a = new Node();
			d->a->kind = NodeKind::Block;
			d->a->pos = cls->namePos;
			d->a->endPos = cls->namePos;
		}
		FuncInfo* f = new FuncInfo();
		f->name = d->name;
		f->decl = d;
		f->external = cls->external != 0;
		f->defined = true;
		f->instance = m_instanceOrigin.count(info->name) > 0;
		if (m_names.count(d->name) == 0)
			m_names[d->name] = "a sub";
		m_funcs[d->name].push_back(f);
		m_funcList.push_back(f);
		if (!m_inDeclarations || f->instance)
		{
			resolveFunction(f);
			f->label = codeLabel(f->name, true);
		}
	}

	bool CodeGen::derives(const StructInfo* from, const StructInfo* to, uint32_t* offset, int* depth)
	{
		if (from == nullptr || to == nullptr)
			return false;
		if (from == to)
		{
			if (offset) *offset = 0;
			if (depth) *depth = 0;
			return true;
		}
		if (to->kind == 2)
		{
			for (const auto& slot : from->ifaceSlots)
			{
				if (slot.iface == to)
				{
					if (offset) *offset = slot.offset;
					if (depth) *depth = 1;
					return true;
				}
			}
			return false;
		}
		uint32_t o = 0;
		int steps = 0;
		for (const StructInfo* c = from; c != nullptr; c = c->base)
		{
			if (c == to)
			{
				if (offset) *offset = o;
				if (depth) *depth = steps;
				return true;
			}
			o += c->baseOffset;
			steps++;
		}
		return false;
	}

	bool CodeGen::pointerAdjust(Type* from, Type* to, int64_t& delta)
	{
		delta = 0;
		if (from == nullptr || to == nullptr || from->kind != Type::Kind::Pointer || to->kind != Type::Kind::Pointer)
			return false;
		if (from->base->kind != Type::Kind::Struct || to->base->kind != Type::Kind::Struct || from->base == to->base)
			return false;
		uint32_t offset = 0;
		if (derives(from->base->structInfo, to->base->structInfo, &offset))
		{
			delta = offset;
			return true;
		}
		if (derives(to->base->structInfo, from->base->structInfo, &offset))
		{
			delta = -(int64_t)offset;
			return true;
		}
		return false;
	}

	StructInfo* CodeGen::structOfMethod(const FuncInfo* f)
	{
		if (f == nullptr || f->decl->owner == nullptr || (f->decl->value & 4) == 0)
			return nullptr;
		auto it = m_structs.find(f->decl->owner->name);
		return it == m_structs.end() ? nullptr : it->second;
	}

	// The same name and parameters (this not counted), static or not.
	bool CodeGen::sameSignature(const FuncInfo* a, const FuncInfo* b)
	{
		if (a->decl->shortName != b->decl->shortName || a->type == nullptr || b->type == nullptr || a->type->variadic != b->type->variadic)
			return false;
		const size_t skipA = (a->decl->value & 8) != 0 ? 0 : 1, skipB = (b->decl->value & 8) != 0 ? 0 : 1;
		const auto& pa = a->type->params;
		const auto& pb = b->type->params;
		if (pa.size() < skipA || pb.size() < skipB || pa.size() - skipA != pb.size() - skipB)
			return false;
		if (!std::equal(pa.begin() + (std::ptrdiff_t)skipA, pa.end(), pb.begin() + (std::ptrdiff_t)skipB))
			return false;
		if ((a->type->owned >> skipA) != (b->type->owned >> skipB))
		{
			// (the same parameters, but not the same own ones: callers would destroy a value twice, or never)
			const FuncInfo* later = a->decl->pos.line > b->decl->pos.line || a->decl->pos.file != b->decl->pos.file ? a : b;
			if ((a->decl->value & 8) == 0 && (b->decl->value & 8) == 0)
				error(later->decl->namePos, "'" + a->decl->shortName + "': the same parameters as " + (later == a ? b : a)->name + ", but not the same own ones (own is part of a sub's type: they must match)");
			return false;
		}
		if (a->type->throwsName != b->type->throwsName)
		{
			// (the callers of one would not handle the other's errors)
			const FuncInfo* later = a->decl->pos.line > b->decl->pos.line || a->decl->pos.file != b->decl->pos.file ? a : b;
			const FuncInfo* other = later == a ? b : a;
			auto l_throws = [](const FuncInfo* f) { return f->type->throws != nullptr ? "throws " + f->type->throwsName : std::string("throws nothing"); };
			error(later->decl->namePos, "'" + a->decl->shortName + "': the same parameters as " + other->name + ", but it " + l_throws(later) + " and " + other->name + " " +
				l_throws(other) + " (throws is part of a sub's type: they must match)");
		}
		return true;
	}

	std::vector<FuncInfo*> CodeGen::methodsOf(StructInfo* info, const std::string& name)
	{
		std::vector<FuncInfo*> out;
		auto l_add = [&](StructInfo* s) {
			auto it = m_funcs.find(s->name + "::" + name);
			if (it == m_funcs.end())
				return;
			const size_t before = out.size();
			for (FuncInfo* f : it->second)
			{
				// (those of a class it extends that one closer overrides: not candidates)
				bool hidden = false;
				for (size_t i = 0; i < before; i++)
					hidden = hidden || sameSignature(f, out[i]);
				if (!hidden && std::find(out.begin(), out.end(), f) == out.end())
					out.push_back(f);
			}
		};
		l_add(info);
		if (name == "new" || name == "del" || info->kind == 0)
			return out;		// (constructors and destructors are not inherited)
		for (StructInfo* b = info->base; b != nullptr; b = b->base)
			l_add(b);
		for (const auto& slot : info->ifaceSlots)
			l_add(slot.iface);
		return out;
	}

	StructInfo* CodeGen::qualifierStruct(const std::string& name, std::string* member)
	{
		// (the last '::' outside a template's arguments: Ns::Array<Ns::Point>::get)
		int angles = 0;
		for (size_t i = name.size(); i-- > 1; )
		{
			if (name[i] == '>') angles++;
			else if (name[i] == '<') angles--;
			else if (angles == 0 && name[i] == ':' && name[i - 1] == ':')
			{
				auto it = m_structs.find(name.substr(0, i - 1));
				if (it == m_structs.end())
					return nullptr;
				if (member)
					*member = name.substr(i + 1);
				return it->second;
			}
		}
		return nullptr;
	}

	std::string CodeGen::memberName(StructInfo* info, const std::string& name)
	{
		for (StructInfo* c = info; c != nullptr; c = c->base)
		{
			const std::string full = c->name + "::" + name;
			if (m_consts.count(full) > 0 || (m_globals.count(full) > 0 && m_globals[full]->decl != nullptr && m_globals[full]->decl->owner != nullptr))
				return full;
		}
		return "";
	}

	// The table of a class: its base's slots (a class's sub that overrides one takes its place),
	// then a slot per virtual sub it adds. An interface: its parent's, then its subs. Checks what
	// a class may redefine (only virtual subs, with override), and what an abstract class lacks.
	void CodeGen::buildVtables(StructInfo* info)
	{
		if (info->vtablesBuilt || info->kind == 0)
			return;
		if (info->vtablesBuilding)
			error(info->pos, "internal error: the table of " + info->name + " needs itself");
		resolveStruct(info, info->pos);
		info->vtablesBuilding = true;
		struct Done { StructInfo* i; ~Done() { i->vtablesBuilding = false; } } done { info };
		if (info->base != nullptr)
			buildVtables(info->base);
		for (StructInfo* i : info->interfaces)
			buildVtables(i);
		std::vector<FuncInfo*> own;
		for (FuncInfo* f : m_funcList)
		{
			if (f->decl->owner == info->decl && (f->decl->value & 4) != 0 && f->decl->name.rfind(info->name + "::", 0) == 0)
			{
				if (f->type == nullptr)
					resolveFunction(f);
				own.push_back(f);
			}
		}
		if (info->isInterface())
		{
			if (!info->interfaces.empty())
				info->vslots = info->interfaces[0]->vslots;
			for (FuncInfo* f : own)
			{
				for (const auto& slot : info->vslots)
				{
					if (sameSignature(slot.decl, f))
						error(f->decl->namePos, "'" + f->decl->shortName + "' is a sub of " + structOfMethod(slot.decl)->name + " already (" + info->name + " extends it)");
				}
				if ((f->decl->value & 1024) != 0)
					info->vslots.push_back({ f, nullptr });		// (final subs: helpers, not in the table)
			}
			info->vtablesBuilt = true;
			return;
		}
		if (info->base != nullptr && info->base->dynamic)
			info->vslots = info->base->vslots;
		for (FuncInfo* m : own)
		{
			Node* d = m->decl;
			const std::string& n = d->shortName;
			if (n == "new" || n == "del")
				continue;		// (each class has its own: not inherited)
			// What it inherits with this name (its classes', its interfaces').
			std::vector<FuncInfo*> inherited;
			for (StructInfo* b = info->base; b != nullptr; b = b->base)
			{
				auto it = m_funcs.find(b->name + "::" + n);
				if (it != m_funcs.end())
					inherited.insert(inherited.end(), it->second.begin(), it->second.end());
			}
			for (const auto& slot : info->ifaceSlots)
			{
				auto it = m_funcs.find(slot.iface->name + "::" + n);
				if (it == m_funcs.end())
					continue;
				for (FuncInfo* g : it->second)
				{
					if (std::find(inherited.begin(), inherited.end(), g) == inherited.end())
						inherited.push_back(g);
				}
			}
			if (const StructField* bf = info->field(n))
			{
				if (bf->owner != nullptr)
					error(d->namePos, "'" + n + "' is a field of " + bf->owner->name + " (" + info->name + " extends it): a sub cannot have its name");
			}
			bool matched = false;
			for (FuncInfo* a : inherited)
			{
				if (a->type == nullptr)
					resolveFunction(a);
				if (!sameSignature(m, a))
					continue;
				const StructInfo* where = structOfMethod(a);
				const std::string aName = where->name + "::" + n;
				if ((d->value & 8) != 0 || (a->decl->value & 8) != 0)
					error(d->namePos, "'" + n + "' with these parameters is a " + std::string((a->decl->value & 8) != 0 ? "static " : "") + "sub of " + where->name +
						" already (" + info->name + " extends it): a class cannot hide what it inherits (give it another name)");
				if (!isVirtualSub(a))
					error(d->namePos, "'" + n + "' with these parameters is a sub of " + where->name + " already, and it is not virtual: a class cannot hide what it inherits "
						"(give it another name, or make " + aName + " virtual: virt sub " + n + "(...))");
				if ((d->value & 2048) == 0)
					error(d->namePos, "'" + n + "' redefines the virtual sub " + aName + ": mark it override (override sub " + n + "(...))");
				if ((a->decl->value & 4096) != 0)
					error(d->namePos, aName + " is final: no class overrides it");
				if (m->type->base != a->type->base)
					error(d->namePos, "'" + n + "' returns " + m->type->base->name() + ", but " + aName + " (which it overrides) returns " + a->type->base->name());
				if ((m->decl->value & 64) != (a->decl->value & 64))
					error(d->namePos, "'" + n + "' and " + aName + " (which it overrides) are both readonly subs, or neither (readonly makes this a const pointer)");
				matched = true;
			}
			if ((d->value & 2048) != 0 && !matched)
				error(d->namePos, "'" + n + "' is marked override, but no class or interface " + info->name + " extends has a virtual sub " + n + " with these parameters");
			if (!isVirtualSub(m))
				continue;
			// Its slot: the class virtual it overrides, or a new one.
			int slot = -1;
			for (size_t i = 0; i < info->vslots.size(); i++)
			{
				if (sameSignature(info->vslots[i].decl, m))
					slot = (int)i;
			}
			FuncInfo* impl = (d->value & 1024) != 0 ? nullptr : m;
			if (slot >= 0)
				info->vslots[(size_t)slot].impl = impl;
			else
				info->vslots.push_back({ m, impl });
		}
		info->vtablesBuilt = true;
		if (info->isAbstract)
			return;
		// A class that is built: a body for every virtual sub (its own or inherited).
		for (FuncInfo* m : own)
		{
			if ((m->decl->value & 1024) != 0)
				error(m->decl->namePos, "'" + m->decl->shortName + "' is pure: " + info->name + " is then an abstract class (abstract class " + info->decl->shortName + " ...)");
		}
		for (const auto& slot : info->vslots)
		{
			if (slot.impl == nullptr)
				error(info->decl->namePos, info->name + " has no body for the pure sub " + structOfMethod(slot.decl)->name + "::" + slot.decl->decl->shortName +
					" (override it, or make " + info->decl->shortName + " an abstract class)");
		}
		for (const auto& is : info->ifaceSlots)
		{
			buildVtables(is.iface);
			for (const auto& slot : is.iface->vslots)
			{
				if (finalOverrider(info, slot.decl, nullptr) == nullptr)
					error(info->decl->namePos, info->name + " does not implement " + structOfMethod(slot.decl)->name + "::" + slot.decl->decl->shortName +
						" (override sub " + slot.decl->decl->shortName + "(...) { ... }, or make " + info->decl->shortName + " an abstract class)");
			}
		}
	}

	int CodeGen::slotOf(StructInfo* info, const FuncInfo* f)
	{
		buildVtables(info);
		for (size_t i = 0; i < info->vslots.size(); i++)
		{
			if (info->vslots[i].decl == f || info->vslots[i].impl == f)
				return (int)i;
		}
		return -1;
	}

	FuncInfo* CodeGen::finalOverrider(StructInfo* info, const FuncInfo* m, StructInfo** where)
	{
		for (StructInfo* c = info; c != nullptr; c = c->base)
		{
			auto it = m_funcs.find(c->name + "::" + m->decl->shortName);
			if (it == m_funcs.end())
				continue;
			for (FuncInfo* g : it->second)
			{
				if ((g->decl->value & 8) == 0 && (g->decl->value & 1024) == 0 && sameSignature(g, m))
				{
					if (where)
						*where = c;
					return g;
				}
			}
		}
		return nullptr;
	}

	std::string CodeGen::vtableLabel(StructInfo* info, uint32_t offset)
	{
		if (!m_discarding && !m_options.analysis && m_vtableRequests.insert(info).second)
			m_vtableQueue.push_back(info);
		return "__vt_" + symbolChars(info->name) + (offset != 0 ? "_" + std::to_string(offset) : "");
	}

	std::string CodeGen::typeinfoLabel(StructInfo* info)
	{
		const std::string label = "__ti_" + symbolChars(info->name);
		if (!m_typeinfos.insert(info).second)
			return label;
		// The class's type (dynamic_cast walks it): its name, its base's type (0: none) and where the
		// base is in it, then how many interfaces it can be used as, and each one's type and where its
		// table pointer is. An interface: its name, then zeroes.
		auto l_word = [](uint32_t w) {
			char buf[40];
			std::snprintf(buf, sizeof(buf), "0x%02X, 0x%02X, 0x%02X, 0x%02X", (w >> 24) & 0xFF, (w >> 16) & 0xFF, (w >> 8) & 0xFF, w & 0xFF);
			return std::string(buf);
		};
		std::string line = "\t$" + label + " $" + stringLiteral(info->name) + ", ";
		if (info->base != nullptr && info->base->isClass())
			line += "$" + typeinfoLabel(info->base) + ", " + l_word(info->baseOffset);
		else
			line += l_word(0) + ", " + l_word(0);
		std::vector<StructInfo::IfaceSlot> slots;
		if (info->isClass())
			slots = info->ifaceSlots;
		line += ", " + l_word((uint32_t)slots.size());
		for (const auto& s : slots)
			line += ", $" + typeinfoLabel(s.iface) + ", " + l_word(s.offset);
		m_data.push_back(line);
		return label;
	}

	// A table: the type, the offset of its vtable pointer in the object (the object is at the
	// pointer minus that), the destructor's slot (reserved: 0, 0), then each slot: the sub, and
	// what to add to the pointer the call is made with (this, for the sub's class).
	void CodeGen::emitVtables(StructInfo* info)
	{
		buildVtables(info);
		auto l_word = [](int64_t v) {
			char buf[40];
			const uint32_t w = (uint32_t)v;
			std::snprintf(buf, sizeof(buf), "0x%02X, 0x%02X, 0x%02X, 0x%02X", (w >> 24) & 0xFF, (w >> 16) & 0xFF, (w >> 8) & 0xFF, w & 0xFF);
			return std::string(buf);
		};
		auto l_offsetOf = [&](StructInfo* c) {
			uint32_t o = 0;
			derives(info, c, &o);
			return (int64_t)o;
		};
		for (const auto& v : info->vptrs)
		{
			std::string line = "\t$" + vtableLabel(info, v.offset) + " $" + typeinfoLabel(info) + ", " + l_word(v.offset);
			// (the destructor's slot: the class's own del, on the whole object)
			if (FuncInfo* del = destructorOf(info))
			{
				markUsed(del);
				del->addressTaken = true;
				line += ", $" + del->label + ", " + l_word(-(int64_t)v.offset);
			}
			else
				line += ", " + l_word(0) + ", " + l_word(0);
			auto l_entry = [&](FuncInfo* g, StructInfo* where) {
				if (g == nullptr)
				{
					line += ", " + l_word(0) + ", " + l_word(0);
					return;
				}
				markUsed(g);
				g->addressTaken = true;
				line += ", $" + g->label + ", " + l_word(l_offsetOf(where) - (int64_t)v.offset);
			};
			if (v.view == info)
			{
				for (const auto& slot : info->vslots)
					l_entry(slot.impl, structOfMethod(slot.impl));
			}
			else
			{
				for (const auto& slot : v.view->vslots)
				{
					StructInfo* where = nullptr;
					FuncInfo* g = finalOverrider(info, slot.decl, &where);
					l_entry(g, where);
				}
			}
			m_data.push_back(line);
		}
	}

	void CodeGen::storeVptrs(const Reg& addr, Type* t, int32_t offset)
	{
		if (t->kind != Type::Kind::Struct || t->structInfo->vptrs.empty())
			return;
		for (const auto& v : t->structInfo->vptrs)
			emit("omov *" + addr + ", $" + vtableLabel(t->structInfo, v.offset) + ", dword " + imm(offset + (int32_t)v.offset));
	}

	void CodeGen::copyValue(const Reg& dst, const Reg& src, Type* t)
	{
		copyMemory(dst, src, t->size());
		storeVptrs(dst, t);		// (a class value copied from a derived object: its own table)
	}

	void CodeGen::requireValueType(Type* t, const SourcePos& pos, const std::string& what)
	{
		if (t == nullptr || t->kind != Type::Kind::Struct || !t->structInfo->onlyPointer())
			return;
		const StructInfo* info = t->structInfo;
		if (info->isInterface())
			error(pos, info->name + " is an interface: it cannot be " + what + " (only pointed at: " + info->name + "*, to an object of a class that implements it)");
		error(pos, info->name + " is an abstract class: it cannot be " + what + " (only pointed at: " + info->name + "*, to an object of a class derived from it)");
	}

	// A class's constructor: Base::new(...) as one of its statements builds the base's part (the
	// base's constructor without arguments is called first otherwise, if the base has constructors),
	// then the class's vtable pointers are set (the base's constructor set its own).
	void CodeGen::prepareConstructor(FuncInfo* f)
	{
		Node* d = f->decl;
		if ((d->value & 4) == 0 || d->shortName != "new" || d->a == nullptr || d->owner == nullptr)
			return;
		auto it = m_structs.find(d->owner->name);
		if (it == m_structs.end() || !it->second->isClass() || !m_preparedCtors.insert(d).second)
			return;
		StructInfo* info = it->second;
		resolveStruct(info, d->pos);
		std::vector<Node*>& body = d->a->list;
		size_t at = 0;
		if (info->base != nullptr)
		{
			const std::string baseNew = info->base->name + "::new";
			bool explicitCall = false;
			for (size_t i = 0; i < body.size(); i++)
			{
				Node* s = body[i];
				if (s->kind != NodeKind::ExprStmt || s->a->kind != NodeKind::Call)
					continue;
				Node* callee = s->a->a;
				bool isBase = callee->kind == NodeKind::Ident && callee->name == baseNew;
				if (callee->kind == NodeKind::Member && callee->value == 0 && callee->name == "new" && callee->a->kind == NodeKind::Ident)
				{
					// Base.new(...) (Base<T>.new(...) in a template)
					resolveTemplateIdent(callee->a);
					isBase = callee->a->name == info->base->name;
				}
				if (!isBase)
					continue;
				if (explicitCall)
					error(s->pos, info->base->name + "::new is called twice (the " + info->base->name + " part is built once)");
				explicitCall = true;
				s->a->callFlags |= 4;
				at = i + 1;
			}
			if (!explicitCall && m_funcs.count(baseNew) > 0)
			{
				bool hasDefault = false;
				for (FuncInfo* b : m_funcs[baseNew])
				{
					if (b->type == nullptr)
						resolveFunction(b);
					hasDefault = hasDefault || (b->requiredParams <= 1 && b->type->variadic == 0);	// (this)
				}
				if (!hasDefault)
					error(d->namePos, info->name + "::new must build its " + info->base->name + " part: " + info->base->name +
						" has no constructor without arguments (call " + info->base->name + "::new(...) in it, as one of its statements)");
				Node* id = new Node();
				id->kind = NodeKind::Ident;
				id->pos = d->namePos;
				id->name = baseNew;
				id->text = baseNew;
				id->resolved = true;
				Node* call = new Node();
				call->kind = NodeKind::Call;
				call->pos = d->namePos;
				call->a = id;
				call->callFlags = 4;
				Node* stmt = new Node();
				stmt->kind = NodeKind::ExprStmt;
				stmt->pos = d->namePos;
				stmt->a = call;
				body.insert(body.begin(), stmt);
				at = 1;
			}
		}
		if (!info->vptrs.empty())
		{
			Node* sv = new Node();
			sv->kind = NodeKind::SetVtables;
			sv->pos = d->namePos;
			body.insert(body.begin() + (std::ptrdiff_t)at, sv);
		}
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

	// =============================== Destructors, copies, moves ===============================
	bool CodeGen::destructible(Type* t)
	{
		while (t != nullptr && t->kind == Type::Kind::Array)
			t = t->base;
		if (t == nullptr || t->kind != Type::Kind::Struct)
			return false;
		resolveStruct(t->structInfo, t->structInfo->pos);
		return destructorOf(t->structInfo) != nullptr;
	}

	FuncInfo* CodeGen::destructorOf(StructInfo* info)
	{
		auto it = m_funcs.find(info->name + "::del");
		if (it == m_funcs.end() || it->second.empty() || (it->second[0]->decl->value & 4) == 0)
			return nullptr;
		return it->second[0];
	}

	// A struct whose fields (or base) have destructors gets del() (it destroys them).
	void CodeGen::implicitDestructor(StructInfo* info)
	{
		if (info->isInterface() || info->anonymous || m_funcs.count(info->name + "::del") > 0)
			return;
		bool needed = info->base != nullptr && destructible(m_types.structType(info->base));
		for (const StructField& f : info->fields)
			needed = needed || (f.owner == nullptr && destructible(f.type));
		if (!needed)
			return;
		Node* cls = info->decl;
		Node* d = new Node();
		d->kind = NodeKind::FuncDecl;
		d->pos = cls->namePos;
		d->namePos = cls->namePos;
		d->name = info->name + "::del";
		d->shortName = "del";
		d->text = "__implicit";
		d->scope = info->name;
		d->owner = cls;
		d->value = 4;
		d->external = cls->external;
		Node* self = new Node();
		self->kind = NodeKind::VarDecl;
		self->pos = cls->namePos;
		self->namePos = cls->namePos;
		self->name = "this";
		TypeSyntax* named = new TypeSyntax();
		named->pos = cls->namePos;
		named->name = info->name;
		named->written = cls->shortName;
		named->resolved = true;
		self->typeSyntax = new TypeSyntax();
		self->typeSyntax->kind = TypeSyntax::Kind::Pointer;
		self->typeSyntax->pos = cls->namePos;
		self->typeSyntax->base = named;
		d->list.push_back(self);
		if (cls->external == 0)
		{
			d->a = new Node();
			d->a->kind = NodeKind::Block;
			d->a->pos = cls->namePos;
			d->a->endPos = cls->namePos;
		}
		FuncInfo* f = new FuncInfo();
		f->name = d->name;
		f->decl = d;
		f->external = cls->external != 0;
		f->defined = true;
		f->instance = m_instanceOrigin.count(info->name) > 0;
		if (m_names.count(d->name) == 0)
			m_names[d->name] = "a sub";
		m_funcs[d->name].push_back(f);
		m_funcList.push_back(f);
		if (!m_inDeclarations || f->instance)
		{
			resolveFunction(f);
			f->label = codeLabel(f->name, true);
		}
	}

	// A destructor: [its own tables] [defer: its fields, the last first, then its base's destructor]
	// then its body (so they are destroyed when it ends, also by a return).
	void CodeGen::prepareDestructor(FuncInfo* f)
	{
		Node* d = f->decl;
		if ((d->value & 4) == 0 || d->shortName != "del" || d->a == nullptr || d->owner == nullptr || !m_preparedDtors.insert(d).second)
			return;
		auto it = m_structs.find(d->owner->name);
		if (it == m_structs.end())
			return;
		StructInfo* info = it->second;
		resolveStruct(info, d->pos);
		bool members = info->base != nullptr && destructible(m_types.structType(info->base));
		for (const StructField& field : info->fields)
			members = members || (field.owner == nullptr && destructible(field.type));
		std::vector<Node*>& body = d->a->list;
		if (members)
		{
			Node* destroy = new Node();
			destroy->kind = NodeKind::Destroy;
			destroy->pos = d->a->endPos;
			destroy->value = 0;
			Node* defer = new Node();
			defer->kind = NodeKind::Defer;
			defer->pos = d->namePos;
			defer->a = destroy;
			body.insert(body.begin(), defer);
		}
		if (!info->vptrs.empty())
		{
			// (while a base's destructor runs, the object is of the base's class again)
			Node* sv = new Node();
			sv->kind = NodeKind::SetVtables;
			sv->pos = d->namePos;
			body.insert(body.begin(), sv);
		}
	}

	FuncInfo* CodeGen::copyConstructorOf(StructInfo* info)
	{
		auto it = m_funcs.find(info->name + "::new");
		if (it == m_funcs.end())
			return nullptr;
		Type* self = m_types.structType(info);
		for (FuncInfo* f : it->second)
		{
			if (f->type == nullptr && !f->resolving)
				resolveFunction(f);
			if (f->type == nullptr || f->type->params.size() != 2 || f->type->variadic != 0)
				continue;
			Type* p = f->type->params[1];
			if (p->kind == Type::Kind::Pointer && p->base == self)
				return f;
		}
		return nullptr;
	}

	bool CodeGen::ownsResources(StructInfo* info)
	{
		FuncInfo* del = destructorOf(info);
		return (del != nullptr && del->decl->text != "__implicit") || copyConstructorOf(info) != nullptr;
	}

	bool CodeGen::customCopy(Type* t)
	{
		while (t != nullptr && t->kind == Type::Kind::Array)
			t = t->base;
		if (t == nullptr || t->kind != Type::Kind::Struct)
			return false;
		StructInfo* info = t->structInfo;
		if (copyConstructorOf(info) != nullptr)
			return true;
		if (info->base != nullptr && customCopy(m_types.structType(info->base)))
			return true;
		for (const StructField& f : info->fields)
		{
			if (f.owner == nullptr && customCopy(f.type))
				return true;
		}
		return false;
	}

	bool CodeGen::customAssign(Type* t)
	{
		while (t != nullptr && t->kind == Type::Kind::Array)
			t = t->base;
		if (t == nullptr || t->kind != Type::Kind::Struct)
			return false;
		StructInfo* info = t->structInfo;
		if (m_funcs.count(info->name + "::operator=") > 0 || ownsResources(info))
			return true;
		if (info->base != nullptr && customAssign(m_types.structType(info->base)))
			return true;
		for (const StructField& f : info->fields)
		{
			if (f.owner == nullptr && customAssign(f.type))
				return true;
		}
		return false;
	}

	void CodeGen::requireCopyable(Type* t, const SourcePos& pos)
	{
		while (t != nullptr && t->kind == Type::Kind::Array)
			t = t->base;
		if (t == nullptr || t->kind != Type::Kind::Struct)
			return;
		StructInfo* info = t->structInfo;
		if (ownsResources(info) && copyConstructorOf(info) == nullptr)
			error(pos, info->name + " has a destructor (it owns something): copying it needs a copy constructor (sub new(var o: const " + info->name +
				"*)); or pass a pointer (a call's result, a returned variable are moved, not copied)");
		if (FuncInfo* cc = copyConstructorOf(info))
		{
			// (priv sub new(var o: const ref T): only its own subs copy it; prot: its derived classes' too)
			checkSubAccess(cc, pos);
			return;
		}
		if (info->base != nullptr)
			requireCopyable(m_types.structType(info->base), pos);
		for (const StructField& f : info->fields)
		{
			if (f.owner == nullptr)
				requireCopyable(f.type, pos);
		}
	}

	// f (a method) on self + offset, with one more argument (a register or an immediate).
	void CodeGen::callMethod(FuncInfo* f, const Reg& self, int32_t offset, const std::string& arg)
	{
		markUsed(f);
		if (f->isInline && !m_discarding)
			f->addressTaken = true;		// (called from here, not expanded: it needs its code)
		Reg a = alloc({});
		emit("mov " + a + ", " + self);
		addImmediate(a, offset);
		emit("push " + a);
		release(a);
		if (!arg.empty())
			emit("push " + arg);
		emit("push " + std::string(arg.empty() ? "1" : "2"));
		emit("call $" + f->label);
	}

	void CodeGen::genDestroy(const Reg& addr, Type* t, int32_t offset)
	{
		if (!destructible(t))
			return;
		if (t->kind == Type::Kind::Struct)
		{
			callMethod(destructorOf(t->structInfo), addr, offset);
			return;
		}
		// An array: its elements, the last first.
		const uint32_t size = t->base->size();
		if (t->count <= 8)
		{
			for (uint32_t i = t->count; i-- > 0; )
				genDestroy(addr, t->base, offset + (int32_t)(i * size));
			return;
		}
		Reg p = alloc({});
		emit("mov " + p + ", " + addr);
		addImmediate(p, offset + (int64_t)(t->count - 1) * size);
		Reg n = alloc({});
		emit("mov " + n + ", " + imm(t->count));
		const std::string loop = newLabel();
		label(loop);
		genDestroy(p, t->base, 0);
		addImmediate(p, -(int64_t)size);
		emit("subip " + n + ", 1");
		emit("mov ACC, " + n);
		emit("jne $" + loop + ", 0");
		release(n);
		release(p);
	}

	void CodeGen::genCopy(const Reg& dst, const Reg& src, Type* t, const SourcePos& pos)
	{
		if (!customCopy(t))
		{
			requireCopyable(t, pos);
			if (t->kind == Type::Kind::Struct)
				copyValue(dst, src, t);
			else
				copyMemory(dst, src, t->size());
			return;
		}
		requireCopyable(t, pos);
		auto l_part = [&](Type* pt, uint32_t offset) {
			Reg d = alloc(pos);
			emit("mov " + d + ", " + dst);
			addImmediate(d, offset);
			Reg s = alloc(pos);
			emit("mov " + s + ", " + src);
			addImmediate(s, offset);
			genCopy(d, s, pt, pos);
			release(s);
			release(d);
		};
		if (t->kind == Type::Kind::Array)
		{
			for (uint32_t i = 0; i < t->count; i++)
				l_part(t->base, i * t->base->size());
			return;
		}
		StructInfo* info = t->structInfo;
		if (FuncInfo* cc = copyConstructorOf(info))
		{
			callMethod(cc, dst, 0, src);
			return;
		}
		// Member by member: the bytes, then the parts that copy themselves (they overwrite theirs).
		copyMemory(dst, src, t->size());
		if (info->base != nullptr && customCopy(m_types.structType(info->base)))
			l_part(m_types.structType(info->base), info->baseOffset);
		for (const StructField& f : info->fields)
		{
			if (f.owner == nullptr && customCopy(f.type))
				l_part(f.type, f.offset);
		}
		storeVptrs(dst, t);
	}

	void CodeGen::genAssignValue(const Reg& dst, const Reg& src, Type* t, const SourcePos& pos)
	{
		if (!customAssign(t))
		{
			copyMemory(dst, src, t->size());
			return;
		}
		auto l_part = [&](Type* pt, uint32_t offset, bool assign) {
			Reg d = alloc(pos);
			emit("mov " + d + ", " + dst);
			addImmediate(d, offset);
			Reg s = alloc(pos);
			emit("mov " + s + ", " + src);
			addImmediate(s, offset);
			if (assign)
				genAssignValue(d, s, pt, pos);
			else
				copyMemory(d, s, pt->size());
			release(s);
			release(d);
		};
		if (t->kind == Type::Kind::Array)
		{
			for (uint32_t i = 0; i < t->count; i++)
				l_part(t->base, i * t->base->size(), true);
			return;
		}
		StructInfo* info = t->structInfo;
		auto it = m_funcs.find(info->name + "::operator=");
		if (it != m_funcs.end())
		{
			// Its operator= (taking a T* / const T*, or a T: a copy).
			for (FuncInfo* f : it->second)
			{
				if (f->type == nullptr)
					resolveFunction(f);
				if (f->type->params.size() != 2)
					continue;
				Type* p = f->type->params[1];
				if (p->kind == Type::Kind::Pointer && p->base == t)
				{
					callMethod(f, dst, 0, src);
					return;
				}
				if (p == t)
				{
					Reg copy = alloc(pos);
					emit("mov " + copy + ", FP");
					addImmediate(copy, temporary(t->size()));
					genCopy(copy, src, t, pos);
					callMethod(f, dst, 0, copy);
					genDestroy(copy, t, 0);
					release(copy);
					return;
				}
			}
			error(pos, info->name + " has an operator=, but none takes a " + info->name + " (sub operator=(var o: const " + info->name + "*))");
		}
		if (ownsResources(info))
		{
			// The old value destroyed, then a copy (not when it is assigned to itself).
			const std::string same = newLabel();
			emit("mov ACC, " + dst);
			emit("jeq $" + same + ", " + src);
			genDestroy(dst, t, 0);
			genCopy(dst, src, t, pos);
			label(same);
			return;
		}
		// Member by member.
		uint32_t at = 0;
		if (info->base != nullptr)
		{
			l_part(m_types.structType(info->base), info->baseOffset, customAssign(m_types.structType(info->base)));
			at = info->baseOffset + info->base->size;
		}
		for (const StructField& f : info->fields)
		{
			if (f.owner != nullptr)
				continue;
			if (f.offset > at)
			{
				// (vtable pointers in between: copied as they are, restored by the caller)
				Reg d = alloc(pos);
				emit("mov " + d + ", " + dst);
				addImmediate(d, at);
				Reg s = alloc(pos);
				emit("mov " + s + ", " + src);
				addImmediate(s, at);
				copyMemory(d, s, f.offset - at);
				release(s);
				release(d);
			}
			l_part(f.type, f.offset, customAssign(f.type));
			at = f.offset + f.type->size();
		}
	}

	void CodeGen::genInitValue(const Reg& dst, Node* src, const Reg& value, Type* t)
	{
		if ((src->kind == NodeKind::Call || src->kind == NodeKind::Construct) && t->kind == Type::Kind::Struct)
		{
			copyValue(dst, value, t);		// (moved: the call's temporary is not destroyed)
			consumeTemp(src);
			return;
		}
		genCopy(dst, value, t, src->pos);
	}

	size_t CodeGen::beginExpr(void)
	{
		m_exprStarts.push_back(m_out != nullptr ? m_out->size() : 0);
		return m_temps.size();
	}

	void CodeGen::endExpr(size_t mark)
	{
		for (size_t i = m_temps.size(); i-- > mark; )
		{
			const Temp temp = m_temps[i];
			if (temp.consumed)
				continue;
			Reg r = alloc({});
			std::string skip;
			if (temp.flag != 0)
			{
				// (made in a branch: only if it was)
				skip = newLabel();
				emit("movo " + r + ", *FP, dword " + imm(temp.flag));
				emit("mov ACC, " + r);
				emit("jeq $" + skip + ", 0");
				m_out->insert(m_out->begin() + (std::ptrdiff_t)temp.codePos, "\tomov *FP, 0x00000000, dword " + imm(temp.flag));
			}
			emit("mov " + r + ", FP");
			genDestroy(r, temp.type, temp.offset);
			if (!skip.empty())
				label(skip);
			release(r);
		}
		m_temps.resize(std::min(mark, m_temps.size()));
		if (!m_exprStarts.empty())
			m_exprStarts.pop_back();
	}

	void CodeGen::addTemp(int32_t offset, Type* t, Node* node)
	{
		if (!destructible(t))
			return;
		Temp temp { offset, t, 0, 0, false, node };
		if (m_condDepth > 0 && !m_exprStarts.empty())
		{
			temp.flag = temporary(4);
			temp.codePos = m_exprStarts.back();
			emit("omov *FP, 0x00000001, dword " + imm(temp.flag));
		}
		m_temps.push_back(temp);
	}

	bool CodeGen::consumeTemp(Node* n)
	{
		for (size_t i = m_temps.size(); i-- > 0; )
		{
			if (m_temps[i].node == n && !m_temps[i].consumed)
			{
				m_temps[i].consumed = true;
				return true;
			}
		}
		return false;
	}

	// The type of the sub a call calls (nullptr: not a call of a sub).
	Type* CodeGen::calleeType(const Node* e)
	{
		if (e == nullptr || e->kind != NodeKind::Call || e->vaBuiltin != 0)
			return nullptr;
		if (e->func != nullptr)
			return e->func->type;
		Type* t = e->a != nullptr ? decay(e->a->type) : nullptr;
		if (t != nullptr && t->kind == Type::Kind::Pointer && t->base != nullptr)
			t = t->base;
		return t != nullptr && t->kind == Type::Kind::Function ? t : nullptr;
	}

	// The slots before a sub's parameters: a struct result's address, then the address of its
	// caller's error slot (a sub that throws).
	size_t CodeGen::hiddenSlots(const Type* fn)
	{
		return (returnsStruct(fn) ? 1 : 0) + (fn != nullptr && fn->throws != nullptr ? 1 : 0);
	}

	// Does the statement always leave (return, throw, break, continue, at its end)?
	bool CodeGen::leaves(const Node* s)
	{
		if (s == nullptr)
			return false;
		switch (s->kind)
		{
			case NodeKind::Return: case NodeKind::Throw: case NodeKind::Break: case NodeKind::Continue: return true;
			case NodeKind::Block: return !s->list.empty() && leaves(s->list.back());
			case NodeKind::If: return s->c != nullptr && leaves(s->b) && leaves(s->c);
			default: return false;
		}
	}

	// A call of a sub that throws, with its handler (try / catch): the handler's checks. Its error
	// goes in a slot of this frame (8 bytes: 1 when it failed, then the error; the handler's
	// constValue), which catch (e) names.
	void CodeGen::checkHandler(Node* e, Type* expected)
	{
		Node* h = e->handler;
		const bool isTry = h->kind == NodeKind::Try;
		Type* fn = calleeType(e);
		if (fn == nullptr || fn->throws == nullptr)
			error(h->pos, std::string(isTry ? "try" : "catch") + ": " + (e->func != nullptr ? e->func->name : std::string("this sub")) + " doesn't throw (" +
				(isTry ? "try" : "catch") + " is for the calls of subs that throw)");
		const int32_t slot = temporary(8);
		h->constValue = slot;
		Type* ret = fn->base;
		if (isTry)
		{
			FuncInfo* self = m_inline.empty() ? m_func : m_inline.back().func;
			if (self == nullptr || self->type == nullptr || self->type->throws == nullptr)
				error(h->pos, "try throws the error on: " + (self != nullptr ? self->name : std::string("the code here")) + " must throw " + fn->throwsName +
					" too (sub ...() throws " + fn->throwsName + "), or catch it here (f(...) catch ...)");
			if (self->type->throwsName != fn->throwsName)
				error(h->pos, "try: the call throws " + fn->throwsName + ", " + self->name + " throws " + self->type->throwsName +
					" (catch it: f(...) catch (e) { throw ...; })");
			if (m_inDefer > 0)
				error(h->pos, "try cannot leave a defer (catch the error there)");
			return;
		}
		// catch (e): the error, in the slot's second word (its last bytes for a smaller enum).
		h->var = nullptr;
		if (!h->name.empty())
		{
			VarInfo* v = new VarInfo();
			v->name = h->name;
			v->type = fn->throws;
			v->storage = VarInfo::Storage::Local;
			v->offset = slot + 4 + (int32_t)(4 - fn->throws->size());
			v->pos = h->pos;
			v->defPos = h->namePos;
			v->isConst = true;
			h->var = v;
			if (m_options.index != nullptr && !m_discarding)
			{
				indexName(v->defPos, v->name, v->defPos, hoverVar(v));
				completeLocal(v);
			}
		}
		if (h->c != nullptr)
		{
			if (ret->kind != Type::Kind::Void && !leaves(h->c))
				error(h->c->endPos.line > 0 ? h->c->endPos : h->pos, "this catch block must leave (return, throw, break or continue): the call has a value (" + ret->name() +
					") it cannot give otherwise (f(...) catch value gives one)");
			return;		// (its statements: when it is generated)
		}
		if (ret->kind == Type::Kind::Void)
			error(h->pos, "catch value: the call returns nothing (catch { ... } runs a block)");
		m_scopes.push_back({});
		if (h->var != nullptr)
			m_scopes.back()[h->name] = h->var;
		check(h->b, ret);
		requireConvertible(h->b, ret, "as the catch value");
		m_scopes.pop_back();
		if (ret->isReference())
			error(h->pos, "catch value: the call returns a reference");
	}

	bool CodeGen::makesTemps(Node* e)
	{
		if (e == nullptr)
			return false;
		if (e->kind == NodeKind::Call && e->handler != nullptr && makesTemps(e->handler->b))
			return true;
		if (e->kind == NodeKind::Construct && destructible(e->type))
			return true;
		if (e->kind == NodeKind::Call)
		{
			if (destructible(e->type))
				return true;
			Type* fn = e->func != nullptr ? e->func->type : nullptr;
			for (size_t i = 0; fn != nullptr && i < fn->params.size(); i++)
			{
				if (fn->params[i]->kind == Type::Kind::Struct && destructible(fn->params[i]))
					return true;
			}
		}
		if (e->kind == NodeKind::FuncDecl)
			return false;
		for (Node* c : { e->a, e->b, e->c, e->d })
		{
			if (makesTemps(c))
				return true;
		}
		for (Node* c : e->list)
		{
			if (makesTemps(c))
				return true;
		}
		return false;
	}

	void CodeGen::genCondBranch(Node* e, const std::string& target, bool jumpIfTrue)
	{
		const size_t mark = beginExpr();
		if (!makesTemps(e))
		{
			genBranch(e, target, jumpIfTrue);
			endExpr(mark);
			return;
		}
		// Its value first, then its temporaries destroyed, then the jump.
		Reg r = gen(e);
		convert(r, decay(e->type), m_types.boolType());
		endExpr(mark);
		emit("mov ACC, " + r);
		emit(std::string(jumpIfTrue ? "jne" : "jeq") + " $" + target + ", 0");
		release(r);
	}

	// ===================================== References =====================================
	// A ref T parameter is a pointer the caller passes (to its argument); in the sub, the name is
	// what it points at. A sub returning ref T returns a pointer; its call is what it points at.

	// An argument (or a returned value) for a reference: its address (an & node, marked), or (const
	// ref T, a value) the value in a temporary (Materialize: its address).
	static bool boundReference(const Node* n)
	{
		return (n->kind == NodeKind::Unary && n->name == "&" && (n->callFlags & 32) != 0) || n->kind == NodeKind::Materialize;
	}

	void CodeGen::bindReference(Node* from, Type* to, const std::string& context)
	{
		if (boundReference(from))
			return;
		Type* t = to->base;
		Type* ft = from->type;
		auto l_wrap = [&](NodeKind kind) {
			Node* inner = new Node(*from);
			const SourcePos pos = from->pos;
			*from = Node();
			from->pos = pos;
			from->kind = kind;
			from->a = inner;
			if (kind == NodeKind::Unary)
				from->name = "&";
			from->callFlags = 32;
			check(from);
		};
		const bool related = ft == t || (ft != nullptr && ft->kind == Type::Kind::Struct && t->kind == Type::Kind::Struct && derives(ft->structInfo, t->structInfo));
		const bool structTemp = (from->kind == NodeKind::Call || from->kind == NodeKind::Construct) && ft != nullptr && ft->kind == Type::Kind::Struct;
		if (related && (from->lvalue || structTemp) && (to->constTarget || !from->readOnly))
		{
			if (!to->constTarget && structTemp)
				error(from->pos, "a ref " + t->name() + " " + context + " takes a variable (something in memory), not a call's result (const ref " + t->name() + " takes values too)");
			l_wrap(NodeKind::Unary);
			return;
		}
		if (!to->constTarget)
		{
			if (related && from->readOnly)
				error(from->pos, (from->readOnlyWhy.empty() ? std::string("this is a constant") : from->readOnlyWhy) + ": a ref " + t->name() + " " + context +
					" could change it (const ref " + t->name() + " cannot)");
			error(from->pos, "a ref " + t->name() + " " + context + " takes a variable of type " + t->name() + " (something in memory)" +
				(ft != nullptr ? ", not " + std::string(from->lvalue ? "a " + ft->name() : "a value") : std::string()) + " (const ref " + t->name() + " takes values too)");
		}
		// const ref T: a value of T (or what converts to it) in a temporary.
		if (!related)
			requireConvertible(from, t, context);
		if ((from->kind == NodeKind::Call || from->kind == NodeKind::Construct) && from->type != nullptr && from->type->kind == Type::Kind::Struct)
			l_wrap(NodeKind::Unary);
		else
			l_wrap(NodeKind::Materialize);
	}

	// The cost of an argument for a reference (-1: it cannot be one).
	int CodeGen::referenceCost(Node* from, Type* to)
	{
		Type* t = to->base;
		Type* ft = from->type;
		if (ft == nullptr)
			return -1;
		int depth = 0;
		if (boundReference(from))
		{
			// (already its address)
			if (ft->kind != Type::Kind::Pointer)
				return -1;
			if (ft->constTarget && !to->constTarget)
				return -1;
			if (ft->base == t)
				return 0;
			return (ft->base->kind == Type::Kind::Struct && t->kind == Type::Kind::Struct && derives(ft->base->structInfo, t->structInfo, nullptr, &depth)) ? depth : -1;
		}
		int base = -1;
		if (ft == t)
			base = 0;
		else if (ft->kind == Type::Kind::Struct && t->kind == Type::Kind::Struct && derives(ft->structInfo, t->structInfo, nullptr, &depth))
			base = depth;
		const bool structTemp = (from->kind == NodeKind::Call || from->kind == NodeKind::Construct) && ft->kind == Type::Kind::Struct;
		if (!to->constTarget)
			return (base >= 0 && from->lvalue && !from->readOnly && !structTemp) ? base : -1;
		if (base >= 0)
			return base;
		const int c = builtinConversionCost(from, t);
		return c >= 0 ? c + 1 : -1;
	}

	// A call of a sub returning a reference: what it refers to (e becomes *call).
	void CodeGen::dereferenceResult(Node* e)
	{
		if (e->type == nullptr || !e->type->isReference() || (e->callFlags & 16) != 0)
			return;
		Node* call = new Node(*e);
		call->callFlags |= 16;
		const SourcePos pos = e->pos;
		*e = Node();
		e->kind = NodeKind::Unary;
		e->name = "*";
		e->pos = pos;
		e->a = call;
		check(e);
	}

	// References are parameters, results and locals only.
	void CodeGen::rejectReference(Type* t, const SourcePos& pos, const std::string& what)
	{
		if (t != nullptr && t->isReference())
			error(pos, "a reference (" + t->name() + ") is for parameters, results and locals, not " + what + " (use a pointer)");
	}

	// n (something in memory) is in the sub's frame: a local or a value parameter (its fields,
	// its array's elements), or what a reference local to one refers to. A reference to it ends
	// when the sub returns.
	bool CodeGen::refersToFrame(const Node* n)
	{
		const Node* root = n;
		while ((root->kind == NodeKind::Member && root->value == 0) || (root->kind == NodeKind::Index && root->a->type != nullptr && root->a->type->kind == Type::Kind::Array))
			root = root->a;
		if (root->kind == NodeKind::Unary && root->name == "*" && root->a->kind == NodeKind::Ident && root->a->var != nullptr && root->a->var->type != nullptr
			&& root->a->var->type->isReference())
			return root->a->var->refFrame;		// (a reference: what it refers to)
		return root->kind == NodeKind::Ident && root->var != nullptr && (root->var->storage == VarInfo::Storage::Local || root->var->storage == VarInfo::Storage::Param)
			&& (root->var->type == nullptr || !root->var->type->isReference());
	}

	// A reference local's declaration (var x: ref T = v; ref x = v; const ref x = v;): s->a bound
	// (its address), and the variable's type. declared: var x: ref T's type (nullptr: ref x / const
	// ref x: T is v's type, const when v is constant). A const ref to a value (a literal, a call's
	// result, what converts to T) keeps the value itself: x is then a constant T (as C++ keeps the
	// temporary as long as the reference), and the result is T.
	Type* CodeGen::bindLocalReference(Node* s, Type* declared)
	{
		const std::string what = "'" + s->name + "'";
		if (s->c != nullptr)
			error(s->c->pos, what + " is a reference: it refers to a variable, it has no constructor");
		if (s->a == nullptr)
			error(s->pos, what + " is a reference: it needs what it refers to (" + s->name + " = <variable>)");
		if (s->a->kind == NodeKind::InitList)
			error(s->a->pos, what + " is a reference: it refers to a variable, not to { ... }");
		check(s->a, declared != nullptr ? declared->base : nullptr);
		if (boundReference(s->a) && s->a->type != nullptr && s->a->type->kind == Type::Kind::Pointer)
			return declared != nullptr ? declared : m_types.pointer(s->a->type->base, s->a->type->constTarget, true);	// (bound by an earlier pass)
		Type* to = declared;
		if (to == nullptr)
		{
			Type* t = s->a->type;
			if (t == nullptr || t->kind == Type::Kind::Null || t->kind == Type::Kind::Void || t->kind == Type::Kind::Function)
				error(s->pos, "cannot tell what " + what + " refers to (var " + s->name + ": ref <type> = ...)");
			if (t->kind == Type::Kind::Array)
				error(s->a->pos, what + ": a reference to an array is not supported (use a pointer to its first element)");
			if (s->refKind == 1 && !s->a->lvalue)
				error(s->a->pos, "ref " + s->name + " refers to a variable (something in memory), not a value (auto " + s->name + " = ... copies it, const ref " +
					s->name + " = ... keeps it)");
			to = m_types.pointer(t, s->refKind == 2 || s->a->readOnly, true);
		}
		Type* t = to->base;
		Type* ft = s->a->type;
		const bool related = ft == t || (ft != nullptr && ft->kind == Type::Kind::Struct && t->kind == Type::Kind::Struct && derives(ft->structInfo, t->structInfo));
		const bool structTemp = (s->a->kind == NodeKind::Call || s->a->kind == NodeKind::Construct) && ft != nullptr && ft->kind == Type::Kind::Struct;
		if (to->constTarget && (!related || !s->a->lvalue || structTemp))
		{
			// const ref to a value: the value, kept as a constant T.
			requireConvertible(s->a, t, "for " + what);
			s->constKind = 1;
			return t;
		}
		requireConvertible(s->a, to, "for " + what);		// (its address: bindReference)
		return to;
	}

	// ================================== User conversions ==================================
	// A struct's conversions (operator cast <type>), its own and those it inherits.
	std::string CodeGen::subLabelName(FuncInfo* f, const std::string& name)
	{
		if (f->decl->shortName.rfind("operator cast ", 0) != 0 || f->type == nullptr || f->decl->owner == nullptr)
			return name;
		return f->decl->owner->name + "::operator cast " + f->type->base->name();
	}

	std::vector<FuncInfo*> CodeGen::castOperators(StructInfo* info)
	{
		std::vector<FuncInfo*> out;
		resolveStruct(info, info->pos);
		std::vector<StructInfo*> chain;
		for (StructInfo* c = info; c != nullptr; c = c->base)
			chain.push_back(c);
		for (const auto& slot : info->ifaceSlots)
			chain.push_back(slot.iface);
		for (StructInfo* c : chain)
		{
			const std::string prefix = c->name + "::operator cast ";
			for (auto it = m_funcs.lower_bound(prefix); it != m_funcs.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
			{
				for (FuncInfo* f : it->second)
				{
					if (f->type == nullptr)
						resolveFunction(f);
					const bool hidden = std::any_of(out.begin(), out.end(), [&](FuncInfo* g) { return g->type->base == f->type->base; });
					if (!hidden)
						out.push_back(f);
				}
			}
		}
		return out;
	}

	// from as a `to`: a constructor of `to` taking it (implicit ones only, unless explicitToo), or a
	// conversion of from's struct (operator cast) giving `to` (or what converts to it). One step,
	// no chains.
	bool CodeGen::userConversion(Node* from, Type* to, UserConv& out, bool explicitToo, bool report)
	{
		Type* ft = from->type;
		if (m_noUserConv || ft == nullptr || to == nullptr || ft == to)
			return false;
		const bool fromStruct = ft->kind == Type::Kind::Struct, toStruct = to->kind == Type::Kind::Struct;
		if (!fromStruct && !toStruct)
			return false;
		struct Guard { bool& b; Guard(bool& x) : b(x) { b = true; } ~Guard() { b = false; } } guard { m_noUserConv };
		std::vector<UserConv> found;
		if (toStruct && !to->structInfo->onlyPointer())
		{
			resolveStruct(to->structInfo, from->pos);
			auto it = m_funcs.find(to->structInfo->name + "::new");
			for (size_t i = 0; it != m_funcs.end() && i < it->second.size(); i++)
			{
				FuncInfo* f = it->second[i];
				if (f->type == nullptr)
					resolveFunction(f);
				if (!explicitToo && (f->decl->value & 16384) == 0)
					continue;
				if (f->type->params.size() < 2 || f->requiredParams > 2 || f->type->variadic != 0)
					continue;
				const int c = conversionCost(from, f->type->params[1]);
				if (c >= 0)
					found.push_back({ f, true, 10 + c });
			}
		}
		if (fromStruct)
		{
			for (FuncInfo* g : castOperators(ft->structInfo))
			{
				if (!explicitToo && (g->decl->value & 16384) == 0)
					continue;
				Type* x = g->type->base;
				Node value;
				value.kind = NodeKind::TypeArg;
				value.type = x;
				const int c = x == to ? 0 : conversionCost(&value, to);
				if (c >= 0)
					found.push_back({ g, false, 10 + c });
			}
		}
		if (found.empty())
			return false;
		std::sort(found.begin(), found.end(), [](const UserConv& a, const UserConv& b) { return a.cost < b.cost; });
		if (found.size() > 1 && found[0].cost == found[1].cost)
		{
			if (report)
				error(from->pos, "ambiguous conversion of " + ft->name() + " to " + to->name() + ": " + found[0].f->name + " and " + found[1].f->name);
			return false;
		}
		out = found[0];
		return true;
	}

	// target becomes inner converted (a Construct node: the object built from it; or the call of
	// its operator cast).
	void CodeGen::applyConversion(Node* target, Node* inner, const UserConv& c)
	{
		const SourcePos pos = inner->pos;
		*target = Node();
		target->pos = pos;
		if (c.ctor)
		{
			target->kind = NodeKind::Construct;
			target->a = inner;
			target->func = c.f;
			target->methodClass = structOfMethod(c.f);
		}
		else
		{
			Node* callee = new Node();
			callee->kind = NodeKind::Member;
			callee->pos = pos;
			callee->namePos = pos;
			callee->a = inner;
			callee->name = c.f->decl->shortName;
			target->kind = NodeKind::Call;
			target->a = callee;
		}
		check(target);
	}

	// cast<T>(x) / static_cast<T>(x) with a struct (x's or T): its conversions (also the ones that
	// are not implicit; a constructor of T taking x). false: none (e is checked as before).
	bool CodeGen::castConversion(Node* e, Type* to)
	{
		Type* from = e->a->type;
		if (from == nullptr || from == to || (from->kind != Type::Kind::Struct && to->kind != Type::Kind::Struct))
			return false;
		UserConv c;
		if (!userConversion(e->a, to, c, true, true))
			return false;
		Node* conv = new Node();
		applyConversion(conv, e->a, c);
		if (conv->type == to)
			*e = *conv;
		else
		{
			e->a = conv;		// (an operator cast to a type that converts to T)
			check(e);
		}
		return true;
	}

	// A condition: a number or a pointer (a struct: its implicit conversion to bool).
	void CodeGen::checkCondition(Node* e)
	{
		check(e);
		Type* t = decay(e->type);
		if (t != nullptr && t->kind == Type::Kind::Struct)
			requireConvertible(e, m_types.boolType(), "as a condition");
		else if (t == nullptr || !t->isScalar())
			error(e->pos, "the condition must be an integer or a pointer");
	}

	// The zeroes, default values and tables of a new struct value at a.
	void CodeGen::storeDefaults(const Reg& a, Type* t)
	{
		zeroMemory(a, t->size());
		std::vector<std::pair<int32_t, const StructField*>> defaults;
		std::vector<std::pair<int32_t, std::string>> vptrs;
		collectDefaults(t, 0, defaults, &vptrs);
		for (const auto& d : defaults)
		{
			std::string val;
			if (d.second->init->kind == NodeKind::StrLit)
				val = "$" + stringLiteral(d.second->init->text);
			else if (d.second->init->kind == NodeKind::Ident && d.second->init->func != nullptr)
				val = "$" + d.second->init->func->label;
			else
				val = imm(constConvert(d.second->init->constValue, decay(d.second->init->type), d.second->type));
			Place p;
			p.base = a;
			p.ownBase = false;
			if (d.first != 0)
				p.offset = "dword " + imm(d.first);
			storePlace(p, val, d.second->type);
		}
		for (const auto& vp : vptrs)
			emit("omov *" + a + ", $" + vp.second + ", dword " + imm(vp.first));
	}

	// ===================================== Operators =====================================
	// An operator on a struct value (a + b, -a, a[i], a(x), a += b, ++a): the struct's
	// operator<op> sub (a method: on the first operand), or a sub operator<op> found from here or
	// from the operands' namespaces (the operands as its arguments). A pointer parameter (T*,
	// const T*) takes a struct operand by its address. e becomes the call. false: there is none.
	bool CodeGen::userOperator(Node* e, const std::string& op, const std::vector<Node*>& operands, bool membersOnly)
	{
		auto l_struct = [](Node* n) -> StructInfo* {
			Type* t = n->type;
			return (t != nullptr && t->kind == Type::Kind::Struct) ? t->structInfo : nullptr;
		};
		std::vector<FuncInfo*> members, frees;
		if (StructInfo* s = l_struct(operands[0]))
		{
			resolveStruct(s, e->pos);
			for (FuncInfo* f : methodsOf(s, op))
			{
				if ((f->decl->value & 8) == 0)
					members.push_back(f);
			}
		}
		if (!membersOnly)
		{
			// (from where the code is, and from each struct operand's namespace)
			std::vector<std::string> scopes;
			FuncInfo* cur = m_inline.empty() ? m_func : m_inline.back().func;
			auto l_up = [&](std::string s) {
				while (true)
				{
					if (std::find(scopes.begin(), scopes.end(), s) == scopes.end())
						scopes.push_back(s);
					if (s.empty())
						break;
					const size_t sep = s.rfind("::");
					s = sep == std::string::npos ? "" : s.substr(0, sep);
				}
			};
			l_up(cur != nullptr ? cur->decl->scope : "");
			for (Node* o : operands)
			{
				if (StructInfo* s = l_struct(o))
					l_up(s->decl->scope);
			}
			for (const std::string& s : scopes)
			{
				auto it = m_funcs.find(s.empty() ? op : s + "::" + op);
				if (it == m_funcs.end())
					continue;
				for (FuncInfo* f : it->second)
				{
					if ((f->decl->value & 4) == 0)
						frees.push_back(f);
				}
			}
		}
		if (members.empty() && frees.empty())
			return false;
		// The best one: each operand converted to its parameter (or its address taken).
		auto l_cost = [&](Node* arg, Type* param, bool& address) -> int {
			address = false;
			const int c = conversionCost(arg, param);
			if (c >= 0)
				return c;
			StructInfo* s = l_struct(arg);
			int depth = 0;
			if (s != nullptr && param->kind == Type::Kind::Pointer && param->base->kind == Type::Kind::Struct && derives(s, param->base->structInfo, nullptr, &depth))
			{
				const bool addressable = arg->lvalue || arg->kind == NodeKind::Call || arg->kind == NodeKind::Construct || (arg->kind == NodeKind::Ident && arg->var != nullptr);
				if (!addressable || (arg->readOnly && !param->constTarget))
					return -1;
				address = true;
				return depth;
			}
			// (a value its struct converts from: built, then passed by its address)
			UserConv u;
			if (param->kind == Type::Kind::Pointer && param->constTarget && param->base->kind == Type::Kind::Struct && userConversion(arg, param->base, u, false, false))
			{
				address = true;
				return u.cost;
			}
			return -1;
		};
		FuncInfo* best = nullptr;
		bool bestMember = false, ambiguous = false;
		std::vector<bool> bestAddress;
		int bestCost = 0x7FFFFFFF;
		auto l_try = [&](FuncInfo* f, bool member) {
			if (f->type == nullptr)
				resolveFunction(f);
			const auto& params = f->type->params;
			// (a method: params[0] is this, for operands[0])
			const size_t args = operands.size();
			if (params.size() < args || f->requiredParams > args || f->type->variadic != 0)
				return;
			int cost = 0;
			std::vector<bool> address(args, false);
			for (size_t i = 0; i < args; i++)
			{
				bool a = false;
				int c = 0;
				if (member && i == 0)
				{
					// this: a readonly operator for a constant, the other one preferred for a variable
					// (as methods: their this is a const T* / a T*)
					const bool readonlyOp = (f->decl->value & 64) != 0;
					if (operands[0]->readOnly && !readonlyOp)
						return;
					c = (readonlyOp && !operands[0]->readOnly) ? 1 : 0;
				}
				else
					c = l_cost(operands[i], params[i], a);
				if (c < 0)
					return;
				cost += c;
				address[i] = a;
			}
			if (cost < bestCost)
			{
				best = f;
				bestMember = member;
				bestAddress = address;
				bestCost = cost;
				ambiguous = false;
			}
			else if (cost == bestCost && best != f)
				ambiguous = true;
		};
		for (FuncInfo* f : members)
			l_try(f, true);
		for (FuncInfo* f : frees)
			l_try(f, false);
		std::string types;
		for (Node* o : operands)
			types += (types.empty() ? "" : ", ") + (o->type != nullptr ? o->type->name() : std::string("?"));
		if (best == nullptr)
		{
			std::string msg = "no " + op + " takes (" + types + "); candidates:";
			for (FuncInfo* f : members)
				msg += "\n    " + f->decl->name + f->type->name().substr(3) + " (" + posString(f->decl->pos) + ")";
			for (FuncInfo* f : frees)
				msg += "\n    " + f->decl->name + f->type->name().substr(3) + " (" + posString(f->decl->pos) + ")";
			error(e->pos, msg);
		}
		if (ambiguous)
			error(e->pos, "ambiguous " + op + " for (" + types + ")");
		// The call (each operand as it is, or its address).
		std::vector<Node*> args;
		for (size_t i = bestMember ? 1 : 0; i < operands.size(); i++)
		{
			Node* a = operands[i];
			Type* param = best->type->params[i];
			if (bestAddress[i] && (a->type == nullptr || a->type->kind != Type::Kind::Struct || !derives(a->type->structInfo, param->base->structInfo)))
			{
				UserConv u;
				userConversion(a, param->base, u, false, true);
				Node* conv = new Node();
				applyConversion(conv, a, u);
				a = conv;
			}
			if (bestAddress[i])
			{
				Node* addr = new Node();
				addr->kind = NodeKind::Unary;
				addr->name = "&";
				addr->pos = a->pos;
				addr->a = a;
				a = addr;
			}
			args.push_back(a);
		}
		Node* callee = new Node();
		callee->pos = e->pos;
		callee->namePos = e->pos;
		if (bestMember)
		{
			callee->kind = NodeKind::Member;
			callee->a = operands[0];
			callee->name = op;
		}
		else
		{
			callee->kind = NodeKind::Ident;
			callee->name = best->decl->name;
			callee->text = op;
			callee->resolved = true;
		}
		const SourcePos pos = e->pos;
		*e = Node();
		e->kind = NodeKind::Call;
		e->pos = pos;
		e->a = callee;
		e->list = args;
		check(e);
		return true;
	}

	// x-> with x a struct: its operator->, again while the result is a struct (C++ goes on the same
	// way), until a pointer.
	void CodeGen::arrowOperator(Node*& obj)
	{
		for (int depth = 0; obj->type != nullptr && obj->type->kind == Type::Kind::Struct; depth++)
		{
			if (depth >= 16)
				error(obj->pos, "operator-> goes on more than 16 times (a struct whose operator-> gives itself?)");
			Type* st = obj->type;
			Node* call = new Node();
			call->pos = obj->pos;
			if (!userOperator(call, "operator->", { obj }, true))
				error(obj->pos, "-> needs a pointer to a struct, or a struct with an operator-> (" + st->name() + " has none)");
			obj = call;
		}
	}

	// a OP b with a struct operand: its operator, or (none) != from ==, > / <= / >= from <.
	void CodeGen::binaryOperator(Node* e)
	{
		const std::string op = e->name;
		Node* a = e->a;
		Node* b = e->b;
		if (userOperator(e, "operator" + op, { a, b }))
			return;
		auto l_node = [&](NodeKind kind, const std::string& name, Node* x, Node* y) {
			Node* n = new Node();
			n->kind = kind;
			n->name = name;
			n->pos = e->pos;
			n->a = x;
			n->b = y;
			return n;
		};
		auto l_not = [&](Node* inner) {
			const SourcePos pos = e->pos;
			*e = Node();
			e->kind = NodeKind::Unary;
			e->name = "!";
			e->pos = pos;
			e->a = inner;
			check(e);
		};
		if (op == "!=")
		{
			Node* eq = l_node(NodeKind::Binary, "==", a, b);
			if (userOperator(eq, "operator==", { a, b }))
				return l_not(eq);
		}
		else if (op == ">" || op == "<=" || op == ">=")
		{
			// a > b: b < a; a <= b: !(b < a); a >= b: !(a < b)
			Node* x = op == ">=" ? a : b;
			Node* y = op == ">=" ? b : a;
			if (op == ">")
			{
				if (userOperator(e, "operator<", { x, y }))
					return;
			}
			else
			{
				Node* lt = l_node(NodeKind::Binary, "<", x, y);
				if (userOperator(lt, "operator<", { x, y }))
					return l_not(lt);
			}
		}
		error(e->pos, "no operator" + op + " for " + a->type->name() + " and " + b->type->name() + " (a sub operator" + op + "(...) in the struct, or a top-level one)" +
			(op == "!=" ? " (or operator==)" : op == ">" || op == "<=" || op == ">=" ? " (or operator<)" : ""));
	}

	// x.method(args) / p->method(args): Struct::method(&x / p, args) (the call is rewritten).
	void CodeGen::rewriteMethodCall(Node* e)
	{
		Node* callee = e->a;
		Node* obj = callee->a;
		if (obj->kind == NodeKind::Ident)
			resolveTemplateIdent(obj);		// Array<u32>.make(), T.make()
		// Enum.Member and Constant.field are not methods (a struct constant's are: on its read-only
		// copy; a const computed when the program runs is a variable).
		if (obj->kind == NodeKind::Ident && lookupVar(obj->name) == nullptr && m_enums.count(obj->name) > 0)
			return;
		if (obj->kind == NodeKind::Ident && lookupVar(obj->name) == nullptr && m_consts.count(obj->name) > 0)
		{
			decideConst(obj->name);
			const ConstInfo& c = m_consts[obj->name];
			if (!c.runtime && (c.type == nullptr || c.type->kind != Type::Kind::Struct))
				return;
		}
		auto l_isStatic = [&](const std::vector<FuncInfo*>& funcs) -> FuncInfo* {
			for (FuncInfo* f : funcs)
			{
				if ((f->decl->value & 8) != 0)
					return f;
			}
			return nullptr;
		};
		// Struct.name(args): a static sub of the struct (as Struct::name(args)).
		if (obj->kind == NodeKind::Ident && callee->value == 0 && lookupVar(obj->name) == nullptr && m_structs.count(obj->name) > 0)
		{
			StructInfo* info = m_structs[obj->name];
			if (m_funcs.count(obj->name + "::" + callee->name) == 0 && m_templates.count(obj->name + "::" + callee->name) > 0)
			{
				// A static method template: Struct.make<T>(...).
				for (Node* arg : e->list)
					check(arg);
				std::vector<Type*> explicitArgs;
				for (TypeSyntax* ta : callee->templateArgs)
				{
					resolveTypeNames(ta, m_func != nullptr ? m_func->decl->scope : std::string());
					explicitArgs.push_back(templateArgument(ta));
				}
				const std::string inst = instantiate(obj->name + "::" + callee->name, explicitArgs, &e->list, info, callee->namePos, false);
				callee->name = inst.substr(obj->name.size() + 2);
				callee->templateArgs.clear();
			}
			if (callee->name == "new" && (e->callFlags & 4) == 0)
			{
				// T.new(args): a T built by its constructor (a temporary, as a call's result). (As a
				// statement of a derived class's constructor, Base.new(...) builds its base's part.)
				if (m_options.index != nullptr)
					indexName(obj->pos, writtenName(obj), info->decl->namePos, hoverStruct(info));
				const SourcePos pos = e->pos;
				std::vector<Node*> args = e->list;
				*e = Node();
				e->kind = NodeKind::Construct;
				e->pos = pos;
				e->list = args;
				e->methodClass = info;
				return;
			}
			std::string method = obj->name + "::" + callee->name;
			resolveStruct(info, callee->namePos);
			const std::vector<FuncInfo*> funcs = methodsOf(info, callee->name);
			if (funcs.empty())
				error(callee->namePos, "struct " + obj->name + " has no static sub '" + callee->name + "'");
			FuncInfo* found = l_isStatic(funcs);
			VarInfo* self = lookupVar("this");
			if (found == nullptr && self != nullptr && self->type->kind == Type::Kind::Pointer && self->type->base->kind == Type::Kind::Struct
				&& self->type->base->structInfo != info && derives(self->type->base->structInfo, info))
			{
				// Base.m(args) in a sub of a class derived from Base (Base<T>.m(args) in a template):
				// Base's m on this (as Base::m(args)).
				if (m_options.index != nullptr)
					indexName(obj->pos, writtenName(obj), info->decl->namePos, hoverStruct(info));
				Node* thisNode = new Node();
				thisNode->kind = NodeKind::Ident;
				thisNode->name = "this";
				thisNode->pos = callee->namePos;
				thisNode->resolved = true;
				Node* id = new Node();
				id->kind = NodeKind::Ident;
				id->name = method;
				id->text = writtenName(obj) + "." + callee->name;
				id->pos = callee->namePos;
				id->resolved = true;
				id->callFlags = 8;
				e->a = id;
				e->list.insert(e->list.begin(), thisNode);
				return;
			}
			if (found == nullptr)
				error(callee->namePos, "'" + callee->name + "' is a method of " + obj->name + ", not a static sub: call it on a struct (x." +
					callee->name + "(...)), or as " + method + "(&x, ...)");
			method = found->decl->name;		// (maybe a class it extends)
			if (m_options.index != nullptr)
				indexName(obj->pos, writtenName(obj), info->decl->namePos, hoverStruct(info));
			Node* id = new Node();
			id->kind = NodeKind::Ident;
			id->name = method;
			id->text = callee->name;
			id->pos = callee->namePos;
			e->a = id;
			return;
		}
		check(obj);
		if (callee->value == 1)
		{
			arrowOperator(callee->a);		// (s->m() on a struct: its operator->)
			obj = callee->a;
		}
		Type* t = obj->type;
		if (t == nullptr)
			return;
		if ((callee->name == "new" || callee->name == "del") && inPlace(e, obj))
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
		if (m_funcs.count(info->name + "::" + callee->name) == 0 && m_templates.count(info->name + "::" + callee->name) > 0)
		{
			// A method template: its instance for the arguments (x.map<f32>(...), or deduced).
			for (Node* arg : e->list)
				check(arg);
			std::vector<Type*> explicitArgs;
			for (TypeSyntax* ta : callee->templateArgs)
			{
				resolveTypeNames(ta, m_func != nullptr ? m_func->decl->scope : std::string());
				explicitArgs.push_back(templateArgument(ta));
			}
			const std::string inst = instantiate(info->name + "::" + callee->name, explicitArgs, &e->list, info, callee->namePos, false);
			callee->name = inst.substr(info->name.size() + 2);
			callee->templateArgs.clear();
		}
		const std::string method = info->name + "::" + callee->name;
		const std::vector<FuncInfo*> funcs = methodsOf(info, callee->name);
		if (funcs.empty())
			error(callee->namePos, (info->anonymous ? info->displayName() : (info->isClass() ? "class " : info->isInterface() ? "interface " : "struct ") + info->name) +
				" has no field or method '" + callee->name + "'");
		if (FuncInfo* st = l_isStatic(funcs))
			error(callee->namePos, "'" + callee->name + "' is a static sub of " + structOfMethod(st)->name + " (it has no this): call it as " +
				info->name + "." + callee->name + "(...)");
		// (an object that is a variable has exactly its type: its subs are called directly)
		const bool exact = callee->value == 0 && obj->kind == NodeKind::Ident && obj->var != nullptr;
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
		id->isPrivate = callee->isPrivate;
		id->resolved = true;
		id->methodClass = info;
		id->callFlags = 1 | (exact ? 2 : 0);
		e->a = id;
		e->list.insert(e->list.begin(), self);
		// p->new(args) / x.new(args) written in the code (not a declaration's var x: T(args), whose
		// defaults are already there): the object's defaults and tables first, as for a new value.
		if (callee->name == "new" && !(obj->kind == NodeKind::Ident && obj->name == " ctor"))
			e->callFlags |= 64;
	}

	// p->new(args) / p->del() that the compiler does itself, for any type (so that a template can build
	// and destroy its T in memory it manages): new() for a type without constructors (its defaults; a
	// number, a pointer: 0), new(value) with a value of the type itself (a copy, moved if it is a call's
	// result: as a variable's initialization; a number, a pointer: what converts to it), and del() for a
	// type without a destructor (nothing; an array: its elements'). true: e became an InPlace. The others
	// (a constructor, a destructor of the struct's) are its method calls.
	bool CodeGen::inPlace(Node* e, Node* obj)
	{
		Node* callee = e->a;
		const bool isNew = callee->name == "new";
		if (!callee->templateArgs.empty() || (e->callFlags & 4) != 0 || (obj->kind == NodeKind::Ident && obj->name == " ctor"))
			return false;
		Type* t = decay(obj->type);
		Type* target = nullptr;
		if (callee->value == 1)
		{
			if (t->kind != Type::Kind::Pointer)
				return false;
			target = t->base;
		}
		else if (obj->type->kind == Type::Kind::Struct)
			target = obj->type;
		else
			return false;
		bool own = false;		// the struct's own new / del
		if (target->kind == Type::Kind::Struct)
		{
			resolveStruct(target->structInfo, callee->pos);
			if (target->structInfo->field(callee->name) != nullptr)
				return false;
			own = !methodsOf(target->structInfo, callee->name).empty();
		}
		const std::string written = (callee->value == 1 ? "p->" : "x.") + callee->name;
		if (target->kind == Type::Kind::Void)
			error(callee->namePos, written + "(...) on a void*: cast it to a pointer to the type first");
		if (target->isReference())
			error(callee->namePos, written + "(...) on a reference");
		for (Node* arg : e->list)
			check(arg);
		if (isNew)
		{
			const bool copy = e->list.size() == 1 && (target->kind == Type::Kind::Struct ? decay(e->list[0]->type) == target : target->kind != Type::Kind::Array);
			if (!copy)
			{
				if (own)
					return false;		// (its constructors)
				if (!e->list.empty())
					error(e->pos, target->kind == Type::Kind::Struct ? target->name() + " has no constructor (sub new(...)): " + written + "() gives its default values, " +
						written + "(value) copies a " + target->name() :
						written + "(...) on a " + target->name() + "*: " + written + "() is 0 (" + written + "(value): the value)");
			}
		}
		else
		{
			if (own)
				return false;		// (its destructor)
			if (!e->list.empty())
				error(e->pos, written + "() takes no arguments");
		}
		if ((callee->value == 1 && t->constTarget) || (callee->value == 0 && obj->readOnly))
			error(callee->namePos, written + "(): " + (callee->value == 1 ? "through a const " + t->name() : std::string("on a constant")) + " (it changes what is there)");
		Node* addr = obj;
		if (callee->value == 0)
		{
			addr = new Node();
			addr->kind = NodeKind::Unary;
			addr->name = "&";
			addr->pos = obj->pos;
			addr->a = obj;
		}
		const SourcePos pos = e->pos;
		const std::vector<Node*> args = e->list;
		*e = Node();
		e->kind = NodeKind::InPlace;
		e->pos = pos;
		e->a = addr;
		e->list = args;
		e->value = isNew ? 0 : 1;
		return true;
	}

	// take(p): the value p points at, moved out (its bytes: no copy constructor; p's memory is not
	// destroyed: what is there is no longer a value, as after a move). A struct: a temporary, as a call's
	// result (moved into a variable, destroyed when the statement ends if it isn't). false: not take (a
	// variable or a sub of that name).
	bool CodeGen::takeBuiltin(Node* e)
	{
		Node* callee = e->a;
		if (callee->kind != NodeKind::Ident || callee->name != "take" || !callee->templateArgs.empty() || lookupVar(callee->name) != nullptr
			|| m_funcs.count(callee->name) > 0 || m_templates.count(callee->name) > 0)
			return false;
		if (e->list.size() != 1)
			error(e->pos, "take(p) takes one pointer: the value it points at, moved out");
		Node* p = e->list[0];
		check(p);
		Type* t = decay(p->type);
		if (t->kind != Type::Kind::Pointer || t->base->kind == Type::Kind::Void || t->base->isReference())
			error(p->pos, "take(p) takes a pointer to a value (not " + t->name() + ")");
		if (t->constTarget)
			error(p->pos, "take(p) through a const " + t->name() + ": it moves the value out (what is left is no longer one)");
		if (t->base->kind == Type::Kind::Array)
			error(p->pos, "take(p): an array is not a value (take its elements)");
		const SourcePos pos = e->pos;
		*e = Node();
		e->pos = pos;
		e->a = p;
		if (t->base->kind == Type::Kind::Struct)
		{
			e->kind = NodeKind::Construct;
			e->value = 2;
			e->methodClass = t->base->structInfo;
		}
		else
		{
			e->kind = NodeKind::Unary;		// (a number, a pointer: its value)
			e->name = "*";
		}
		check(e);
		e->lvalue = false;
		return true;
	}

	// forget(v): v (a local, or an own parameter) is not destroyed when its scope ends: what it held
	// was moved somewhere else (its bytes copied: memcpy, p->new(take(&v))...). Anywhere in v's scope
	// (in an if, a loop...): v has a drop flag that its destructor checks (see setDropFlag). Nothing for
	// a type without a destructor. false: not forget (a variable or a sub of that name).
	bool CodeGen::forgetBuiltin(Node* e)
	{
		Node* callee = e->a;
		if (callee->kind != NodeKind::Ident || callee->name != "forget" || !callee->templateArgs.empty() || lookupVar(callee->name) != nullptr
			|| m_funcs.count(callee->name) > 0 || m_templates.count(callee->name) > 0)
			return false;
		if (e->list.size() != 1 || e->list[0]->kind != NodeKind::Ident)
			error(e->pos, "forget(v) takes a variable: a local, or an own parameter");
		Node* v = e->list[0];
		check(v);
		VarInfo* var = v->var;
		if (var == nullptr || var->isStatic || (var->storage != VarInfo::Storage::Local && var->storage != VarInfo::Storage::Param))
			error(v->pos, "forget(v) takes a local or an own parameter (not a global, a static or a sub)");
		if (var->param && !var->owned && var->type->kind == Type::Kind::Struct)
			error(v->pos, "forget(" + v->name + "): its caller destroys a parameter (forget takes a local, or an own parameter: var " + v->name + ": own " + var->type->name() + ")");
		if (var->type->isReference())
			error(v->pos, "forget(" + v->name + "): a reference doesn't own what it refers to");
		e->vaBuiltin = 4;
		e->func = nullptr;
		e->type = m_types.voidType();
		return true;
	}

	void CodeGen::genForget(Node* s)
	{
		VarInfo* var = s->a->list[0]->var;
		if (var->hasDropFlag)
			emit("omov *FP, 0x00000000, dword " + imm(var->dropFlag));		// (its destructor: skipped)
		else if (destructible(var->type))
			error(s->pos, "internal error: forget(" + var->name + ") without a drop flag");
	}

	// The names forget(name) takes in a sub's body: its variables of those names get a drop flag (a
	// variable of the same name elsewhere too: only a flag more).
	void CodeGen::collectForgets(Node* n, std::set<std::string>& out)
	{
		if (n == nullptr || n->kind == NodeKind::FuncDecl)
			return;
		if (n->kind == NodeKind::Call && n->a != nullptr && n->a->kind == NodeKind::Ident && n->a->name == "forget" && n->list.size() == 1 && n->list[0]->kind == NodeKind::Ident)
			out.insert(n->list[0]->name);
		for (Node* c : { n->a, n->b, n->c, n->d })
			collectForgets(c, out);
		for (Node* c : n->list)
			collectForgets(c, out);
	}

	// v's drop flag (if forget may take it): 1 from here on, 0 once forget(v) runs; its destructor
	// runs only while it is 1.
	void CodeGen::setDropFlag(VarInfo* v)
	{
		if (m_forgetNames.count(v->name) == 0 || !destructible(v->type))
			return;
		v->dropFlag = temporary(4);
		v->hasDropFlag = true;
		emit("omov *FP, 0x00000001, dword " + imm(v->dropFlag));
	}

	// The own parameters' destructors, when the sub ends (as locals': skipped by forget, a return moves them).
	void CodeGen::destroyOwnedParams(const std::vector<VarInfo*>& params)
	{
		for (VarInfo* v : params)
		{
			if (!v->owned || !destructible(v->type))
				continue;
			setDropFlag(v);
			Node* destroy = new Node();
			destroy->kind = NodeKind::Destroy;
			destroy->value = 1;
			destroy->pos = v->pos;
			destroy->var = v;
			m_defers.back().push_back({ destroy, m_scopes, m_atomic, v });
		}
	}

	void CodeGen::checkCall(Node* e)
	{
		implicitMember(e, true);			// name(...) in a struct's sub: maybe this->name(...)
		if (e->a->kind == NodeKind::Member)
			rewriteMethodCall(e);
		if (e->kind == NodeKind::Construct || e->kind == NodeKind::InPlace)
		{
			check(e);		// (T.new(args), p->new(...) / p->del() done in place)
			return;
		}
		Node* callee = e->a;
		e->func = nullptr;
		e->vaBuiltin = 0;
		e->vslot = -1;
		if (takeBuiltin(e) || forgetBuiltin(e))
			return;
		if (callee->kind == NodeKind::Ident && lookupVar(callee->name) == nullptr && m_funcs.count(callee->name) == 0
			&& (callee->name == "va_count" || callee->name == "va_start" || callee->name == "va_types"))
		{
			// A variadic sub's extra arguments: how many, where the first is, where their types are.
			FuncInfo* f = m_func;
			if (f == nullptr || f->type == nullptr || f->type->variadic == 0)
				error(e->pos, callee->name + "() is for subs with ... (the extra arguments of the sub it is written in)");
			if (callee->name == "va_types" && f->type->variadic != 2)
				error(e->pos, "va_types() is for subs with ...typed (the arguments' types are passed only then)");
			if (!e->list.empty())
				error(e->pos, callee->name + "() takes no arguments");
			e->vaBuiltin = callee->name == "va_count" ? 1 : callee->name == "va_start" ? 2 : 3;
			e->type = m_types.u32();
			return;
		}
		if (callee->kind == NodeKind::Ident && lookupVar(callee->name) == nullptr && m_funcs.count(callee->name) == 0 && m_templates.count(callee->name) > 0)
		{
			// A template's sub: its instance for the arguments (explicit max<u32>(...), the others
			// deduced from the arguments' types).
			for (Node* arg : e->list)
				check(arg);
			std::vector<Type*> explicitArgs;
			for (TypeSyntax* t : callee->templateArgs)
			{
				resolveTypeNames(t, m_func != nullptr ? m_func->decl->scope : std::string());
				explicitArgs.push_back(templateArgument(t));
			}
			callee->name = instantiate(callee->name, explicitArgs, &e->list, nullptr, callee->pos, false);
			callee->templateArgs.clear();
		}
		// A method: its struct's (and those it inherits); X::m(...) with X a class: also what X
		// inherits.
		std::vector<FuncInfo*> methodCands;
		StructInfo* qualifier = nullptr;
		if (callee->kind == NodeKind::Ident && lookupVar(callee->name) == nullptr)
		{
			std::string member;
			if (callee->methodClass != nullptr)
				methodCands = methodsOf(callee->methodClass, callee->name.substr(callee->methodClass->name.size() + 2));
			else if ((qualifier = qualifierStruct(callee->name, &member)) != nullptr && qualifier->kind != 0)
			{
				resolveStruct(qualifier, callee->pos);
				methodCands = methodsOf(qualifier, member);
			}
		}
		if (callee->kind == NodeKind::Ident && lookupVar(callee->name) == nullptr && (m_funcs.count(callee->name) > 0 || !methodCands.empty()))
		{
			const std::vector<FuncInfo*>& cands = !methodCands.empty() ? methodCands : m_funcs[callee->name];
			// The subs get their types after the declarations (constants, enum values, array
			// sizes, default values): a call there is not a constant.
			for (FuncInfo* f : cands)
			{
				// (constexpr / consteval subs: their types now, they can be evaluated)
				if (f->type == nullptr && f->decl->constKind != 0 && !f->instance && !f->resolving)
				{
					f->resolving = true;
					try { resolveFunction(f); }
					catch (CompileError&) { f->resolving = false; f->type = nullptr; f->paramNames.clear(); f->defaults.clear(); f->requiredParams = 0; throw; }
					f->resolving = false;
				}
				if (f->type == nullptr)
					error(e->pos, "a sub call is not a constant");
			}
			for (Node* arg : e->list)
				check(arg);
			for (FuncInfo* g : cands)
			{
				if (g->broken)
					throw g->brokenError;		// (a template instance whose signature doesn't work for its types)
			}
			// X::m(args) in a sub of X (or of a class derived from it): on this, if m is a method
			// (a class's subs: tried first; a struct's: when the arguments are not (this, ...)).
			if (qualifier == nullptr)
				qualifier = qualifierStruct(callee->name);
			VarInfo* self = lookupVar("this");
			if (qualifier != nullptr && callee->methodClass == nullptr && (callee->callFlags & 8) == 0 && self != nullptr && self->type->kind == Type::Kind::Pointer
				&& self->type->base->kind == Type::Kind::Struct && derives(self->type->base->structInfo, qualifier))
			{
				bool ambiguous = false;
				const bool asWritten = !self->type->base->structInfo->isClass() && bestOverload(cands, e->list, ambiguous) != nullptr;
				if (!asWritten)
				{
					Node* thisNode = new Node();
					thisNode->kind = NodeKind::Ident;
					thisNode->name = "this";
					thisNode->pos = callee->pos;
					thisNode->resolved = true;
					check(thisNode);
					std::vector<Node*> withThis = e->list;
					withThis.insert(withThis.begin(), thisNode);
					FuncInfo* g = bestOverload(cands, withThis, ambiguous);
					if (g != nullptr && (g->decl->value & 12) == 4)
					{
						e->list = withThis;
						callee->callFlags |= 8;
					}
				}
			}
			FuncInfo* f = resolveOverload(callee->name, e->list, e->pos, &cands);
			markUsed(f);
			// (A for-in may use a range's priv begin / end / next / get: its cursors only come from
			// begin and next, so an unchecked get is safe there.)
			if (!callee->isPrivate)
				checkSubAccess(f, callee->pos);
			StructInfo* owner = structOfMethod(f);
			if (owner != nullptr && f->decl->shortName == "new")
			{
				// Base::new(...): the base's part of the object, in the constructor of a class derived from it.
				StructInfo* building = (self != nullptr && self->type->kind == Type::Kind::Pointer && self->type->base->kind == Type::Kind::Struct) ? self->type->base->structInfo : nullptr;
				const bool baseCall = (callee->callFlags & 8) != 0 && building != nullptr && building != owner;
				if (baseCall && (e->callFlags & 4) == 0)
					error(e->pos, owner->name + "::new(...) builds the " + owner->name + " part of a " + building->name + ": only as a statement of " + building->name +
						"'s constructor (sub new), and only for the class it extends");
				if (owner->isAbstract && !baseCall)
					error(e->pos, owner->name + " is an abstract class: it is not built (a class derived from it is, its constructor calls " + owner->name + "::new(...))");
			}
			// A virtual sub called on an object: through its table (the object may be of a class
			// derived from its type), unless its type is exact (a variable) or nothing overrides it.
			if ((callee->callFlags & 1) != 0 && isVirtualSub(f) && (callee->callFlags & 2) == 0 && (f->decl->value & 4096) == 0
				&& !(callee->methodClass != nullptr && callee->methodClass->isFinal))
			{
				e->vslot = slotOf(owner, f);
				if (e->vslot < 0)
					error(e->pos, "internal error: no slot for " + f->name);
			}
			else if ((callee->callFlags & 1) != 0 && owner != nullptr && f->decl->shortName == "del" && owner->dynamic && (callee->callFlags & 2) == 0
				&& !(callee->methodClass != nullptr && callee->methodClass->isFinal))
				e->vslot = -2;		// (p->del(): the object's own destructor, from its table's destructor slot)
			else if ((f->decl->value & 1024) != 0)
				error(callee->pos, "'" + f->name + "' is pure: it has no body (a class derived from " + owner->name + " gives it one; call it on an object: p->" +
					f->decl->shortName + "(...))");
			if (f->interrupt)
				error(e->pos, "interrupt subs cannot be called (they end with rti)");
			for (size_t i = 0; i < e->list.size(); i++)
			{
				if (i >= f->type->params.size())
				{
					checkVarArg(e->list[i]);
					continue;
				}
				check(e->list[i], f->type->params[i]);
				requireConvertible(e->list[i], f->type->params[i], "for parameter '" + f->paramNames[i] + "' of " + f->name);
			}
			e->func = f;
			e->type = f->type->base;
			if (m_options.index != nullptr)
				indexName(callee->pos, callee->text.empty() ? callee->name : callee->text, f->decl->namePos, hoverFunc(f));
			bool references = f->type->base->isReference();
			for (Type* p : f->type->params)
				references = references || p->isReference();
			if (e->vslot == -1 && !destructible(e->type) && !references)
				ctFold(e, f);		// constexpr / consteval: its value, if it can be computed now
			dereferenceResult(e);
			return;
		}
		check(callee);
		Type* t = callee->type;
		if (t != nullptr && t->kind == Type::Kind::Struct)
		{
			// a(args) on a struct: operator()
			std::vector<Node*> operands { callee };
			for (Node* arg : e->list)
			{
				check(arg);
				operands.push_back(arg);
			}
			if (!userOperator(e, "operator()", operands, true))
				error(e->pos, "no operator() for " + t->name() + " (a sub operator()(...) in the struct)");
			return;
		}
		if (t != nullptr && t->kind == Type::Kind::Pointer && t->base->isFunction())
			t = t->base;
		if (t == nullptr || !t->isFunction())
			error(callee->pos, "this is not a sub (or a sub pointer)");
		if (t->variadic != 0 && e->list.size() < t->params.size())
			error(e->pos, "this sub takes at least " + std::to_string(t->params.size()) + " arguments, not " + std::to_string(e->list.size()));
		if (t->variadic == 0 && e->list.size() != t->params.size())
			error(e->pos, "this sub takes " + std::to_string(t->params.size()) + " arguments, not " + std::to_string(e->list.size()));
		for (size_t i = 0; i < e->list.size(); i++)
		{
			if (i >= t->params.size())
			{
				checkVarArg(e->list[i]);
				continue;
			}
			check(e->list[i], t->params[i]);
			requireConvertible(e->list[i], t->params[i], "for argument " + std::to_string(i + 1));
		}
		e->type = t->base;
		dereferenceResult(e);
	}

	// An argument for ...: any scalar (one 32-bit slot), arrays as pointers.
	void CodeGen::checkVarArg(Node* arg)
	{
		check(arg);
		Type* t = decay(arg->type);
		if (t == nullptr)
			error(arg->pos, "an overloaded sub as an argument for ...: cast it to the sub type it should be");
		if (t->kind == Type::Kind::Struct)
			error(arg->pos, "structs are passed by pointer, also to ... (&value)");
		if (!t->isScalar())
			error(arg->pos, "this cannot be an argument for ... (" + t->name() + ")");
	}

	// The slot an argument for ... is pushed as: integers smaller than 32 bits (and bool) widened
	// to i32 / u32, as they would be in arithmetic.
	Type* CodeGen::varArgType(Type* t)
	{
		t = decay(t);
		if (t->kind == Type::Kind::Null)
			return m_types.pointer(m_types.voidType());
		if (t->isInteger() && t->kind != Type::Kind::U32 && t->kind != Type::Kind::I32)
			return t->isSigned() ? m_types.i32() : m_types.u32();
		return t;
	}

	// ...typed: the code each argument's type is passed with (the SDK's dr::VaTypes).
	uint32_t CodeGen::varArgTypeCode(Type* t)
	{
		t = decay(t);
		switch (t->kind)
		{
			case Type::Kind::Bool: return 1;
			case Type::Kind::I8: return 2;
			case Type::Kind::U8: return 3;
			case Type::Kind::I16: return 4;
			case Type::Kind::U16: return 5;
			case Type::Kind::I32: return 6;
			case Type::Kind::U32: return 7;
			case Type::Kind::F32: return 8;
			case Type::Kind::Pointer:
				if (t->base->kind == Type::Kind::U8 || t->base->kind == Type::Kind::I8)
					return 10;		// a string (u8*, i8*)
				return 9;
			case Type::Kind::Null: return 9;
			case Type::Kind::Function: return 11;
			default: return 0;
		}
	}

	void CodeGen::checkMember(Node* e, bool placeWanted)
	{
		// Enum.Member
		if (e->value == 0 && e->a->kind == NodeKind::Ident && lookupVar(e->a->name) == nullptr && m_enums.count(e->a->name) > 0)
		{
			EnumInfo& info = m_enums[e->a->name];
			resolveEnum(info);
			if (m_options.index != nullptr)
				indexName(e->a->pos, writtenName(e->a), info.decl->namePos, hoverEnum(e->a->name));
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
			e->type = enumType(info);
			e->isConst = true;
			e->constValue = info.values[e->name];
			return;
		}
		// Struct.NAME: a constant of the struct (constexpr NAME = ... in it).
		if (e->value == 0 && e->a->kind == NodeKind::Ident && lookupVar(e->a->name) == nullptr)
		{
			resolveTemplateIdent(e->a);
			std::string full = e->a->name + "::" + e->name;
			if (m_structs.count(e->a->name) > 0 && m_structs[e->a->name]->kind != 0)
			{
				resolveStruct(m_structs[e->a->name], e->pos);
				const std::string inherited = memberName(m_structs[e->a->name], e->name);
				if (!inherited.empty())
					full = inherited;
			}
			if (m_structs.count(e->a->name) > 0 && (m_consts.count(full) > 0 || (m_globals.count(full) > 0 && m_globals[full]->decl != nullptr && m_globals[full]->decl->owner != nullptr)))
			{
				if (m_options.index != nullptr)
					indexName(e->a->pos, writtenName(e->a), m_structs[e->a->name]->decl->namePos, hoverStruct(m_structs[e->a->name]));
				e->kind = NodeKind::Ident;
				e->name = full;
				e->text = writtenName(e->a) + "." + shownName(e->name).substr(shownName(e->name).rfind(':') + 1);
				e->pos = e->namePos;
				e->resolved = true;
				e->a = nullptr;
				checkIdent(e, nullptr);
				return;
			}
		}
		// Constant.field: the field's value (its place in the constant's copy for &).
		if (e->value == 0 && !placeWanted)
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
				checkFieldAccess(st->structInfo, f, e->namePos);
				if (f->type->kind == Type::Kind::Struct)
					goto memberOfValue;		// (in the constant's read-only copy)
				e->type = f->type;
				e->constValue = c->fields[path + e->name];
				if (f->type->isInteger())
					e->isConst = true;
				else
					e->constPtr = true;	// pointers and sub pointers
				return;
			}
		}
	memberOfValue:
		check(e->a);
		if (e->value == 1)
			arrowOperator(e->a);		// (s->x on a struct: its operator->)
		Type* t = e->a->type;
		if (t == nullptr)
			error(e->a->pos, "not a struct");
		bool throughConst = false;
		if (e->value == 1)
		{
			t = decay(t);
			if (t->kind != Type::Kind::Pointer || t->base->kind != Type::Kind::Struct)
				error(e->pos, "-> needs a pointer to a struct (found " + t->name() + ")");
			throughConst = t->constTarget;
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
		const std::string staticName = memberName(t->structInfo, e->name);
		if (f == nullptr && !staticName.empty())
		{
			// x.NAME / p->NAME: a constant or a static field of its struct (x is only its type).
			const std::string written = e->name;
			e->kind = NodeKind::Ident;
			e->name = staticName;
			e->text = written;
			e->pos = e->namePos;
			e->resolved = true;
			e->a = nullptr;
			e->value = 0;
			checkIdent(e, nullptr);
			return;
		}
		if (f == nullptr)
			error(e->namePos, (t->structInfo->anonymous ? t->structInfo->displayName() : "struct " + t->structInfo->name) + " has no field '" + e->name + "'");
		if (m_options.index != nullptr && !m_discarding)
			indexName(e->namePos, e->name, f->namePos, hoverField(t->structInfo, *f));
		checkFieldAccess(t->structInfo, f, e->namePos);
		e->type = f->type;
		e->lvalue = (e->value == 1) || e->a->lvalue;
		e->readOnly = (e->value == 0) && e->a->readOnly;
		if (e->readOnly)
			e->readOnlyWhy = e->a->readOnlyWhy;
		const StructInfo* fieldOwner = f->owner != nullptr ? f->owner : t->structInfo;
		if (f->isReadonly && !insideStruct(fieldOwner))
		{
			e->readOnly = true;
			e->readOnlyWhy = "'" + e->name + "' is readonly: only the subs of " + fieldOwner->name + " can change it";
		}
		if (throughConst)
		{
			e->readOnly = true;
			e->readOnlyWhy = "'" + e->name + "' is reached through a const pointer (" + decay(e->a->type)->name() + "): it cannot be changed through it" +
				(e->a->kind == NodeKind::Ident && e->a->name == "this" ? " (this sub is readonly)" : "");
		}
		// A const field: set when the struct is built (its default, { ... }, this->field in new).
		if (f->isConst && !(constructing(fieldOwner) && e->value == 1 && e->a->kind == NodeKind::Ident && e->a->name == "this"))
		{
			e->readOnly = true;
			e->readOnlyWhy = "'" + e->name + "' is a const field of " + fieldOwner->name + ": it gets its value when the struct is built "
				"(its default value, { ... }, or this->" + e->name + " in new), then never changes";
		}
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

	int CodeGen::sourceIndex(const std::string& file)
	{
		auto it = m_sourceIndex.find(file);
		if (it != m_sourceIndex.end())
			return it->second;
		const int index = (int)m_sourcePaths.size();
		m_sourceIndex[file] = index;
		m_sourcePaths.push_back(file);
		return index;
	}

	void CodeGen::loc(const SourcePos& pos)
	{
		if (pos.file.empty() || pos.line <= 0)
			return;
		loc(Loc { sourceIndex(pos.file), pos.line, (int)m_inline.size() });
	}

	void CodeGen::loc(const Loc& l, bool statement)
	{
		if (m_out == nullptr || l.file < 0)
			return;
		m_out->push_back(".loc " + std::to_string(l.file) + " " + std::to_string(l.line) + " " + std::to_string(l.depth) + (statement ? "" : " 0"));
		m_loc = l;
	}

	void CodeGen::noLoc(void)
	{
		if (m_out == nullptr)
			return;
		m_out->push_back(".loc -");
		m_loc = Loc {};
	}

	std::string CodeGen::newLabel(void)
	{
		return "__L" + std::to_string(m_labelCounter++);
	}

	CodeGen::Reg CodeGen::alloc(const SourcePos& pos)
	{
		if (m_regDepth >= m_regLimit)
		{
			if (m_optPass == 2)
				throw OptRetry {};
			error(pos, "expression too complex (more than 16 temporary values): split it");
		}
		m_regDepth++;
		m_optMaxDepth = std::max(m_optMaxDepth, m_regDepth);
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
		if (from == to || from == nullptr)
			return;
		int64_t delta = 0;
		if (pointerAdjust(from, to, delta))
		{
			// A class pointer as a pointer to a class it extends / an interface (or back, for
			// static_cast): where that part of the object is (null stays null).
			if (delta != 0)
			{
				const std::string l = newLabel();
				emit("mov ACC, " + r);
				emit("jeq $" + l + ", 0");
				addImmediate(r, delta);
				label(l);
			}
			return;
		}
		if (to->isFloat())
		{
			if (from->isInteger())
				emit((from->kind == Type::Kind::U32 ? "utof " : "itof ") + r);
			return;
		}
		if (from->isFloat() && to->isInteger())
		{
			if (to->kind == Type::Kind::Bool)
			{
				// f != 0.0 (also for -0.0)
				emit("feq " + r + ", 0x00000000");
				emit("mov " + r + ", ACC");
				emit("xorip " + r + ", 1");
				return;
			}
			emit((isUnsignedKind(to) ? "ftou " : "ftoi ") + r);
			if (to->size() < 4)
				normalize(r, to);
			return;
		}
		if (!to->isInteger())
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
			default: copyValue(addr, value, t); break;
		}
	}

	// for (auto x in range) body -> a for loop, by the range's type:
	//   a..b      { var __e = b; for (var x: T = a; x < __e; x++) body }   (T: the integer type of a and b)
	//   T[N]      { var __a = range; for (var __i: u32 = 0; __i < N; __i++) { auto x = __a[__i]; body } }
	//   a struct (or a pointer to one) with begin() / end() / next(c) / get(c):
	//             { var __r = &range; var __end = __r->end();
	//               for (var __c = __r->begin(); __c != __end; __c = __r->next(__c)) { auto x = *__r->get(__c); body } }
	//             (a cursor that points at the elements: next is c + 1, get is c, if the struct has none)
	// auto& x / var x: T* (T the element's type): x = the element's address instead of its value.
	Node* CodeGen::forInLoop(Node* s)
	{
		if (!s->list.empty())
			return s->list[0];
		Node* var = s->a;
		const SourcePos pos = s->pos;
		const std::string tag = "__forin" + s->text + "_";
		auto l_node = [&](NodeKind kind) { Node* n = new Node(); n->kind = kind; n->pos = pos; return n; };
		auto l_ident = [&](const std::string& name) { Node* n = l_node(NodeKind::Ident); n->name = name; n->resolved = true; return n; };
		auto l_unary = [&](const char* op, Node* a) { Node* n = l_node(NodeKind::Unary); n->name = op; n->a = a; return n; };
		auto l_binary = [&](const char* op, Node* a, Node* b) { Node* n = l_node(NodeKind::Binary); n->name = op; n->a = a; n->b = b; return n; };
		auto l_decl = [&](const std::string& name, TypeSyntax* type, Node* value) {
			Node* n = l_node(NodeKind::VarDecl);
			n->name = name;
			n->namePos = (name == var->name) ? var->namePos : pos;
			n->typeSyntax = type;
			n->a = value;
			return n;
		};
		auto l_method = [&](Node* object, const std::string& name, std::vector<Node*> args) {
			Node* member = l_node(NodeKind::Member);
			member->value = 1;		// ->
			member->isPrivate = true;	// (the loop's own call: begin / end / next / get may be priv)
			member->a = object;
			member->name = name;
			member->namePos = s->b->pos;
			Node* call = l_node(NodeKind::Call);
			call->a = member;
			call->list = args;
			return call;
		};
		auto l_named = [&](Type* t) { TypeSyntax* ts = new TypeSyntax(); ts->pos = pos; ts->name = t->name(); ts->resolved = true; return ts; };
		// The body, after the variable's declaration (its own block: the variable is new each time).
		auto l_body = [&](Node* first) {
			Node* b = l_node(NodeKind::Block);
			b->endPos = (s->c->kind == NodeKind::Block) ? s->c->endPos : pos;
			b->list = { first, s->c };
			return b;
		};
		// x from the element's address: its value, the address (auto&, var x: T*), or a reference
		// to the element (ref x, const ref x, var x: ref T).
		auto l_element = [&](Node* address, Type* element) {
			bool pointer = (s->value == 1);
			if (var->typeSyntax != nullptr)
			{
				Type* t = resolveType(var->typeSyntax);
				pointer = (t->kind == Type::Kind::Pointer && !t->isReference() && t->base == element && element->kind != Type::Kind::Void);
			}
			Node* d = l_decl(var->name, var->typeSyntax, pointer ? address : l_unary("*", address));
			d->refKind = var->refKind;
			return d;
		};
		Node* outer = l_node(NodeKind::Block);
		outer->endPos = (s->c->kind == NodeKind::Block) ? s->c->endPos : pos;
		Node* loop = l_node(NodeKind::For);
		if (s->d != nullptr)
		{
			// a..b: integers; the variable's type from both (the one of a typed side, else i32).
			check(s->b);
			check(s->d);
			Type* ta = decay(s->b->type);
			Type* tb = decay(s->d->type);
			if (ta == nullptr || tb == nullptr || !ta->isInteger() || !tb->isInteger() || ta->kind == Type::Kind::Bool || tb->kind == Type::Kind::Bool)
				error(s->b->pos, "a range of numbers (a..b) needs integers");
			const bool litA = s->b->kind == NodeKind::IntLit && s->b->name != "u", litB = s->d->kind == NodeKind::IntLit && s->d->name != "u";
			Type* t = (litA && !litB) ? tb : (litB && !litA) ? ta : (ta == tb) ? ta
				: (ta->kind == Type::Kind::U32 || tb->kind == Type::Kind::U32) ? m_types.u32() : m_types.i32();
			if (litA && litB)
				t = m_types.i32();
			outer->list.push_back(l_decl(tag + "end", l_named(t), s->d));
			loop->a = l_decl(var->name, var->typeSyntax != nullptr ? var->typeSyntax : l_named(t), s->b);
			loop->b = l_binary("<", l_ident(var->name), l_ident(tag + "end"));
			loop->c = l_node(NodeKind::PostInc);
			loop->c->a = l_ident(var->name);
			loop->d = s->c;
		}
		else
		{
			check(s->b);
			Type* rt = s->b->type;
			if (rt != nullptr && rt->kind == Type::Kind::Array)
			{
				if (rt->count == 0)
					error(s->b->pos, "for-in over an array needs its size");
				outer->list.push_back(l_decl(tag + "a", nullptr, s->b));		// (decays: a pointer to the first element)
				loop->a = l_decl(tag + "i", l_named(m_types.u32()), l_node(NodeKind::IntLit));
				Node* count = l_node(NodeKind::IntLit);
				count->value = rt->count;
				count->name = "u";
				loop->b = l_binary("<", l_ident(tag + "i"), count);
				loop->c = l_node(NodeKind::PostInc);
				loop->c->a = l_ident(tag + "i");
				Node* index = l_node(NodeKind::Index);
				index->a = l_ident(tag + "a");
				index->b = l_ident(tag + "i");
				loop->d = l_body(l_element(l_unary("&", index), rt->base));
			}
			else
			{
				Type* t = (rt != nullptr) ? decay(rt) : nullptr;
				const bool viaPointer = t != nullptr && t->kind == Type::Kind::Pointer && t->base != nullptr && t->base->kind == Type::Kind::Struct;
				Type* st = viaPointer ? t->base : t;
				if (st == nullptr || st->kind != Type::Kind::Struct)
					error(s->b->pos, "for-in needs a range: an array, a struct (or a pointer to one) with begin() and end() (and next(c), get(c)), "
						"or numbers: a..b" + std::string(rt != nullptr ? " (this is " + rt->name() + ")" : ""));
				StructInfo* info = st->structInfo;
				resolveStruct(info, s->b->pos);
				const std::string who = info->anonymous ? info->displayName() : info->name;
				auto l_find = [&](const std::string& method) -> FuncInfo* {
					const std::vector<FuncInfo*> funcs = methodsOf(info, method);		// (a class: also those it inherits)
					return funcs.empty() ? nullptr : funcs[0];
				};
				FuncInfo* fBegin = l_find("begin");
				FuncInfo* fEnd = l_find("end");
				FuncInfo* fNext = l_find("next");
				FuncInfo* fGet = l_find("get");
				if (fBegin == nullptr || fEnd == nullptr)
					error(s->b->pos, who + " is not a range for for-in: it needs begin() -> C and end() -> C (the cursors of the first element and of one past "
						"the last), and next(c: C) -> C and get(c: C) -> T* or ref T (unless C points at the elements)");
				Type* cursor = fBegin->type->base;
				if (cursor == nullptr || !cursor->isScalar() || cursor->kind == Type::Kind::Void)
					error(s->b->pos, who + "::begin() must return a cursor (an integer or a pointer), not " + (cursor ? cursor->name() : "void"));
				if (fGet != nullptr)
				{
					// get(c): the overload taking the cursor (a map's get(key) is another).
					for (FuncInfo* g : methodsOf(info, "get"))
					{
						const size_t self = (g->decl->value & 8) != 0 ? 0 : 1;
						if (g->type != nullptr && g->type->params.size() == self + 1 && g->type->params[self] == cursor)
						{
							fGet = g;
							break;
						}
					}
				}
				if (fEnd->type->base != cursor)
					error(s->b->pos, who + "::end() must return the cursor type of begin() (" + cursor->name() + "), not " + fEnd->type->base->name());
				if ((fNext == nullptr || fGet == nullptr) && cursor->kind != Type::Kind::Pointer)
					error(s->b->pos, who + " needs " + std::string(fNext == nullptr ? "next(c: " : "get(c: ") + cursor->name() + ")" +
						(fNext == nullptr ? " -> " + cursor->name() : " -> T* (or ref T)") + " for for-in (only a cursor that points at the elements can do without)");
				if (fNext != nullptr && fNext->type->base != cursor)
					error(s->b->pos, who + "::next() must return the cursor type of begin() (" + cursor->name() + ")");
				Type* element = (fGet != nullptr) ? fGet->type->base : cursor;
				if (element == nullptr || element->kind != Type::Kind::Pointer)
					error(s->b->pos, who + "::get() must return the element: a pointer to it (T*) or a reference (ref T, const ref T), not " + (element ? element->name() : "void"));
				const bool refGet = fGet != nullptr && element->isReference();		// (get(c) is the element: &get(c) its address)
				if (!viaPointer && !s->b->lvalue)
					error(s->b->pos, "the range of a for-in must be a variable (or a pointer to one)");
				outer->list.push_back(l_decl(tag + "r", nullptr, viaPointer ? s->b : l_unary("&", s->b)));
				outer->list.push_back(l_decl(tag + "end", nullptr, l_method(l_ident(tag + "r"), "end", {})));
				loop->a = l_decl(tag + "c", nullptr, l_method(l_ident(tag + "r"), "begin", {}));
				loop->b = l_binary("!=", l_ident(tag + "c"), l_ident(tag + "end"));
				Node* step = l_node(NodeKind::Assign);
				step->name = "=";
				step->a = l_ident(tag + "c");
				if (fNext != nullptr)
					step->b = l_method(l_ident(tag + "r"), "next", { l_ident(tag + "c") });
				else
				{
					Node* one = l_node(NodeKind::IntLit);
					one->value = 1;
					step->b = l_binary("+", l_ident(tag + "c"), one);
				}
				loop->c = step;
				Node* address = (fGet != nullptr) ? l_method(l_ident(tag + "r"), "get", { l_ident(tag + "c") }) : l_ident(tag + "c");
				if (refGet)
					address = l_unary("&", address);
				loop->d = l_body(l_element(address, element->base));
			}
		}
		outer->list.push_back(loop);
		s->list.push_back(outer);
		return outer;
	}

	// ======================================= Templates ======================================
	// template(T1, optional T2) where (condition) { structs and subs }: each struct / sub is an
	// entity, instantiated on use (Array<u32> as a type, max<u32>(...) or max(a, b) with its types
	// deduced, x.map<f32>(...)): its tokens parsed again, the parameters standing for types (an
	// optional one not given: m_notGiven, an error where it is used), its name Array<u32>. Its
	// declarations are then registered like the program's; its subs are generated if used.

	void CodeGen::registerTemplate(Node* d)
	{
		TemplateDef* def = d->tmpl;
		if (m_options.index != nullptr && d->external == 0)
		{
			for (size_t i = 0; i < def->params.size(); i++)
			{
				const SourcePos p = templateParamPos(def, def->params[i]);
				if (!p.file.empty())
					indexName(p, def->params[i], p, def->params[i] + comment({ std::string(def->paramTypes[i] != nullptr ? "template value parameter" : "template parameter") +
						(i >= def->required ? " (optional)" : "") }));
			}
		}
		if (def->where != nullptr)
			resolveNames(def->where, def->scope);
		for (size_t i = 0; i < def->params.size(); i++)
		{
			resolveTypeNames(def->paramTypes[i], def->scope);
			resolveTypeNames(def->defaultTypes[i], def->scope);
			resolveNames(def->defaultValues[i], def->scope);
		}
		for (TemplateEntity& e : def->entities)
		{
			for (Guard& g : e.guards)
				resolveNames(g.condition, def->scope);
		}
		for (TemplateAssert& a : def->asserts)
		{
			resolveNames(a.condition, def->scope);
			for (Guard& g : a.guards)
				resolveNames(g.condition, def->scope);
		}
		for (size_t i = 0; i < def->entities.size(); i++)
		{
			const TemplateEntity& e = def->entities[i];
			const std::string full = def->scope.empty() ? e.name : def->scope + "::" + e.name;
			auto& list = m_templates[full];
			if (list.empty())
				declareName(full, def->pos, "a template");
			else if (list[0].def->entities[list[0].entity].isStruct != e.isStruct)
				error(def->pos, "'" + full + "' is a template struct and a template sub: one name, one kind");
			list.push_back({ def, i });
			m_templateShortNames.insert(e.name);
		}
	}

	// ================================ Templates in analysis ================================
	// The language server: a template's parameters (their declaration, their uses in instances),
	// and the templates nothing instantiates, analyzed through an instance with stand-in arguments.

	// Where a template's parameter is declared (in its head), or an empty position.
	static SourcePos templateParamPos(const TemplateDef* def, const std::string& name)
	{
		const auto& src = def->source;
		size_t i = 0;
		while (i < src.size() && !(src[i].kind == TokenKind::Punct && src[i].text == "("))
			i++;
		int depth = 0;
		bool expectName = true;
		for (; i < src.size(); i++)
		{
			const Token& t = src[i];
			if (t.kind == TokenKind::Punct && (t.text == "(" || t.text == "<" || t.text == "["))
			{
				if (++depth == 1)
					expectName = true;
				continue;
			}
			if (t.kind == TokenKind::Punct && (t.text == ")" || t.text == ">" || t.text == "]"))
			{
				if (--depth == 0)
					break;
				continue;
			}
			if (depth == 1 && t.kind == TokenKind::Punct && t.text == ",")
			{
				expectName = true;
				continue;
			}
			if (depth == 1 && expectName && t.kind == TokenKind::Identifier)
			{
				if (t.text == "optional")
					continue;
				if (t.text == name)
					return t.pos;
				expectName = false;
			}
		}
		return {};
	}

	static std::string posKey(const SourcePos& p)
	{
		return p.file + ":" + std::to_string(p.line) + ":" + std::to_string(p.column);
	}

	// A use of a template's parameter in an instance (pos: the token's): its hover and declaration.
	void CodeGen::indexTemplateParam(const SourcePos& pos, const std::string& written)
	{
		auto it = m_paramUses.find(posKey(pos));
		if (it == m_paramUses.end() || m_discarding)
			return;
		const std::string name = written.empty() ? it->second.name : written;
		std::string types;
		for (const std::string& t : it->second.types)
			types += (types.empty() ? "" : ", ") + t;
		indexName(pos, name, it->second.def, name + comment({ "template parameter", (it->second.probe ? "analyzed with " : "here: ") + types }));
	}

	// The stand-in arguments of a template nothing instantiates: the defaults, then for each type
	// parameter the first type its where accepts (numbers, then the program's structs and classes);
	// a value parameter: 1. nullptr: none fits.
	bool CodeGen::probeArguments(const TemplateDef* def, std::vector<Type*>& args)
	{
		args.assign(def->params.size(), nullptr);
		std::vector<Type*> candidates = { m_types.i32(), m_types.u32(), m_types.u8(), m_types.i16(), m_types.u16(), m_types.i8(), m_types.f32(), m_types.boolType() };
		for (auto& s : m_structs)
		{
			if (!s.second->anonymous && !s.second->onlyPointer() && m_instanceOrigin.count(s.first) == 0 && s.second->complete)
				candidates.push_back(m_types.structType(s.second));
		}
		auto l_holds = [&](const std::vector<Type*>& a) {
			if (def->where == nullptr)
				return true;
			Bindings b;
			for (size_t i = 0; i < a.size(); i++)
				b[def->params[i]] = a[i] != nullptr ? a[i] : (i < def->required ? m_types.i32() : nullptr);
			try { return evalCondition(def->where, &b) != 0; }
			catch (CompileError&) { return false; }
		};
		for (size_t i = 0; i < def->required; i++)
		{
			if (def->paramTypes[i] != nullptr)
			{
				Type* t = nullptr;
				try { t = resolveType(def->paramTypes[i]); }
				catch (CompileError&) { return false; }
				if (!t->isArithmetic())
					return false;
				args[i] = makeValueArg(t, 1, nullptr, def->pos);
				continue;
			}
			for (Type* c : candidates)
			{
				args[i] = c;
				if (l_holds(args))
					break;
				args[i] = nullptr;
			}
			if (args[i] == nullptr)
				return false;
		}
		args.resize(def->required);
		return true;
	}

	void CodeGen::analyzeTemplates(void)
	{
		for (Node* d : m_program->list)
		{
			if (d->kind != NodeKind::TemplateDecl || d->external != 0 || d->tmpl == nullptr)
				continue;
			const TemplateDef* def = d->tmpl;
			bool pending = false;
			for (size_t e = 0; e < def->entities.size(); e++)
				pending = pending || m_instantiatedEntities.count({ def, e }) == 0;
			if (!pending)
				continue;
			StructInfo* owner = nullptr;
			if (def->owner != nullptr)
			{
				auto it = m_structs.find(def->owner->name);
				if (it == m_structs.end())
					continue;
				owner = it->second;
			}
			std::vector<Type*> args;
			bool fits = false;
			attempt([&] { fits = probeArguments(def, args); });
			if (!fits)
				continue;
			for (size_t e = 0; e < def->entities.size(); e++)
			{
				const TemplateEntity& ent = def->entities[e];
				if (m_instantiatedEntities.count({ def, e }) > 0)
					continue;		// (an instance of the program's has it analyzed)
				const size_t funcsBefore = m_funcList.size();
				const size_t errorsBefore = m_errors.size();
				const size_t syntaxBefore = m_syntaxErrors.size();
				m_probing = true;
				m_probeEntity = { def, e };
				attempt([&] {
					struct Reset { std::pair<const TemplateDef*, size_t>& p; ~Reset() { p = { nullptr, 0 }; } } reset { m_probeEntity };
					instantiate(def->scope.empty() ? ent.name : def->scope + "::" + ent.name, args, nullptr, owner, def->pos, ent.isStruct);
				});
				if (m_globalsTyped < m_globalDecls.size())
					attempt([&] { genGlobalTypes(m_globalsTyped); });		// (its static locals)
				for (size_t i = funcsBefore; i < m_funcList.size(); i++)
				{
					FuncInfo* f = m_funcList[i];
					if (!f->defined || f->decl->a == nullptr || f->broken || f->generated || f->decl->constKind == 3)
						continue;
					f->used = true;
					attempt([&] { f->generated = true; genFunction(f); });
				}
				m_probing = false;
				// (only its syntax errors: the others may be about the stand-in arguments)
				m_errors.resize(errorsBefore);
				for (size_t q = syntaxBefore; q < m_syntaxErrors.size(); q++)
				{
					const CompileError& err = m_syntaxErrors[q];
					const bool known = std::any_of(m_errors.begin(), m_errors.end(), [&](const CompileError& x) { return x.pos.line == err.pos.line && x.pos.column == err.pos.column && x.pos.file == err.pos.file; });
					if (!known)
						m_errors.push_back(err);
				}
			}
		}
	}

	// ===== Value arguments =====
	// A value argument is a compile-time constant of its own (__tv_<n>, declared nowhere in the
	// source): in an instance, the parameter's name is replaced by it, so the parameter is used like
	// any constant (N in array sizes, C.field, C as a value...). The same value is the same argument.
	Type* CodeGen::makeValueArg(Type* t, int64_t value, const std::vector<uint8_t>* bytes, const SourcePos& pos)
	{
		ConstInfo c;
		std::string text;
		if (t->kind == Type::Kind::Struct)
		{
			// Its fields (by path) and its text: Name{1, 2, {3}}.
			std::function<std::string(StructInfo*, uint32_t, const std::string&)> l_text = [&](StructInfo* info, uint32_t offset, const std::string& prefix) {
				std::string s = "{";
				for (size_t i = 0; i < info->fields.size(); i++)
				{
					const StructField& f = info->fields[i];
					s += i > 0 ? ", " : "";
					if (f.type->kind == Type::Kind::Struct)
					{
						s += l_text(f.type->structInfo, offset + f.offset, prefix + f.name + ".");
						continue;
					}
					if (!f.type->isScalar())
						error(pos, "a template's struct value cannot have a field of type " + f.type->name() + " ('" + prefix + f.name + "')");
					uint32_t v = 0;
					for (uint32_t k = 0; k < f.type->size(); k++)
						v = (v << 8) | (*bytes)[offset + f.offset + k];
					const int64_t n = wrapTo(f.type->isSigned() ? (int64_t)(f.type->size() == 1 ? (int8_t)v : f.type->size() == 2 ? (int16_t)v : (int32_t)v) : (int64_t)v, f.type);
					c.fields[prefix + f.name] = n;
					s += f.type->isFloat() ? floatText(n) : f.type->kind == Type::Kind::Bool ? std::string(n ? "true" : "false") : std::to_string(n);
				}
				return s + "}";
			};
			text = (t->structInfo->anonymous ? "struct" : shownName(t->structInfo->name)) + l_text(t->structInfo, 0, "");
		}
		else
		{
			value = wrapTo(value, t);
			c.value = value;
			text = t->isFloat() ? floatText(value) : t->kind == Type::Kind::Bool ? std::string(value ? "true" : "false") : std::to_string(value);
		}
		if (Type* known = m_types.findValueArg(t, text))
			return known;
		const std::string name = "__tv_" + std::to_string(++m_valueArgCount);
		Node* decl = new Node();
		decl->kind = NodeKind::ConstDecl;
		decl->name = decl->shortName = name;
		decl->text = text;
		decl->pos = decl->namePos = pos;
		decl->constKind = 2;
		c.decl = decl;
		c.type = t;
		c.resolved = true;
		m_consts[name] = c;
		m_names[name] = "a constant";
		return m_types.valueArg(t, text, name);
	}

	Type* CodeGen::valueArgument(Node* e, Type* paramType, const Bindings* bindings)
	{
		// A struct: a struct constant, or a struct value computed when compiling.
		const bool wantStruct = paramType != nullptr && paramType->kind == Type::Kind::Struct;
		if (e->kind == NodeKind::Ident && lookupVar(e->name) == nullptr && m_consts.count(e->name) > 0)
		{
			decideConst(e->name);
			const ConstInfo& c = m_consts[e->name];
			if (!c.runtime && c.type != nullptr && c.type->kind == Type::Kind::Struct)
			{
				VarInfo* v = constStorage(e->name);
				if (paramType != nullptr && paramType != c.type)
					error(e->pos, "this template's parameter is " + paramType->name() + ", not " + c.type->name());
				return makeValueArg(c.type, 0, &m_ctBlobs[v], e->pos);
			}
		}
		if (wantStruct || (bindings == nullptr && e->kind != NodeKind::IntLit))
		{
			check(e, paramType);
			if (e->type != nullptr && e->type->kind == Type::Kind::Struct)
			{
				if (e->kind != NodeKind::Ident || e->var == nullptr || m_ctBlobs.count(e->var) == 0)
					error(e->pos, "a template's value argument must be known when compiling (a constant, or a constexpr sub's value)");
				if (paramType != nullptr && paramType != e->type)
					error(e->pos, "this template's parameter is " + paramType->name() + ", not " + e->type->name());
				return makeValueArg(e->type, 0, &m_ctBlobs[e->var], e->pos);
			}
			if (wantStruct)
				error(e->pos, "this template's parameter is " + paramType->name() + ": give a struct value");
		}
		// A number (bindings: it may use the template's parameters: sizeof(T), N + 1).
		Type* t = paramType;
		int64_t value = 0;
		if (bindings != nullptr && (paramType == nullptr || paramType->isInteger()))
			value = evalCondition(e, bindings);
		else
		{
			check(e, paramType);
			if (!e->isConst || e->type == nullptr || !(e->type->isArithmetic() || e->type->kind == Type::Kind::Bool))
				error(e->pos, "a template's value argument must be a compile-time constant (a number, a bool, an f32, a struct constant)");
			if (t == nullptr)
				t = e->type;
			value = constConvert(e->constValue, decay(e->type), t);
		}
		if (t == nullptr)
			t = m_types.i32();
		if (!t->isArithmetic())
			error(e->pos, "a template's value parameter is a number, a bool, an f32 or a struct (not " + t->name() + ")");
		return makeValueArg(t, value, nullptr, e->pos);
	}

	Type* CodeGen::templateArgument(TypeSyntax* a, const Bindings* bindings)
	{
		if (a->kind == TypeSyntax::Kind::Value)
			return valueArgument(a->value, nullptr, bindings);
		// A name alone: a constant's (a value), or a type's.
		if (a->kind == TypeSyntax::Kind::Named && a->fixed == nullptr && a->args.empty() && m_consts.count(a->name) > 0 && !isTypeName(a->name)
			&& (bindings == nullptr || bindings->count(a->written.empty() ? a->name : a->written) == 0))
		{
			Node* id = new Node();
			id->kind = NodeKind::Ident;
			id->name = a->name;
			id->text = a->written;
			id->pos = a->pos;
			id->resolved = true;
			return valueArgument(id, nullptr, nullptr);
		}
		if (a->kind == TypeSyntax::Kind::Named && a->fixed == nullptr && a->args.empty() && !isTypeName(a->name) && m_globals.count(a->name) > 0)
			error(a->pos, "'" + (a->written.empty() ? a->name : a->written) + "' is a variable: a template's value argument must be a compile-time constant");
		return conditionType(a, bindings);
	}

	// ===== if constexpr around declarations, static_assert =====
	bool CodeGen::guardHolds(Node* d)
	{
		Node* g = d->guard;
		if (g == nullptr)
			return true;
		if (!guardHolds(g))
			return false;
		if (g->value < 0)
		{
			const bool outer = m_inDeclarations;
			m_inDeclarations = true;		// (constants only: as in the declarations)
			resolveNames(g->a, g->scope);
			try
			{
				g->value = evalCondition(g->a, nullptr) != 0 ? 1 : 0;
			}
			catch (CompileError&)
			{
				m_inDeclarations = outer;
				throw;
			}
			m_inDeclarations = outer;
		}
		return g->value == (d->guardElse ? 0 : 1);
	}

	// The program's declarations in if constexpr: those whose conditions are true are collected
	// (in the order of the source: one can use what an earlier one declared); the others are gone.
	void CodeGen::applyGuards(Node* program)
	{
		std::vector<Node*> kept;
		for (Node* d : program->list)
		{
			if (d->guard == nullptr)
			{
				kept.push_back(d);
				continue;
			}
			bool holds = false;
			attempt([&] { holds = guardHolds(d); });
			if (!holds)
				continue;
			d->guard = nullptr;		// (decided: a declaration like the others)
			Node one;
			one.kind = NodeKind::Program;
			one.list.push_back(d);
			collect(&one);
			resolveNames(&one);
			kept.push_back(d);
		}
		program->list = kept;
	}

	// A struct's members in if constexpr: the fields and static_asserts whose conditions say so.
	void CodeGen::filterMembers(Node* structDecl)
	{
		std::vector<Node*> kept;
		for (Node* f : structDecl->list)
		{
			if (f->guard == nullptr || guardHolds(f))
			{
				f->guard = nullptr;
				kept.push_back(f);
			}
		}
		structDecl->list = kept;
	}

	void CodeGen::checkStaticAssert(Node* s, const Bindings* bindings)
	{
		if (evalCondition(s->a, bindings) == 0)
			error(s->pos, "static_assert failed: " + (s->name.empty() ? s->text : s->name));
	}

	void CodeGen::markUsed(FuncInfo* f)
	{
		if (f != nullptr && f->broken)
			throw f->brokenError;
		if (f != nullptr && !m_discarding)
			f->used = true;
	}

	void CodeGen::resolveTemplateIdent(Node* e)
	{
		if (e->typeSyntax != nullptr && e->typeSyntax->fixed != nullptr)
		{
			// T in an instance, as a value: the struct it stands for (T.make()).
			Type* t = resolveType(e->typeSyntax);
			if (t->kind == Type::Kind::Struct)
				e->name = t->structInfo->name;
			return;
		}
		if (e->templateArgs.empty() || lookupVar(e->name) != nullptr)
			return;
		auto it = m_templates.find(e->name);
		if (it == m_templates.end())
			error(e->pos, (e->text.empty() ? e->name : e->text) + " is not a template");
		std::vector<Type*> args;
		for (TypeSyntax* t : e->templateArgs)
			args.push_back(templateArgument(t));
		const bool isStruct = it->second[0].def->entities[it->second[0].entity].isStruct;
		e->name = instantiate(e->name, args, nullptr, nullptr, e->pos, isStruct);
		e->templateArgs.clear();
	}

	// A sub entity parsed as written (T as an unknown name): its parameters' types, to deduce the
	// template's arguments from a call's.
	Node* CodeGen::genericHead(const TemplateDef* def, size_t entity)
	{
		auto key = std::make_pair(def, entity);
		auto it = m_genericHeads.find(key);
		if (it != m_genericHeads.end())
			return it->second;
		const TemplateEntity& e = def->entities[entity];
		std::vector<Token> tokens(def->tokens.begin() + (std::ptrdiff_t)e.first, def->tokens.begin() + (std::ptrdiff_t)e.last);
		Token end;
		end.kind = TokenKind::End;
		end.pos = tokens.empty() ? def->pos : tokens.back().pos;
		tokens.push_back(end);
		Parser p(tokens);
		p.addTemplateNames(m_templateShortNames);
		Node* head = nullptr;
		std::vector<Node*> parsed;
		try { parsed = p.parseInstance(*def, entity, def->owner); }
		catch (CompileError& err) { m_templateSyntaxError = true; m_syntaxErrors.push_back(err); throw; }
		for (Node* n : parsed)
		{
			if (n->kind == NodeKind::FuncDecl && n->shortName == e.name)
				head = n;
		}
		m_genericHeads[key] = head;
		return head;
	}

	bool CodeGen::unify(TypeSyntax* p, Type* actual, const TemplateDef* def, std::vector<Type*>& args, bool fromLiteral, std::string& why)
	{
		if (p == nullptr || actual == nullptr)
			return true;
		switch (p->kind)
		{
			case TypeSyntax::Kind::Named:
			{
				if (!p->args.empty())
				{
					// Array<T> and an instance of Array: its arguments.
					if (actual->kind != Type::Kind::Struct)
						return true;
					auto origin = m_instanceOrigin.find(actual->structInfo->name);
					if (origin == m_instanceOrigin.end())
						return true;
					const std::string& templ = origin->second.templ;
					const size_t sep = templ.rfind("::");
					if ((sep == std::string::npos ? templ : templ.substr(sep + 2)) != p->name.substr(p->name.rfind(':') == std::string::npos ? 0 : p->name.rfind(':') + 1))
						return true;
					for (size_t i = 0; i < p->args.size() && i < origin->second.args.size(); i++)
					{
						if (!unify(p->args[i], origin->second.args[i], def, args, fromLiteral, why))
							return false;
					}
					return true;
				}
				const std::string& name = p->written.empty() ? p->name : p->written;
				for (size_t i = 0; i < def->params.size(); i++)
				{
					if (def->params[i] != name)
						continue;
					if (args[i] == nullptr)
						args[i] = actual;
					else if (args[i] != actual && !fromLiteral)
					{
						why = name + " is " + args[i]->name() + " from one argument and " + actual->name() + " from another";
						return false;
					}
					return true;
				}
				return true;
			}
			case TypeSyntax::Kind::Pointer:
				if (p->isRef)
					return unify(p->base, actual, def, args, fromLiteral, why);		// (ref T: from the argument's own type)
				if (actual->kind == Type::Kind::Pointer || actual->kind == Type::Kind::Array)
					return unify(p->base, actual->base, def, args, fromLiteral, why);
				return true;
			case TypeSyntax::Kind::Array:
				if (actual->kind == Type::Kind::Array)
				{
					// T[N] with N a value parameter: N is the array's size.
					if (p->count != nullptr && p->count->kind == NodeKind::Ident)
					{
						const std::string& n = p->count->text.empty() ? p->count->name : p->count->text;
						for (size_t i = 0; i < def->params.size(); i++)
						{
							if (def->params[i] != n || def->paramTypes[i] == nullptr)
								continue;
							Type* count = makeValueArg(m_types.u32(), actual->count, nullptr, p->pos);
							if (args[i] == nullptr)
								args[i] = count;
							else if (args[i] != count && !(args[i]->valueArg && m_consts[args[i]->valueConst].value == actual->count))
							{
								why = n + " is " + args[i]->name() + " from one argument and " + std::to_string(actual->count) + " from another";
								return false;
							}
						}
					}
					return unify(p->base, actual->base, def, args, fromLiteral, why);
				}
				return true;
			case TypeSyntax::Kind::Value:
			case TypeSyntax::Kind::ValueType:
			case TypeSyntax::Kind::BaseType:
				return true;		// (a value written in the pattern: Ring<T, 4>*: nothing to deduce)
			case TypeSyntax::Kind::Function:
				if (actual->kind == Type::Kind::Pointer && actual->base != nullptr && actual->base->kind == Type::Kind::Function)
					actual = actual->base;
				if (actual->kind != Type::Kind::Function)
					return true;
				for (size_t i = 0; i < p->params.size() && i < actual->params.size(); i++)
				{
					if (!unify(p->params[i], actual->params[i], def, args, fromLiteral, why))
						return false;
				}
				return unify(p->base, actual->base, def, args, fromLiteral, why);
		}
		return true;
	}

	// The arguments a call gives a template's sub: from its parameters' types (args: the explicit
	// ones first). A literal (5, 1.5, null) only decides a parameter no typed argument decided.
	bool CodeGen::deduce(const TemplateDef* def, size_t entity, std::vector<Type*>& args, std::vector<Node*>& callArgs, bool method, std::string& why)
	{
		Node* head = genericHead(def, entity);
		if (head == nullptr)
			return true;
		std::vector<Node*> params(head->list.begin(), head->list.end());
		if (method && (head->value & 8) == 0 && !params.empty())
			params.erase(params.begin());		// (this)
		// (the number of arguments: overloads in one template are told apart by it too)
		size_t required = 0;
		for (Node* p : params)
			required += p->a == nullptr ? 1 : 0;
		if ((callArgs.size() > params.size() && head->variadic == 0) || callArgs.size() < required)
		{
			why = "this " + head->shortName + " takes " + (required == params.size() ? std::to_string(params.size()) : std::to_string(required) + " to " + std::to_string(params.size())) +
				" argument" + (params.size() == 1 ? "" : "s") + ", not " + std::to_string(callArgs.size());
			return false;
		}
		for (int pass = 0; pass < 2; pass++)
		{
			for (size_t i = 0; i < params.size() && i < callArgs.size(); i++)
			{
				Node* a = callArgs[i];
				const bool literal = a->kind == NodeKind::IntLit || a->kind == NodeKind::FloatLit || a->kind == NodeKind::Null || a->kind == NodeKind::BoolLit;
				if (literal != (pass == 1) || a->type == nullptr)
					continue;
				if (!unify(params[i]->typeSyntax, a->type, def, args, literal, why))
					return false;
			}
		}
		return true;
	}

	std::string CodeGen::instantiate(const std::string& name, std::vector<Type*> args, std::vector<Node*>* callArgs, StructInfo* owner,
		const SourcePos& pos, bool wantStruct)
	{
		struct Depth { int& d; Depth(int& x) : d(x) { d++; } ~Depth() { d--; } } depth(m_instanceDepth);
		if (m_instanceDepth > 64)
			error(pos, "templates instantiated in each other too deep (64 levels): " + name);
		auto it = m_templates.find(name);
		if (it == m_templates.end())
			error(pos, name + " is not a template");
		auto l_display = [&](const std::vector<Type*>& a) {
			std::string s;
			for (Type* t : a)
			{
				if (t != nullptr)
					s += (s.empty() ? "" : ", ") + t->name();
			}
			return s.empty() ? name : name + "<" + s + ">";
		};
		struct Match { TemplateCandidate c; std::vector<Type*> args; };
		std::vector<Match> matches;
		std::vector<std::string> reasons;
		const std::pair<const TemplateDef*, size_t> only = m_probeEntity;
		m_probeEntity = { nullptr, 0 };		// (only for this call, not the instances it makes)
		for (const TemplateCandidate& c : it->second)
		{
			const TemplateDef* def = c.def;
			if (only.first != nullptr && (def != only.first || c.entity != only.second))
				continue;		// (the language server's probe: this entity)
			const TemplateEntity& ent = def->entities[c.entity];
			const std::string where = " (template at " + posString(def->pos) + ")";
			if (ent.isStruct != wantStruct)
			{
				reasons.push_back(l_display(args) + ": " + std::string(ent.isStruct ? "a struct template, not a sub" : "a sub template, not a type") + where);
				continue;
			}
			if (args.size() > def->params.size())
			{
				reasons.push_back(l_display(args) + ": " + "it takes " + std::to_string(def->params.size()) + " type" + (def->params.size() == 1 ? "" : "s") + ", not " + std::to_string(args.size()) + where);
				continue;
			}
			std::vector<Type*> a = args;
			a.resize(def->params.size(), nullptr);
			std::string why;
			if (callArgs != nullptr && !deduce(def, c.entity, a, *callArgs, owner != nullptr, why))
			{
				reasons.push_back(l_display(a.empty() ? args : a) + ": " + why + where);
				continue;
			}
			// Types and values where the parameters want them (a value: in the parameter's type);
			// the defaults of those not given (they may use the parameters before them).
			bool ok = true;
			Bindings known;
			std::vector<bool> defaulted(a.size(), false);
			for (size_t i = 0; i < a.size() && ok; i++)
			{
				const bool valueParam = def->paramTypes[i] != nullptr;
				Type* paramType = nullptr;
				if (valueParam)
				{
					paramType = conditionType(def->paramTypes[i], &known);
					if (paramType == nullptr || paramType->valueArg)
						error(def->paramTypes[i]->pos, "the type of " + def->params[i] + " uses a parameter that is not a type");
				}
				if (a[i] == nullptr && (def->defaultTypes[i] != nullptr || def->defaultValues[i] != nullptr))
				{
					a[i] = def->defaultTypes[i] != nullptr ? conditionType(def->defaultTypes[i], &known) : valueArgument(def->defaultValues[i], paramType, &known);
					defaulted[i] = true;
				}
				if (a[i] != nullptr && valueParam && !a[i]->valueArg)
				{
					reasons.push_back(l_display(a) + ": " + def->params[i] + " is a value (" + def->params[i] + ": " + paramType->name() + "), not the type " + a[i]->name() + where);
					ok = false;
				}
				else if (a[i] != nullptr && valueParam && a[i]->base != paramType)
				{
					// (a number in the parameter's type: Array<u32, 16> and Array<u32, 16u> are one)
					Type* from = a[i]->base;
					const ConstInfo& c = m_consts[a[i]->valueConst];
					if (from->kind == Type::Kind::Struct || paramType->kind == Type::Kind::Struct || !paramType->isArithmetic())
					{
						reasons.push_back(l_display(a) + ": " + def->params[i] + " is " + paramType->name() + ", not " + from->name() + where);
						ok = false;
					}
					else
						a[i] = makeValueArg(paramType, constConvert(c.value, from, paramType), nullptr, pos);
				}
				if (a[i] != nullptr)
					known[def->params[i]] = a[i];
			}
			if (!ok)
				continue;
			for (size_t i = 0; i < def->required && ok; i++)
			{
				if (a[i] == nullptr)
				{
					reasons.push_back(l_display(a.empty() ? args : a) + ": " + "can't tell " + def->params[i] + (callArgs != nullptr ? " from the arguments: give it (" + name + "<...>(...))" :
						": " + name + " needs " + std::to_string(def->required) + " type" + (def->required == 1 ? "" : "s")) + where);
					ok = false;
				}
			}
			for (size_t i = def->required; i < a.size() && ok; i++)
			{
				if (a[i] == nullptr || defaulted[i])
					continue;
				bool mentioned = false;
				for (size_t k = ent.first; k < ent.last && !mentioned; k++)
					mentioned = def->tokens[k].kind == TokenKind::Identifier && def->tokens[k].text == def->params[i];
				if (!mentioned)
				{
					reasons.push_back(l_display(a.empty() ? args : a) + ": " + ent.name + " doesn't use " + def->params[i] + ": leave it out" + where);
					ok = false;
				}
			}
			if (!ok)
				continue;
			if (def->where != nullptr)
			{
				Bindings b;
				for (size_t i = 0; i < a.size(); i++)
					b[def->params[i]] = a[i];
				if (evalCondition(def->where, &b) == 0)
				{
					// Which part of the condition is false (the parts of a top-level 'and').
					std::string part;
					std::vector<Node*> conj;
					std::function<void(Node*)> l_flatten = [&](Node* n) {
						if (n->kind == NodeKind::Binary && n->name == "&&") { l_flatten(n->a); l_flatten(n->b); }
						else conj.push_back(n);
					};
					l_flatten(def->where);
					if (conj.size() > 1 && conj.size() == def->whereParts.size())
					{
						for (size_t k = 0; k < conj.size() && part.empty(); k++)
						{
							if (evalCondition(conj[k], &b) == 0)
								part = def->whereParts[k];
						}
					}
					if (part.empty())
					{
						for (size_t k = 0; k < def->whereParts.size(); k++)
							part += (k > 0 ? " and " : "") + def->whereParts[k];
					}
					reasons.push_back(l_display(a.empty() ? args : a) + ": " + "its where is false: " + part + where);
					continue;
				}
			}
			// The template body's if constexpr around it.
			if (!ent.guards.empty())
			{
				Bindings b;
				for (size_t i = 0; i < a.size(); i++)
					b[def->params[i]] = a[i];
				bool in = true;
				for (const Guard& g : ent.guards)
				{
					if ((evalCondition(g.condition, &b) != 0) != g.expected)
					{
						in = false;
						break;
					}
				}
				if (!in)
				{
					reasons.push_back(l_display(a.empty() ? args : a) + ": " + "not there for these arguments (an if constexpr around it)" + where);
					continue;
				}
			}
			matches.push_back({ c, a });
		}
		if (matches.empty())
		{
			if (reasons.size() == 1)
				error(pos, reasons[0]);
			std::string all;
			for (const auto& r : reasons)
				all += "\n  " + r;
			error(pos, l_display(args) + ": no template fits:" + all);
		}
		if (matches.size() > 1)
		{
			std::string all;
			for (const auto& m : matches)
				all += "\n  template at " + posString(m.c.def->pos);
			error(pos, l_display(matches[0].args) + ": more than one template fits (their where conditions overlap):" + all);
		}
		const Match& m = matches[0];
		const TemplateDef* def = m.c.def;
		const TemplateEntity& ent = def->entities[m.c.entity];
		std::string shortName = ent.name + "<";
		{
			bool first = true;
			for (Type* t : m.args)
			{
				if (t == nullptr)
					continue;
				shortName += (first ? "" : ", ") + t->name();
				first = false;
			}
			shortName += ">";
		}
		const std::string full = def->scope.empty() ? shortName : def->scope + "::" + shortName;
		// (overloaded subs of one template: one instance name, each instantiated when one of its calls
		// needs it)
		const std::string key = ent.isStruct ? full : full + "#" + std::to_string(m.c.entity);
		// An alias template's instance is the type it names: a struct's name for a struct (so that
		// Name<...> works wherever a struct instance does), else the alias's own (see templateType).
		auto l_result = [&](const std::string& n) -> std::string {
			if (!ent.isAlias)
				return n;
			Type* t = aliasType(n, pos);
			return t->kind == Type::Kind::Struct ? t->structInfo->name : n;
		};
		if (m_instances.count(key) > 0)
			return l_result(full);
		m_instances[key] = full;
		// The template body's static_asserts (those its if constexpr keep), for these arguments.
		if (!def->asserts.empty())
		{
			Bindings b;
			for (size_t i = 0; i < m.args.size(); i++)
				b[def->params[i]] = m.args[i];
			for (const TemplateAssert& as : def->asserts)
			{
				bool in = true;
				for (const Guard& g : as.guards)
					in = in && (evalCondition(g.condition, &b) != 0) == g.expected;
				if (in && evalCondition(as.condition, &b) == 0)
					error(as.pos, "static_assert failed: " + (as.message.empty() ? as.text : as.message) + " (in " + full + ", instantiated at " + posString(pos) + ")");
			}
		}
		m_instantiatedEntities.insert({ def, m.c.entity });
		// Its tokens: the parameters standing for their types, its name the instance's.
		std::vector<Token> tokens(def->tokens.begin() + (std::ptrdiff_t)ent.first, def->tokens.begin() + (std::ptrdiff_t)ent.last);
		for (Token& t : tokens)
		{
			if (t.kind != TokenKind::Identifier)
				continue;
			for (size_t i = 0; i < def->params.size(); i++)
			{
				if (t.text != def->params[i])
					continue;
				if (m_options.index != nullptr)
				{
					// (the language server: what the parameter is here, where it is declared)
					ParamUse& use = m_paramUses[posKey(t.pos)];
					use.name = t.text;
					use.def = templateParamPos(def, t.text);
					use.probe = use.probe || (m_probing && use.types.empty());
					if (!m_probing && use.probe)
					{
						use.probe = false;
						use.types.clear();
					}
					use.types.insert(m.args[i] == nullptr ? std::string("(not given)") : m.args[i]->valueArg ? m.args[i]->valueText : m.args[i]->name());
				}
				if (m.args[i] != nullptr && m.args[i]->valueArg)
					t.text = m.args[i]->valueConst;		// a value: its constant
				else
					t.fixedType = m.args[i] != nullptr ? m.args[i] : m_notGiven;
				break;
			}
		}
		tokens[ent.nameToken - ent.first].text = shortName;
		tokens[ent.nameToken - ent.first].fixedType = nullptr;
		// Its own name alone (not Name<...>) in it: this instance (sizeof(Buffer), Node* next, a
		// recursive call).
		for (size_t k = 0; k + 1 < tokens.size(); k++)
		{
			const bool member = k > 0 && tokens[k - 1].kind == TokenKind::Punct && (tokens[k - 1].text == "." || tokens[k - 1].text == "->" || tokens[k - 1].text == "::");
			if (tokens[k].kind == TokenKind::Identifier && tokens[k].text == ent.name && tokens[k].fixedType == nullptr && !member
				&& !(tokens[k + 1].kind == TokenKind::Punct && tokens[k + 1].text == "<"))
				tokens[k].text = shortName;
		}
		Token end;
		end.kind = TokenKind::End;
		end.pos = tokens.back().pos;
		tokens.push_back(end);
		const std::string note = " (in " + full + ", instantiated at " + posString(pos) + ")";
		auto l_withNote = [&](const std::function<void()>& fn) {
			try
			{
				fn();
			}
			catch (CompileError& err)
			{
				if (err.message.find(" (in ") == std::string::npos)
					err.message += note;
				throw;
			}
		};
		Parser parser(tokens);
		parser.addTemplateNames(m_templateShortNames);
		Node* program = new Node();
		program->kind = NodeKind::Program;
		program->pos = pos;
		l_withNote([&] {
			try { program->list = parser.parseInstance(*def, m.c.entity, def->owner); }
			catch (CompileError& err) { m_templateSyntaxError = true; m_syntaxErrors.push_back(err); throw; }
		});
		const size_t funcsBefore = m_funcList.size();
		const size_t globalsBefore = m_globalDecls.size();
		m_instanceOrigin[full] = { name, m.args };
		l_withNote([&] {
			collect(program);
			applyGuards(program);		// (if constexpr around its methods, in its struct: declared before the names are looked up)
			resolveNames(program);
		});
		// (only this instance's own subs: resolving them can instantiate others, e.g. a struct a sub
		// returns, whose subs are added after them and resolved by their own instantiation)
		const std::vector<FuncInfo*> own(m_funcList.begin() + (std::ptrdiff_t)funcsBefore, m_funcList.end());
		for (FuncInfo* f : own)
		{
			f->instance = true;
			f->instanceNote = note;
			try
			{
				l_withNote([&] { resolveFunction(f); });
			}
			catch (CompileError& err)
			{
				// (raised if something uses it: see markUsed)
				f->broken = true;
				f->brokenError = err;
				f->paramNames.clear();
				f->defaults.clear();
				f->requiredParams = 0;
				f->type = m_types.function({}, m_types.voidType());
			}
			f->label = codeLabel(subLabelName(f, f->name), (f->decl->value & 4) != 0);
			if (!f->broken)
			{
				// (its parameter types: overloads of one template, instantiated one by one, each have a
				// label of their own; instances are only in this program)
				f->label += "__";
				for (Type* p : f->type->params)
					f->label += "_" + mangle(p);
				if (f->type->params.empty())
					f->label += "_void";
			}
		}
		if (ent.isAlias)
		{
			std::string out;
			l_withNote([&] { out = l_result(full); });
			if (m_globalDecls.size() > globalsBefore && m_globalsTyped > 0)
				l_withNote([&] { genGlobalTypes(m_globalsTyped); });
			return out;
		}
		if (wantStruct)
		{
			auto info = m_structs.find(full);
			if (info == m_structs.end())
				error(pos, "internal error: no struct " + full);
			l_withNote([&] { resolveStruct(info->second, pos); });
		}
		if (m_globalDecls.size() > globalsBefore && m_globalsTyped > 0)
			l_withNote([&] { genGlobalTypes(m_globalsTyped); });
		(void)owner;
		return full;
	}

	// The type a condition names: a template parameter (from bindings; nullptr: an optional one
	// not given), or any type.
	Type* CodeGen::conditionType(TypeSyntax* ts, const Bindings* bindings)
	{
		switch (ts->kind)
		{
			case TypeSyntax::Kind::Named:
				if (ts->fixed == m_notGiven)
					return nullptr;		// (an optional parameter not given, in an instance)
				if (ts->fixed == nullptr && bindings != nullptr && ts->args.empty())
				{
					auto it = bindings->find(ts->written.empty() ? ts->name : ts->written);
					if (it != bindings->end())
						return it->second;
				}
				if (!ts->args.empty() && bindings != nullptr)
				{
					std::vector<Type*> args;
					for (TypeSyntax* a : ts->args)
					{
						Type* t = templateArgument(a, bindings);
						if (t == nullptr)
							error(a->pos, "an optional template parameter that is not given");
						args.push_back(t);
					}
					return templateType(ts->name, args, ts->pos);
				}
				return resolveType(ts);
			case TypeSyntax::Kind::Pointer:
			{
				Type* base = conditionType(ts->base, bindings);
				if (base == nullptr)
					error(ts->pos, "an optional template parameter that is not given");
				return m_types.pointer(base, ts->constTarget, ts->isRef);
			}
			case TypeSyntax::Kind::Array:
			{
				Type* base = conditionType(ts->base, bindings);
				if (base == nullptr || ts->count == nullptr)
					error(ts->pos, "an array type in a condition needs its element type and size");
				return m_types.array(base, (uint32_t)constExpr(ts->count, "an array size"));
			}
			case TypeSyntax::Kind::ValueType:
				// value_type(N), N a template parameter (in a where: from its argument).
				if (bindings != nullptr && ts->value != nullptr && ts->value->kind == NodeKind::Ident)
				{
					auto it = bindings->find(ts->value->text.empty() ? ts->value->name : ts->value->text);
					if (it != bindings->end())
					{
						if (it->second == nullptr || it->second == m_notGiven)
							error(ts->pos, "value_type(" + it->first + "): " + it->first + " is not given");
						if (!it->second->valueArg)
							error(ts->pos, "value_type(" + it->first + "): " + it->first + " is a type here (" + it->second->name() + "), not a value");
						return it->second->base;
					}
				}
				return resolveType(ts);
			case TypeSyntax::Kind::BaseType:
			{
				Type* inner = conditionType(ts->base, bindings);
				if (inner == nullptr)
					error(ts->pos, "an optional template parameter that is not given");
				return baseType(inner, ts->absolute, ts->pos);
			}
			default:
				return resolveType(ts);
		}
	}

	// base_type(T) / absolute_base_type(T): the class T extends / the first class of its chain (T
	// itself if it extends none); a pointer to a class: a pointer to it.
	Type* CodeGen::baseType(Type* t, bool absolute, const SourcePos& pos)
	{
		const char* what = absolute ? "absolute_base_type" : "base_type";
		Type* st = t->kind == Type::Kind::Pointer ? t->base : t;
		if (st == nullptr || st->kind != Type::Kind::Struct)
			error(pos, std::string(what) + "(" + t->name() + "): a class (or a pointer to one) is needed");
		resolveStruct(st->structInfo, pos);
		StructInfo* info = st->structInfo;
		if (!absolute && info->base == nullptr)
			error(pos, std::string("base_type(") + t->name() + "): " + info->name + " extends no class (has_base(T) tells)");
		while (info->base != nullptr)
		{
			info = info->base;
			if (!absolute)
				break;
		}
		Type* result = m_types.structType(info);
		if (m_options.index != nullptr && !m_discarding)
			indexName(pos, what, info->decl->namePos, std::string(what) + "(...): " + result->name(true) + comment({ absolute ? "the first class of its chain" : "the class it extends" }));
		return t->kind == Type::Kind::Pointer ? m_types.pointer(result, t->constTarget) : result;
	}

	// The built-ins of conditions: name, arguments (T a type, N a name, * repeats), what they compute.
	// Adding one: one entry here (the parser and the language server see it through
	// findConditionBuiltin).
	struct ConditionEval
	{
		typedef int64_t (*Fn)(CodeGen& cg, const std::vector<Type*>& t, const std::vector<std::string>& n, const SourcePos& pos);
		struct Entry { ConditionBuiltin spec; Fn fn; };

		static Type* structOf(Type* t) { return (t->kind == Type::Kind::Pointer && t->base != nullptr && t->base->kind == Type::Kind::Struct) ? t->base : t; }

		// A struct's (or a class's, also inherited) method named n[0] that keep() accepts; with a sub
		// type (t[1]): with those parameters (this not counted) and that result. templates: a method
		// template counts (without a sub type).
		template<typename Keep>
		static int64_t hasMethod(const char* name, CodeGen& cg, const std::vector<Type*>& t, const std::vector<std::string>& n, const SourcePos& pos, bool templates, Keep keep)
		{
			Type* st = structOf(t[0]);
			if (st->kind != Type::Kind::Struct) return 0;
			const std::string m = st->structInfo->name + "::" + n[0];
			if (st->structInfo->kind != 0)
				cg.resolveStruct(st->structInfo, pos);
			const std::vector<FuncInfo*> funcs = st->structInfo->kind != 0 ? cg.methodsOf(st->structInfo, n[0]) :
				(cg.m_funcs.count(m) > 0 ? cg.m_funcs[m] : std::vector<FuncInfo*>());
			Type* sig = nullptr;
			if (t.size() >= 2)
			{
				sig = t[1]->kind == Type::Kind::Pointer && t[1]->base->isFunction() ? t[1]->base : t[1];
				if (!sig->isFunction())
					error(pos, std::string(name) + "'s third argument is a sub type (sub(u32, f32) -> i32*), not " + t[1]->name());
			}
			if (sig == nullptr && templates && cg.m_templates.count(m) > 0)
				return 1;
			for (FuncInfo* f : funcs)
			{
				if (!keep(f->decl->value))
					continue;
				if (sig == nullptr)
					return 1;
				if (f->type == nullptr && !f->instance && !f->resolving)
				{
					// (in the declarations, the subs have no types yet: this one's now)
					f->resolving = true;
					try { cg.resolveFunction(f); }
					catch (CompileError&) { f->resolving = false; f->type = nullptr; f->paramNames.clear(); f->defaults.clear(); f->requiredParams = 0; throw; }
					f->resolving = false;
				}
				if (f->type == nullptr)
					error(pos, std::string(name) + ": the type of " + f->name + " is not known yet here");
				const size_t skip = (f->decl->value & 8) != 0 ? 0 : 1;		// (this)
				const auto& params = f->type->params;
				if (params.size() < skip || params.size() - skip != sig->params.size() || f->type->base != sig->base || f->type->variadic != sig->variadic)
					continue;
				if (std::equal(sig->params.begin(), sig->params.end(), params.begin() + (std::ptrdiff_t)skip))
					return 1;
			}
			return 0;
		}

		// A field named n[0] (also inherited) that keep() accepts, of type t[1] if given.
		template<typename Keep>
		static int64_t hasField(CodeGen& cg, const std::vector<Type*>& t, const std::vector<std::string>& n, const SourcePos& pos, Keep keep)
		{
			Type* st = structOf(t[0]);
			if (st->kind != Type::Kind::Struct) return 0;
			cg.resolveStruct(st->structInfo, pos);
			const StructField* f = st->structInfo->field(n[0]);
			return f != nullptr && keep(*f) && (t.size() < 2 || f->type == t[1]);
		}

		// A class (or a pointer to one) and its classes up its chain (not itself).
		static StructInfo* classOf(CodeGen& cg, Type* t, const SourcePos& pos)
		{
			Type* st = structOf(t);
			if (st->kind != Type::Kind::Struct) return nullptr;
			cg.resolveStruct(st->structInfo, pos);
			return st->structInfo;
		}
		static bool hasParent(StructInfo* c, StructInfo* p)
		{
			if (c == nullptr || p == nullptr)
				return false;
			for (StructInfo* b = c->base; b != nullptr; b = b->base)
				if (b == p)
					return true;
			return false;
		}

		static const std::vector<Entry>& table(void)
		{
			static const std::vector<Entry> entries = {
				{ { "is_given", "T", "one type parameter: was it given (an optional one)" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0] != nullptr; } },
				{ { "is_value", "T", "one template parameter (or constant): was it given a value (not a type)" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0] != nullptr && t[0]->valueArg; } },
				{ { "is_integer", "T", "one type" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->isInteger() && t[0]->kind != Type::Kind::Bool; } },
				{ { "is_signed", "T", "one type" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->isSigned(); } },
				{ { "is_unsigned", "T", "one type" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->isInteger() && !t[0]->isSigned() && t[0]->kind != Type::Kind::Bool; } },
				{ { "is_float", "T", "one type" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->isFloat(); } },
				{ { "is_decimal", "T", "one type" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->isFloat(); } },
				{ { "is_numeric", "T", "one type" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return (t[0]->isInteger() && t[0]->kind != Type::Kind::Bool) || t[0]->isFloat(); } },
				{ { "is_bool", "T", "one type" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->kind == Type::Kind::Bool; } },
				{ { "is_pointer", "T", "one type" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->kind == Type::Kind::Pointer; } },
				{ { "is_array", "T", "one type" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->kind == Type::Kind::Array; } },
				{ { "is_struct", "T", "one type (a struct, a class or an interface)" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->kind == Type::Kind::Struct; } },
				{ { "is_class", "T", "one type" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->kind == Type::Kind::Struct && t[0]->structInfo->isClass(); } },
				{ { "is_interface", "T", "one type" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->kind == Type::Kind::Struct && t[0]->structInfo->isInterface(); } },
				{ { "is_abstract", "T", "one type (an abstract class or an interface: only pointed at)" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t {
					return t[0]->kind == Type::Kind::Struct && t[0]->structInfo->onlyPointer(); } },
				{ { "is_base_of", "TT", "two types (is the first the second, a class it extends or an interface it implements)" }, [](CodeGen& cg, const auto& t, const auto&, const auto& pos) -> int64_t {
					if (t[0]->kind != Type::Kind::Struct || t[1]->kind != Type::Kind::Struct) return 0;
					cg.resolveStruct(t[1]->structInfo, pos);
					return cg.derives(t[1]->structInfo, t[0]->structInfo); } },
				{ { "is_sub", "T", "one type (a sub pointer type)" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->isFunction(); } },
				{ { "is_scalar", "T", "one type (fits a register: integers, floats, pointers, sub pointers)" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->isScalar(); } },
				{ { "is_builtin", "T", "one type (i8 ... u32, f32, bool, void)" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->kind <= Type::Kind::F32; } },
				{ { "is_type", "TT", "two types (are they the same)" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0] == t[1]; } },
				{ { "is_one_of", "TT*", "a type, then the types it may be" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t {
					for (size_t i = 1; i < t.size(); i++) if (t[0] == t[i]) return 1;
					return 0; } },
				{ { "is_pointer_to", "TT", "two types (is the first a pointer to the second)" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->kind == Type::Kind::Pointer && t[0]->base == t[1]; } },
				{ { "is_convertible", "TT", "two types (does a value of the first convert to the second)" }, [](CodeGen& cg, const auto& t, const auto&, const auto&) -> int64_t {
					Node value;
					value.kind = NodeKind::TypeArg;
					value.type = t[0];
					return cg.conversionCost(&value, t[1]) >= 0; } },
				// (visibility: priv, prot, or neither: pub; readonly fields)
				{ { "has_priv_method", "TNT?", "as has_method: a priv method" }, [](CodeGen& cg, const auto& t, const auto& n, const auto& pos) -> int64_t {
					return hasMethod("has_priv_method", cg, t, n, pos, false, [](int v) { return (v & 16) != 0; }); } },
				{ { "has_prot_method", "TNT?", "as has_method: a prot method" }, [](CodeGen& cg, const auto& t, const auto& n, const auto& pos) -> int64_t {
					return hasMethod("has_prot_method", cg, t, n, pos, false, [](int v) { return (v & 8192) != 0; }); } },
				{ { "has_pub_method", "TNT?", "as has_method: a method for everyone (neither priv nor prot)" }, [](CodeGen& cg, const auto& t, const auto& n, const auto& pos) -> int64_t {
					return hasMethod("has_pub_method", cg, t, n, pos, false, [](int v) { return (v & (16 | 8192)) == 0; }); } },
				{ { "has_priv_field", "TNT?", "as has_field: a priv field" }, [](CodeGen& cg, const auto& t, const auto& n, const auto& pos) -> int64_t {
					return hasField(cg, t, n, pos, [](const StructField& f) { return f.isPrivate; }); } },
				{ { "has_prot_field", "TNT?", "as has_field: a prot field" }, [](CodeGen& cg, const auto& t, const auto& n, const auto& pos) -> int64_t {
					return hasField(cg, t, n, pos, [](const StructField& f) { return f.isProtected; }); } },
				{ { "has_pub_field", "TNT?", "as has_field: a field everyone reads (neither priv nor prot; readonly too)" }, [](CodeGen& cg, const auto& t, const auto& n, const auto& pos) -> int64_t {
					return hasField(cg, t, n, pos, [](const StructField& f) { return !f.isPrivate && !f.isProtected; }); } },
				{ { "has_readonly_field", "TNT?", "as has_field: a readonly field" }, [](CodeGen& cg, const auto& t, const auto& n, const auto& pos) -> int64_t {
					return hasField(cg, t, n, pos, [](const StructField& f) { return f.isReadonly; }); } },
				{ { "has_base", "T", "a class (or a pointer to one) that extends a class (or a struct)" }, [](CodeGen& cg, const auto& t, const auto&, const auto& pos) -> int64_t {
					StructInfo* c = classOf(cg, t[0], pos);
					return c != nullptr && c->base != nullptr; } },
				{ { "has_parent", "TT", "a class, and a class somewhere up its chain (not itself; interfaces: is_base_of)" }, [](CodeGen& cg, const auto& t, const auto&, const auto& pos) -> int64_t {
					return hasParent(classOf(cg, t[0], pos), classOf(cg, t[1], pos)); } },
				{ { "has_common_base", "TTT", "two classes, and a class up the chains of both" }, [](CodeGen& cg, const auto& t, const auto&, const auto& pos) -> int64_t {
					StructInfo* p = classOf(cg, t[2], pos);
					return hasParent(classOf(cg, t[0], pos), p) && hasParent(classOf(cg, t[1], pos), p); } },
				{ { "has_friend", "TT", "a struct or class, and a struct or class it names as a friend" }, [](CodeGen& cg, const auto& t, const auto&, const auto& pos) -> int64_t {
					StructInfo* c = classOf(cg, t[0], pos);
					StructInfo* f = classOf(cg, t[1], pos);
					if (c == nullptr || f == nullptr) return 0;
					cg.resolveFriends(c);
					return std::find(c->friendClasses.begin(), c->friendClasses.end(), f->name) != c->friendClasses.end(); } },
				// (with a sub type: a method with those parameters, this not counted, and that return type)
				{ { "has_method", "TNT?", "a struct type, a method name, and optionally its sub type (has_method(T, find, sub(u32) -> i32*))" },
					[](CodeGen& cg, const auto& t, const auto& n, const auto& pos) -> int64_t { return hasMethod("has_method", cg, t, n, pos, true, [](int) { return true; }); } },
				{ { "has_field", "TNT?", "a struct type, a field name, and optionally its type (has_field(T, count, u32))" },
					[](CodeGen& cg, const auto& t, const auto& n, const auto& pos) -> int64_t { return hasField(cg, t, n, pos, [](const StructField&) { return true; }); } },
				{ { "is_range", "T", "one type (a for-in can loop over it: an array, or a struct with begin() and end())" }, [](CodeGen& cg, const auto& t, const auto&, const auto&) -> int64_t {
					if (t[0]->kind == Type::Kind::Array) return 1;
					Type* st = structOf(t[0]);
					if (st->kind != Type::Kind::Struct) return 0;
					if (st->structInfo->kind != 0)
					{
						cg.resolveStruct(st->structInfo, {});
						return !cg.methodsOf(st->structInfo, "begin").empty() && !cg.methodsOf(st->structInfo, "end").empty();
					}
					return cg.m_funcs.count(st->structInfo->name + "::begin") > 0 && cg.m_funcs.count(st->structInfo->name + "::end") > 0; } },
				{ { "size_of", "T", "one type (its size in bytes, like sizeof)" }, [](CodeGen& cg, const auto& t, const auto&, const auto& pos) -> int64_t {
					if (t[0]->kind == Type::Kind::Struct) cg.resolveStruct(t[0]->structInfo, pos);
					return t[0]->size(); } },
				{ { "array_count", "T", "one type (an array's element count; 0 for other types)" }, [](CodeGen&, const auto& t, const auto&, const auto&) -> int64_t { return t[0]->kind == Type::Kind::Array ? t[0]->count : 0; } },
			};
			return entries;
		}
	};

	const ConditionBuiltin* findConditionBuiltin(const std::string& name)
	{
		for (const auto& e : ConditionEval::table())
		{
			if (name == e.spec.name)
				return &e.spec;
		}
		return nullptr;
	}

	std::vector<const ConditionBuiltin*> conditionBuiltins(void)
	{
		std::vector<const ConditionBuiltin*> all;
		for (const auto& e : ConditionEval::table())
			all.push_back(&e.spec);
		return all;
	}

	int64_t CodeGen::evalCondition(Node* e, const Bindings* bindings)
	{
		auto l_bound = [&](const std::string& name) -> const Type* const* {
			if (bindings == nullptr) return nullptr;
			auto it = bindings->find(name);
			return it == bindings->end() ? nullptr : &it->second;
		};
		switch (e->kind)
		{
			case NodeKind::IntLit:
			case NodeKind::BoolLit:
				return e->value;
			case NodeKind::Unary:
			{
				const int64_t v = evalCondition(e->a, bindings);
				if (e->name == "!") return v == 0;
				if (e->name == "-") return -v;
				if (e->name == "~") return ~v;
				break;
			}
			case NodeKind::Binary:
			{
				const std::string& op = e->name;
				if (op == "&&") return evalCondition(e->a, bindings) != 0 && evalCondition(e->b, bindings) != 0;
				if (op == "||") return evalCondition(e->a, bindings) != 0 || evalCondition(e->b, bindings) != 0;
				const int64_t a = evalCondition(e->a, bindings), b = evalCondition(e->b, bindings);
				if (op == "==") return a == b;
				if (op == "!=") return a != b;
				if (op == "<") return a < b;
				if (op == "<=") return a <= b;
				if (op == ">") return a > b;
				if (op == ">=") return a >= b;
				if (op == "+") return a + b;
				if (op == "-") return a - b;
				if (op == "*") return a * b;
				if ((op == "/" || op == "%") && b == 0) error(e->pos, "division by zero in a condition");
				if (op == "/") return a / b;
				if (op == "%") return a % b;
				if (op == "&") return a & b;
				if (op == "|") return a | b;
				if (op == "^") return a ^ b;
				if (op == "<<") return a << b;
				if (op == ">>") return a >> b;
				break;
			}
			case NodeKind::Call:
			{
				if (e->a->kind != NodeKind::Ident)
					break;
				const ConditionEval::Entry* entry = nullptr;
				for (const auto& x : ConditionEval::table())
				{
					if (e->a->name == x.spec.name)
						entry = &x;
				}
				if (entry == nullptr)
					break;
				std::vector<Type*> types;
				std::vector<std::string> names;
				const std::string spec = entry->spec.args;
				const bool variadic = !spec.empty() && spec.back() == '*';
				const bool lastOptional = !spec.empty() && spec.back() == '?';
				const size_t fixed = (variadic || lastOptional) ? spec.size() - 1 : spec.size();
				if (variadic ? e->list.size() < fixed : lastOptional ? (e->list.size() + 1 < fixed || e->list.size() > fixed) : e->list.size() != fixed)
					error(e->pos, std::string(entry->spec.name) + " takes " + entry->spec.help);
				for (Node* arg : e->list)
				{
					if (arg->kind == NodeKind::TypeArg)
					{
						Type* t = templateArgument(arg->typeSyntax, bindings);
						const std::string builtin = entry->spec.name;
						if (t != nullptr && t->valueArg && builtin != "is_given" && builtin != "is_value")
							error(arg->pos, "'" + (arg->typeSyntax->written.empty() ? arg->typeSyntax->name : arg->typeSyntax->written) + "' is the value " + t->name() +
								" here, not a type (is_value(T) tells which; value_type(T) is a value's type)");
						if (t == nullptr && std::string(entry->spec.name) != "is_given")
							error(arg->pos, arg->typeSyntax->name + " is an optional template parameter that is not given here (check is_given(" + arg->typeSyntax->name + ") first)");
						if (t == m_notGiven)
							t = nullptr;
						types.push_back(t);
					}
					else
						names.push_back(arg->text);
				}
				return entry->fn(*this, types, names, e->pos);
			}
			case NodeKind::SizeofType:
			{
				Type* t = conditionType(e->typeSyntax, bindings);
				if (t == nullptr || t == m_notGiven)
					error(e->pos, "sizeof of an optional template parameter that is not given");
				if (t->kind == Type::Kind::Struct)
					resolveStruct(t->structInfo, e->pos);
				return t->size();
			}
			case NodeKind::SizeofExpr:
				if (e->a != nullptr && e->a->kind == NodeKind::Ident)
				{
					if (const Type* const* t = l_bound(e->a->text.empty() ? e->a->name : e->a->text))
					{
						if (*t == nullptr)
							error(e->pos, "sizeof of an optional template parameter that is not given");
						if ((*t)->kind == Type::Kind::Struct)
							resolveStruct((*t)->structInfo, e->pos);
						return (*t)->size();
					}
				}
				break;
			case NodeKind::Ident:
				if (const Type* const* t = l_bound(e->text.empty() ? e->name : e->text))
				{
					if (*t != nullptr && (*t)->valueArg && (*t)->base->kind != Type::Kind::Struct)
						return m_consts[(*t)->valueConst].value;		// a value parameter
					if (*t == nullptr || !(*t)->valueArg)
						error(e->pos, "'" + (e->text.empty() ? e->name : e->text) + "' is a type: use it in a built-in (is_integer(T), is_type(T, u32), sizeof(T), ...)");
					error(e->pos, "'" + (e->text.empty() ? e->name : e->text) + "' is a struct value: use its fields (" + (e->text.empty() ? e->name : e->text) + ".field)");
				}
				break;
			case NodeKind::Member:
			{
				// C.field (C a struct value parameter).
				std::string path = e->name;
				Node* root = e->a;
				while (root->kind == NodeKind::Member && root->value == 0)
				{
					path = root->name + "." + path;
					root = root->a;
				}
				if (root->kind == NodeKind::Ident && e->value == 0)
				{
					if (const Type* const* t = l_bound(root->text.empty() ? root->name : root->text))
					{
						if (*t == nullptr || !(*t)->valueArg || (*t)->base->kind != Type::Kind::Struct)
							error(e->pos, "'" + (root->text.empty() ? root->name : root->text) + "' is not a struct value");
						const ConstInfo& c = m_consts[(*t)->valueConst];
						auto it = c.fields.find(path);
						if (it == c.fields.end())
							error(e->pos, (*t)->base->name() + " has no (number) field '" + path + "'");
						return it->second;
					}
				}
				break;
			}
			default:
				break;
		}
		return constExpr(e, "a compile-time condition (constants, sizeof, comparisons, and / or / not, the built-ins: is_integer(T), ...)");
	}

	int32_t CodeGen::temporary(uint32_t size)
	{
		m_localCursor -= (int32_t)alignUp(size == 0 ? 4 : size, 4);
		m_localMin = std::min(m_localMin, m_localCursor);
		return m_localCursor;
	}

	bool CodeGen::isSharedData(const VarInfo* v) const
	{
		return m_options.shared && v->storage == VarInfo::Storage::Global && !v->readOnly;
	}

	void CodeGen::varAddress(const Reg& dst, VarInfo* v, bool taken)
	{
		if (!v->reg.empty())
			error({ "<compiler>", 0, 0 }, "internal error: the address of register variable '" + v->name + "'");
		if (v->optId >= 0 && m_optPass == 1 && taken)
			m_optVars[v->optId].eligible = false;
		switch (v->storage)
		{
			case VarInfo::Storage::Global:
				v->addressUsed = true;
				if (isSharedData(v))
				{
					// The process's copy: table[slot] + the offset (see CodeGenOptions::shared).
					emit("mov " + dst + ", [$__ds_libtable]");
					if (v->external)
					{
						emit("movo " + dst + ", *" + dst + ", dword $__dsslot__" + v->symbol);
						emit("addip " + dst + ", $__dsoff__" + v->symbol);
					}
					else
					{
						emit("movo " + dst + ", *" + dst + ", dword $__ds_slot");
						emit("addip " + dst + ", $__dsrw__" + v->symbol);
					}
				}
				else
					emit("mov " + dst + ", $" + v->symbol);
			break;
			case VarInfo::Storage::Local:
				emit("mov " + dst + ", FP");
				addImmediate(dst, v->offset);
			break;
			case VarInfo::Storage::Param:
				if (v->type->kind == Type::Kind::Struct)
				{
					// A struct (by value): its slot holds the address of the caller's copy.
					emit("movo " + dst + ", *PP, dword " + imm(-4 * v->offset));
					break;
				}
				// Arguments are 32-bit slots (big endian): a smaller value is in the slot's last bytes.
				emit("mov " + dst + ", PP");
				addImmediate(dst, -4 * v->offset + (4 - (int64_t)v->type->size()));
			break;
		}
	}

	void CodeGen::loadVar(const Reg& dst, VarInfo* v, bool fromMemory)
	{
		Type* t = v->type;
		optUse(v);
		if (!v->reg.empty() && !fromMemory)
		{
			emit("mov " + dst + ", " + v->reg);
			return;
		}
		if (!t->isScalar())
		{
			varAddress(dst, v);
			return;
		}
		const char* sizeSuffix = (t->size() == 1 ? "b" : (t->size() == 2 ? "w" : ""));
		if (v->storage == VarInfo::Storage::Global && isSharedData(v))
		{
			varAddress(dst, v, false);
			load(dst, dst, t);
			return;
		}
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
			// omov *d, *s, offset: the word at *s to d + offset (d stays, s moves on).
			for (uint32_t i = 0; i < words; i++)
			{
				emit("omov *" + d + ", *" + s + ", dword " + imm(i * 4));
				emit("addip " + s + ", 4");
			}
			for (uint32_t i = 0; i < size % 4; i++)
			{
				emit("omovb *" + d + ", *" + s + ", dword " + imm(words * 4 + i));
				emit("addip " + s + ", 1");
			}
			return;
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
		const uint32_t words = size / 4;
		if (words <= 8)
		{
			// One omov per word / byte, at dst + offset.
			for (uint32_t i = 0; i < words; i++)
				emit("omov *" + dst + ", 0x00000000, dword " + imm(i * 4));
			for (uint32_t i = 0; i < size % 4; i++)
				emit("omovb *" + dst + ", 0x00000000, dword " + imm(words * 4 + i));
			return;
		}
		const std::string d = ScratchDst;
		emit("mov " + d + ", " + dst);
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
		if ((e->isConst && e->type && (e->type->isArithmetic() || e->type->kind == Type::Kind::Null)) || e->constPtr)
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
					if (e->a->kind == NodeKind::Ident && e->a->var->ctValue)
					{
						// A struct value computed when compiling: a copy in the frame (as a call's result).
						const uint32_t size = e->a->var->type->size();
						Reg d = alloc(e->pos);
						emit("mov " + d + ", FP");
						addImmediate(d, temporary(size));
						Reg src = genAddr(e->a);
						copyMemory(d, src, size);
						release(src);
						return d;
					}
					return genAddr(e->a);
				}
				Reg r = gen(e->a);
				if (op == "-" && e->type->isFloat())
					emit("fneg " + r);
				else if (op == "!" && decay(e->a->type)->isFloat())
				{
					emit("feq " + r + ", 0x00000000");	// 1 when 0.0 / -0.0
					emit("mov " + r + ", ACC");
				}
				else if (op == "-")
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
			case NodeKind::Switch:
			{
				Reg out = alloc(e->pos);
				genSwitch(e, &out);
				return out;
			}
			case NodeKind::Materialize:
			{
				Type* t = e->type->base;
				const int32_t slot = temporary(t->size());
				Reg v = gen(e->a);
				Reg r = alloc(e->pos);
				emit("mov " + r + ", FP");
				addImmediate(r, slot);
				if (t->kind == Type::Kind::Struct)
					genCopy(r, v, t, e->pos);
				else
				{
					convert(v, decay(e->a->type), t);
					store(r, v, t);
				}
				emit("mov " + v + ", " + r);		// (registers are released in order)
				release(r);
				return v;
			}
			case NodeKind::InPlace:
			{
				Type* target = decay(e->a->type)->base;
				Reg addr = gen(e->a);
				if (e->value == 1)
				{
					if (destructible(target))
						genDestroy(addr, target, 0);
				}
				else if (e->list.empty())
					storeDefaults(addr, target);
				else
				{
					Node* arg = e->list[0];
					Reg v = gen(arg);
					if (target->kind == Type::Kind::Struct)
						genInitValue(addr, arg, v, target);		// (a copy, or a call's result moved in)
					else
					{
						convert(v, decay(arg->type), target);
						store(addr, v, target);
					}
					release(v);
				}
				return addr;
			}
			case NodeKind::Construct:
			if (e->value == 2)
			{
				// take(p): *p's bytes in a temporary (moved: no copy constructor).
				Type* t = e->type;
				const int32_t slot = temporary(t->size());
				Reg src = gen(e->a);
				Reg r = alloc(e->pos);
				emit("mov " + r + ", FP");
				addImmediate(r, slot);
				copyMemory(r, src, t->size());
				emit("mov " + src + ", " + r);
				release(r);
				addTemp(slot, t, e);
				return src;
			}
			{
				// A temporary: its defaults, then its constructor with the value (destroyed when the
				// statement ends, unless moved).
				Type* t = e->type;
				const int32_t slot = temporary(t->size());
				Reg r = alloc(e->pos);
				emit("mov " + r + ", FP");
				addImmediate(r, slot);
				storeDefaults(r, t);
				if (e->a == nullptr)
				{
					// T.new(args): the call, on the temporary.
					if (FuncInfo* f = e->func)
					{
						markUsed(f);
						if (f->isInline && !m_discarding)
							f->addressTaken = true;
						const auto& params = f->type->params;
						emit("push " + r);
						for (size_t i = 1; i < params.size(); i++)
						{
							if (i - 1 >= e->list.size())
							{
								Node* def = f->defaults[i];
								emit("push " + (def->kind == NodeKind::StrLit ? "$" + stringLiteral(def->text) : imm(constConvert(def->constValue, decay(def->type), params[i]))));
								continue;
							}
							Node* arg = e->list[i - 1];
							Reg v = gen(arg);
							if (params[i]->kind == Type::Kind::Struct)
							{
								Reg c = alloc(e->pos);
								const int32_t argSlot = temporary(params[i]->size());
								emit("mov " + c + ", FP");
								addImmediate(c, argSlot);
								genInitValue(c, arg, v, params[i]);
								emit("push " + c);
								release(c);
								release(v);
								if (!ownedParam(f->type, i))
									addTemp(argSlot, params[i], nullptr);
								continue;
							}
							convert(v, decay(arg->type), params[i]);
							emit("push " + v);
							release(v);
						}
						emit("push " + std::to_string(params.size()));
						emit("call $" + f->label);
					}
					addTemp(slot, t, e);
					return r;
				}
				Type* p = e->func->type->params[1];
				Reg v = gen(e->a);
				if (p->kind == Type::Kind::Struct)
				{
					Reg c = alloc(e->pos);
					const int32_t argSlot = temporary(p->size());
					emit("mov " + c + ", FP");
					addImmediate(c, argSlot);
					genInitValue(c, e->a, v, p);
					callMethod(e->func, r, 0, c);
					release(c);
					if (!ownedParam(e->func->type, 1))
						addTemp(argSlot, p, nullptr);
				}
				else
				{
					convert(v, decay(e->a->type), p);
					callMethod(e->func, r, 0, v);
				}
				release(v);
				addTemp(slot, t, e);
				return r;
			}
			case NodeKind::Ternary:
			{
				Reg r = alloc(e->pos);
				std::string lElse = newLabel(), lEnd = newLabel();
				genBranch(e->a, lElse, false);
				m_condDepth++;
				struct Depth { int& d; ~Depth() { d--; } } depth { m_condDepth };
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
				if (e->type->isScalar())
				{
					// movo base, *base, offset: the element / field without adding the offset first.
					Place p = genPlace(e);
					loadPlace(p.base, p, e->type);
					release(p.offsetReg);
					return p.base;
				}
				Reg r = genAddr(e);
				load(r, r, e->type);
				return r;
			}
			case NodeKind::Cast:
			if (e->value == 3)
			{
				Reg r = gen(e->a);
				Type* from = decay(e->a->type);
				if (from->kind == Type::Kind::Null || conversionCost(e->a, e->type) >= 0)
				{
					convert(r, from, e->type);		// (to a class it extends: known when compiling)
					return r;
				}
				// __ds_dyncast(a pointer to a table pointer of the object, the type wanted)
				const std::string done = newLabel();
				emit("mov ACC, " + r);
				emit("jeq $" + done + ", 0");
				addImmediate(r, from->base->structInfo->vptrs[0].offset);
				emit("push " + r);
				emit("push $" + typeinfoLabel(e->type->base->structInfo));
				emit("push 2");
				emit("call $__ds_dyncast");
				emit("mov " + r + ", RV");
				label(done);
				m_helpers.insert("__ds_dyncast");
				return r;
			}
			if (e->value == 1)
			{
				// bitcast: registers hold the bits already (a smaller integer: in its own form); a
				// struct or an array is its address (loaded from, or a copy in the frame).
				Type* from = e->a->type;
				Type* to = e->type;
				const bool fromMemory = from->kind == Type::Kind::Struct || from->kind == Type::Kind::Array;
				const bool toMemory = to->kind == Type::Kind::Struct;
				Reg r = gen(e->a);
				if (fromMemory && !toMemory)
					load(r, r, to);
				else if (!fromMemory && toMemory)
				{
					Reg d = alloc(e->pos);
					emit("mov " + d + ", FP");
					addImmediate(d, temporary(to->size()));
					store(d, r, from);
					emit("mov " + r + ", " + d);		// (registers are released in order: the address in r)
					release(d);
				}
				else if (!fromMemory)
					normalize(r, to);
				return r;
			}
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
			case NodeKind::Cast:
				if (e->value == 1 && e->type->kind == Type::Kind::Struct)
					return gen(e);		// (bitcast<Struct>(...).field)
			break;
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
			case NodeKind::Call:
				if (e->type != nullptr && e->type->kind == Type::Kind::Struct)
					return gen(e);		// a struct result: its place in this frame
			break;
			case NodeKind::Construct:
				return gen(e);
			default: break;
		}
		error(e->pos, "this has no address");
	}

	CodeGen::Place CodeGen::genPlace(Node* e)
	{
		Place p;
		switch (e->kind)
		{
			case NodeKind::Ident:
				if (e->var != nullptr && e->var->type->isScalar() && (e->var->storage == VarInfo::Storage::Local || e->var->storage == VarInfo::Storage::Param))
				{
					VarInfo* v = e->var;
					optUse(v);
					p.ownBase = false;
					if (!v->reg.empty())
					{
						p.base = v->reg;
						p.isReg = true;
						return p;
					}
					const bool local = (v->storage == VarInfo::Storage::Local);
					p.base = local ? "FP" : "PP";
					const int64_t offset = local ? v->offset : -4 * v->offset + (4 - (int64_t)v->type->size());
					if (offset != 0)
						p.offset = "dword " + imm(offset);
					return p;
				}
			break;
			case NodeKind::Index:
			{
				p.base = (e->a->type->kind == Type::Kind::Array) ? genAddr(e->a) : gen(e->a);
				const uint32_t size = e->type->size();
				if (e->b->isConst)
				{
					if ((uint32_t)(e->b->constValue * size) != 0)
						p.offset = "dword " + imm(e->b->constValue * size);
				}
				else
				{
					p.offsetReg = gen(e->b);
					scale(p.offsetReg, size);
					p.offset = p.offsetReg;
				}
				return p;
			}
			case NodeKind::Member:
			{
				Type* st = (e->value == 1) ? decay(e->a->type)->base : e->a->type;
				p.base = (e->value == 1) ? gen(e->a) : genAddr(e->a);
				const uint32_t offset = st->structInfo->field(e->name)->offset;
				if (offset != 0)
					p.offset = "dword " + imm(offset);
				return p;
			}
			default: break;
		}
		p.base = genAddr(e);
		return p;
	}

	static const char* sizeSuffix(const Type* t)
	{
		return t->size() == 1 ? "b" : (t->size() == 2 ? "w" : "");
	}

	void CodeGen::loadPlace(const Reg& dst, const Place& p, Type* t)
	{
		if (p.isReg)
		{
			emit("mov " + dst + ", " + p.base);	// (kept normalized: every store converts)
			return;
		}
		if (p.offset.empty())
			emit(std::string("mov") + sizeSuffix(t) + " " + dst + ", *" + p.base);
		else
			emit(std::string("mov") + sizeSuffix(t) + "o " + dst + ", *" + p.base + ", " + p.offset);
		if (t->kind == Type::Kind::I8 || t->kind == Type::Kind::I16)
			normalize(dst, t);
	}

	void CodeGen::storePlace(const Place& p, const std::string& value, Type* t)
	{
		if (p.isReg)
		{
			emit("mov " + p.base + ", " + value);
			return;
		}
		const bool isRegister = value.size() >= 2 && value[0] == 'R' && std::isdigit((unsigned char)value[1]);
		if (!isRegister)
		{
			// An immediate (or a label): omov takes one, at base + offset (0 if none).
			emit(std::string("omov") + sizeSuffix(t) + " *" + p.base + ", " + value + ", " + (p.offset.empty() ? std::string("dword 0x00000000") : p.offset));
			return;
		}
		if (p.offset.empty())
		{
			emit(std::string("mov") + sizeSuffix(t) + " *" + p.base + ", " + value);
			return;
		}
		// omov *base, reg, offset (extmov): the register at base + offset.
		emit(std::string("omov") + sizeSuffix(t) + " *" + p.base + ", " + value + ", " + p.offset);
	}

	void CodeGen::releasePlace(const Place& p)
	{
		release(p.offsetReg);
		if (p.ownBase)
			release(p.base);
	}

	CodeGen::Reg CodeGen::placeResult(const Place& p, const Reg& v)
	{
		if (!p.ownBase && p.offsetReg.empty())
			return v;
		emit("mov " + p.base + ", " + v);
		release(v);
		release(p.offsetReg);
		return p.base;
	}

	std::string CodeGen::constImmediate(Node* value, Type* t)
	{
		if (value == nullptr || !value->isConst || value->type == nullptr)
			return "";
		const Type* from = decay(value->type);
		const bool number = from->isInteger() || from->isFloat();
		if (t->kind == Type::Kind::Bool)
			return number ? imm(constTruth(value->constValue, from) ? 1 : 0) : "";
		if ((t->isInteger() || t->isFloat()) && number)
			return imm(constConvert(value->constValue, from, t));
		if (t->kind == Type::Kind::Pointer && from->kind == Type::Kind::Null)
			return imm(0);
		return "";
	}

	std::string CodeGen::argImmediate(Node* arg, Type* param)
	{
		if (!param->isScalar())
			return "";
		foldStringConst(arg);
		if (arg->kind == NodeKind::StrLit && param->kind == Type::Kind::Pointer)
			return "$" + stringLiteral(arg->text);
		if (arg->kind == NodeKind::Ident && arg->var == nullptr && arg->func != nullptr && param->kind == Type::Kind::Function)
			return "$" + arg->func->label;
		return constImmediate(arg, param);
	}

	void CodeGen::genDiscard(Node* e)
	{
		if (e->kind == NodeKind::Assign)
			release(genAssign(e, true));
		else if (e->kind == NodeKind::PreInc || e->kind == NodeKind::PreDec || e->kind == NodeKind::PostInc || e->kind == NodeKind::PostDec)
			release(genIncDec(e, true));
		else
			release(gen(e));
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
			if (constTruth(e->constValue, decay(e->type)) == jumpIfTrue)
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
				m_condDepth++;		// (the second side may not run: its temporaries are flagged)
				genBranch(e->b, target, jumpIfTrue);
				m_condDepth--;
			}
			else
			{
				std::string skip = newLabel();
				genBranch(e->a, skip, !jumpIfTrue);
				m_condDepth++;
				genBranch(e->b, target, jumpIfTrue);
				m_condDepth--;
				label(skip);
			}
			return;
		}
		if (e->kind == NodeKind::Binary && isComparison(e->name) && (decay(e->a->type)->isFloat() || decay(e->b->type)->isFloat()))
		{
			// feq / flt / fle set ACC to 1 / 0 (0 when an operand is NaN, so every comparison
			// with NaN is false, and != true). > and >= swap the operands.
			Type* f = m_types.f32();
			Reg a = gen(e->a);
			convert(a, decay(e->a->type), f);
			Reg b = gen(e->b);
			convert(b, decay(e->b->type), f);
			const std::string& op = e->name;
			bool negate = false;
			if (op == "==") emit("feq " + a + ", " + b);
			else if (op == "!=") { emit("feq " + a + ", " + b); negate = true; }
			else if (op == "<") emit("flt " + a + ", " + b);
			else if (op == "<=") emit("fle " + a + ", " + b);
			else if (op == ">") emit("flt " + b + ", " + a);
			else emit("fle " + b + ", " + a);
			const bool jumpOnOne = (jumpIfTrue != negate);
			emit(std::string(jumpOnOne ? "jne" : "jeq") + " $" + target + ", 0");
			release(b);
			release(a);
			return;
		}
		if (e->kind == NodeKind::Binary && isComparison(e->name))
		{
			Type* ta = decay(e->a->type);
			Type* tb = decay(e->b->type);
			// Unsigned order (== and != are the same either way).
			const bool isUnsigned = ((ta->isInteger() && tb->isInteger()) ? arithmeticType(ta, tb)->kind == Type::Kind::U32 : true)
									&& e->name != "==" && e->name != "!=";
			Reg a = gen(e->a);
			std::string operand;
			Reg b;
			if (e->b->isConst)
				operand = imm(e->b->constValue);
			else
			{
				b = gen(e->b);
				operand = b;
			}
			// Unsigned order: the u jumps (jgru, jlsu, jgeu, jleu) compare as u32s.
			emit("mov ACC, " + a);
			emit(std::string(jumpFor(e->name, jumpIfTrue)) + (isUnsigned ? "u" : "") + " $" + target + ", " + operand);
			release(b);
			release(a);
			return;
		}
		Reg r = gen(e);
		if (decay(e->type)->isFloat())
		{
			// True when not 0.0 / -0.0 (NaN is true): feq gives 1 for zero.
			emit("feq " + r + ", 0x00000000");
			emit(std::string(jumpIfTrue ? "jeq" : "jne") + " $" + target + ", 0");
			release(r);
			return;
		}
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
		if (e->type->isFloat())
		{
			Reg a = gen(e->a);
			convert(a, ta, e->type);
			Reg b;
			std::string operand;
			if (e->b->isConst)
				operand = imm(constConvert(e->b->constValue, tb, e->type));	// the f32's bits
			else
			{
				b = gen(e->b);
				convert(b, tb, e->type);
				operand = b;
			}
			const char* inst = (op == "+") ? "fadd " : (op == "-") ? "fsub " : (op == "*") ? "fmul " : "fdiv ";
			emit(inst + a + ", " + operand);
			release(b);
			return a;
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

	CodeGen::Reg CodeGen::genAssign(Node* e, bool discard)
	{
		Type* t = e->a->type;
		if (t->kind == Type::Kind::Struct)
		{
			Reg d = genAddr(e->a);
			Reg s = gen(e->b);
			// (a class: the object keeps its vtable pointers, it may be of a derived class)
			std::vector<Reg> kept;
			for (const auto& v : t->structInfo->vptrs)
			{
				kept.push_back(alloc(e->pos));
				emit("movo " + kept.back() + ", *" + d + ", dword " + imm(v.offset));
			}
			if (!customAssign(t))
				copyMemory(d, s, t->size());
			else if ((e->b->kind == NodeKind::Call || e->b->kind == NodeKind::Construct) && ownsResources(t->structInfo) && m_funcs.count(t->structInfo->name + "::operator=") == 0)
			{
				// From a call's result: the old value destroyed, the result moved in.
				genDestroy(d, t, 0);
				copyMemory(d, s, t->size());
				consumeTemp(e->b);
			}
			else
				genAssignValue(d, s, t, e->pos);
			for (size_t i = kept.size(); i-- > 0; )
			{
				emit("omov *" + d + ", " + kept[i] + ", dword " + imm(t->structInfo->vptrs[i].offset));
				release(kept[i]);
			}
			release(s);
			release(d);
			return "";
		}
		Place d = genPlace(e->a);
		if (e->name == "=")
		{
			if (discard)
			{
				const std::string c = constImmediate(e->b, t);
				if (!c.empty())
				{
					storePlace(d, c, t);
					releasePlace(d);
					return "";
				}
			}
			Reg v = gen(e->b);
			convert(v, decay(e->b->type), t);
			storePlace(d, v, t);
			if (discard)
			{
				release(v);
				releasePlace(d);
				return "";
			}
			return placeResult(d, v);
		}
		const std::string op = e->name.substr(0, e->name.size() - 1);
		if (t->isFloat())
		{
			Reg v = alloc(e->pos);
			loadPlace(v, d, t);
			Reg x;
			std::string operand;
			if (e->b->isConst)
				operand = imm(constConvert(e->b->constValue, decay(e->b->type), t));
			else
			{
				x = gen(e->b);
				convert(x, decay(e->b->type), t);
				operand = x;
			}
			const char* inst = (op == "+") ? "fadd " : (op == "-") ? "fsub " : (op == "*") ? "fmul " : "fdiv ";
			emit(inst + v + ", " + operand);
			release(x);
			storePlace(d, v, t);
			if (discard)
			{
				release(v);
				releasePlace(d);
				return "";
			}
			return placeResult(d, v);
		}
		Reg v = alloc(e->pos);
		loadPlace(v, d, t);
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
		storePlace(d, v, t);
		if (discard)
		{
			release(v);
			releasePlace(d);
			return "";
		}
		return placeResult(d, v);
	}

	CodeGen::Reg CodeGen::genIncDec(Node* e, bool discard)
	{
		Type* t = e->type;
		const bool inc = (e->kind == NodeKind::PreInc || e->kind == NodeKind::PostInc);
		const bool post = (e->kind == NodeKind::PostInc || e->kind == NodeKind::PostDec);
		const int64_t step = (t->kind == Type::Kind::Pointer) ? std::max<uint32_t>(1, t->base->size()) : 1;
		auto l_step = [&](const Reg& r) {
			if (t->isFloat())
				emit("fadd " + r + ", " + imm(bitsOf(inc ? 1.0f : -1.0f)));
			else
				addImmediate(r, inc ? step : -step);
			if (t->isInteger())
				normalize(r, t);
		};
		Place d = genPlace(e->a);
		Reg v = alloc(e->pos);
		loadPlace(v, d, t);
		if (post && !discard)
		{
			// The old value is the result: the new one in another register.
			Reg n = alloc(e->pos);
			emit("mov " + n + ", " + v);
			l_step(n);
			storePlace(d, n, t);
			release(n);
			return placeResult(d, v);
		}
		l_step(v);
		storePlace(d, v, t);
		if (discard)
		{
			release(v);
			releasePlace(d);
			return "";
		}
		return placeResult(d, v);
	}

	// Inline sub call: the arguments go into new locals of the caller, the body is generated
	// here (seeing only its parameters and the globals), return jumps to the end, with the
	// value in RV (as a real call).
	void CodeGen::checkInlineRecursion(void)
	{
		// A cycle of calls between subs declared inline (each reported once, at the call that
		// closes it).
		std::map<FuncInfo*, int> state;	// 1: on the current path, 2: done
		std::function<void(FuncInfo*)> l_visit = [&](FuncInfo* f) {
			state[f] = 1;
			for (const auto& call : m_inlineCalls[f])
			{
				if (state[call.first] == 1)
				{
					attempt([&] {
						if (call.first == f)
							error(call.second, "inline sub '" + f->name + "' calls itself: inline subs cannot be recursive");
						error(call.second, "inline sub '" + call.first->name + "' calls itself (directly or through another "
							"inline sub): inline subs cannot be recursive");
					});
				}
				else if (state[call.first] == 0)
					l_visit(call.first);
			}
			state[f] = 2;
		};
		for (FuncInfo* f : m_funcList)
		{
			if (state[f] == 0)
				l_visit(f);
		}
	}

	CodeGen::Reg CodeGen::genInlineCall(Node* e, FuncInfo* f)
	{
		prepareConstructor(f);
		prepareDestructor(f);
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
		std::vector<VarInfo*> inlineParams;
		for (size_t i = 0; i < params.size(); i++)
		{
			VarInfo* v = new VarInfo();
			v->name = f->paramNames[i];
			v->type = params[i];
			v->storage = VarInfo::Storage::Local;
			v->pos = f->decl->list[i]->pos;
			v->defPos = f->decl->list[i]->namePos;
			v->param = true;
			v->isConst = f->decl->list[i]->constKind == 1;
			if (m_options.index != nullptr && !m_discarding)
				indexName(v->defPos, v->name, v->defPos, hoverVar(v));
			optRegister(v, false);
			Place slot;
			slot.ownBase = false;
			if (v->reg.empty())
			{
				v->offset = temporary(params[i]->kind == Type::Kind::Struct ? params[i]->size() : 4);	// (a struct: its copy)
				slot.base = "FP";
				slot.offset = "dword " + imm(v->offset);
			}
			else
			{
				slot.base = v->reg;
				slot.isReg = true;
			}
			std::string value;
			if (i >= e->list.size())
			{
				Node* def = f->defaults[i];
				value = (def->kind == NodeKind::StrLit) ? "$" + stringLiteral(def->text) : imm(constConvert(def->constValue, decay(def->type), params[i]));
			}
			else
				value = argImmediate(e->list[i], params[i]);
			if (i == 0 && (e->callFlags & 64) != 0)
			{
				// p->new(args): the object's defaults, then the constructor's body on it.
				Reg r = gen(e->list[0]);
				convert(r, decay(e->list[0]->type), params[0]);
				storeDefaults(r, params[0]->base);
				storePlace(slot, r, params[0]);
				release(r);
			}
			else if (!value.empty() && params[i]->isScalar())
				storePlace(slot, value, params[i]);
			else if (params[i]->isScalar())
			{
				Reg r = gen(e->list[i]);
				convert(r, decay(e->list[i]->type), params[i]);
				storePlace(slot, r, params[i]);
				release(r);
			}
			else
			{
				Reg r = gen(e->list[i]);
				convert(r, decay(e->list[i]->type), params[i]);
				Reg a = alloc(e->pos);
				varAddress(a, v);
				if (params[i]->kind == Type::Kind::Struct)
				{
					genInitValue(a, e->list[i], r, params[i]);		// (destroyed when the statement ends; own: by the sub)
					if (ownedParam(f->type, i))
						v->owned = true;
					else
						addTemp(v->offset, params[i], nullptr);
				}
				else
					store(a, r, params[i]);
				release(a);
				release(r);
			}
			paramScope[v->name] = v;
			inlineParams.push_back(v);
		}
		// The body sees its parameters (and the globals), not the caller's variables or loops.
		auto scopes = std::move(m_scopes);
		auto loops = std::move(m_loops);
		auto defers = std::move(m_defers);
		const int inDefer = m_inDefer;
		m_scopes.clear();
		m_scopes.push_back(std::move(paramScope));
		m_loops.clear();
		m_defers.clear();
		m_defers.emplace_back();
		m_inDefer = 0;
		auto forgetNames = std::move(m_forgetNames);
		m_forgetNames.clear();
		collectForgets(f->decl->a, m_forgetNames);
		destroyOwnedParams(inlineParams);
		const int32_t result = returnsStruct(f->type) ? temporary(f->type->base->size()) : 0;	// (a struct result: here)
		int32_t errorSlot = 0;
		if (f->type->throws != nullptr)
		{
			if (e->handler == nullptr)
				error(e->pos, "internal error: a call of a sub that throws without try / catch");
			errorSlot = (int32_t)e->handler->constValue;
			emit("omov *FP, 0x00000000, dword " + imm(errorSlot));
		}
		m_inline.push_back({ f, newLabel(), result, errorSlot, m_temps.size() });
		const int stmtDepth = m_stmtDepth;
		m_stmtDepth = m_regDepth;	// the caller's temporaries stay in their registers
		const Loc callerLoc = m_loc;
		Node* body = f->decl->a;
		for (Node* s : body->list)
			genStatement(s);
		m_forgetNames = std::move(forgetNames);
		const bool endsWithReturn = !body->list.empty() && (body->list.back()->kind == NodeKind::Return || body->list.back()->kind == NodeKind::Throw);
		if (!endsWithReturn)
		{
			loc(body->endPos);
			genDefers(0);
			if (f->type->base->kind != Type::Kind::Void && !returnsStruct(f->type))
				emit("mov RV, 0");
		}
		label(m_inline.back().endLabel);
		m_inline.pop_back();
		// The rest of the caller's statement (the result, the rest of the expression).
		if (callerLoc.file >= 0)
			loc(callerLoc, false);
		else
			noLoc();
		m_stmtDepth = stmtDepth;
		optEndScope(m_scopes.front());		// the parameters
		m_scopes = std::move(scopes);
		m_loops = std::move(loops);
		m_defers = std::move(defers);
		m_inDefer = inDefer;
		if (f->type->base->kind == Type::Kind::Void)
		{
			if (f->type->throws != nullptr)
				genCallError(e, "");
			return "";
		}
		Reg r = alloc(e->pos);
		if (returnsStruct(f->type))
		{
			emit("mov " + r + ", FP");
			addImmediate(r, result);
			if (f->type->throws != nullptr)
				genCallError(e, r);
			addTemp(result, f->type->base, e);
			return r;
		}
		emit("mov " + r + ", RV");
		if (f->type->throws != nullptr)
			genCallError(e, r);
		return r;
	}

	CodeGen::Reg CodeGen::genCall(Node* e)
	{
		if (e->vaBuiltin == 4)
			error(e->pos, "forget(v) is a statement of its own (forget(v);)");
		if (e->vaBuiltin != 0)
		{
			// The count the caller pushed is in the CPU's frame (FP + 104: after the 26 words of
			// the frame); the arguments are at PP - 4 * i, the extra ones after the parameters,
			// then (...typed) their types.
			const int64_t fixed = (int64_t)m_func->type->params.size() + (int64_t)hiddenSlots(m_func->type);
			const bool typed = m_func->type->variadic == 2;
			Reg r = alloc(e->pos);
			if (e->vaBuiltin == 2)
			{
				emit("mov " + r + ", PP");
				addImmediate(r, -4 * fixed);
				return r;
			}
			emit("movo " + r + ", *FP, dword " + imm(104));
			addImmediate(r, -fixed);
			if (typed)
				emit("rsh " + r + ", 1");
			if (e->vaBuiltin == 1)
				return r;
			// va_types: after the count extra values.
			Reg t = alloc(e->pos);
			emit("lsh " + r + ", 2");
			emit("mov " + t + ", PP");
			addImmediate(t, -4 * fixed);
			emit("subip " + t + ", " + r);
			emit("mov " + r + ", " + t);
			release(t);
			return r;
		}
		FuncInfo* f = e->func;
		const bool virtualCall = e->vslot != -1;
		// (slot n: the sub at 16 + 8n, what to add to this at 20 + 8n; the destructor: at 8 and 12)
		const int32_t slotOffset = e->vslot == -2 ? 8 : 16 + 8 * e->vslot;
		if (f && f->isInline && !virtualCall)
			return genInlineCall(e, f);
		if (f && m_options.noInline && m_func && (m_func->decl->value & 2) != 0 && (f->decl->value & 2) != 0)
			m_inlineCalls[m_func].push_back({ f, e->pos });
		Type* fnType = nullptr;
		Reg fr;
		if (virtualCall)
		{
			fr = alloc(e->pos);		// (the sub, from the table: after the object's pointer is known)
			fnType = f->type;
		}
		else if (f == nullptr)
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
		// A struct result: the address of the caller's room for it, first.
		int32_t result = 0;
		if (returnsStruct(fnType))
		{
			result = temporary(fnType->base->size());
			Reg r = alloc(e->pos);
			emit("mov " + r + ", FP");
			addImmediate(r, result);
			emit("push " + r);
			release(r);
		}
		// A sub that throws: the address of the error slot (cleared: the sub only writes it when it fails).
		if (fnType->throws != nullptr)
		{
			if (e->handler == nullptr)
				error(e->pos, "internal error: a call of a sub that throws without try / catch");
			const int32_t slot = (int32_t)e->handler->constValue;
			emit("omov *FP, 0x00000000, dword " + imm(slot));
			Reg r = alloc(e->pos);
			emit("mov " + r + ", FP");
			addImmediate(r, slot);
			emit("push " + r);
			release(r);
		}
		for (size_t i = 0; i < e->list.size(); i++)
		{
			if (i >= params.size())
			{
				// An extra argument (...): its own type, widened to a 32-bit slot.
				Type* slot = varArgType(e->list[i]->type);
				const std::string value = argImmediate(e->list[i], slot);
				if (!value.empty())
				{
					emit("push " + value);
					continue;
				}
				Reg r = gen(e->list[i]);
				convert(r, decay(e->list[i]->type), slot);
				emit("push " + r);
				release(r);
				continue;
			}
			if (i == 0 && virtualCall)
			{
				// this: the object's table has the sub (slot n: at 16 + 8n) and what to add to this
				// for it (at 20 + 8n).
				Reg self = gen(e->list[0]);
				convert(self, decay(e->list[0]->type), params[0]);
				emit("mov " + fr + ", *" + self);
				Reg delta = alloc(e->pos);
				emit("movo " + delta + ", *" + fr + ", dword " + imm(slotOffset + 4));
				emit("movo " + fr + ", *" + fr + ", dword " + imm(slotOffset));
				emit("addip " + self + ", " + delta);
				release(delta);
				emit("push " + self);
				release(self);
				continue;
			}
			if (i == 0 && (e->callFlags & 64) != 0)
			{
				// p->new(args): the object's defaults, then its constructor on it.
				Reg self = gen(e->list[0]);
				convert(self, decay(e->list[0]->type), params[0]);
				storeDefaults(self, params[0]->base);
				emit("push " + self);
				release(self);
				continue;
			}
			if (params[i]->kind == Type::Kind::Struct)
			{
				// By value: a copy in this frame, passed by its address (a call's result: itself),
				// destroyed when the statement ends. An own parameter: the sub destroys it (a call's
				// result is given to it, a copy is not destroyed here).
				Reg src = gen(e->list[i]);
				const bool own = ownedParam(fnType, i);
				if ((e->list[i]->kind == NodeKind::Call || e->list[i]->kind == NodeKind::Construct) && (own || destructible(params[i]) || customCopy(params[i])))
				{
					storeVptrs(src, params[i]);
					if (own)
						consumeTemp(e->list[i]);
					emit("push " + src);
					release(src);
					continue;
				}
				Reg dst = alloc(e->pos);
				const int32_t slot = temporary(params[i]->size());
				emit("mov " + dst + ", FP");
				addImmediate(dst, slot);
				genCopy(dst, src, params[i], e->list[i]->pos);
				emit("push " + dst);
				release(dst);
				release(src);
				if (!own)
					addTemp(slot, params[i], nullptr);
				continue;
			}
			const std::string value = argImmediate(e->list[i], params[i]);
			if (!value.empty())
			{
				emit("push " + value);
				continue;
			}
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
				emit("push " + imm(constConvert(def->constValue, decay(def->type), params[i])));
		}
		size_t slots = params.size() + hiddenSlots(fnType);
		if (fnType->variadic != 0 && e->list.size() > params.size())
		{
			const size_t extra = e->list.size() - params.size();
			if (fnType->variadic == 2)
			{
				for (size_t i = params.size(); i < e->list.size(); i++)
					emit("push " + imm(varArgTypeCode(e->list[i]->type)));
				slots += extra;
			}
			slots += extra;
		}
		emit("push " + std::to_string(slots));
		if (f && !virtualCall)
			emit("call $" + f->label);
		else
			emit("call " + fr);
		if (fnType->base->kind == Type::Kind::Void)
		{
			release(fr);
			if (fnType->throws != nullptr)
				genCallError(e, "");
			return "";
		}
		if (fr.empty())
			fr = alloc(e->pos);
		if (returnsStruct(fnType))
		{
			// The value: the caller's copy (a struct value is its address; a temporary only if the
			// call didn't fail).
			emit("mov " + fr + ", FP");
			addImmediate(fr, result);
			if (fnType->throws != nullptr)
				genCallError(e, fr);
			addTemp(result, fnType->base, e);
			return fr;
		}
		emit("mov " + fr + ", RV");
		if (fnType->throws != nullptr)
			genCallError(e, fr);
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
				{
					checkFieldAccess(type->structInfo, &fields[i], init->list[i]->pos);
					genInit(addr, fields[i].type, init->list[i], offset + (int32_t)fields[i].offset);
				}
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
			genInitValue(d, init, s, type);
			release(s);
			release(d);
			return;
		}
		if (type->kind == Type::Kind::Array)
			error(init->pos, "arrays are initialized with { ... } or a string");
		Place p;
		p.base = addr;
		p.ownBase = false;
		if (offset != 0)
			p.offset = "dword " + imm(offset);
		const std::string c = constImmediate(init, type);
		if (!c.empty())
		{
			storePlace(p, c, type);
			return;
		}
		Reg v = gen(init);
		convert(v, decay(init->type), type);
		storePlace(p, v, type);
		release(v);
	}

	// The struct's default field values (the memory was zeroed), and its classes' vtable pointers
	// (vptrs: the offset, and the table's label).
	void CodeGen::collectDefaults(Type* type, int32_t offset, std::vector<std::pair<int32_t, const StructField*>>& out, std::vector<std::pair<int32_t, std::string>>* vptrs)
	{
		if (type->kind == Type::Kind::Struct)
		{
			if (vptrs != nullptr)
			{
				for (const auto& v : type->structInfo->vptrs)
					vptrs->push_back({ offset + (int32_t)v.offset, vtableLabel(type->structInfo, v.offset) });
			}
			for (const auto& f : type->structInfo->fields)
			{
				if (f.init)
					out.push_back({ offset + (int32_t)f.offset, &f });
				else
					collectDefaults(f.type, offset + (int32_t)f.offset, out, vptrs);
			}
		}
		else if (type->kind == Type::Kind::Array && type->base->kind == Type::Kind::Struct)
		{
			for (uint32_t i = 0; i < type->count; i++)
				collectDefaults(type->base, offset + (int32_t)(i * type->base->size()), out, vptrs);
		}
	}

	void CodeGen::genLocalVar(Node* s)
	{
		if (m_scopes.back().count(s->name) > 0)
			error(s->pos, "'" + s->name + "' is already declared in this block");
		Type* t = nullptr;
		bool refFrame = false;
		inferArraySize(s->typeSyntax, s->a, s->pos);
		if (s->typeSyntax)
			t = resolveType(s->typeSyntax);
		else if (s->refKind == 0)
		{
			check(s->a);
			t = decay(s->a->type);
			if (t == nullptr || t->kind == Type::Kind::Null || t->kind == Type::Kind::Void)
				error(s->pos, "cannot tell the type of '" + s->name + "' (write var " + s->name + ": <type>)");
			if (t->isInteger() && s->a->kind == NodeKind::IntLit && s->a->name != "u")	// (var x = 5u: a u32)
				t = m_types.i32();
		}
		if (s->refKind != 0 || t->isReference())
		{
			// A reference local: a pointer to what it refers to (used as that: see checkIdent).
			t = bindLocalReference(s, s->refKind != 0 ? nullptr : t);
			if (t->isReference())
				refFrame = refersToFrame(s->a->a);
		}
		if (t->kind == Type::Kind::Void)
			error(s->pos, "variables cannot be void");
		if (t->kind == Type::Kind::Struct)
		{
			resolveStruct(t->structInfo, s->pos);
			requireValueType(t, s->pos, "a variable");
		}
		VarInfo* v = new VarInfo();
		v->refFrame = refFrame;
		v->name = s->name;
		v->type = t;
		v->storage = VarInfo::Storage::Local;
		v->pos = s->pos;
		v->defPos = s->namePos;
		v->isConst = s->constKind == 1;
		v->constKind = s->constKind;
		if (m_options.index != nullptr && s->hoisted == nullptr)
		{
			indexName(v->defPos, v->name, v->defPos, hoverVar(v));
			completeLocal(v);
		}
		optRegister(v, false);
		if (v->reg.empty())
		{
			m_localCursor -= (int32_t)alignUp(std::max<uint32_t>(t->size(), 1), 4);
			v->offset = m_localCursor;
			m_localMin = std::min(m_localMin, m_localCursor);
		}

		const bool aggregate = (t->kind == Type::Kind::Struct || t->kind == Type::Kind::Array);
		std::vector<std::pair<int32_t, const StructField*>> defaults;
		std::vector<std::pair<int32_t, std::string>> vptrs;
		collectDefaults(t, 0, defaults, &vptrs);
		if (aggregate && (s->a == nullptr || s->a->kind == NodeKind::InitList))
		{
			if (s->a != nullptr || !defaults.empty() || t->kind == Type::Kind::Struct)
			{
				Reg a = alloc(s->pos);
				varAddress(a, v);
				zeroMemory(a, t->size());
				for (const auto& d : defaults)
				{
					std::string val;
					if (d.second->init->kind == NodeKind::StrLit)
						val = "$" + stringLiteral(d.second->init->text);
					else if (d.second->init->kind == NodeKind::Ident && d.second->init->func != nullptr)
						val = "$" + d.second->init->func->label;
					else
						val = imm(constConvert(d.second->init->constValue, decay(d.second->init->type), d.second->type));
					Place p;
					p.base = a;
					p.ownBase = false;
					if (d.first != 0)
						p.offset = "dword " + imm(d.first);
					storePlace(p, val, d.second->type);
				}
				for (const auto& vp : vptrs)
					emit("omov *" + a + ", $" + vp.second + ", dword " + imm(vp.first));
				if (s->a)
					genInit(a, t, s->a, 0);
				release(a);
			}
		}
		else if (s->a && !v->reg.empty())
		{
			// A register variable (a scalar): the value into its register.
			foldStringConst(s->a);
			check(s->a, t);
			requireConvertible(s->a, t, "in the initialization");
			Place p;
			p.base = v->reg;
			p.ownBase = false;
			p.isReg = true;
			const std::string c = constImmediate(s->a, t);
			if (!c.empty())
				storePlace(p, c, t);
			else
			{
				Reg r = gen(s->a);
				convert(r, decay(s->a->type), t);
				storePlace(p, r, t);
				release(r);
			}
		}
		else if (s->a)
			genInit("FP", t, s->a, v->offset);	// the variable at FP + offset
		if (s->c)
			genConstructor(v, s);
		m_scopes.back()[s->name] = v;
		if (destructible(t) && !m_defers.empty())
		{
			setDropFlag(v);
			// Its destructor, when its scope ends (as a defer: also by return, break, continue).
			Node* destroy = new Node();
			destroy->kind = NodeKind::Destroy;
			destroy->value = 1;
			destroy->pos = s->pos;
			destroy->var = v;
			m_defers.back().push_back({ destroy, m_scopes, m_atomic, v });
		}
	}

	void CodeGen::genConstructor(VarInfo* v, Node* decl)
	{
		Type* t = v->type;
		if (t->kind != Type::Kind::Struct)
			error(decl->c->pos, "'" + decl->name + "' is " + t->name() + ": only struct variables have a constructor (var p: Point(...))");
		StructInfo* info = t->structInfo;
		if (m_funcs.count(info->name + "::new") == 0)
			error(decl->c->pos, (info->anonymous ? info->displayName() : "struct " + info->name) + " has no constructor (sub new(...) in the struct)");
		// The variable is bound to a hidden name: the arguments see the names around the
		// declaration (var x: P(x) uses the outer x).
		m_scopes.push_back({ { " ctor", v } });
		check(decl->c);
		release(gen(decl->c));
		m_scopes.pop_back();
	}

	bool CodeGen::hasDefers(size_t downTo) const
	{
		for (size_t i = downTo; i < m_defers.size(); i++)
		{
			if (!m_defers[i].empty())
				return true;
		}
		return false;
	}

	void CodeGen::genDefers(size_t downTo)
	{
		for (size_t i = m_defers.size(); i-- > downTo; )
		{
			// (copied: generating a defer adds frames)
			const std::vector<Deferred> frame = m_defers[i];
			for (size_t j = frame.size(); j-- > 0; )
				genDeferred(frame[j]);
		}
	}

	void CodeGen::genDeferred(const Deferred& d)
	{
		if (d.destroys != nullptr && d.destroys == m_movedVar)
			return;		// (a return moved this local out)
		// With the variables of where it was written; no loops to leave; the defers in it have a
		// frame of their own (run at its end).
		auto scopes = std::move(m_scopes);
		auto loops = std::move(m_loops);
		m_scopes = d.scopes;
		m_loops.clear();
		const int atomic = m_atomic;
		m_atomic = d.atomic;		// (run where it was written: inside or outside of atomic blocks)
		m_inDefer++;
		m_defers.emplace_back();
		genStatement(d.body);
		if (hasDefers(m_defers.size() - 1))
			genDefers(m_defers.size() - 1);
		m_defers.pop_back();
		m_inDefer--;
		m_atomic = atomic;
		m_scopes = std::move(scopes);
		m_loops = std::move(loops);
	}

	void CodeGen::genStatement(Node* s)
	{
		if (s->kind == NodeKind::Destroy)
		{
			Reg r = alloc(s->pos);
			if (s->value == 1)
			{
				// (a local's destructor, when its scope ends; with a drop flag: unless forget(v) ran)
				std::string skip;
				if (s->var->hasDropFlag)
				{
					skip = newLabel();
					emit("movo " + r + ", *FP, dword " + imm(s->var->dropFlag));
					emit("mov ACC, " + r);
					emit("jeq $" + skip + ", 0");
				}
				varAddress(r, s->var);
				genDestroy(r, s->var->type, 0);
				if (!skip.empty())
					label(skip);
			}
			else
			{
				// (a destructor's end: its fields, the last first, then its base's part)
				VarInfo* self = lookupVar("this");
				loadVar(r, self);
				StructInfo* info = self->type->base->structInfo;
				for (size_t i = info->fields.size(); i-- > 0; )
				{
					if (info->fields[i].owner == nullptr)
						genDestroy(r, info->fields[i].type, (int32_t)info->fields[i].offset);
				}
				if (info->base != nullptr && destructible(m_types.structType(info->base)))
					callMethod(destructorOf(info->base), r, (int32_t)info->baseOffset);
			}
			release(r);
			return;
		}
		if (s->kind == NodeKind::SetVtables)
		{
			// (a class's constructor, after its base's: its own tables)
			VarInfo* self = lookupVar("this");
			Reg r = alloc(s->pos);
			loadVar(r, self);
			storeVptrs(r, self->type->base);
			release(r);
			return;
		}
		if (s->kind != NodeKind::Block)
			loc(s->pos);
		switch (s->kind)
		{
			case NodeKind::Block:
			{
				m_scopes.push_back({});
				// (endPos == pos: a for-in's own block around a body that isn't a block. Its
				// variable's scope, for the language server, ends with the enclosing one's, as a
				// for's does.)
				const bool generated = s->endPos.line == s->pos.line && s->endPos.column == s->pos.column && s->endPos.file == s->pos.file;
				m_scopeEnds.push_back(generated && !m_scopeEnds.empty() ? m_scopeEnds.back() : s->endPos);
				m_defers.emplace_back();
				if (s->atomic)
					m_atomic++;
				for (Node* st : s->list)
					genStatement(st);
				if (s->atomic)
					m_atomic--;
				// Its defers, unless it ends with a jump (that ran them).
				const NodeKind last = s->list.empty() ? NodeKind::Block : s->list.back()->kind;
				if (last != NodeKind::Return && last != NodeKind::Break && last != NodeKind::Continue && hasDefers(m_defers.size() - 1))
				{
					loc(s->endPos);
					genDefers(m_defers.size() - 1);
				}
				m_defers.pop_back();
				optEndScope(m_scopes.back());
				m_scopes.pop_back();
				m_scopeEnds.pop_back();
			}
			break;
			case NodeKind::Defer:
				if (m_defers.empty())
					error(s->pos, "internal error: defer outside of a block");
				m_defers.back().push_back({ s->a, m_scopes, m_atomic });
			break;
			case NodeKind::StaticAssert:
				checkStaticAssert(s, nullptr);
			break;
			case NodeKind::VarDecl:
				if (s->hoisted != nullptr)
				{
					// A local constant (hidden at the top level): nothing here if it is a compile-time
					// constant; otherwise a constant variable, computed here at every call.
					Node* h = s->hoisted;
					if (h->kind == NodeKind::ConstDecl && m_consts.count(h->name) > 0)
					{
						decideConst(h->name);
						if (!m_consts[h->name].runtime)
							break;
					}
					else if (m_globals.count(h->name) == 0 || !m_globals[h->name]->localRuntime)
						break;		// a constant array (its values are constants: in the program)
				}
				{
					const size_t mark = beginExpr();
					genLocalVar(s);
					endExpr(mark);
				}
			break;
			case NodeKind::ExprStmt:
			{
				check(s->a);
				if (s->a->kind == NodeKind::Call && s->a->vaBuiltin == 4)
				{
					genForget(s);
					break;
				}
				if (s->a->kind == NodeKind::Ident && s->a->func == nullptr && s->a->var == nullptr && m_funcs.count(s->a->name))
					error(s->pos, "'" + s->a->name + "' is a sub: call it with " + s->a->name + "()");
				const size_t mark = beginExpr();
				genDiscard(s->a);
				endExpr(mark);
			}
			break;
			case NodeKind::Assert:
			{
				// if (!condition) __assert_fail(file, line, "condition", message)
				check(s->a);
				if (!decay(s->a->type)->isScalar())
					error(s->a->pos, "the assert condition must be an integer or a pointer");
				if (s->b)
				{
					check(s->b);
					requireConvertible(s->b, m_types.pointer(m_types.u8(), true), "the assert message");
				}
				if (m_options.noAsserts)
					break;
				if (m_funcs.count("__assert_fail") == 0)
					error(s->pos, "assert needs a sub __assert_fail(var file: string, var line: u32, var condition: string, "
						"var message: string) to call when the condition is false: #import(\"libstd\") in a DragonOS program, or write "
						"your own (e.g. calling the SDK's dr::assert_fail_halt)");
				auto l_node = [&](NodeKind kind) { Node* n = new Node(); n->kind = kind; n->pos = s->pos; return n; };
				Node* call = l_node(NodeKind::Call);
				call->a = l_node(NodeKind::Ident);
				call->a->name = "__assert_fail";
				Node* file = l_node(NodeKind::StrLit);
				file->text = s->pos.file;
				Node* line = l_node(NodeKind::IntLit);
				line->value = s->pos.line;
				Node* condition = l_node(NodeKind::StrLit);
				condition->text = s->text;
				Node* message = s->b;
				if (message == nullptr)
				{
					message = l_node(NodeKind::StrLit);
					message->text = "";
				}
				call->list = { file, line, condition, message };
				check(call);
				const std::string lEnd = newLabel();
				genCondBranch(s->a, lEnd, true);
				release(gen(call));
				label(lEnd);
			}
			break;
			case NodeKind::DebugPrint:
			{
				// The string goes to the host's terminal (the debug_print instruction).
				check(s->a);
				requireConvertible(s->a, m_types.pointer(m_types.u8(), true), "the debug_print argument");
				const size_t mark = beginExpr();
				Reg r = gen(s->a);
				emit("debug_print " + r);
				release(r);
				endExpr(mark);
			}
			break;
			case NodeKind::If:
			{
				if (s->isConstexpr)
				{
					// if constexpr: only the branch the condition chooses is generated (the other is
					// not even checked: in a template instance, it can use what this instance lacks).
					if (evalCondition(s->a, nullptr) != 0)
						genStatement(s->b);
					else if (s->c)
						genStatement(s->c);
					break;
				}
				checkCondition(s->a);
				std::string lElse = newLabel(), lEnd = newLabel();
				genCondBranch(s->a, s->c ? lElse : lEnd, false);
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
				checkCondition(s->a);
				std::string lStart = newLabel(), lEnd = newLabel();
				m_loopDepth++;
				label(lStart);
				genCondBranch(s->a, lEnd, false);
				m_loops.push_back({ lEnd, lStart, m_defers.size(), SIZE_MAX, m_temps.size() });
				genStatement(s->b);
				m_loops.pop_back();
				emit("jmp $" + lStart);	// (the body's last line: then the condition, on the while's line)
				label(lEnd);
				m_loopDepth--;
			}
			break;
			case NodeKind::DoWhile:
			{
				std::string lStart = newLabel(), lCont = newLabel(), lEnd = newLabel();
				m_loopDepth++;
				label(lStart);
				m_loops.push_back({ lEnd, lCont, m_defers.size(), SIZE_MAX, m_temps.size() });
				genStatement(s->b);
				m_loops.pop_back();
				label(lCont);
				checkCondition(s->a);
				loc(s->a->pos);
				genCondBranch(s->a, lStart, true);
				label(lEnd);
				m_loopDepth--;
			}
			break;
			case NodeKind::ForIn:
				genStatement(forInLoop(s));
			break;
			case NodeKind::Switch:
				genSwitch(s);
			break;
			case NodeKind::For:
			{
				m_scopes.push_back({});
				// (a for-in's own body block, endPos == pos: as a body that isn't a block)
				const bool block = s->d != nullptr && s->d->kind == NodeKind::Block && !(s->d->endPos.line == s->d->pos.line && s->d->endPos.column == s->d->pos.column);
				m_scopeEnds.push_back(block ? s->d->endPos : (m_scopeEnds.empty() ? SourcePos {} : m_scopeEnds.back()));
				m_defers.emplace_back();		// (its variable's destructor: when the loop ends)
				if (s->a)
					genStatement(s->a);
				std::string lStart = newLabel(), lCont = newLabel(), lEnd = newLabel();
				m_loopDepth++;
				label(lStart);
				if (s->b)
				{
					checkCondition(s->b);
					genCondBranch(s->b, lEnd, false);
				}
				m_loops.push_back({ lEnd, lCont, m_defers.size(), SIZE_MAX, m_temps.size() });
				genStatement(s->d);
				m_loops.pop_back();
				loc(s->pos);
				label(lCont);
				if (s->c)
				{
					check(s->c);
					const size_t mark = beginExpr();
					genDiscard(s->c);
					endExpr(mark);
				}
				emit("jmp $" + lStart);
				label(lEnd);
				m_loopDepth--;
				if (hasDefers(m_defers.size() - 1))
					genDefers(m_defers.size() - 1);
				m_defers.pop_back();
				optEndScope(m_scopes.back());
				m_scopes.pop_back();
				m_scopeEnds.pop_back();
			}
			break;
			case NodeKind::Return:
			{
				FuncInfo* fn = m_inline.empty() ? m_func : m_inline.back().func;
				Type* ret = fn->type->base;
				if (m_inDefer > 0)
					error(s->pos, "return cannot leave a defer");
				Reg r;
				const size_t exprMark = beginExpr();
				VarInfo* moved = nullptr;
				if (s->a && ret->kind == Type::Kind::Struct)
				{
					// A struct: copied to the caller's room for it (inline: in the frame; otherwise its
					// address is the hidden first argument), before the defers run. RV: that address.
					check(s->a, ret);
					requireConvertible(s->a, ret, "in the return value");
					Reg src = gen(s->a);
					r = alloc(s->pos);
					if (!m_inline.empty())
					{
						emit("mov " + r + ", FP");
						addImmediate(r, m_inline.back().resultOffset);
					}
					else
						emit("movo " + r + ", *PP, dword " + imm(0));
					// A local of the sub returned: moved out (its destructor does not run); a call's
					// result: moved; anything else: copied.
					Node* value = s->a;
					if (value->kind == NodeKind::Ident && value->var != nullptr && ((value->var->storage == VarInfo::Storage::Local && !value->var->param) || value->var->owned)
						&& value->var->type == ret)
					{
						copyValue(r, src, ret);
						moved = value->var;
					}
					else
						genInitValue(r, value, src, ret);
					emit("mov " + src + ", " + r);		// (registers are released in order: the address in src)
					release(r);
					r = src;
				}
				else if (s->a)
				{
					if (ret->kind == Type::Kind::Void)
						error(s->pos, fn->name + " returns void");
					if (ret->isReference() && !boundReference(s->a))
					{
						// A reference: to something that lives after the sub returns (not a local, a
						// value parameter or a temporary).
						check(s->a);
						if (!s->a->lvalue)
							error(s->a->pos, fn->name + " returns a reference (" + ret->name() + "): to something in memory that lives on, not a value");
						const Node* root = s->a;
						while ((root->kind == NodeKind::Member && root->value == 0) || (root->kind == NodeKind::Index && root->a->type != nullptr && root->a->type->kind == Type::Kind::Array))
							root = root->a;
						if (root->kind == NodeKind::Ident && root->var != nullptr && (root->var->storage == VarInfo::Storage::Local || root->var->storage == VarInfo::Storage::Param))
							error(s->a->pos, fn->name + " returns a reference to '" + (root->text.empty() ? root->name : root->text) + "', which ends when it returns " +
								"(a local or a parameter: return a value, or a reference to something that lives on)");
						if (root->kind == NodeKind::Unary && root->name == "*" && root->a->kind == NodeKind::Ident && root->a->var != nullptr && root->a->var->refFrame)
							error(s->a->pos, fn->name + " returns what '" + (root->a->text.empty() ? root->a->name : root->a->text) + "' refers to, which ends when it returns " +
								"(a local or a parameter of " + fn->name + ")");
					}
					check(s->a, ret);
					requireConvertible(s->a, ret, "in the return value");
					r = gen(s->a);
					convert(r, decay(s->a->type), ret);
				}
				else if (ret->kind != Type::Kind::Void)
					error(s->pos, fn->name + " must return a value (" + ret->name() + ")");
				// The value first, then its temporaries, then every defer of the sub (it stays in its
				// register: calls keep them).
				const bool temps = m_temps.size() > exprMark || m_temps.size() > exitTempBase();
				endExpr(exprMark);
				genExitTemps(exitTempBase());		// (from a catch block / a switch's case in an expression: the expression's)
				const bool deferred = hasDefers(0);
				if (deferred)
				{
					const int stmtDepth = m_stmtDepth;
					m_stmtDepth = m_regDepth;
					VarInfo* outerMoved = m_movedVar;
					m_movedVar = moved;
					genDefers(0);
					m_movedVar = outerMoved;
					m_stmtDepth = stmtDepth;
					loc(s->pos);
				}
				const bool deferredOrTemps = deferred || temps;
				if (!r.empty())
				{
					// The value's last instruction writes r (a load / move): it can write RV itself.
					// (Not after defers: their code comes last.)
					std::string& last = m_out->back();
					bool retargeted = false;
					for (const char* inst : { "mov ", "movo ", "movbo ", "movwo " })
					{
						if (deferredOrTemps)
							break;
						const std::string head = std::string("\t") + inst + r + ", ";
						if (last.rfind(head, 0) == 0 && last.find(", " + r, head.size() - 2) == std::string::npos)
						{
							const std::string source = last.substr(head.size());
							if (source == "RV")
								m_out->pop_back();			// mov r, RV (a call's value): already there
							else
								last = std::string("\t") + inst + "RV, " + source;
							retargeted = true;
							break;
						}
					}
					if (!retargeted)
						emit("mov RV, " + r);
					release(r);
				}
				if (!m_inline.empty())
					emit("jmp $" + m_inline.back().endLabel);
				else
					emit(m_func->interrupt ? "rti" : "ret");
			}
			break;
			case NodeKind::Throw:
			{
				FuncInfo* fn = m_inline.empty() ? m_func : m_inline.back().func;
				if (fn == nullptr || fn->type == nullptr || fn->type->throws == nullptr)
					error(s->pos, "throw: " + (fn != nullptr ? fn->name : std::string("the code here")) + " doesn't throw (sub ...() throws E { ... throw E.Member; })");
				if (m_inDefer > 0)
					error(s->pos, "throw cannot leave a defer");
				check(s->a, fn->type->throws);
				requireConvertible(s->a, fn->type->throws, "as the error (" + fn->type->throwsName + ")");
				const size_t exprMark = beginExpr();
				Reg r = gen(s->a);
				convert(r, decay(s->a->type), fn->type->throws);
				endExpr(exprMark);
				genThrowExit(r, s->pos);
				release(r);
			}
			break;
			case NodeKind::Break:
			case NodeKind::Continue:
			{
				const bool isBreak = s->kind == NodeKind::Break;
				if (m_loops.empty() || (!isBreak && m_loops.back().continueLabel.empty()))
					error(s->pos, std::string(isBreak ? "break" : "continue") + (m_inDefer > 0 ? " cannot leave a defer" : isBreak ? " outside of a loop or a switch" : " outside of a loop"));
				const size_t depth = isBreak || m_loops.back().continueDepth == SIZE_MAX ? m_loops.back().deferDepth : m_loops.back().continueDepth;
				genExitTemps(isBreak || m_loops.back().continueTempBase == SIZE_MAX ? m_loops.back().tempBase : m_loops.back().continueTempBase);
				if (hasDefers(depth))
				{
					genDefers(depth);
					loc(s->pos);
				}
				emit("jmp $" + (isBreak ? m_loops.back().breakLabel : m_loops.back().continueLabel));
			}
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

	// ======================================== Errors ========================================
	void CodeGen::genExitTemps(size_t base)
	{
		for (size_t i = m_temps.size(); i-- > base; )
		{
			const Temp temp = m_temps[i];
			if (temp.consumed)
				continue;
			Reg r = alloc({});
			std::string skip;
			if (temp.flag != 0)
			{
				// (made in a branch: only if it was; its flag is cleared at its expression's start)
				skip = newLabel();
				emit("movo " + r + ", *FP, dword " + imm(temp.flag));
				emit("mov ACC, " + r);
				emit("jeq $" + skip + ", 0");
			}
			emit("mov " + r + ", FP");
			genDestroy(r, temp.type, temp.offset);
			if (!skip.empty())
				label(skip);
			release(r);
		}
	}

	void CodeGen::genThrowExit(const Reg& value, const SourcePos& pos)
	{
		// The error into the caller's slot (inline: in this frame): the error, then the flag.
		Reg p = alloc(pos);
		if (!m_inline.empty())
		{
			emit("mov " + p + ", FP");
			addImmediate(p, m_inline.back().errorOffset);
		}
		else
			emit("movo " + p + ", *PP, dword " + imm(-4 * (int64_t)errorSlotIndex(m_func->type)));
		emit("omov *" + p + ", " + value + ", dword " + imm(4));
		emit("omov *" + p + ", 0x00000001, dword " + imm(0));
		release(p);
		// The temporaries of the expressions it is in, then every defer of the sub.
		genExitTemps(exitTempBase());
		if (hasDefers(0))
		{
			const int stmtDepth = m_stmtDepth;
			m_stmtDepth = m_regDepth;
			genDefers(0);
			m_stmtDepth = stmtDepth;
			loc(pos);
		}
		if (!m_inline.empty())
			emit("jmp $" + m_inline.back().endLabel);
		else
			emit(m_func->interrupt ? "rti" : "ret");
	}

	void CodeGen::genCallError(Node* call, const Reg& r)
	{
		Node* h = call->handler;
		if (h == nullptr)
			error(call->pos, "internal error: a call of a sub that throws without try / catch");
		const int32_t slot = (int32_t)h->constValue;
		const std::string ok = newLabel();
		Reg t = alloc(call->pos);
		emit("movo " + t + ", *FP, dword " + imm(slot));
		emit("mov ACC, " + t);
		emit("jeq $" + ok + ", 0");
		if (h->kind == NodeKind::Try)
		{
			emit("movo " + t + ", *FP, dword " + imm(slot + 4));
			genThrowExit(t, h->pos);
			release(t);
			label(ok);
			return;
		}
		release(t);
		Type* ret = calleeType(call)->base;
		if (h->c != nullptr)
		{
			// catch { ... }: its block, with the error's variable (the caller's registers stay).
			m_scopes.push_back({});
			if (h->var != nullptr)
				m_scopes.back()[h->name] = h->var;
			m_scopeEnds.push_back(h->c->endPos);
			const int stmtDepth = m_stmtDepth;
			m_stmtDepth = m_regDepth;
			genStatement(h->c);
			m_stmtDepth = stmtDepth;
			m_scopeEnds.pop_back();
			m_scopes.pop_back();
		}
		else
		{
			// catch value: in r (a struct: built where the call's result goes).
			m_condDepth++;
			Reg v = gen(h->b);
			if (ret->kind == Type::Kind::Struct)
				genInitValue(r, h->b, v, ret);
			else
			{
				convert(v, decay(h->b->type), ret);
				emit("mov " + r + ", " + v);
			}
			release(v);
			m_condDepth--;
		}
		label(ok);
	}

	// ======================================== switch ========================================
	// The cases' values (constants of the switch's type): [lo, hi] and the case, sorted (overlaps are
	// errors); elseCase: the else (-1: none). A switch on an enum's members without else: a warning
	// for the members no case has.
	void CodeGen::switchEntries(Node* s, Type* t, std::vector<SwitchEntry>& entries, int& elseCase)
	{
		entries.clear();
		elseCase = -1;
		const bool isBool = t->kind == Type::Kind::Bool;
		const int64_t minV = isBool ? 0 : t->isSigned() ? -(int64_t(1) << (8 * t->size() - 1)) : 0;
		const int64_t maxV = isBool ? 1 : t->isSigned() ? (int64_t(1) << (8 * t->size() - 1)) - 1 : (int64_t(1) << (8 * t->size())) - 1;
		std::string enumName;
		bool allEnum = true;
		auto l_value = [&](Node* x) -> int64_t {
			check(x);
			if (!x->isConst)
				error(x->pos, "a case's value is known when compiling (a literal, a constant, an enum member, a constexpr call)" +
					(x->kind == NodeKind::Call && !m_ctWhy.empty() ? std::string(": ") + m_ctWhy : std::string()));
			Type* xt = decay(x->type);
			if (xt == nullptr || !xt->isInteger())
				error(x->pos, "a case's value is an integer, a character, a bool or an enum member (not " + (xt ? xt->name() : std::string("?")) + ")");
			const int64_t v = xt->isSigned() ? (int64_t)(int32_t)x->constValue : x->constValue;
			if (v < minV || v > maxV)
				error(x->pos, std::to_string(v) + " is not a value of the switch's type (" + t->name() + ")");
			return v;
		};
		for (size_t ci = 0; ci < s->list.size(); ci++)
		{
			Node* c = s->list[ci];
			if (c->value == 1)
			{
				elseCase = (int)ci;
				continue;
			}
			for (Node* v : c->list)
			{
				const bool range = v->kind == NodeKind::Binary && v->name == "..";
				const int64_t lo = l_value(range ? v->a : v);
				const int64_t hi = range ? l_value(v->b) - 1 : lo;
				if (range && hi < lo)
					error(v->pos, "an empty range (a..b is from a to b - 1)");
				Node* single = range ? nullptr : v;
				if (single != nullptr && single->kind == NodeKind::Member && single->a != nullptr && single->a->kind == NodeKind::Ident && m_enums.count(single->a->name) > 0
					&& (enumName.empty() || enumName == single->a->name))
					enumName = single->a->name;
				else
					allEnum = false;
				entries.push_back({ lo, hi, ci, v->pos });
			}
		}
		std::sort(entries.begin(), entries.end(), [](const SwitchEntry& a, const SwitchEntry& b) { return a.lo < b.lo; });
		for (size_t i = 1; i < entries.size(); i++)
		{
			if (entries[i].lo <= entries[i - 1].hi)
				error(entries[i].pos, "this value is a case already (" + posString(entries[i - 1].pos) + ")");
		}
		if (elseCase < 0 && allEnum && !enumName.empty() && (s->value == 2 || (!m_discarding && m_warnedSwitches.insert(posKey(s->pos)).second)))
		{
			std::string missing;
			for (const Node* m : m_enums[enumName].decl->list)
			{
				const int64_t v = m_enums[enumName].values[m->name];
				const bool covered = std::any_of(entries.begin(), entries.end(), [&](const SwitchEntry& e) { return v >= e.lo && v <= e.hi; });
				if (!covered)
					missing += (missing.empty() ? "" : ", ") + m->name;
			}
			if (!missing.empty())
			{
				if (s->value == 2)
					error(s->pos, "this switch's value: no case for " + missing + " (add them, or else => value)");
				warning(s->pos, "switch on " + enumName + " without else: no case for " + missing);
			}
		}
		else if (elseCase < 0 && s->value == 2 && !(allEnum && !enumName.empty()))
			error(s->pos, "a switch used as a value needs else => value (or a case for every member of an enum)");
	}

	// switch (x) { is A => a, else => b }: its value's type (the cases' values: one type, as ?:'s
	// two; a case's block must leave). Without else, the cases cover an enum's members.
	void CodeGen::checkSwitchValue(Node* e, Type* expected)
	{
		check(e->a);
		Type* t = decay(e->a->type);
		if (t != nullptr && t->kind == Type::Kind::Struct)
		{
			requireConvertible(e->a, m_types.i32(), "as a switch's value");
			t = decay(e->a->type);
		}
		if (t == nullptr || !t->isInteger())
			error(e->a->pos, "a switch takes an integer, a character, a bool or an enum (not " + (t ? t->name() : std::string("?")) + ")");
		std::vector<SwitchEntry> entries;
		int elseCase = -1;
		switchEntries(e, t, entries, elseCase);
		Type* result = nullptr;
		Node* first = nullptr;
		for (Node* c : e->list)
		{
			if (c->a == nullptr)
			{
				if (!leaves(c->b))
					error(c->b->endPos.line > 0 ? c->b->endPos : c->pos, "this case's block must leave (return, throw, break or continue): the switch needs a value (is A => value)");
				continue;
			}
			check(c->a, expected);
			Type* ct = decay(c->a->type);
			if (ct == nullptr || ct->kind == Type::Kind::Void)
				error(c->a->pos, "a case's value: this gives none");
			if (result == nullptr)
			{
				result = ct;
				first = c->a;
				continue;
			}
			Type* common = nullptr;
			if (result->isArithmetic() && ct->isArithmetic())
				common = (result == ct ? ct : arithmeticType(result, ct));
			else if (result == ct)
				common = ct;
			else if (samePointee(result, ct))
				common = m_types.pointer(result->base, true);
			else if (result->isPointer() && ct->kind == Type::Kind::Null)
				common = result;
			else if (ct->isPointer() && result->kind == Type::Kind::Null)
				common = ct;
			else if ((result->isFunction() && ct->kind == Type::Kind::Null) || (ct->isFunction() && result->kind == Type::Kind::Null))
				common = result->isFunction() ? result : ct;
			if (common == nullptr)
				error(c->a->pos, "the switch's values have different types: " + result->name() + " (" + posString(first->pos) + ") and " + ct->name());
			result = common;
		}
		if (result == nullptr)
			error(e->pos, "a switch used as a value needs a case with a value (is A => value)");
		if (result->kind == Type::Kind::Struct)
		{
			for (Node* c : e->list)
			{
				if (c->a != nullptr)
					requireConvertible(c->a, result, "as the switch's value");
			}
		}
		e->type = result;
	}

	void CodeGen::genSwitch(Node* s, const Reg* out)
	{
		// out: a switch used as a value (checked already: checkSwitchValue); each case gives it.
		const bool asValue = out != nullptr;
		if (!asValue)
			check(s->a);
		Type* t = decay(s->a->type);
		if (t != nullptr && t->kind == Type::Kind::Struct)
		{
			requireConvertible(s->a, m_types.i32(), "as a switch's value");		// (an implicit operator cast)
			t = decay(s->a->type);
		}
		if (t == nullptr || !t->isInteger())
			error(s->a->pos, "a switch takes an integer, a character, a bool or an enum (not " + (t ? t->name() : std::string("?")) + ")");
		std::vector<SwitchEntry> entries;
		int elseCase = -1;
		switchEntries(s, t, entries, elseCase);
		const bool fall = s->value == 1;
		const bool isSigned = t->isSigned();
		std::vector<std::string> labels;
		for (size_t i = 0; i < s->list.size(); i++)
			labels.push_back(newLabel());
		const std::string lEnd = newLabel();
		// (a value without else: its cases cover an enum; anything else gets the last case's)
		const std::string lDefault = elseCase >= 0 ? labels[(size_t)elseCase] : asValue ? labels.back() : lEnd;
		Type* valueType = asValue ? s->type : nullptr;
		const int32_t resultSlot = asValue && valueType->kind == Type::Kind::Struct ? temporary(valueType->size()) : 0;
		const std::string less = isSigned ? "jgr" : "jgru", more = isSigned ? "jls" : "jlsu";	// (r < x: x > ACC; r > x: x < ACC)

		// The value (its temporaries destroyed), then the case it is.
		const size_t mark = beginExpr();
		Reg r = gen(s->a);
		endExpr(mark);
		auto l_chain = [&](size_t from, size_t to) {
			for (size_t i = from; i < to; i++)
			{
				const SwitchEntry& e = entries[i];
				if (e.lo == e.hi)
				{
					emit("mov ACC, " + r);
					emit("jeq $" + labels[e.c] + ", " + imm(e.lo));
					continue;
				}
				const std::string skip = newLabel();
				emit("mov ACC, " + r);
				emit(less + " $" + skip + ", " + imm(e.lo));
				emit(more + " $" + skip + ", " + imm(e.hi));
				emit("jmp $" + labels[e.c]);
				label(skip);
			}
			emit("jmp $" + lDefault);
		};
		std::function<void(size_t, size_t)> l_search = [&](size_t from, size_t to) {
			if (to - from <= 3)
				return l_chain(from, to);
			const size_t mid = (from + to) / 2;
			const SwitchEntry& e = entries[mid];
			const std::string left = newLabel(), right = newLabel();
			emit("mov ACC, " + r);
			emit(less + " $" + left + ", " + imm(e.lo));
			emit(more + " $" + right + ", " + imm(e.hi));
			emit("jmp $" + labels[e.c]);
			label(left);
			l_search(from, mid);
			label(right);
			l_search(mid + 1, to);
		};
		int64_t covered = 0;
		for (const SwitchEntry& e : entries)
			covered += e.hi - e.lo + 1;
		const int64_t span = entries.empty() ? 0 : entries.back().hi - entries.front().lo + 1;
		if (entries.size() >= 4 && span <= 1024 && covered * 2 >= span)
		{
			// Dense: a table of jumps (each one jmp, 5 bytes), at the value minus the smallest one.
			const int64_t low = entries.front().lo;
			const std::string table = newLabel();
			addImmediate(r, -low);
			emit("mov ACC, " + r);
			emit("jlsu $" + lDefault + ", " + imm(span - 1));		// (span - 1 < r - low: not in it)
			emit("mulip " + r + ", 5");
			Reg b = alloc(s->pos);
			emit("mov " + b + ", $" + table);
			emit("addip " + r + ", " + b);
			release(b);
			m_out->push_back(std::string("\t") + LowMark + "mov IP, " + r);
			label(table);
			size_t k = 0;
			for (int64_t v = low; v < low + span; v++)
			{
				while (k < entries.size() && entries[k].hi < v)
					k++;
				const std::string& target = (k < entries.size() && v >= entries[k].lo) ? labels[entries[k].c] : lDefault;
				m_out->push_back(std::string("\t") + LowMark + "jmp $" + target);
			}
		}
		else if (entries.size() > 4)
			l_search(0, entries.size());		// sparse: a binary search
		else
			l_chain(0, entries.size());
		release(r);

		if (asValue)
		{
			// Each case: its value into out (a struct: built in the frame), or its block (it leaves).
			// break / continue in a block are the loop's around the switch.
			for (size_t i = 0; i < s->list.size(); i++)
			{
				Node* c = s->list[i];
				label(labels[i]);
				if (c->a == nullptr)
				{
					const int stmtDepth = m_stmtDepth;
					m_stmtDepth = m_regDepth;
					genStatement(c->b);
					m_stmtDepth = stmtDepth;
					continue;
				}
				m_condDepth++;
				Reg v = gen(c->a);
				if (valueType->kind == Type::Kind::Struct)
				{
					Reg a = alloc(c->a->pos);
					emit("mov " + a + ", FP");
					addImmediate(a, resultSlot);
					genInitValue(a, c->a, v, valueType);
					release(a);
				}
				else
				{
					convert(v, decay(c->a->type), valueType);
					emit("mov " + *out + ", " + v);
				}
				release(v);
				m_condDepth--;
				if (i + 1 < s->list.size())
					emit("jmp $" + lEnd);
			}
			label(lEnd);
			if (valueType->kind == Type::Kind::Struct)
			{
				emit("mov " + *out + ", FP");
				addImmediate(*out, resultSlot);
				addTemp(resultSlot, valueType, s);
			}
			return;
		}

		// The cases: break leaves the switch (continue: the loop around it); without fall, a case ends it.
		size_t continueDepth = 0;
		size_t continueTempBase = 0;
		std::string continueLabel;
		if (!m_loops.empty())
		{
			continueLabel = m_loops.back().continueLabel;
			continueDepth = m_loops.back().continueDepth == SIZE_MAX ? m_loops.back().deferDepth : m_loops.back().continueDepth;
			continueTempBase = m_loops.back().continueTempBase == SIZE_MAX ? m_loops.back().tempBase : m_loops.back().continueTempBase;
		}
		m_loops.push_back({ lEnd, continueLabel, m_defers.size(), continueDepth, m_temps.size(), continueTempBase });
		for (size_t i = 0; i < s->list.size(); i++)
		{
			label(labels[i]);
			genStatement(s->list[i]->b);
			if (!fall && i + 1 < s->list.size())
			{
				const auto& body = s->list[i]->b->list;
				const NodeKind last = body.empty() ? NodeKind::Block : body.back()->kind;
				if (last != NodeKind::Return && last != NodeKind::Break && last != NodeKind::Continue && last != NodeKind::Throw)
					emit("jmp $" + lEnd);
			}
		}
		m_loops.pop_back();
		label(lEnd);
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
			if (m_atomic > 0 && line.size() > 3 && (line[3] == ' ' || line[3] == '\t')
				&& std::tolower((unsigned char)line[0]) == 'i' && std::tolower((unsigned char)line[1]) == 'n' && std::tolower((unsigned char)line[2]) == 't')
				error(pos, "int in an atomic block: the CPU ignores every interrupt while they are disabled, int too");
			if (line[0] == '.')
				switchedSection = true;
			else if (line[0] != '@' && line.rfind("##", 0) != 0)
				loc(pos);
			if (m_optPass == 1)
			{
				// The registers it names are not used for variables.
				for (size_t k = 0; k < line.size(); k++)
				{
					const char c = line[k];
					if ((c == 'R' || c == 'r') && (k == 0 || !(std::isalnum((unsigned char)line[k - 1]) || line[k - 1] == '_' || line[k - 1] == '$'))
						&& k + 1 < line.size() && std::isdigit((unsigned char)line[k + 1]))
					{
						size_t e = k + 1;
						while (e < line.size() && std::isdigit((unsigned char)line[e]))
							e++;
						if (e >= line.size() || !(std::isalnum((unsigned char)line[e]) || line[e] == '_'))
							m_lowRegs.insert(std::atoi(line.c_str() + k + 1));
					}
				}
				std::string lower = line;
				std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				if (lower.find("reg(") != std::string::npos)
					m_lowOpaque = true;
			}

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
				const std::string written = line.substr(i + 1, j - i - 1);
				std::string name = written;
				// Not a local: looked up from the sub's namespace (or the low block's, at the top level).
				bool local = false;
				for (const auto& sc : m_scopes)
					local = local || sc.count(name) > 0;
				if (!name.empty() && !local)
					name = aliasedScope(qualify(name, topLevel ? low->scope : (m_func != nullptr ? m_func->decl->scope : std::string())));
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
					indexLow({ pos.file, pos.line, base + (int)i + 1 }, written, name);
				}
				if (!name.empty() && lookupVar(name) == nullptr && m_consts.count(name) > 0)
					decideConst(name);		// (a const computed when the program runs: a variable)
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
						checkFieldAccess(st->structInfo, f, pos);
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
			// (Marked for the peephole pass, which doesn't look into low code.)
			if (m_optPass != 0)
				m_out->push_back(std::string("\t") + LowMark + out);
			else
				emit(out);
			for (VarInfo* v : storeBack)
			{
				std::string reg;
				for (const auto& s : subs)
				{
					if (s.var == v && s.form == 0)
						reg = s.reg;
				}
				if (!v->reg.empty())
				{
					// Truncated as a store to its memory would.
					emit("mov " + v->reg + ", " + reg);
					switch (v->type->kind)
					{
						case Type::Kind::I8: emit("lsh " + v->reg + ", 24"); emit("rsh " + v->reg + ", 24"); break;
						case Type::Kind::I16: emit("lsh " + v->reg + ", 16"); emit("rsh " + v->reg + ", 16"); break;
						case Type::Kind::U8: case Type::Kind::Bool: emit("andip " + v->reg + ", 0x000000FF"); break;
						case Type::Kind::U16: emit("andip " + v->reg + ", 0x0000FFFF"); break;
						default: break;
					}
				}
				else if (v->storage == VarInfo::Storage::Global && !isSharedData(v))
				{
					const char* suffix = (v->type->size() == 1 ? "b" : (v->type->size() == 2 ? "w" : ""));
					emit(std::string("mov") + suffix + " [$" + v->symbol + "], " + reg);
				}
				else
				{
					emit(std::string("push ") + ScratchAddr);
					varAddress(ScratchAddr, v, false);	// (a register variable has no store-back to its memory)
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
		prepareConstructor(f);
		prepareDestructor(f);
		// The body (into body): pass 1 / 2 of -O run it once each.
		auto l_body = [&](std::vector<std::string>& body) {
			m_out = &body;
			m_func = f;
			m_scopes.clear();
			m_scopes.push_back({});
			m_optTick = 0;
			m_optMaxDepth = 0;
			m_loopDepth = 0;
			if (m_optPass != 0)
				m_optVars.clear();
			if (m_optPass == 1)
			{
				m_lowRegs.clear();
				m_lowOpaque = false;
			}
			std::vector<VarInfo*> regParams;
			std::vector<VarInfo*> allParams;
			for (size_t i = 0; i < f->paramNames.size(); i++)
			{
				VarInfo* v = new VarInfo();
				v->name = f->paramNames[i];
				v->type = f->type->params[i];
				v->storage = VarInfo::Storage::Param;
				v->offset = (int32_t)i + (int32_t)hiddenSlots(f->type);	// (slot 0: the result's address; then the error slot's)
				v->pos = f->decl->list[i]->pos;
				v->defPos = f->decl->list[i]->namePos;
				v->param = true;
				v->owned = ownedParam(f->type, i);
				v->isConst = f->decl->list[i]->constKind == 1;
				if (m_options.index != nullptr)
					indexName(v->defPos, v->name, v->defPos, hoverVar(v));
				m_scopes.back()[v->name] = v;
				allParams.push_back(v);
				optRegister(v, true);
				if (!v->reg.empty())
					regParams.push_back(v);
			}
			m_scopeEnds.clear();
			if (f->decl->a != nullptr)
			{
				m_scopeEnds.push_back(f->decl->a->endPos);
				for (size_t i = 0; i < f->paramNames.size(); i++)
					completeLocal(m_scopes.back()[f->paramNames[i]]);
			}
			m_localCursor = 4;
			m_localMin = 4;
			m_loops.clear();
			m_defers.clear();
			m_defers.emplace_back();
			m_inDefer = 0;
			m_atomic = 0;
			m_regDepth = 0;
			m_forgetNames.clear();
			collectForgets(f->decl->a, m_forgetNames);
			destroyOwnedParams(allParams);
			// Register parameters: their values into their registers.
			for (VarInfo* v : regParams)
				loadVar(v->reg, v, true);
			Node* bodyNode = f->decl->a;
			for (Node* s : bodyNode->list)
				genStatement(s);
			const bool endsWithReturn = !bodyNode->list.empty() && bodyNode->list.back()->kind == NodeKind::Return;
			if (!endsWithReturn)
			{
				loc(bodyNode->endPos);
				genDefers(0);
				if (f->type->base->kind != Type::Kind::Void && !returnsStruct(f->type))
					emit("mov RV, 0");
				emit(f->interrupt ? "rti" : "ret");
			}
		};
		std::vector<std::string> body;
		const bool optimize = m_options.optimize && !m_options.analysis && m_options.index == nullptr && !m_discarding;
		if (!optimize)
			l_body(body);
		else
		{
			struct Reset { CodeGen* cg; ~Reset() { cg->m_optPass = 0; cg->m_regLimit = TempRegisters; cg->m_optRegs.clear(); } } reset { this };
			m_optPass = 1;
			l_body(body);
			optAssign();
			bool any = false;
			for (const auto& r : m_optRegs)
				any = any || !r.empty();
			if (any)
			{
				const int passOneDepth = m_optMaxDepth;
				const size_t passOneVars = m_optVars.size();
				std::vector<std::string> optimized;
				m_optPass = 2;
				m_regLimit = passOneDepth;
				try
				{
					l_body(optimized);
					// (Both passes must see the same variables, in the same order: otherwise
					// pass 1's code is kept.)
					if (m_optVars.size() == passOneVars)
						body = std::move(optimized);
				}
				catch (const OptRetry&)
				{
					// (Keeps pass 1's code: the variables in memory.)
					m_inline.clear();
					m_stmtDepth = 0;
					m_regLimit = TempRegisters;
				}
			}
			m_optPass = 0;
			m_regLimit = TempRegisters;
			peephole(body);
		}
		m_out = &m_code;
		m_code.push_back("");
		if (!m_discarding)
		{
			// (After the top-level low blocks, the first sub is not a safe place to cut.)
			if (m_subEmitted || m_topLows.empty())
				m_pieceStarts.push_back(f->label);
			m_subEmitted = true;
		}
		label(f->label);
		loc(f->decl->pos);	// the prologue
		if (m_localMin < 4)
		{
			// Room for the locals: they are at FP + offset (offsets <= 0).
			emit(std::string("mov ") + ScratchCounter + ", SP");
			emit(std::string("subip ") + ScratchCounter + ", " + imm(4 - m_localMin));
			emit(std::string("mov SP, ") + ScratchCounter);
		}
		for (std::string& line : body)
		{
			if (line.size() > 1 && line[1] == LowMark)
				line.erase(1, 1);
		}
		m_code.insert(m_code.end(), body.begin(), body.end());
		m_scopes.clear();
		m_defers.clear();
		m_func = nullptr;
	}

	// ==================================== -O: register variables ====================================
	void CodeGen::optRegister(VarInfo* v, bool functionParam)
	{
		if (m_optPass == 0)
			return;
		v->optId = (int)m_optVars.size();
		OptVar o;
		o.start = m_optTick++;
		o.param = functionParam;
		o.eligible = v->type != nullptr && v->type->isScalar() && v->type->size() <= 4;
		m_optVars.push_back(o);
		if (m_optPass == 2 && (size_t)v->optId < m_optRegs.size())
			v->reg = m_optRegs[v->optId];
	}

	void CodeGen::optUse(VarInfo* v)
	{
		if (m_optPass != 1 || v->optId < 0)
			return;
		double w = 1;
		for (int i = 0; i < std::min(m_loopDepth, 5); i++)
			w *= 8;
		m_optVars[v->optId].weight += w;
	}

	void CodeGen::optEndScope(const std::map<std::string, VarInfo*>& scope)
	{
		if (m_optPass != 1)
		{
			m_optTick += (m_optPass != 0) ? 1 : 0;
			return;
		}
		const int tick = m_optTick++;
		for (const auto& entry : scope)
		{
			VarInfo* v = entry.second;
			if (v->optId >= 0 && !m_optVars[v->optId].param && m_optVars[v->optId].end == 0x7FFFFFFF)
				m_optVars[v->optId].end = tick;
		}
	}

	void CodeGen::optAssign(void)
	{
		m_optRegs.assign(m_optVars.size(), "");
		if (m_lowOpaque)
			return;
		// The registers above the temporaries pass 1 needed (R17..R20 are the code generator's scratch).
		std::vector<int> pool;
		for (int r = TempRegisters; r > m_optMaxDepth; r--)
		{
			if (m_lowRegs.count(r) == 0)
				pool.push_back(r);
		}
		std::vector<int> order;
		for (size_t i = 0; i < m_optVars.size(); i++)
		{
			const OptVar& o = m_optVars[i];
			// A parameter used once costs as much in a register (its load at the start).
			if (o.eligible && o.weight > (o.param ? 1.5 : 0))
				order.push_back((int)i);
		}
		std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return m_optVars[a].weight > m_optVars[b].weight; });
		std::map<int, std::vector<int>> taken;		// register -> the variables in it
		for (int id : order)
		{
			const OptVar& o = m_optVars[id];
			for (int r : pool)
			{
				bool free = true;
				for (int other : taken[r])
				{
					const OptVar& p = m_optVars[other];
					if (o.start <= p.end && p.start <= o.end)
					{
						free = false;
						break;
					}
				}
				if (free)
				{
					taken[r].push_back(id);
					m_optRegs[id] = "R" + std::to_string(r);
					break;
				}
			}
		}
	}

	// ==================================== -O: peephole pass ====================================
	// Works on one sub's code: its temporaries (R1..R16) are only live inside a statement, so
	// copies through them can be removed when nothing reads them afterwards (followed along
	// the jumps of the sub). Low code is opaque (it can do anything with any register).
	namespace
	{
		enum class InsKind { None, Def, RW, Read, Jump, CondJump, End, Barrier, LowOp };
		struct PLine
		{
			std::string text;
			InsKind kind { InsKind::None };	// None: a label, a directive, an empty line
			std::string mn;
			std::vector<std::string> ops;
			std::string label;				// a label line: its name
			bool low { false };				// a low block's line: looked at, never changed
		};

		bool isGeneralReg(const std::string& s, int* number = nullptr)
		{
			if (s.size() < 2 || s.size() > 3 || s[0] != 'R')
				return false;
			for (size_t i = 1; i < s.size(); i++)
			{
				if (!std::isdigit((unsigned char)s[i]))
					return false;
			}
			const int n = std::atoi(s.c_str() + 1);
			if (number)
				*number = n;
			return n >= 1 && n <= 20;
		}

		bool tokenChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

		// A register operand (not memory: *R, [..]; not an immediate or a label).
		bool isGeneralRegOrSpecial(const std::string& s)
		{
			if (s.empty() || s[0] == '*' || s[0] == '[' || s[0] == '$' || std::isdigit((unsigned char)s[0]) || s[0] == '-')
				return false;
			for (char c : s)
			{
				if (!tokenChar(c) && c != '(' && c != ')')
					return false;
			}
			return true;
		}

		bool isTemp(const std::string& s)
		{
			int n = 0;
			return isGeneralReg(s, &n) && n <= TempRegisters;
		}

		bool mentions(const std::string& op, const std::string& r)
		{
			for (size_t at = op.find(r); at != std::string::npos; at = op.find(r, at + 1))
			{
				const bool before = (at > 0) && (tokenChar(op[at - 1]) || op[at - 1] == '$');
				const bool after = (at + r.size() < op.size()) && tokenChar(op[at + r.size()]);
				if (!before && !after)
					return true;
			}
			return false;
		}

		std::string replaceToken(const std::string& op, const std::string& from, const std::string& to)
		{
			std::string out;
			size_t i = 0;
			while (i < op.size())
			{
				if (op.compare(i, from.size(), from) == 0)
				{
					const bool before = (i > 0) && (tokenChar(op[i - 1]) || op[i - 1] == '$');
					const bool after = (i + from.size() < op.size()) && tokenChar(op[i + from.size()]);
					if (!before && !after)
					{
						out += to;
						i += from.size();
						continue;
					}
				}
				out += op[i++];
			}
			return out;
		}

		void parseLine(PLine& l)
		{
			l.kind = InsKind::None;
			l.low = false;
			l.mn.clear();
			l.ops.clear();
			l.label.clear();
			const std::string& t = l.text;
			if (t.empty())
				return;
			if (t[0] != '\t')
			{
				if (t[0] != '.' && t.back() == ':')
					l.label = t.substr(0, t.size() - 1);
				return;
			}
			if (t.size() > 1 && t[1] == LowMark)
			{
				// A low line: an instruction that only names registers (R1..R20) reads and writes
				// those (LowOp); control flow, labels, directives, calls, interrupts, reg(..) and
				// anything unclear are barriers.
				l.kind = InsKind::Barrier;
				std::string body = t.substr(2);
				for (const char* scratch : LowScratch)	// (S1 / S2: not R registers)
				{
					for (size_t at = body.find(scratch); at != std::string::npos; at = body.find(scratch))
						body.replace(at, std::strlen(scratch), "S");
				}
				if (body.empty() || body[0] == '.' || body[0] == '@' || body[0] == '#' || body.find(':') != std::string::npos
					|| body.find("reg(") != std::string::npos || body.find("REG(") != std::string::npos)
					return;
				std::string mn = body.substr(0, body.find_first_of(" \t"));
				std::transform(mn.begin(), mn.end(), mn.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				if (mn.empty() || mn[0] == 'j' || mn == "call" || mn == "ret" || mn == "rti" || mn == "hlt" || mn == "int"
					|| mn == "push" || mn == "pop" || mn.find("debug") == 0 || !std::isalpha((unsigned char)mn[0]))
					return;
				for (int k = 1; k <= 20; k++)
				{
					if (mentions(body, "r" + std::to_string(k)))
						return;		// (lower case: not followed)
				}
				PLine plain;
				plain.text = "\t" + body;
				parseLine(plain);
				l.low = true;
				if (plain.kind == InsKind::Def || plain.kind == InsKind::RW || plain.kind == InsKind::Read)
				{
					l.kind = plain.kind;
					l.mn = plain.mn;
					l.ops = plain.ops;
					return;
				}
				l.kind = InsKind::LowOp;
				l.mn = mn;
				l.ops = { body.substr(mn.size()) };
				return;
			}
			const size_t sp = t.find(' ', 1);
			l.mn = t.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
			if (sp != std::string::npos)
			{
				std::string rest = t.substr(sp + 1);
				size_t start = 0;
				while (true)
				{
					const size_t comma = rest.find(',', start);
					std::string op = rest.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
					const size_t f = op.find_first_not_of(" \t"), e = op.find_last_not_of(" \t");
					l.ops.push_back(f == std::string::npos ? std::string() : op.substr(f, e - f + 1));
					if (comma == std::string::npos)
						break;
					start = comma + 1;
				}
			}
			static const std::set<std::string> defs { "mov", "movb", "movw", "movo", "movbo", "movwo", "pop" };
			static const std::set<std::string> rws { "addip", "subip", "mulip", "divip", "divipu", "andip", "orip", "xorip", "lsh", "rsh",
				"notip", "neg", "fneg", "fadd", "fsub", "fmul", "fdiv", "itof", "utof", "ftoi", "ftou" };
			static const std::set<std::string> reads { "push", "omov", "omovw", "omovb", "feq", "flt", "fle", "debug_print", "call" };
			static const std::set<std::string> condJumps { "jeq", "jne", "jgr", "jls", "jge", "jle", "jgru", "jlsu", "jgeu", "jleu" };
			if (defs.count(l.mn) && !l.ops.empty())
				l.kind = InsKind::Def;
			else if (rws.count(l.mn) && !l.ops.empty())
				l.kind = InsKind::RW;
			else if (reads.count(l.mn))
				l.kind = InsKind::Read;
			else if (l.mn == "jmp" && l.ops.size() == 1)
				l.kind = InsKind::Jump;
			else if (condJumps.count(l.mn) && l.ops.size() == 2)
				l.kind = InsKind::CondJump;
			else if (l.mn == "ret" || l.mn == "rti" || l.mn == "hlt")
				l.kind = InsKind::End;
			else
				l.kind = InsKind::Barrier;
		}

		void rebuild(PLine& l)
		{
			std::string t = "\t" + l.mn;
			for (size_t i = 0; i < l.ops.size(); i++)
				t += (i == 0 ? " " : ", ") + l.ops[i];
			l.text = t;
		}

		// The first operand is only written (a register destination of a mov / pop).
		bool writeOnlyFirst(const PLine& l)
		{
			return l.kind == InsKind::Def && isGeneralRegOrSpecial(l.ops[0]);
		}
	}

	void CodeGen::peephole(std::vector<std::string>& code)
	{
		std::vector<PLine> lines(code.size());
		for (size_t i = 0; i < code.size(); i++)
		{
			lines[i].text = code[i];
			parseLine(lines[i]);
		}
		std::map<std::string, size_t> labels;
		for (size_t i = 0; i < lines.size(); i++)
		{
			if (!lines[i].label.empty())
				labels[lines[i].label] = i;
		}
		auto l_remove = [&](size_t i) { lines[i].text.clear(); parseLine(lines[i]); };
		auto l_reads = [&](const PLine& l, const std::string& r) {
			if (l.kind == InsKind::Barrier)
				return true;
			for (size_t k = writeOnlyFirst(l) ? 1 : 0; k < l.ops.size(); k++)
			{
				if (mentions(l.ops[k], r))
					return true;
			}
			return false;
		};
		auto l_writes = [&](const PLine& l, const std::string& r) {
			if (l.kind == InsKind::Barrier)
				return true;
			if (l.kind == InsKind::LowOp)
				return mentions(l.ops[0], r);
			return (l.kind == InsKind::Def || l.kind == InsKind::RW) && l.ops[0] == r;
		};
		auto l_mentions = [&](const PLine& l, const std::string& r) {
			if (l.kind == InsKind::Barrier)
				return true;
			for (const auto& op : l.ops)
			{
				if (mentions(op, r))
					return true;
			}
			return false;
		};
		auto l_target = [&](const PLine& l) -> long {
			const std::string& op = l.ops[0];
			if (op.size() < 2 || op[0] != '$')
				return -1;
			auto it = labels.find(op.substr(1));
			return it == labels.end() ? -1 : (long)it->second;
		};
		// Can r (a temporary) be read from line `from` on (on any path, before it is set again)?
		std::vector<int> seen(lines.size() + 1, 0);
		int generation = 0;
		auto l_liveFrom = [&](size_t from, const std::string& r) -> bool {
			generation++;
			std::vector<size_t> work { from };
			int budget = 4000;
			while (!work.empty())
			{
				size_t i = work.back();
				work.pop_back();
				for (; ; i++)
				{
					if (--budget < 0 || i >= lines.size())
						return true;
					const PLine& l = lines[i];
					if (l.kind == InsKind::None)
					{
						if (!l.label.empty())
						{
							if (seen[i] == generation)
								break;
							seen[i] = generation;
						}
						continue;
					}
					if (l_reads(l, r))
						return true;
					if (l_writes(l, r))
						break;
					if (l.kind == InsKind::End)
						break;
					if (l.kind == InsKind::Jump || l.kind == InsKind::CondJump)
					{
						const long t = l_target(l);
						if (t < 0)
							return true;
						work.push_back((size_t)t);
						if (l.kind == InsKind::Jump)
							break;
					}
				}
			}
			return false;
		};
		auto l_liveAfter = [&](size_t i, const std::string& r) { return l_liveFrom(i + 1, r); };
		for (int round = 0; round < 8; round++)
		{
			bool changed = false;
			for (size_t i = 0; i < lines.size(); i++)
			{
				PLine& l = lines[i];
				if (l.low || l.kind != InsKind::Def || l.mn != "mov" || l.ops.size() != 2 || !isGeneralRegOrSpecial(l.ops[0]))
					continue;
				const std::string dst = l.ops[0], src = l.ops[1];
				// mov X, X
				if (dst == src)
				{
					l_remove(i);
					changed = true;
					continue;
				}
				// mov Rt, <register / immediate / label> that nothing reads.
				if (isTemp(dst) && src[0] != '*' && src[0] != '[' && !l_liveAfter(i, dst))
				{
					l_remove(i);
					changed = true;
					continue;
				}
				// mov X, Rt: Rt's value computed in X itself (from where Rt is set).
				if (isTemp(src) && isGeneralReg(dst))
				{
					long s = -1;
					int steps = 0;
					for (long j = (long)i - 1; j >= 0 && steps < 64; j--)
					{
						const PLine& p = lines[j];
						if (p.kind == InsKind::None)
						{
							if (!p.label.empty())
								break;
							continue;
						}
						steps++;
						if (p.low || p.kind == InsKind::Barrier || p.kind == InsKind::LowOp || p.kind == InsKind::Jump || p.kind == InsKind::CondJump || p.kind == InsKind::End)
							break;
						if (writeOnlyFirst(p) && p.ops[0] == src && !l_reads(p, src))
						{
							// (X can be read here: before it is written.)
							if (p.ops[0] != dst)
								s = j;
							break;
						}
						if (l_mentions(p, dst))
							break;
					}
					if (s >= 0 && !l_liveAfter(i, src))
					{
						for (size_t k = (size_t)s; k < i; k++)
						{
							PLine& q = lines[k];
							if (q.kind == InsKind::None)
								continue;
							for (auto& op : q.ops)
								op = replaceToken(op, src, dst);
							rebuild(q);
						}
						l_remove(i);
						changed = true;
						continue;
					}
				}
				// mov Rt, Y: the next reads of Rt read Y.
				if (isTemp(dst) && isGeneralReg(src))
				{
					bool allReplaced = true;
					size_t resume = lines.size();	// where the rest is looked at (that line included)
					bool dead = false;
					for (size_t j = i + 1; j < lines.size(); j++)
					{
						PLine& q = lines[j];
						resume = j;
						if (q.kind == InsKind::None)
						{
							if (!q.label.empty())
								break;
							continue;
						}
						if (q.low || q.kind == InsKind::Barrier || q.kind == InsKind::LowOp || q.kind == InsKind::End)
							break;
						if (q.kind == InsKind::RW && q.ops[0] == dst)
						{
							allReplaced = false;	// changed in place: it needs its own copy
							break;
						}
						bool replaced = false;
						for (size_t k = writeOnlyFirst(q) ? 1 : 0; k < q.ops.size(); k++)
						{
							if (mentions(q.ops[k], dst))
							{
								q.ops[k] = replaceToken(q.ops[k], dst, src);
								replaced = true;
							}
						}
						if (replaced)
						{
							rebuild(q);
							changed = true;
						}
						if (l_writes(q, dst))
						{
							dead = true;
							break;
						}
						if (l_writes(q, src) || q.kind == InsKind::Jump || q.kind == InsKind::CondJump)
							break;
					}
					if (allReplaced && (dead || !l_liveFrom(resume, dst)))
					{
						l_remove(i);
						changed = true;
					}
				}
			}
			if (!changed)
				break;
		}
		code.clear();
		for (const PLine& l : lines)
		{
			if (!l.text.empty() || l.kind != InsKind::None)
				code.push_back(l.text);
		}
	}

	// The constant bytes of a global (big endian); false if a part needs code at run time.
	static void putBytes(std::vector<uint8_t>& bytes, uint32_t offset, uint32_t size, int64_t value)
	{
		for (uint32_t i = 0; i < size; i++)
			bytes[offset + i] = (uint8_t)((uint64_t)value >> (8 * (size - 1 - i)));
	}

	void CodeGen::genGlobalTypes(size_t from)
	{
		// Types first (subs can use any global).
		for (size_t gi = from; gi < m_globalDecls.size(); gi++)
		{
			Node* d = m_globalDecls[gi];
			const bool ok = attempt([&] {
				if (d->kind == NodeKind::ConstDecl && m_globals.count(d->name) == 0)
				{
					decideConst(d->name);		// (a variable: typed there)
					return;
				}
				VarInfo* v = m_globals[d->name];
				if (v->type == nullptr)
					typeGlobal(d, v);
			});
			if (!ok && m_globals.count(d->name) > 0 && m_globals[d->name]->type == nullptr)
				m_globals[d->name]->type = m_types.i32();
		}
		m_globalsTyped = m_globalDecls.size();
	}

	void CodeGen::typeGlobal(Node* d, VarInfo* v)
	{
		if (m_options.rom && v->isConst && !v->external)
			error(d->pos, "--rom: '" + shownName(d->name) + "' is a constant whose value is computed when the program runs: it needs RAM, "
				"but the program (and its data) is in ROM (use constexpr values, or keep it in RAM: a local, or a pointer to a RAM address)");
		if (m_options.rom && !v->readOnly && v->isStatic && !v->external)
			error(d->pos, "--rom: '" + d->text + "' is a static variable (stored in the program, like a global), but the program is in ROM, "
				"where nothing can change: use const for read-only values, and keep what changes in RAM (locals, or a pointer to a RAM address)");
		if (m_options.rom && !v->readOnly && !v->external)
			error(d->pos, "--rom: '" + d->name + "' is a global variable, but the program (and its data) is in ROM, "
				"where nothing can change: use const for read-only values, and keep what changes in RAM (locals, or a pointer to a RAM address)");
		Type* t = nullptr;
		if (!v->external)
			inferArraySize(d->typeSyntax, d->a, d->pos);
		if (d->refKind != 0)
			error(d->pos, "a reference is for parameters, results and locals, not globals or static variables (use a pointer)");
		if (d->typeSyntax)
			t = resolveType(d->typeSyntax);
		else
		{
			check(d->a);
			t = decay(d->a->type);
			if (t == nullptr || t->kind == Type::Kind::Null || t->kind == Type::Kind::Void)
				error(d->pos, "cannot tell the type of '" + d->name + "' (write var " + d->name + ": <type>)");
			if (t->isInteger() && d->a->kind == NodeKind::IntLit && d->a->name != "u")
				t = m_types.i32();
		}
		if (t->kind == Type::Kind::Void)
			error(d->pos, "variables cannot be void");
		rejectReference(t, d->pos, "globals or static variables");
		if (t->kind == Type::Kind::Struct)
		{
			resolveStruct(t->structInfo, d->pos);
			requireValueType(t, d->pos, "a variable");
		}
		v->type = t;
	}

	// Their values (from: the ones not generated yet: the static locals of template instances come
	// later).
	void CodeGen::genGlobalValues(size_t from)
	{
		// Values: constants go in the data, the rest is done by __ds_init (which has locals
		// when an initializer calls an inline sub).
		std::vector<std::string>* outerOut = m_out;
		const int32_t outerCursor = m_localCursor, outerMin = m_localMin;
		m_out = &m_initCode;
		m_localCursor = m_initLocalMin;
		m_localMin = m_initLocalMin;
		for (size_t gi = from; gi < m_globalDecls.size(); gi++)
		{
			Node* d = m_globalDecls[gi];
			attempt([&] {
				if (m_globals.count(d->name) == 0)
					return;		// a compile-time constant (no storage)
				VarInfo* v = m_globals[d->name];
				if (v->external)
					return;		// in its library (which initializes it)
				Type* t = v->type;
				std::vector<uint8_t> bytes(t->size(), 0);
				struct Pending { uint32_t offset; Type* type; Node* init; const StructField* field; };
				std::vector<Pending> pending;
				// Pointers to string literals: stored as the literal's address ($symbol in the data).
				std::map<uint32_t, std::string> refs;
				std::vector<std::pair<int32_t, const StructField*>> defaults;
				std::vector<std::pair<int32_t, std::string>> vptrs;
				collectDefaults(t, 0, defaults, &vptrs);
				for (const auto& vp : vptrs)
					refs[(uint32_t)vp.first] = vp.second;
				for (const auto& def : defaults)
				{
					if (def.second->init->kind == NodeKind::StrLit)
						refs[(uint32_t)def.first] = stringLiteral(def.second->init->text);
					else if (def.second->init->kind == NodeKind::Ident && def.second->init->func != nullptr)
						refs[(uint32_t)def.first] = def.second->init->func->label;
					else
					{
						Node* init = def.second->init;
						putBytes(bytes, def.first, def.second->type->size(), init->isConst ? constConvert(init->constValue, decay(init->type), def.second->type) : init->constValue);
					}
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
							{
								checkFieldAccess(p.type->structInfo, &fields[i], init->list[i]->pos);
								work.push_back({ p.offset + fields[i].offset, fields[i].type, init->list[i], nullptr });
							}
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
					if (p.type->kind == Type::Kind::Struct && decay(init->type) == p.type && !v->readOnly)
					{
						if (v->constKind == 3)
							error(init->pos, "constinit '" + shownName(d->name) + "': its value must be known when compiling (this struct is copied when the program starts)");
						pending.push_back(p);		// a struct value (a call's result, another struct): copied by __ds_init
						continue;
					}
					if (p.type->kind == Type::Kind::Struct || p.type->kind == Type::Kind::Array)
						error(init->pos, "global structs and arrays are initialized with { ... }");
					if (init->kind == NodeKind::StrLit)
					{
						refs[p.offset] = stringLiteral(init->text);
						continue;
					}
					refs.erase(p.offset);	// replaces a default
					if (init->isConst || init->constPtr)
						putBytes(bytes, p.offset, p.type->size(), init->isConst ? constConvert(init->constValue, decay(init->type), p.type) : init->constValue);
					else
					{
						if (v->readOnly && (d->constKind == 2 || m_options.rom))
							error(init->pos, "the values of a " + std::string(d->constKind == 2 ? "constexpr" : "constant") + " array must be constants or strings ('" +
								shownName(d->name) + "' is stored in the program" + (m_options.rom ? ", in ROM" : "") + ")");
						if (v->readOnly)
						{
							// A const array whose values are computed when the program runs: a constant
							// variable (in RAM; a local constant's: in its sub, at every call).
							v->readOnly = false;
							v->isConst = true;
							if (d->hoistKind == 1)
							{
								v->localRuntime = true;
								return;
							}
						}
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
				if (v->readOnly && pending.empty() && refs.empty())
					m_ctBlobs[v] = bytes;		// (a constant array: compile-time evaluation reads it)
				if (v->constKind == 3 && (!pending.empty() || d->c != nullptr))
					error((pending.empty() ? d->c : pending[0].init)->pos, "constinit '" + shownName(d->name) + "': its value must be known when compiling "
						"(this is computed when the program starts: use var, or constant values)");
				(isSharedData(v) ? m_dataRW : m_data).push_back(line);
				// Run time initializations.
				const size_t mark = beginExpr();
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
					varAddress(addr, v, false);
					addImmediate(addr, p.offset);
					if (p.type->kind == Type::Kind::Struct)
						genInitValue(addr, p.init, val, p.type);
					else
						store(addr, val, p.type);
					release(addr);
					release(val);
				}
				// var g: Struct(args): its constructor, after the values (in the order of the source).
				if (d->c)
					genConstructor(v, d);
				endExpr(mark);
				if (!m_options.library && destructible(t))
				{
					// (destroyed after main, the last built first)
					std::vector<std::string> chunk;
					std::vector<std::string>* out = m_out;
					m_out = &chunk;
					Reg a = alloc(d->pos);
					varAddress(a, v, false);
					genDestroy(a, t, 0);
					release(a);
					m_out = out;
					m_finiChunks.push_back(chunk);
				}
			});
		}
		m_globalsGenerated = m_globalDecls.size();
		m_initLocalMin = std::min(m_initLocalMin, m_localMin);
		m_out = outerOut;
		m_localCursor = outerCursor;
		m_localMin = outerMin;
	}

	// =============================== Compile-time evaluation ================================
	// constexpr / consteval subs (see CodeGen.hpp). The sub's statements are checked as usual
	// (with its parameters and locals in m_scopes, as variables of this memory), then run here.
	static const uint32_t CtBase = 0x10000000;
	static const int64_t CtMaxSteps = 4000000;
	static const int CtMaxDepth = 256;

	// A value of type t as a 64-bit number (signed types sign-extended; f32: its bits).
	static int64_t ctNumber(uint32_t value, const Type* t)
	{
		if (t != nullptr && t->isSigned())
			return (int32_t)value;
		return (int64_t)value;
	}

	void CodeGen::ctFail(const SourcePos& pos, const std::string& why)
	{
		throw CtFail { pos, why };
	}

	uint32_t CodeGen::ctAlloc(uint32_t size)
	{
		const uint32_t at = (uint32_t)alignUp((uint32_t)m_ct.size(), 4);
		if (at + size > 0x01000000)
			ctFail({}, "it needs more than 16 MiB of memory");
		m_ct.resize(at + std::max<uint32_t>(size, 1), 0);
		return CtBase + at;
	}

	uint32_t CodeGen::ctString(const std::string& text)
	{
		const uint32_t at = ctAlloc((uint32_t)text.size() + 1);
		std::memcpy(&m_ct[at - CtBase], text.data(), text.size());
		return at;
	}

	uint8_t* CodeGen::ctBytes(uint32_t addr, uint32_t size, const SourcePos& pos)
	{
		if (addr == 0)
			ctFail(pos, "it uses a null pointer");
		if (addr < CtBase || (uint64_t)addr - CtBase + size > m_ct.size())
			ctFail(pos, "it uses memory outside its own variables (an address it got from outside, or past the end of a variable)");
		return &m_ct[addr - CtBase];
	}

	uint32_t CodeGen::ctLoad(uint32_t addr, Type* t, const SourcePos& pos)
	{
		const uint32_t size = t->size();
		const uint8_t* p = ctBytes(addr, size, pos);
		uint32_t v = 0;
		for (uint32_t i = 0; i < size; i++)
			v = (v << 8) | p[i];
		switch (t->kind)
		{
			case Type::Kind::I8: return (uint32_t)(int32_t)(int8_t)v;
			case Type::Kind::I16: return (uint32_t)(int32_t)(int16_t)v;
			case Type::Kind::Bool: return v != 0;
			default: return v;
		}
	}

	void CodeGen::ctStore(uint32_t addr, Type* t, uint32_t value, const SourcePos& pos)
	{
		const uint32_t size = t->size();
		uint8_t* p = ctBytes(addr, size, pos);
		for (uint32_t i = 0; i < size; i++)
			p[i] = (uint8_t)(value >> (8 * (size - 1 - i)));
	}

	uint32_t CodeGen::ctConvert(uint32_t value, Type* from, Type* to)
	{
		if (to == nullptr || from == nullptr || to->kind == Type::Kind::Struct || to->kind == Type::Kind::Array || to->kind == Type::Kind::Void)
			return value;
		if (from->kind == Type::Kind::Null)
			return 0;
		if (to->isArithmetic() && (from->isArithmetic()))
			return (uint32_t)constConvert(ctNumber(value, from), from, to);
		if (to->kind == Type::Kind::Bool)
			return value != 0;
		int64_t delta = 0;
		if (value != 0 && pointerAdjust(from, to, delta))
			return value + (uint32_t)delta;		// (a part of a class's object)
		return value;		// pointers, sub pointers
	}

	// The read-only data's copy in this evaluation's memory.
	uint32_t CodeGen::ctBlobAddress(VarInfo* v)
	{
		auto it = m_ctGlobals.find(v);
		if (it != m_ctGlobals.end())
			return it->second;
		const std::vector<uint8_t>& bytes = m_ctBlobs[v];
		const uint32_t at = ctAlloc((uint32_t)bytes.size());
		if (!bytes.empty())
			std::memcpy(&m_ct[at - CtBase], bytes.data(), bytes.size());
		m_ctGlobals[v] = at;
		return at;
	}

	VarInfo* CodeGen::ctBlob(Type* t, const std::vector<uint8_t>& bytes)
	{
		const int n = ++m_ctBlobCount;
		VarInfo* v = new VarInfo();
		v->name = "__ct_value_" + std::to_string(n);
		v->type = t;
		v->storage = VarInfo::Storage::Global;
		v->symbol = "__dsct_" + std::to_string(n);
		v->readOnly = true;
		v->isConst = true;
		v->isStatic = true;
		v->ctValue = true;
		m_globals[v->name] = v;
		m_ctBlobs[v] = bytes;
		m_lazyData.push_back(v);
		return v;
	}

	void CodeGen::ctFields(StructInfo* info, const std::vector<uint8_t>& bytes, uint32_t offset, const std::string& prefix, ConstInfo& c)
	{
		for (const StructField& f : info->fields)
		{
			if (f.type->kind == Type::Kind::Struct)
			{
				ctFields(f.type->structInfo, bytes, offset + f.offset, prefix + f.name + ".", c);
				continue;
			}
			if (!f.type->isScalar())
				error(c.decl->pos, "constant '" + shownName(c.decl->name) + "': field '" + prefix + f.name + "' is " + f.type->name() +
					" (struct constants can only have integer, pointer and struct fields)");
			uint32_t v = 0;
			for (uint32_t i = 0; i < f.type->size(); i++)
				v = (v << 8) | bytes[offset + f.offset + i];
			if (f.type->isPointer() && v != 0)
				error(c.decl->pos, "constant '" + shownName(c.decl->name) + "': field '" + prefix + f.name + "' points into memory that exists only when compiling");
			c.fields[prefix + f.name] = wrapTo(ctNumber(f.type->kind == Type::Kind::I8 ? (uint32_t)(int32_t)(int8_t)v : f.type->kind == Type::Kind::I16 ? (uint32_t)(int32_t)(int16_t)v : v, f.type), f.type);
		}
	}

	// An argument known when compiling: its value (structs, strings, constant arrays: copied into
	// the memory, their address).
	bool CodeGen::ctArgument(Node* a, Type* param, uint32_t& out)
	{
		if (a->isConst && a->type != nullptr && (a->type->isArithmetic() || a->type->kind == Type::Kind::Null))
		{
			out = ctConvert((uint32_t)a->constValue, decay(a->type), param);
			return true;
		}
		if (a->constPtr)
		{
			out = 0;
			return a->constValue == 0;
		}
		if (a->kind == NodeKind::StrLit)
		{
			out = ctString(a->text);
			return true;
		}
		Node* x = (a->kind == NodeKind::Unary && a->name == "&") ? a->a : a;
		if (x->kind == NodeKind::Ident && x->var != nullptr && m_ctBlobs.count(x->var) > 0)
		{
			out = ctBlobAddress(x->var);
			return true;
		}
		return false;
	}

	// After checkCall chose f: if f is constexpr / consteval and the arguments are constants,
	// the call is evaluated and replaced by its value.
	void CodeGen::ctFold(Node* e, FuncInfo* f)
	{
		const int kind = f->decl->constKind;
		if (kind == 0 || m_ctDepth > 0)
			return;		// (while evaluating, calls are run, not folded)
		FuncInfo* current = m_inline.empty() ? m_func : m_inline.back().func;
		const bool inConsteval = current != nullptr && current->decl != nullptr && current->decl->constKind == 3;
		auto l_cannot = [&](const std::string& why) {
			m_ctWhy = why;
			if (kind == 3 && !inConsteval)
				error(e->pos, "'" + f->decl->shortName + "' is consteval (it must be evaluated when compiling), but " + why);
		};
		if (f->type->variadic != 0)
		{
			l_cannot("subs with ... are not evaluated when compiling");
			return;
		}
		if (f->type->base->kind == Type::Kind::Void)
		{
			l_cannot("it returns nothing (it has no value to give)");
			return;
		}
		m_ct.clear();
		m_ctGlobals.clear();
		m_ctSteps = 0;
		std::vector<uint32_t> args;
		const auto& params = f->type->params;
		for (size_t i = 0; i < params.size(); i++)
		{
			Node* a = i < e->list.size() ? e->list[i] : f->defaults[i];
			uint32_t v = 0;
			if (a == nullptr || !ctArgument(a, params[i], v))
			{
				l_cannot("argument " + std::to_string(i + 1) + " (" + f->paramNames[i] + ") is not a compile-time constant");
				return;
			}
			args.push_back(v);
		}
		uint32_t value = 0;
		try
		{
			value = ctCall(f, args, e->pos);
		}
		catch (CtFail& fail)
		{
			const std::string where = fail.pos.file.empty() ? "" : " (" + posString(fail.pos) + ")";
			l_cannot(fail.why + where);
			return;
		}
		catch (CompileError& err)
		{
			if (kind == 3 && !inConsteval)
				throw;
			m_ctWhy = err.message;
			return;		// (its errors are reported where it is compiled)
		}
		m_ctWhy.clear();
		Type* ret = f->type->base;
		if (ret->kind == Type::Kind::Struct)
		{
			// A struct value: read-only data (its pointer fields must be null).
			std::vector<uint8_t> bytes(ret->size());
			std::memcpy(bytes.data(), ctBytes(value, ret->size(), e->pos), ret->size());
			std::function<bool(StructInfo*, uint32_t)> l_pointers = [&](StructInfo* info, uint32_t offset) {
				for (const StructField& fl : info->fields)
				{
					if (fl.type->kind == Type::Kind::Struct && !l_pointers(fl.type->structInfo, offset + fl.offset))
						return false;
					if ((fl.type->isPointer() || fl.type->isFunction()) && (bytes[offset + fl.offset] | bytes[offset + fl.offset + 1] | bytes[offset + fl.offset + 2] | bytes[offset + fl.offset + 3]) != 0)
						return false;
				}
				return true;
			};
			if (!l_pointers(ret->structInfo, 0))
			{
				l_cannot("its value has a pointer into memory that exists only when compiling");
				return;
			}
			VarInfo* v = ctBlob(ret, bytes);
			e->kind = NodeKind::Ident;
			e->name = v->name;
			e->resolved = true;
			e->list.clear();
			e->a = nullptr;
			e->func = nullptr;
			e->var = v;
			e->type = ret;
			e->lvalue = false;
			e->readOnly = false;
			return;
		}
		if (ret->isPointer() || ret->isFunction())
		{
			if (value != 0)
			{
				l_cannot("it returns a pointer (into memory that exists only when compiling)");
				return;
			}
			e->constPtr = true;
			e->constValue = 0;
			return;
		}
		e->isConst = true;
		e->constValue = ret->isFloat() ? (int64_t)value : wrapTo(ctNumber(value, ret), ret);
	}

	uint32_t CodeGen::ctCall(FuncInfo* f, const std::vector<uint32_t>& args, const SourcePos& pos)
	{
		prepareConstructor(f);
		prepareDestructor(f);
		if (f->decl->constKind == 0)
			ctFail(pos, "it calls " + f->name + ", which is not constexpr");
		if (f->decl->a == nullptr)
			ctFail(pos, f->name + " has no body here");
		if (f->type->variadic != 0)
			ctFail(pos, "it calls " + f->name + ", which has ...");
		if ((int)m_ctFuncs.size() >= CtMaxDepth)
			ctFail(pos, "its calls go deeper than " + std::to_string(CtMaxDepth) + " (endless recursion?)");
		// The generator's state: the sub being evaluated is the one checked.
		struct Saved
		{
			CodeGen* cg; FuncInfo* func; std::vector<std::map<std::string, VarInfo*>> scopes; std::vector<InlineCall> inl; bool discarding; std::vector<SourcePos> scopeEnds;
			~Saved() { cg->m_func = func; cg->m_scopes = std::move(scopes); cg->m_inline = std::move(inl); cg->m_discarding = discarding; cg->m_scopeEnds = std::move(scopeEnds); cg->m_ctFuncs.pop_back(); cg->m_ctDepth--; }
		} saved { this, m_func, std::move(m_scopes), std::move(m_inline), m_discarding, std::move(m_scopeEnds) };
		m_ctFuncs.push_back(f);
		m_ctDepth++;
		m_func = f;
		m_scopes.clear();
		m_inline.clear();
		m_scopeEnds.clear();
		m_discarding = true;
		Type* ret = f->type->base;
		const uint32_t result = ret->kind == Type::Kind::Struct ? ctAlloc(ret->size()) : 0;
		const size_t mark = m_ct.size();
		m_scopes.push_back({});
		for (size_t i = 0; i < f->paramNames.size(); i++)
		{
			Type* t = f->type->params[i];
			VarInfo* v = new VarInfo();
			v->name = f->paramNames[i];
			v->type = t;
			v->storage = VarInfo::Storage::Local;
			v->ct = true;
			v->param = true;
			v->isConst = f->decl->list[i]->constKind == 1;
			v->offset = (int32_t)ctAlloc(t->size());
			if (t->kind == Type::Kind::Struct)
				std::memcpy(ctBytes((uint32_t)v->offset, t->size(), pos), ctBytes(args[i], t->size(), pos), t->size());
			else
				ctStore((uint32_t)v->offset, t, args[i], pos);
			m_scopes.back()[v->name] = v;
		}
		uint32_t value = 0;
		const CtFlow flow = ctStatement(f->decl->a, value);
		if (flow != CtFlow::Return)
			value = 0;		// (as at run time: no return gives 0)
		if (ret->kind == Type::Kind::Struct)
		{
			if (flow == CtFlow::Return)
				std::memcpy(ctBytes(result, ret->size(), pos), ctBytes(value, ret->size(), pos), ret->size());
			value = result;
		}
		m_ct.resize(mark);
		return value;
	}

	CodeGen::CtFlow CodeGen::ctStatement(Node* s, uint32_t& result)
	{
		if (++m_ctSteps > CtMaxSteps)
			ctFail(s->pos, "it takes too long (more than " + std::to_string(CtMaxSteps) + " steps)");
		switch (s->kind)
		{
			case NodeKind::SetVtables:
				ctFail(s->pos, "objects of classes with virtual subs (or interfaces) are not built when compiling");
			case NodeKind::Switch:
			{
				// The case the value is (or else), then the next ones while they fall.
				check(s->a);
				Type* t = decay(s->a->type);
				if (t == nullptr || !t->isInteger())
					ctFail(s->a->pos, "a switch on " + (t ? t->name() : std::string("?")));
				const int64_t v = ctNumber(ctEval(s->a), t);
				std::vector<SwitchEntry> entries;
				int elseCase = -1;
				switchEntries(s, t, entries, elseCase);
				int start = elseCase;
				for (const SwitchEntry& e : entries)
				{
					if (v >= e.lo && v <= e.hi)
						start = (int)e.c;
				}
				for (size_t i = start < 0 ? s->list.size() : (size_t)start; i < s->list.size(); i++)
				{
					const CtFlow flow = ctStatement(s->list[i]->b, result);
					if (flow == CtFlow::Break)
						return CtFlow::Normal;
					if (flow != CtFlow::Normal)
						return flow;
					if (s->value != 1)
						break;
				}
				return CtFlow::Normal;
			}
			case NodeKind::Block:
			{
				const size_t mark = m_ct.size();
				m_scopes.push_back({});
				CtFlow flow = CtFlow::Normal;
				for (Node* st : s->list)
				{
					if (st->kind == NodeKind::Defer)
						ctFail(st->pos, "defer is not evaluated when compiling");
					flow = ctStatement(st, result);
					if (flow != CtFlow::Normal)
						break;
				}
				m_scopes.pop_back();
				if (flow != CtFlow::Return)
					m_ct.resize(mark);		// (a returned struct may be in this block's memory)
				return flow;
			}
			case NodeKind::VarDecl:
				if (s->hoisted != nullptr)
				{
					Node* h = s->hoisted;
					if (h->kind == NodeKind::ConstDecl && m_consts.count(h->name) > 0)
					{
						decideConst(h->name);
						if (!m_consts[h->name].runtime)
							return CtFlow::Normal;
					}
					else if (m_globals.count(h->name) == 0 || !m_globals[h->name]->localRuntime)
						return CtFlow::Normal;
				}
				ctLocal(s);
				return CtFlow::Normal;
			case NodeKind::ExprStmt:
				check(s->a);
				ctEval(s->a);
				return CtFlow::Normal;
			case NodeKind::StaticAssert:
				checkStaticAssert(s, nullptr);
				return CtFlow::Normal;
			case NodeKind::Assert:
			{
				check(s->a);
				if (!constTruth(ctEval(s->a), decay(s->a->type)))
					ctFail(s->pos, "assert(" + s->text + ") failed");
				return CtFlow::Normal;
			}
			case NodeKind::If:
			{
				if (s->isConstexpr)
				{
					if (evalCondition(s->a, nullptr) != 0)
						return ctStatement(s->b, result);
					return s->c ? ctStatement(s->c, result) : CtFlow::Normal;
				}
				check(s->a);
				if (!decay(s->a->type)->isScalar())
					error(s->a->pos, "the condition must be an integer or a pointer");
				if (constTruth(ctEval(s->a), decay(s->a->type)))
					return ctStatement(s->b, result);
				return s->c ? ctStatement(s->c, result) : CtFlow::Normal;
			}
			case NodeKind::While:
			case NodeKind::DoWhile:
			{
				check(s->a);
				const bool doWhile = s->kind == NodeKind::DoWhile;
				while (doWhile || constTruth(ctEval(s->a), decay(s->a->type)))
				{
					const CtFlow flow = ctStatement(s->b, result);
					if (flow == CtFlow::Return)
						return flow;
					if (flow == CtFlow::Break)
						break;
					if (doWhile && !constTruth(ctEval(s->a), decay(s->a->type)))
						break;
				}
				return CtFlow::Normal;
			}
			case NodeKind::ForIn:
				return ctStatement(forInLoop(s), result);
			case NodeKind::For:
			{
				const size_t mark = m_ct.size();
				m_scopes.push_back({});
				if (s->a)
					ctStatement(s->a, result);
				if (s->b)
					check(s->b);
				if (s->c)
					check(s->c);
				CtFlow out = CtFlow::Normal;
				while (!s->b || constTruth(ctEval(s->b), decay(s->b->type)))
				{
					const CtFlow flow = ctStatement(s->d, result);
					if (flow == CtFlow::Return)
					{
						out = flow;
						break;
					}
					if (flow == CtFlow::Break)
						break;
					if (s->c)
						ctEval(s->c);
				}
				m_scopes.pop_back();
				if (out != CtFlow::Return)
					m_ct.resize(mark);
				return out;
			}
			case NodeKind::Return:
			{
				FuncInfo* fn = m_ctFuncs.back();
				Type* ret = fn->type->base;
				if (s->a == nullptr)
				{
					if (ret->kind != Type::Kind::Void)
						error(s->pos, fn->name + " must return a value (" + ret->name() + ")");
					result = 0;
					return CtFlow::Return;
				}
				if (ret->kind == Type::Kind::Void)
					error(s->pos, fn->name + " returns void");
				check(s->a, ret);
				requireConvertible(s->a, ret, "in the return value");
				result = ctConvert(ctEval(s->a), decay(s->a->type), ret);
				return CtFlow::Return;
			}
			case NodeKind::Break: return CtFlow::Break;
			case NodeKind::Continue: return CtFlow::Continue;
			case NodeKind::LowBlock: ctFail(s->pos, "low blocks run only on the machine");
			case NodeKind::DebugPrint: ctFail(s->pos, "debug_print runs only on the machine");
			case NodeKind::Defer: ctFail(s->pos, "defer is not evaluated when compiling");
			default: ctFail(s->pos, "this statement is not evaluated when compiling");
		}
	}

	void CodeGen::ctLocal(Node* s)
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
			if (t->isInteger() && s->a->kind == NodeKind::IntLit && s->a->name != "u")
				t = m_types.i32();
		}
		if (t->kind == Type::Kind::Void)
			error(s->pos, "variables cannot be void");
		if (s->refKind != 0 || t->isReference())
			ctFail(s->pos, "reference locals are not evaluated when compiling (use a pointer)");
		if (t->kind == Type::Kind::Struct)
		{
			resolveStruct(t->structInfo, s->pos);
			requireValueType(t, s->pos, "a variable");
		}
		VarInfo* v = new VarInfo();
		v->name = s->name;
		v->type = t;
		v->storage = VarInfo::Storage::Local;
		v->ct = true;
		v->isConst = s->constKind == 1;
		v->offset = (int32_t)ctAlloc(t->size());
		std::vector<std::pair<int32_t, const StructField*>> defaults;
		std::vector<std::pair<int32_t, std::string>> vptrs;
		const bool discarding = m_discarding;
		m_discarding = true;		// (no table: only whether there is one)
		collectDefaults(t, 0, defaults, &vptrs);
		m_discarding = discarding;
		if (!vptrs.empty())
			ctFail(s->pos, "objects of classes with virtual subs (or interfaces) are not built when compiling");
		if (destructible(t))
			ctFail(s->pos, "values with destructors are not built when compiling");
		for (const auto& d : defaults)
		{
			Node* init = d.second->init;
			uint32_t value = 0;
			if (init->kind == NodeKind::StrLit)
				value = ctString(init->text);
			else if (init->kind == NodeKind::Ident && init->func != nullptr)
				ctFail(init->pos, "a sub pointer (field '" + d.second->name + "') is not evaluated when compiling");
			else
				value = (uint32_t)constConvert(init->constValue, decay(init->type), d.second->type);
			ctStore((uint32_t)v->offset + (uint32_t)d.first, d.second->type, value, s->pos);
		}
		if (s->a)
			ctInit((uint32_t)v->offset, t, s->a);
		if (s->c)
		{
			m_scopes.push_back({ { " ctor", v } });
			check(s->c);
			ctEval(s->c);
			m_scopes.pop_back();
		}
		m_scopes.back()[s->name] = v;
	}

	void CodeGen::ctInit(uint32_t addr, Type* type, Node* init)
	{
		foldStringConst(init);
		if (type->kind == Type::Kind::Array && init->kind == NodeKind::StrLit)
		{
			if (type->base->size() != 1)
				error(init->pos, "strings can only initialize u8 / i8 arrays");
			if (init->text.size() + 1 > type->count && init->text.size() != type->count)
				error(init->pos, "the string is longer than the array (" + std::to_string(type->count) + " bytes)");
			const uint32_t n = std::min<uint32_t>((uint32_t)init->text.size() + 1, type->count);
			uint8_t* p = ctBytes(addr, n, init->pos);
			for (uint32_t i = 0; i < n; i++)
				p[i] = i < init->text.size() ? (uint8_t)init->text[i] : 0;
			return;
		}
		if (init->kind == NodeKind::InitList)
		{
			if (type->kind == Type::Kind::Array)
			{
				if (init->list.size() > type->count)
					error(init->pos, "too many values for " + type->name());
				for (size_t i = 0; i < init->list.size(); i++)
					ctInit(addr + (uint32_t)i * type->base->size(), type->base, init->list[i]);
				return;
			}
			if (type->kind == Type::Kind::Struct)
			{
				const auto& fields = type->structInfo->fields;
				if (init->list.size() > fields.size())
					error(init->pos, "too many values for struct " + type->name());
				for (size_t i = 0; i < init->list.size(); i++)
				{
					checkFieldAccess(type->structInfo, &fields[i], init->list[i]->pos);
					ctInit(addr + fields[i].offset, fields[i].type, init->list[i]);
				}
				return;
			}
			error(init->pos, "{ ... } initializes arrays and structs, not " + type->name());
		}
		check(init, type);
		requireConvertible(init, type, "in the initialization");
		if (type->kind == Type::Kind::Struct)
		{
			const uint32_t src = ctEval(init);
			std::memmove(ctBytes(addr, type->size(), init->pos), ctBytes(src, type->size(), init->pos), type->size());
			return;
		}
		if (type->kind == Type::Kind::Array)
			error(init->pos, "arrays are initialized with { ... } or a string");
		ctStore(addr, type, ctConvert(ctEval(init), decay(init->type), type), init->pos);
	}

	// a OP b (arithmetic, comparisons, bits) as the generated code computes it: the operands in
	// `result`'s type (comparisons: in the type they are compared in).
	uint32_t CodeGen::ctArith(const std::string& op, uint32_t a, Type* ta, uint32_t b, Type* tb, Type* result, const SourcePos& pos)
	{
		if (isComparison(op))
		{
			bool r = false;
			if (ta->isFloat() || tb->isFloat())
			{
				const float x = f32Of(constConvert(ctNumber(a, ta), ta, m_types.f32())), y = f32Of(constConvert(ctNumber(b, tb), tb, m_types.f32()));
				if (op == "==") r = x == y; else if (op == "!=") r = x != y; else if (op == "<") r = x < y;
				else if (op == "<=") r = x <= y; else if (op == ">") r = x > y; else r = x >= y;
				return r;
			}
			const bool isUnsigned = !ta->isInteger() || !tb->isInteger() || arithmeticType(ta, tb)->kind == Type::Kind::U32;
			const int64_t x = isUnsigned ? (int64_t)a : (int64_t)(int32_t)a, y = isUnsigned ? (int64_t)b : (int64_t)(int32_t)b;
			if (op == "==") r = x == y; else if (op == "!=") r = x != y; else if (op == "<") r = x < y;
			else if (op == "<=") r = x <= y; else if (op == ">") r = x > y; else r = x >= y;
			return r;
		}
		if (result->isFloat())
		{
			const float x = f32Of(constConvert(ctNumber(a, ta), ta, result)), y = f32Of(constConvert(ctNumber(b, tb), tb, result));
			float r = 0.0f;
			if (op == "+") r = x + y; else if (op == "-") r = x - y; else if (op == "*") r = x * y; else r = x / y;
			return (uint32_t)bitsOf(r);
		}
		const bool isUnsigned = result->kind == Type::Kind::U32;
		const int64_t x = isUnsigned ? (int64_t)a : (int64_t)(int32_t)a, y = isUnsigned ? (int64_t)b : (int64_t)(int32_t)b;
		if ((op == "/" || op == "%") && y == 0)
			ctFail(pos, "it divides by zero");
		int64_t r = 0;
		if (op == "+") r = x + y; else if (op == "-") r = x - y; else if (op == "*") r = x * y;
		else if (op == "/") r = (!isUnsigned && x == INT32_MIN && y == -1) ? x : x / y;
		else if (op == "%") r = (!isUnsigned && x == INT32_MIN && y == -1) ? 0 : x % y;
		else if (op == "&") r = x & y; else if (op == "|") r = x | y; else if (op == "^") r = x ^ y;
		else if (op == "<<") r = (int64_t)((uint32_t)x << (y & 31));
		else if (op == ">>") r = isUnsigned ? (int64_t)((uint32_t)x >> (y & 31)) : (int64_t)((int32_t)x >> (y & 31));
		return (uint32_t)wrapTo(r, result);
	}

	uint32_t CodeGen::ctEval(Node* e)
	{
		if (++m_ctSteps > CtMaxSteps)
			ctFail(e->pos, "it takes too long (more than " + std::to_string(CtMaxSteps) + " steps)");
		if (e->isConst && e->type != nullptr && (e->type->isArithmetic() || e->type->kind == Type::Kind::Null))
			return (uint32_t)e->constValue;
		if (e->constPtr)
		{
			if (e->constValue != 0)
				ctFail(e->pos, "it uses a memory address (" + number(e->constValue) + "): memory exists only when the program runs");
			return 0;
		}
		Type* t = e->type;
		const bool aggregate = t != nullptr && (t->kind == Type::Kind::Struct || t->kind == Type::Kind::Array);
		switch (e->kind)
		{
			case NodeKind::StrLit: return ctString(e->text);
			case NodeKind::Null: return 0;
			case NodeKind::Ident:
			{
				if (e->var == nullptr)
					ctFail(e->pos, "it uses the sub '" + e->name + "' as a value");
				const uint32_t addr = ctAddr(e);
				return aggregate ? addr : ctLoad(addr, t, e->pos);
			}
			case NodeKind::Index:
			case NodeKind::Member:
			{
				const uint32_t addr = ctAddr(e);
				return aggregate ? addr : ctLoad(addr, t, e->pos);
			}
			case NodeKind::Unary:
			{
				const std::string& op = e->name;
				if (op == "&")
				{
					if (e->a->func != nullptr || (e->a->kind == NodeKind::Ident && e->a->var == nullptr))
						ctFail(e->pos, "it takes the address of a sub");
					return ctAddr(e->a);
				}
				if (op == "*")
				{
					const uint32_t p = ctEval(e->a);
					return aggregate ? p : ctLoad(p, t, e->pos);
				}
				Type* ta = decay(e->a->type);
				const uint32_t v = ctEval(e->a);
				if (op == "!")
					return !constTruth(v, ta);
				if (op == "-" && ta->isFloat())
					return v ^ 0x80000000u;
				const int64_t x = ctNumber(ctConvert(v, ta, t), t);
				return (uint32_t)wrapTo(op == "-" ? -x : ~x, t);
			}
			case NodeKind::Binary:
			{
				const std::string& op = e->name;
				Type* ta = decay(e->a->type);
				Type* tb = decay(e->b->type);
				if (op == "&&" || op == "||")
				{
					const bool x = constTruth(ctEval(e->a), ta);
					if (op == "&&" ? !x : x)
						return x;
					return constTruth(ctEval(e->b), tb);
				}
				const uint32_t a = ctEval(e->a);
				const uint32_t b = ctEval(e->b);
				// Pointers: + / - an integer (scaled), the difference of two, comparisons.
				if (ta->kind == Type::Kind::Pointer && tb->isInteger() && (op == "+" || op == "-"))
					return a + (uint32_t)((op == "+" ? 1 : -1) * ctNumber(b, tb) * (int64_t)std::max<uint32_t>(ta->base->size(), 1));
				if (tb->kind == Type::Kind::Pointer && ta->isInteger() && op == "+")
					return b + (uint32_t)(ctNumber(a, ta) * (int64_t)std::max<uint32_t>(tb->base->size(), 1));
				if (ta->kind == Type::Kind::Pointer && tb->kind == Type::Kind::Pointer && op == "-")
					return (uint32_t)(int32_t)(((int64_t)a - (int64_t)b) / (int64_t)std::max<uint32_t>(ta->base->size(), 1));
				return ctArith(op, a, ta, b, tb, t, e->pos);
			}
			case NodeKind::Assign:
			{
				Type* ta = e->a->type;
				const uint32_t addr = ctAddr(e->a);
				if (e->name == "=")
				{
					if (ta->kind == Type::Kind::Struct)
					{
						const uint32_t src = ctEval(e->b);
						std::memmove(ctBytes(addr, ta->size(), e->pos), ctBytes(src, ta->size(), e->pos), ta->size());
						return 0;
					}
					const uint32_t v = ctConvert(ctEval(e->b), decay(e->b->type), ta);
					ctStore(addr, ta, v, e->pos);
					return v;
				}
				const std::string op = e->name.substr(0, e->name.size() - 1);
				Type* tb = decay(e->b->type);
				const uint32_t cur = ctLoad(addr, ta, e->pos);
				const uint32_t b = ctEval(e->b);
				uint32_t v = 0;
				if (ta->kind == Type::Kind::Pointer)
					v = cur + (uint32_t)((op == "+" ? 1 : -1) * ctNumber(b, tb) * (int64_t)std::max<uint32_t>(ta->base->size(), 1));
				else
				{
					Type* rt = ta->isFloat() || tb->isFloat() ? m_types.f32() : (op == "<<" || op == ">>") ? promote(ta) : arithmeticType(ta, tb);
					v = ctConvert(ctArith(op, cur, ta, b, tb, rt, e->pos), rt, ta);
				}
				ctStore(addr, ta, v, e->pos);
				return v;
			}
			case NodeKind::Ternary:
			{
				Node* chosen = constTruth(ctEval(e->a), decay(e->a->type)) ? e->b : e->c;
				return ctConvert(ctEval(chosen), decay(chosen->type), t);
			}
			case NodeKind::Construct:
				ctFail(e->pos, "it builds an object from a value (a conversion: when the program runs)");
			case NodeKind::Materialize:
				ctFail(e->pos, "it passes a value by reference (when the program runs)");
			case NodeKind::InPlace:
				ctFail(e->pos, "it builds or destroys a value in memory (when the program runs)");
			case NodeKind::Call:
			{
				if (e->vaBuiltin != 0)
					ctFail(e->pos, "va_count / va_start / va_types are not evaluated when compiling");
				if (e->handler != nullptr)
					ctFail(e->pos, "it calls a sub that throws (errors: when the program runs)");
				FuncInfo* f = e->func;
				if (f == nullptr)
					ctFail(e->pos, "it calls a sub through a pointer");
				if (e->vslot != -1)
					ctFail(e->pos, "it calls a virtual sub (through its object's table)");
				std::vector<uint32_t> args;
				const auto& params = f->type->params;
				if (e->list.size() > params.size())
					ctFail(e->pos, "it calls " + f->name + " with ... arguments");
				for (size_t i = 0; i < params.size(); i++)
				{
					if (i < e->list.size())
					{
						Node* a = e->list[i];
						uint32_t v = ctEval(a);
						if (params[i]->kind == Type::Kind::Struct)
						{
							// (a copy: the callee changes its own)
							const uint32_t copy = ctAlloc(params[i]->size());
							std::memmove(ctBytes(copy, params[i]->size(), a->pos), ctBytes(v, params[i]->size(), a->pos), params[i]->size());
							v = copy;
						}
						else
							v = ctConvert(v, decay(a->type), params[i]);
						args.push_back(v);
					}
					else
					{
						Node* d = f->defaults[i];
						args.push_back(d->kind == NodeKind::StrLit ? ctString(d->text) : (uint32_t)constConvert(d->constValue, decay(d->type), params[i]));
					}
				}
				return ctCall(f, args, e->pos);
			}
			case NodeKind::Cast:
				if (e->value == 3 && decay(e->a->type)->kind != Type::Kind::Null && conversionCost(e->a, t) < 0)
					ctFail(e->pos, "dynamic_cast reads an object's table (when the program runs)");
				if (e->value == 1)
				{
					Type* from = e->a->type->kind == Type::Kind::Null ? m_types.pointer(m_types.voidType()) : e->a->type;
					const bool fromMemory = from->kind == Type::Kind::Struct || from->kind == Type::Kind::Array;
					const bool toMemory = t->kind == Type::Kind::Struct;
					const uint32_t v = ctEval(e->a);
					if (fromMemory && !toMemory)
						return ctLoad(v, t, e->pos);
					if (!fromMemory && toMemory)
					{
						const uint32_t tmp = ctAlloc(t->size());
						ctStore(tmp, from, v, e->pos);
						return tmp;
					}
					if (fromMemory)
						return v;
					const uint32_t mask = t->size() >= 4 ? 0xFFFFFFFFu : ((1u << (8 * t->size())) - 1);
					return t->isFloat() ? (v & mask) : (uint32_t)wrapTo((int64_t)(v & mask), t);
				}
				return ctConvert(ctEval(e->a), decay(e->a->type), t);
			case NodeKind::PreInc: case NodeKind::PreDec: case NodeKind::PostInc: case NodeKind::PostDec:
			{
				const uint32_t addr = ctAddr(e->a);
				const uint32_t old = ctLoad(addr, t, e->pos);
				const bool inc = e->kind == NodeKind::PreInc || e->kind == NodeKind::PostInc;
				uint32_t v = 0;
				if (t->kind == Type::Kind::Pointer)
					v = old + (inc ? 1u : (uint32_t)-1) * std::max<uint32_t>(t->base->size(), 1);
				else if (t->isFloat())
					v = (uint32_t)bitsOf(f32Of(old) + (inc ? 1.0f : -1.0f));
				else
					v = (uint32_t)wrapTo(ctNumber(old, t) + (inc ? 1 : -1), t);
				ctStore(addr, t, v, e->pos);
				return (e->kind == NodeKind::PreInc || e->kind == NodeKind::PreDec) ? v : old;
			}
			default:
				ctFail(e->pos, "this is not evaluated when compiling");
		}
	}

	uint32_t CodeGen::ctAddr(Node* e)
	{
		switch (e->kind)
		{
			case NodeKind::Ident:
			{
				VarInfo* v = e->var;
				if (v == nullptr)
					ctFail(e->pos, "it uses the sub '" + e->name + "' as a value");
				if (v->ct)
					return (uint32_t)v->offset;
				if (m_ctBlobs.count(v) > 0)
					return ctBlobAddress(v);
				ctFail(e->pos, "it uses '" + (e->text.empty() ? shownName(e->name) : e->text) + "', a variable that exists only when the program runs");
			}
			case NodeKind::Unary:
				if (e->name == "*")
					return ctEval(e->a);
			break;
			case NodeKind::Index:
			{
				Type* ta = decay(e->a->type);
				const uint32_t base = ctEval(e->a);
				return base + (uint32_t)(ctNumber(ctEval(e->b), decay(e->b->type)) * (int64_t)ta->base->size());
			}
			case NodeKind::Member:
			{
				Type* st = e->value == 1 ? decay(e->a->type)->base : e->a->type;
				const StructField* f = st->structInfo->field(e->name);
				const uint32_t base = ctEval(e->a);		// (a struct: its address; ->: the pointer)
				return base + f->offset;
			}
			case NodeKind::Call:
			case NodeKind::Cast:
				if (e->type != nullptr && e->type->kind == Type::Kind::Struct)
					return ctEval(e);
			break;
			default: break;
		}
		ctFail(e->pos, "this has no address when compiling");
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
			m_defers.clear();
			m_inDefer = 0;
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
		const bool hidden = v->isStatic;
		std::string where;
		if (v->param)
			where = "parameter";
		else if (v->storage == VarInfo::Storage::Local)
			where = "local";
		else if (hidden)
			where = (v->readOnly ? "" : "static, ") + ownerOf(v->decl);
		else
			where = v->readOnly ? "constant array" : "global";
		const std::string size = (v->type && !v->type->isScalar()) ? bytes(v->type->size()) : "";
		if (v->isConst && !v->readOnly && v->storage == VarInfo::Storage::Global)
			where = (hidden ? "static, " + ownerOf(v->decl) : "global") + std::string(", computed when the program starts");
		std::string keyword = (v->readOnly || v->isConst) ? (hidden && !v->readOnly ? "static const " : "const ") : v->constKind == 3 ? "constinit " : (hidden ? "static var " : "var ");
		if (v->decl != nullptr && v->decl->owner != nullptr && v->decl->owner->kind == NodeKind::StructDecl && v->storage == VarInfo::Storage::Global)
		{
			// A static field.
			keyword = std::string(v->decl->isPrivate ? "priv " : "") + (v->decl->isReadonly ? "readonly " : "") + (v->isConst ? "static const " : "static var ");
			return keyword + v->decl->shortName + ": " + type + comment({ "static field of " + v->decl->owner->name, size });
		}
		return keyword + (hidden ? declaredName(v->decl) : v->name) + ": " + type + comment({ where, size });
	}

	std::string CodeGen::hoverFunc(FuncInfo* f)
	{
		const bool lambda = f->decl->name.rfind("__lambda_", 0) == 0;
		std::string s;
		if (f->interrupt)
			s += "interrupt ";
		if (f->decl->constKind == 2)
			s += "constexpr ";
		else if (f->decl->constKind == 3)
			s += "consteval ";
		if ((f->decl->value & 2) != 0)
			s += "inline ";
		const bool method = (f->decl->value & 4) != 0;
		const bool isStatic = (f->decl->value & 8) != 0;
		const int v = f->decl->value;
		const bool inInterface = method && f->decl->owner != nullptr && f->decl->owner->classKind == 2;
		s = std::string((v & 16) != 0 ? "priv " : "") + ((v & 8192) != 0 ? "prot " : "") + (isStatic ? "static " : "") + ((v & 512) != 0 ? "virt " : "")
			+ ((v & 1024) != 0 && !inInterface ? "pure " : "") + ((v & 2048) != 0 ? "override " : "") + ((v & 4096) != 0 ? "final " : "") + ((v & 64) != 0 ? "readonly " : "") + s;
		const size_t first = (method && !isStatic) ? 1 : 0;	// this is not shown
		const std::string owner = (method && f->decl->owner) ? f->decl->owner->name : "";
		s += lambda ? "sub(" : "sub " + (method ? owner + "." + f->decl->shortName : f->decl->name) + "(";
		if (f->type != nullptr)
		{
			for (size_t i = first; i < f->type->params.size() && i < f->paramNames.size(); i++)
			{
				const bool constParam = i < f->decl->list.size() && f->decl->list[i]->constKind == 1;
				s += (i > first ? ", " : "") + std::string(constParam ? "const " : "var ") + f->paramNames[i] + ": " + f->type->params[i]->name(true);
				const Node* d = i < f->defaults.size() ? f->defaults[i] : nullptr;
				if (d != nullptr)
				{
					const bool isBool = f->type->params[i]->kind == Type::Kind::Bool;
					s += " = " + (d->kind == NodeKind::StrLit ? quoted(d->text) : !d->isConst ? std::string("...")
						: isBool ? std::string(d->constValue ? "true" : "false") : std::to_string(d->constValue));
				}
			}
		}
		if (f->type != nullptr && f->type->variadic != 0)
			s += std::string(f->type->params.size() > first ? ", " : "") + (f->type->variadic == 2 ? "...typed" : "...");
		s += ")";
		if (f->type != nullptr && f->type->base->kind != Type::Kind::Void)
			s += " -> " + f->type->base->name(true);
		return s + comment({ lambda ? "lambda" : isStatic ? "static sub of " + owner : method ? "method of " + owner : "" });
	}

	std::string CodeGen::hoverConst(const std::string& name)
	{
		const ConstInfo& c = m_consts[name];
		const std::string head = std::string(c.decl->constKind == 2 ? "constexpr " : "const ") + (c.decl->text.empty() ? name : c.decl->text);
		const std::string note = comment({ ownerOf(c.decl) });
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
		if (c.type->isFloat())
			return head + ": " + c.type->name(true) + " = " + floatText(c.value) + note;
		return head + ": " + c.type->name(true) + " = " + number(c.value) + note;
	}

	std::string CodeGen::hoverStruct(StructInfo* info)
	{
		std::string s = info->anonymous ? "struct" : std::string(info->isAbstract ? "abstract " : "") + (info->isFinal ? "final " : "") +
			(info->isClass() ? "class " : info->isInterface() ? "interface " : "struct ") + info->name;
		if (!info->complete)
			return s;
		if (info->base != nullptr)
			s += " ex " + info->base->name;
		else if (info->isInterface() && !info->interfaces.empty())
			s += " ex " + info->interfaces[0]->name;
		for (size_t i = 0; info->isClass() && i < info->interfaces.size(); i++)
			s += (i == 0 ? " impl " : ", ") + info->interfaces[i]->name;
		s += " {  // " + bytes(info->size) + "\n";
		for (const StructField& f : info->fields)
			s += std::string("\t") + (f.isPrivate ? "priv " : "") + (f.isProtected ? "prot " : "") + (f.isReadonly ? "readonly " : "") + (f.isConst ? "const " : "var ") + f.name + ": " +
				(f.type ? f.type->name(true) : "?") + ";  // +" + std::to_string(f.offset) + (f.owner != nullptr ? " (" + f.owner->name + ")" : "") + "\n";
		// Its methods and static subs (the signature, without the comment).
		for (FuncInfo* f : m_funcList)
		{
			if ((f->decl->value & 4) == 0 || f->decl->owner != info->decl)
				continue;
			std::string sig = hoverFunc(f);
			const size_t dot = sig.find(" " + info->name + ".");	// sub Name.m(...) -> sub m(...)
			if (dot != std::string::npos)
				sig.erase(dot + 1, info->name.size() + 1);
			const size_t c = sig.find("  //");
			s += "\t" + (c == std::string::npos ? sig : sig.substr(0, c)) + ";\n";
		}
		return s + "}";
	}

	std::string CodeGen::hoverField(StructInfo* info, const StructField& f)
	{
		const std::string owner = info->anonymous ? "struct { ... }" : info->name;
		return std::string(f.isPrivate ? "priv " : "") + (f.isProtected ? "prot " : "") + (f.isReadonly ? "readonly " : "") + (f.isConst ? "const " : "var ") + f.name + ": " + (f.type ? f.type->name(true) : "?") +
			comment({ "field of " + (f.owner != nullptr ? f.owner->name + " (in " + owner + ")" : owner) + " at +" + std::to_string(f.offset),
			f.type ? bytes(f.type->size()) : "" });
	}

	std::string CodeGen::hoverEnum(const std::string& name)
	{
		EnumInfo& info = m_enums[name];
		std::string s = "enum " + name + (info.decl->typeSyntax && info.type ? ": " + info.type->name() : std::string()) + " {\n";
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
	// written: as in the low block (a local constant / static: its hidden name, shown as written).
	void CodeGen::indexLow(const SourcePos& pos, const std::string& written, const std::string& name)
	{
		const std::string shown = (written == name) && name.find("::") != std::string::npos && m_names.count(name) > 0
			&& ((m_globals.count(name) > 0 && m_globals[name]->isStatic) || (m_consts.count(name) > 0 && !m_consts[name].decl->text.empty()))
			? shownName(name) : written;
		if (VarInfo* v = lookupVar(name))
			indexName(pos, shown, v->defPos, hoverVar(v));
		else if (m_consts.count(name) > 0)
			indexName(pos, shown, m_consts[name].decl->namePos, hoverConst(name));
		else if (m_enums.count(name) > 0)
			indexName(pos, shown, m_enums[name].decl->namePos, hoverEnum(name));
		else if (m_funcs.count(name) > 0 && !m_funcs[name].empty())
			indexName(pos, shown, m_funcs[name][0]->decl->namePos, hoverFunc(m_funcs[name][0]));
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
			l_decl(info->decl->namePos, info->anonymous ? "struct" : declaredName(info->decl), hoverStruct(info));
			for (const StructField& f : info->fields)
				l_decl(f.namePos, f.name, hoverField(info, f));
		}
		for (auto& e : m_enums)
		{
			l_decl(e.second.decl->namePos, declaredName(e.second.decl), hoverEnum(e.first));
			for (const Node* m : e.second.decl->list)
			{
				auto it = e.second.values.find(m->name);
				l_decl(m->namePos, m->name, e.first + "." + m->name + " = " + (it != e.second.values.end() ? number(it->second) : "?"));
			}
		}
		for (auto& c : m_consts)
			l_decl(c.second.decl->namePos, declaredName(c.second.decl), hoverConst(c.first));
		for (auto& a : m_aliases)
			l_decl(a.second.decl->namePos, declaredName(a.second.decl), hoverAlias(a.first));
		for (auto& g : m_globals)
		{
			if (g.second->type != nullptr)
				l_decl(g.second->defPos, g.second->decl ? declaredName(g.second->decl) : shownName(g.first), hoverVar(g.second));
		}
		for (FuncInfo* f : m_funcList)
		{
			const bool lambda = f->decl->name.rfind("__lambda_", 0) == 0;
			l_decl(f->decl->namePos, lambda ? "sub" : declaredName(f->decl), hoverFunc(f));
		}
		m_options.index->rehover(hovers);
	}

	// A type for completion (see Completion in Index.hpp).
	static std::string typeCode(const Type* t)
	{
		if (t == nullptr)
			return "i";
		switch (t->kind)
		{
			case Type::Kind::Void: return "v";
			case Type::Kind::Struct: return "s:" + t->structInfo->name;
			case Type::Kind::Pointer: return t->isRef ? typeCode(t->base) : "p:" + typeCode(t->base);
			case Type::Kind::Array: return "a:" + typeCode(t->base);
			case Type::Kind::Function: return "f:" + typeCode(t->base);
			default: return "i";
		}
	}

	void CodeGen::completeLocal(VarInfo* v)
	{
		// Not the expansions of inline subs (their own body is analyzed too).
		if (m_options.index == nullptr || v == nullptr || !m_inline.empty() || m_scopeEnds.empty())
			return;
		if (v->name.rfind("__forin", 0) == 0)		// (a for-in's own variables: the range, its end, the cursor)
			return;
		CompletionSymbol c;
		c.name = v->name;
		c.kind = v->param ? CompletionSymbol::Param : CompletionSymbol::Local;
		c.type = typeCode(v->type);
		c.hover = hoverVar(v);
		c.def = v->defPos;
		c.end = m_scopeEnds.back();
		for (const CompletionSymbol& l : m_options.index->completion.locals)
		{
			if (l.name == c.name && l.def.line == c.def.line && l.def.column == c.def.column && l.def.file == c.def.file)	// generated again (a defer)
				return;
		}
		m_options.index->completion.locals.push_back(c);
	}

	void CodeGen::indexCompletion(Node* program)
	{
		Completion& out = m_options.index->completion;
		auto l_func = [&](const Node* d) -> FuncInfo* {
			for (FuncInfo* f : m_funcList)
			{
				if (f->decl == d)
					return f;
			}
			return nullptr;
		};
		std::set<std::string> namespaces;
		for (Node* d : program->list)
		{
			CompletionSymbol c;
			c.name = d->shortName.empty() ? d->name : d->shortName;
			c.parent = (d->owner != nullptr && d->owner->kind == NodeKind::StructDecl) ? d->owner->name : d->scope;
			c.def = d->namePos;
			c.isPrivate = d->isPrivate;
			// Local constants / statics: visible in their sub, after their declaration.
			if (!d->text.empty())
			{
				if (d->owner == nullptr || d->owner->kind != NodeKind::FuncDecl || d->owner->a == nullptr)
					continue;
				c.name = d->text;
				c.parent.clear();
				c.end = d->owner->a->endPos;
				if (m_globals.count(d->name) > 0)
				{
					c.kind = d->kind == NodeKind::ConstDecl ? CompletionSymbol::Const : CompletionSymbol::Local;
					c.type = typeCode(m_globals[d->name]->type);
					c.hover = hoverVar(m_globals[d->name]);
				}
				else if (m_consts.count(d->name) > 0)
				{
					c.kind = CompletionSymbol::Const;
					c.type = m_consts[d->name].isString ? "p:i" : typeCode(m_consts[d->name].type);
					c.hover = hoverConst(d->name);
				}
				else if (d->kind == NodeKind::AliasDecl && m_aliases.count(d->name) > 0)
				{
					c.kind = CompletionSymbol::Alias;
					c.type = typeCode(m_aliases[d->name].type);
					c.hover = hoverAlias(d->name);
				}
				else
					continue;
				out.locals.push_back(c);
				continue;
			}
			switch (d->kind)
			{
				case NodeKind::NamespaceDecl:
					out.namespaces.push_back({ d->namePos, d->endPos, d->name, {} });
					if (!namespaces.insert(d->name).second)
						continue;
					c.kind = CompletionSymbol::Namespace;
					c.hover = "namespace " + d->name;
				break;
				case NodeKind::StructDecl:
					if (d->value != 0 || m_structs.count(d->name) == 0)	// nameless
						continue;
					c.kind = CompletionSymbol::Struct;
					c.type = "s:" + d->name;
					c.hover = hoverStruct(m_structs[d->name]);
				break;
				case NodeKind::EnumDecl:
					if (m_enums.count(d->name) == 0)
						continue;
					c.kind = CompletionSymbol::Enum;
					c.hover = hoverEnum(d->name);
					for (const Node* m : d->list)
					{
						auto it = m_enums[d->name].values.find(m->name);
						out.members.push_back({ m->name, d->name, CompletionSymbol::EnumMember, "i",
							d->name + "." + m->name + " = " + (it != m_enums[d->name].values.end() ? number(it->second) : "?"), m->namePos, {}, false });
					}
				break;
				case NodeKind::AliasDecl:
					if (m_aliases.count(d->name) == 0)
						continue;
					c.kind = CompletionSymbol::Alias;
					c.type = typeCode(m_aliases[d->name].type);
					c.hover = hoverAlias(d->name);
				break;
				case NodeKind::ConstDecl:
				case NodeKind::VarDecl:
					c.kind = d->kind == NodeKind::ConstDecl ? CompletionSymbol::Const : CompletionSymbol::Var;
					if (m_globals.count(d->name) > 0)
					{
						c.type = typeCode(m_globals[d->name]->type);
						c.hover = hoverVar(m_globals[d->name]);
					}
					else if (m_consts.count(d->name) > 0)
					{
						c.type = m_consts[d->name].isString ? "p:i" : typeCode(m_consts[d->name].type);
						c.hover = hoverConst(d->name);
					}
					else
						continue;
				break;
				case NodeKind::FuncDecl:
				{
					if (d->name.rfind("__lambda_", 0) == 0)
						continue;
					FuncInfo* f = l_func(d);
					if (f == nullptr)
						continue;
					const bool method = (d->value & 4) != 0;
					c.kind = !method ? CompletionSymbol::Sub : (d->value & 8) != 0 ? CompletionSymbol::StaticSub : CompletionSymbol::Method;
					c.type = f->type != nullptr ? typeCode(f->type) : "f:v";
					c.hover = hoverFunc(f);
					c.isPrivate = (d->value & (16 | 8192)) != 0;	// (prot: also only in its subs, and its derived classes': below)
					if (method)
						out.members.push_back(c);
				}
				break;
				case NodeKind::TemplateDecl:
				{
					// A template's structs / classes and subs (method templates: members of their
					// struct), with the template's head in their hover.
					const TemplateDef* def = d->tmpl;
					if (def == nullptr)
						continue;
					std::string head = "template(";
					for (size_t i = 0; i < def->params.size(); i++)
						head += (i > 0 ? ", " : "") + std::string(i >= def->required ? "optional " : "") + def->params[i];
					head += ")";
					if (!def->whereParts.empty())
					{
						head += " where ";
						for (size_t i = 0; i < def->whereParts.size(); i++)
							head += (i > 0 ? " and " : "") + def->whereParts[i];
					}
					for (size_t at = head.find(" :: "); at != std::string::npos; at = head.find(" :: "))
						head.replace(at, 4, "::");
					for (const TemplateEntity& ent : def->entities)
					{
						if (ent.nameToken >= def->tokens.size())
							continue;
						CompletionSymbol t;
						t.name = ent.name;
						t.def = def->tokens[ent.nameToken].pos;
						bool isStatic = false, priv = false;
						for (size_t k = ent.first; k < ent.nameToken; k++)
						{
							isStatic = isStatic || def->tokens[k].text == "static";
							priv = priv || def->tokens[k].text == "priv" || def->tokens[k].text == "prot";
						}
						t.isPrivate = priv;
						// (its declaration as written: up to its body)
						std::string decl;
						for (size_t k = ent.first; k < ent.last && k < def->tokens.size(); k++)
						{
							const Token& tk = def->tokens[k];
							if (tk.kind == TokenKind::Punct && (tk.text == "{" || tk.text == ";"))
								break;
							const bool tight = tk.kind == TokenKind::Punct && (tk.text == "(" || tk.text == ")" || tk.text == "," || tk.text == ":" || tk.text == "*" || tk.text == "<" || tk.text == ">" || tk.text == "::");
							if (!decl.empty() && !tight && decl.back() != '(' && decl.back() != '<' && decl.back() != ' ' && decl.compare(decl.size() - std::min<size_t>(2, decl.size()), 2, "::") != 0)
								decl += ' ';
							decl += tk.kind == TokenKind::String ? "\"" + tk.text + "\"" : tk.text;
							if (tk.kind == TokenKind::Punct && (tk.text == "," || tk.text == ":"))
								decl += ' ';
						}
						t.hover = head + "\n" + decl;
						if (ent.isStruct)
						{
							t.kind = CompletionSymbol::Struct;
							t.parent = def->scope;
							t.type = "s:" + (def->scope.empty() ? ent.name : def->scope + "::" + ent.name);
							out.globals.push_back(t);
						}
						else if (def->owner != nullptr)
						{
							t.kind = isStatic ? CompletionSymbol::StaticSub : CompletionSymbol::Method;
							t.parent = def->owner->name;
							t.type = "f:v";
							out.members.push_back(t);
						}
						else
						{
							t.kind = CompletionSymbol::Sub;
							t.parent = def->scope;
							t.type = "f:v";
							out.globals.push_back(t);
						}
					}
				}
				continue;
				default: continue;
			}
			out.globals.push_back(c);
		}
		// The methods of template instances (Box<u32>'s): members of the instance.
		std::set<std::string> instanceMembers;
		for (FuncInfo* f : m_funcList)
		{
			const Node* d = f->decl;
			if (!f->instance || (d->value & 4) == 0 || d->owner == nullptr || m_instanceOrigin.count(d->owner->name) == 0 || f->type == nullptr)
				continue;
			// (a method template's instance: the template's name; once)
			const std::string name = d->shortName.find('<') != std::string::npos ? d->shortName.substr(0, d->shortName.find('<')) : d->shortName;
			if (!instanceMembers.insert(d->owner->name + "::" + name + (name == d->shortName ? "/" + f->type->name() : "")).second)
				continue;
			CompletionSymbol c;
			c.name = name;
			c.parent = d->owner->name;
			c.kind = (d->value & 8) != 0 ? CompletionSymbol::StaticSub : CompletionSymbol::Method;
			c.type = typeCode(f->type);
			c.hover = hoverFunc(f);
			c.def = d->namePos;
			c.isPrivate = (d->value & (16 | 8192)) != 0;
			out.members.push_back(c);
		}
		for (auto& s : m_structs)
		{
			for (const StructField& f : s.second->fields)
			{
				if (f.owner != nullptr && f.isPrivate)
					continue;		// (inherited, but only its class's subs use it)
				out.members.push_back({ f.name, s.first, CompletionSymbol::Field, typeCode(f.type), hoverField(s.second, f), f.namePos, {}, f.isPrivate || f.isProtected });
			}
			if (s.second->kind == 0)
				continue;
			// A class's inherited subs (those it does not redefine), as its members.
			std::vector<const StructInfo*> ancestors;
			for (const StructInfo* b = s.second->base; b != nullptr; b = b->base)
				ancestors.push_back(b);
			for (const auto& slot : s.second->ifaceSlots)
			{
				if (slot.iface != s.second)
					ancestors.push_back(slot.iface);
			}
			std::set<std::string> seen;
			for (FuncInfo* f : m_funcList)
				if (f->decl->owner == s.second->decl && (f->decl->value & 4) != 0)
					seen.insert(f->decl->shortName);
			for (const StructInfo* a : ancestors)
			{
				for (FuncInfo* f : m_funcList)
				{
					const Node* d = f->decl;
					if (d->owner != a->decl || (d->value & 4) == 0 || (d->value & 16) != 0 || d->shortName == "new" || f->type == nullptr)
						continue;
					if (!seen.insert(d->shortName + "/" + f->type->name()).second || seen.count(d->shortName) > 0)
						continue;
					CompletionSymbol c;
					c.name = d->shortName;
					c.parent = s.first;
					c.kind = (d->value & 8) != 0 ? CompletionSymbol::StaticSub : CompletionSymbol::Method;
					c.type = typeCode(f->type);
					c.hover = hoverFunc(f);
					c.def = d->namePos;
					c.isPrivate = (d->value & 8192) != 0;
					out.members.push_back(c);
				}
			}
		}
		for (FuncInfo* f : m_funcList)
		{
			if (f->decl->a == nullptr)
				continue;
			CompletionContext ctx { f->decl->pos, f->decl->a->endPos, f->decl->scope, {} };
			// A lambda: the private members of the structs of the sub it is in (d), as the
			// compiler allows.
			const Node* around = f->decl;
			for (int depth = 0; depth < 64 && around->name.rfind("__lambda_", 0) == 0 && around->d != nullptr; depth++)
				around = around->d;
			for (const Node* o = around->owner; o != nullptr; o = o->owner)
			{
				if (o->kind == NodeKind::StructDecl)
					ctx.structs.push_back(o->name);
			}
			out.subs.push_back(ctx);
		}
	}

	// ================================== Library interfaces ==================================
	std::vector<std::string> CodeGen::subTable(void) const
	{
		std::vector<std::string> out;
		for (FuncInfo* f : m_funcList)
		{
			// (A library's subs are in its own disassembly; lambdas have no name to write.)
			if (f->external || !f->defined || f->decl == nullptr || f->decl->a == nullptr || f->decl->name.rfind("__lambda_", 0) == 0)
				continue;
			std::string params;
			for (Type* p : f->type->params)
				params += (params.empty() ? "" : ", ") + p->name();
			out.push_back(f->name + "\t" + f->label + "\t" + params + "\t" + (f->isInline ? "1" : "0") + "\t" + f->decl->pos.file + "\t"
				+ std::to_string(f->decl->pos.line) + "\t" + std::to_string(std::max(f->decl->pos.line, f->decl->a->endPos.line)));
		}
		return out;
	}

	std::vector<std::string> CodeGen::exports(void) const
	{
		std::vector<std::string> out;
		for (FuncInfo* f : m_funcList)
		{
			if (!f->external && !f->instance && f->decl->name.rfind("__lambda_", 0) != 0 && !f->label.empty() && f->decl->constKind != 3 && (f->decl->value & 1024) == 0)
				out.push_back(f->label);	// (inline subs without code: not in the image; template instances: each user has its own)
		}
		for (const auto& g : m_globals)
		{
			if (!g.second->external && !g.second->isStatic)
				out.push_back(g.second->symbol);
		}
		return out;
	}

	// A type as written in an interface (structs by their full name, from the global scope).
	std::string CodeGen::interfaceType(Type* t)
	{
		switch (t->kind)
		{
			case Type::Kind::Pointer:
			case Type::Kind::Array:
			{
				if (t->isReference())
					return std::string(t->constTarget ? "const ref " : "ref ") + interfaceType(t->base);
				std::string base = interfaceType(t->base);
				if (t->base->kind == Type::Kind::Function || (t->constTarget && t->base->kind == Type::Kind::Pointer))
					base = "(" + base + ")";
				return std::string(t->constTarget ? "const " : "") + base + (t->kind == Type::Kind::Pointer ? "*" : "[" + std::to_string(t->count) + "]");
			}
			case Type::Kind::Struct:
			{
				StructInfo* info = t->structInfo;
				if (!info->anonymous)
					return "::" + info->name;
				std::string s = "struct { ";
				for (const StructField& f : info->fields)
					s += std::string(f.isPrivate ? "priv " : "") + "var " + f.name + ": " + interfaceType(f.type) + "; ";
				return s + "}";
			}
			case Type::Kind::Function:
			{
				std::string s = "sub(";
				for (size_t i = 0; i < t->params.size(); i++)
					s += (i > 0 ? ", " : "") + (ownedParam(t, i) ? "own (" + interfaceType(t->params[i]) + ")" : interfaceType(t->params[i]));	// ((): own ::x is own::x)
				if (t->variadic != 0)
					s += std::string(t->params.empty() ? "" : ", ") + (t->variadic == 2 ? "...typed" : "...");
				return s + ") -> " + interfaceType(t->base) + (t->throws != nullptr ? " throws ::" + t->throwsName : "");
			}
			case Type::Kind::Null: return "void*";
			default: return t->name();
		}
	}

	std::string CodeGen::interfaceNumber(int64_t value, Type* t)
	{
		if (t != nullptr && t->kind == Type::Kind::Bool)
			return value ? "true" : "false";
		if (t != nullptr && (t->isPointer() || t->isFunction()))
			return value == 0 ? "null" : "cast<" + interfaceType(t) + ">(" + std::to_string((uint32_t)value) + ")";
		if (t != nullptr && t->isFloat())
			return floatText(value);
		return std::to_string(value);
	}

	// A default value (a constant expression).
	std::string CodeGen::interfaceValue(Node* init, Type* t)
	{
		if (init->kind == NodeKind::StrLit)
			return dscript::quoted(init->text);
		if (init->kind == NodeKind::Ident && init->func != nullptr)
			return init->func->decl->name.rfind("__lambda_", 0) == 0 ? "null" : "::" + init->func->decl->name;
		if (init->isConst)
			return interfaceNumber(t != nullptr ? constConvert(init->constValue, decay(init->type), t) : init->constValue, t);
		if (init->constPtr)
			return interfaceNumber(init->constValue, t);
		return "0";
	}

	std::string CodeGen::interface(std::vector<dragon::dlib::LineInfo>& lines)
	{
		std::vector<std::string> out;
		// A line; pos: where its name (at nameColumn, 0-based) is declared.
		auto l_line = [&](const std::string& text, const SourcePos* pos = nullptr, size_t nameColumn = 0) {
			const size_t first = out.size();
			size_t start = 0;
			while (true)
			{
				const size_t nl = text.find('\n', start);
				out.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
				if (nl == std::string::npos)
					break;
				start = nl + 1;
			}
			if (pos == nullptr || pos->file.empty() || pos->line <= 0)
				return;
			for (size_t i = first; i < out.size(); i++)
			{
				const bool head = i == first;
				lines.push_back({ (uint32_t)(i + 1), pos->file, (uint32_t)(pos->line + (int)(i - first)), head ? pos->column - (int)(nameColumn + 1) : 0 });
			}
		};
		auto l_func = [&](const Node* d) -> FuncInfo* {
			for (FuncInfo* f : m_funcList)
			{
				if (f->decl == d)
					return f;
			}
			return nullptr;
		};
		auto l_token = [&](const Token& t) -> std::string {
			switch (t.kind)
			{
				case TokenKind::String: return dscript::quoted(t.text);
				case TokenKind::LowBlock: return "{" + t.text + "}";		// (after the 'low' keyword's token)
				case TokenKind::Integer:
					if (t.isChar)
					{
						const char c = (char)t.value;
						if (c == '\'' || c == '\\')
							return std::string("'\\") + c + "'";
						if (c < 32 || c >= 127)
						{
							char buf[8];
							std::snprintf(buf, sizeof(buf), "'\\x%02X'", (uint8_t)c);
							return buf;
						}
						return std::string("'") + c + "'";
					}
					return std::to_string(t.value) + (t.isUnsigned ? "u" : "");	// (5u keeps its type in the interface)
				default: return t.text;
			}
		};
		auto l_sub = [&](FuncInfo* f, const std::string& indent) {
			Node* d = f->decl;
			if (f->type == nullptr)
				return;
			const bool method = (d->value & 4) != 0, isStatic = (d->value & 8) != 0;
			const bool inInterface = method && d->owner != nullptr && d->owner->classKind == 2;
			const std::string head = indent + ((d->value & 16) != 0 ? "priv " : "") + ((d->value & 8192) != 0 ? "prot " : "") + (isStatic ? "static " : "")
				+ ((d->value & 512) != 0 ? "virt " : "") + ((d->value & 1024) != 0 && !inInterface ? "pure " : "") + ((d->value & 2048) != 0 ? "override " : "")
				+ ((d->value & 4096) != 0 ? "final " : "") + ((d->value & 16384) != 0 ? "implicit " : "") + ((d->value & 64) != 0 ? "readonly " : "")
				+ (f->interrupt ? "interrupt " : "") + (d->constKind == 2 ? "constexpr " : d->constKind == 3 ? "consteval " : "") + ((d->value & 2) != 0 ? "inline " : "") + "sub ";
			std::string text = head + (d->shortName.rfind("operator cast ", 0) == 0 ? std::string("operator cast") : declaredName(d)) + "(";
			const size_t first = (method && !isStatic) ? 1 : 0;
			for (size_t i = first; i < f->type->params.size() && i < f->paramNames.size(); i++)
			{
				const bool constParam = i < d->list.size() && d->list[i]->constKind == 1;
				text += (i > first ? ", " : "") + std::string(constParam ? "const " : "var ") + f->paramNames[i] + ": " + (ownedParam(f->type, i) ? "own (" + interfaceType(f->type->params[i]) + ")" : interfaceType(f->type->params[i]));
				if (i < f->defaults.size() && f->defaults[i] != nullptr)
					text += " = " + interfaceValue(f->defaults[i], f->type->params[i]);
			}
			if (f->type->variadic != 0)
				text += std::string(f->type->params.size() > first ? ", " : "") + (f->type->variadic == 2 ? "...typed" : "...");
			text += ")";
			if (f->type->base->kind != Type::Kind::Void)
				text += " -> " + interfaceType(f->type->base);
			if (f->type->throws != nullptr)
				text += " throws ::" + f->type->throwsName;
			if (((d->value & 2) == 0 && d->constKind == 0) || d->bodyTokens.empty())
			{
				l_line(text + ";", &d->namePos, head.size());
				return;
			}
			// An inline (constexpr, consteval) sub: its body, each token where it is in the source
			// (the lines and the columns, so that errors and the index point there).
			std::string line = text;
			SourcePos linePos = d->namePos;
			size_t lineName = head.size();
			int lineNr = d->namePos.line;
			bool headLine = true;
			for (const Token& t : d->bodyTokens)
			{
				if (t.pos.line != lineNr && t.pos.file == linePos.file)
				{
					l_line(line, &linePos, lineName);
					line.clear();
					linePos = { t.pos.file, t.pos.line, 1 };
					lineName = 0;
					lineNr = t.pos.line;
					headLine = false;
				}
				if (!headLine && line.size() < (size_t)std::max(0, t.pos.column - 1))
					line.append((size_t)(t.pos.column - 1) - line.size(), ' ');
				else if (!line.empty())
					line += ' ';
				const std::string text = l_token(t);
				line += text;
				lineNr += (int)std::count(text.begin(), text.end(), '\n');
			}
			l_line(line, &linePos, lineName);
		};
		auto l_enum = [&](Node* d, const std::string& indent) {
			EnumInfo& info = m_enums[d->name];
			const std::string head = indent + (d->isPrivate ? "priv " : "") + "enum ";
			l_line(head + declaredName(d) + (d->typeSyntax != nullptr && info.type != nullptr ? ": " + info.type->name() : "") + " {", &d->namePos, head.size());
			for (size_t i = 0; i < d->list.size(); i++)
			{
				const Node* m = d->list[i];
				auto it = info.values.find(m->name);
				const std::string text = indent + "\t" + m->name + " = " + std::to_string(it != info.values.end() ? it->second : 0) + (i + 1 < d->list.size() ? "," : "");
				l_line(text, &m->namePos, indent.size() + 1);
			}
			l_line(indent + "}");
		};
		// A template, as written (its tokens, one line per line of its source: errors in an
		// instance point at the library's sources).
		auto l_template = [&](Node* d, const std::string& indent) {
			std::string text;
			SourcePos linePos;
			int line = -1;
			for (const Token& t : d->tmpl->source)
			{
				if (t.pos.line != line && !text.empty())
				{
					l_line(indent + text, &linePos, indent.size());
					text.clear();
				}
				if (text.empty())
				{
					line = t.pos.line;
					linePos = t.pos;
				}
				text += (text.empty() ? "" : " ") + l_token(t);
			}
			if (!text.empty())
				l_line(indent + text, &linePos, indent.size());
		};
		// A struct constant's value: its fields (up to the first private one: { ... } cannot set
		// those outside the struct, they keep their defaults).
		std::function<std::string(StructInfo*, const std::string&, const ConstInfo&)> l_constFields = [&](StructInfo* info, const std::string& prefix, const ConstInfo& c) {
			std::string s = "{ ";
			for (size_t i = 0; i < info->fields.size(); i++)
			{
				const StructField& f = info->fields[i];
				if (f.isPrivate)
					break;
				s += (i > 0 ? ", " : "");
				if (f.type->kind == Type::Kind::Struct)
					s += l_constFields(f.type->structInfo, prefix + f.name + ".", c);
				else if (f.type->kind == Type::Kind::Array)
					s += "{ }";
				else
				{
					auto it = c.fields.find(prefix + f.name);
					s += interfaceNumber(it != c.fields.end() ? it->second : 0, f.type);
				}
			}
			return s + " }";
		};
		std::function<void(Node*, const std::string&)> l_struct = [&](Node* d, const std::string& indent) {
			StructInfo* info = m_structs[d->name];
			const std::string head = indent + (d->isPrivate ? "priv " : "") + (info->isAbstract ? "abstract " : "") + (info->isFinal ? "final " : "") +
				(info->isClass() ? "class " : info->isInterface() ? "interface " : "struct ");
			// (the class it extends, its interfaces, its friends: by their full names)
			std::string header;
			if (info->base != nullptr)
				header += " ex " + interfaceType(m_types.structType(info->base));
			else if (info->isInterface() && !info->interfaces.empty())
				header += " ex " + interfaceType(m_types.structType(info->interfaces[0]));
			if (info->isClass() && !info->interfaces.empty())
			{
				header += " impl ";
				for (size_t i = 0; i < info->interfaces.size(); i++)
					header += (i > 0 ? ", " : "") + interfaceType(m_types.structType(info->interfaces[i]));
			}
			std::vector<std::string> friends = info->friendClasses;
			friends.insert(friends.end(), info->friendSubs.begin(), info->friendSubs.end());
			for (size_t i = 0; i < friends.size(); i++)
				header += (i == 0 ? " friend ::" : ", ::") + friends[i];
			l_line(head + declaredName(d) + header + " {", &d->namePos, head.size());
			for (const StructField& f : info->fields)
			{
				if (f.owner != nullptr)
					continue;		// (the class it extends has it)
				const std::string h = indent + "\t" + (f.isPrivate ? "priv " : "") + (f.isProtected ? "prot " : "") + (f.isReadonly ? "readonly " : "") + (f.isConst ? "const " : "var ");
				l_line(h + f.name + ": " + interfaceType(f.type) + (f.init != nullptr ? " = " + interfaceValue(f.init, f.type) : "") + ";", &f.namePos, h.size());
			}
			for (Node* n : m_program->list)
			{
				if (n->owner != d || n->external != 0 || !n->text.empty() || n->name.rfind("__lambda_", 0) == 0)
					continue;
				if (n->kind == NodeKind::StructDecl && m_structs.count(n->name) > 0)
					l_struct(n, indent + "\t");
				else if (n->kind == NodeKind::EnumDecl && m_enums.count(n->name) > 0)
					l_enum(n, indent + "\t");
				else if (n->kind == NodeKind::FuncDecl)
				{
					if (FuncInfo* f = l_func(n))
						l_sub(f, indent + "\t");
				}
				else if (n->kind == NodeKind::TemplateDecl)
					l_template(n, indent + "\t");		// (a method template)
				else if (n->kind == NodeKind::AliasDecl && m_aliases.count(n->name) > 0)
				{
					const std::string h = indent + "\t" + (n->isPrivate ? "priv " : "") + "alias ";
					l_line(h + n->shortName + " = " + (m_aliases[n->name].type ? interfaceType(m_aliases[n->name].type) : "i32") + ";", &n->namePos, h.size());
				}
				else if (n->kind == NodeKind::VarDecl || (n->kind == NodeKind::ConstDecl && m_globals.count(n->name) > 0))
				{
					// A static field (its data is in the library); a static const computed when the
					// library starts: static readonly var (a constant of the importers).
					VarInfo* v = m_globals.count(n->name) > 0 ? m_globals[n->name] : nullptr;
					const bool computedConst = v != nullptr && v->isConst;
					const std::string h = indent + "\t" + (n->isPrivate ? "priv " : "") + (n->isProtected ? "prot " : "") + (computedConst ? "static readonly var " : std::string(n->isReadonly ? "readonly " : "") + "static var ");
					l_line(h + n->shortName + ": " + interfaceType(v != nullptr && v->type != nullptr ? v->type : m_types.i32()) + ";", &n->namePos, h.size());
				}
				else if (n->kind == NodeKind::ConstDecl && m_consts.count(n->name) > 0)
				{
					// A constant of the struct (static const with a compile-time value: the same).
					const ConstInfo& c = m_consts[n->name];
					const std::string h = indent + "\t" + (n->isPrivate ? "priv " : "") + (n->isProtected ? "prot " : "") + "constexpr ";
					std::string text = h + n->shortName;
					if (c.isString)
						text += " = " + dscript::quoted(c.text) + ";";
					else if (c.type != nullptr && c.type->kind == Type::Kind::Struct)
						text += ": " + interfaceType(c.type) + " = " + l_constFields(c.type->structInfo, "", c) + ";";
					else
						text += ": " + (c.type ? interfaceType(c.type) : std::string("i32")) + " = " + interfaceNumber(c.value, c.type) + ";";
					l_line(text, &n->namePos, h.size());
				}
			}
			l_line(indent + "}");
		};

		for (Node* d : m_program->list)
		{
			if (d->external != 0 || !d->text.empty() || d->owner != nullptr || d->kind == NodeKind::NamespaceDecl || d->kind == NodeKind::LowBlock)
				continue;
			if (d->kind == NodeKind::FuncDecl && (d->name.rfind("__lambda_", 0) == 0 || l_func(d) == nullptr))
				continue;
			if ((d->kind == NodeKind::StructDecl && (d->value != 0 || m_structs.count(d->name) == 0)) || (d->kind == NodeKind::EnumDecl && m_enums.count(d->name) == 0))
				continue;
			const std::string indent = d->scope.empty() ? "" : "\t";
			if (!d->scope.empty())
				l_line("namespace " + d->scope + " {");
			const std::string name = declaredName(d);
			switch (d->kind)
			{
				case NodeKind::StructDecl: l_struct(d, indent); break;
				case NodeKind::EnumDecl: l_enum(d, indent); break;
				case NodeKind::TemplateDecl: l_template(d, indent); break;
				case NodeKind::FuncDecl: l_sub(l_func(d), indent); break;
				case NodeKind::AliasDecl:
				{
					const std::string head = indent + "alias ";
					l_line(head + name + " = " + (m_aliases[d->name].type ? interfaceType(m_aliases[d->name].type) : "i32") + ";", &d->namePos, head.size());
				}
				break;
				case NodeKind::VarDecl:
				{
					VarInfo* v = m_globals[d->name];
					const std::string head = indent + (v->isConst ? "readonly var " : "var ");		// (readonly var: a constant variable of the library)
					l_line(head + name + ": " + interfaceType(v->type ? v->type : m_types.i32()) + ";", &d->namePos, head.size());
				}
				break;
				case NodeKind::ConstDecl:
				{
					const bool variable = m_globals.count(d->name) > 0 && m_globals[d->name]->isConst;
					const std::string head = indent + (variable ? "readonly var " : "const ");
					std::string text = head + name;
					if (m_globals.count(d->name) > 0)
						text += ": " + interfaceType(m_globals[d->name]->type ? m_globals[d->name]->type : m_types.i32()) + ";";	// a constant array (its data is in the library), a constant variable
					else
					{
						const ConstInfo& c = m_consts[d->name];
						if (c.isString)
							text += " = " + dscript::quoted(c.text) + ";";
						else if (c.type != nullptr && c.type->kind == Type::Kind::Struct)
							text += ": " + interfaceType(c.type) + " = " + l_constFields(c.type->structInfo, "", c) + ";";
						else
							text += ": " + (c.type ? interfaceType(c.type) : std::string("i32")) + " = " + interfaceNumber(c.value, c.type) + ";";
					}
					l_line(text, &d->namePos, head.size());
				}
				break;
				default: break;
			}
			if (!d->scope.empty())
				l_line("}");
		}
		std::string text;
		for (const auto& l : out)
			text += l + "\n";
		return text;
	}

	std::vector<std::string> CodeGen::generate(Node* program)
	{
		collect(program);
		resolveNames(program);
		applyGuards(program);
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
			if (c.second.decl->constKind == 1)
			{
				// const: if its value is not a compile-time constant (or not yet: it may use subs,
				// globals), it is decided after the declarations (decideConst).
				try { resolveConst(c.first, c.second.decl->pos); }
				catch (CompileError&) {}
				continue;
			}
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
				ts.written = declaredName(a.second.decl);
				ts.resolved = true;
				ts.pos = a.second.decl->pos;
				resolveType(&ts);
			}))
			{
				a.second.resolving = false;
				if (a.second.type == nullptr)
					a.second.type = m_types.i32();
			}
		}
		for (size_t fi = 0; fi < m_funcList.size(); fi++)		// (instantiating a template adds subs)
		{
			FuncInfo* f = m_funcList[fi];
			if (f->instance || f->type != nullptr)
				continue;		// (resolved when instantiated, or already: a constexpr sub a constant used)
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
		for (Node* sa : m_staticAsserts)
			attempt([&] { checkStaticAssert(sa, nullptr); });
		// Overloads: merge declarations with their definition; labels.
		for (auto& entry : m_funcs)
		{
			auto& funcs = entry.second;
			attempt([&] {
				for (size_t i = 0; i < funcs.size(); i++)
				{
					for (size_t j = i + 1; j < funcs.size(); )
					{
						if (funcs[i]->type->params != funcs[j]->type->params || funcs[i]->type->variadic != funcs[j]->type->variadic)
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
				f->label = codeLabel(subLabelName(f, entry.first), (f->decl->value & 4) != 0);
				// A library's sub has the label it has there (the overloads it has there).
				size_t overloads = funcs.size();
				if (f->decl->external != 0)
					overloads = (size_t)std::count_if(funcs.begin(), funcs.end(), [&](FuncInfo* g) { return g->decl->external == f->decl->external; });
				if (overloads > 1)
				{
					f->label += "__";
					for (Type* p : f->type->params)
						f->label += "_" + mangle(p);
					if (f->type->params.empty() && f->type->variadic == 0)
						f->label += "_void";
					if (f->type->variadic != 0)
						f->label += f->type->variadic == 2 ? "_vatyped" : "_va";
				}
			}
		}
		resolveSubDefaults();
		// The classes' tables (what they override, what an abstract class lacks).
		for (auto& s : m_structs)
		{
			if (s.second->kind != 0 && m_instanceOrigin.count(s.first) == 0)
				attempt([&] { buildVtables(s.second); });
		}
		// Analysis: a file without main is fine (it is included by others).
		FuncInfo* mainFunc = nullptr;
		bool mainArgs = false;	// main(var argc: u32, var argv: string*)
		m_program = program;
		if (m_options.library)
		{
			if (m_funcs.count("main") > 0)
				error(m_funcs["main"][0]->decl->pos, "a library has no main (programs import it: #import(...))");
		}
		else if (m_funcs.count("main") == 0)
		{
			if (!m_options.analysis)
				error({ program->pos.file, 1, 1 }, "no 'main' sub");
		}
		else
		{
			mainFunc = m_funcs["main"][0];
			attempt([&] {
				// main() or main(var argc: u32, var argv: string*) (argc can be i32, argv i8**).
				const auto& params = mainFunc->type->params;
				auto l_isArgv = [](Type* t) {
					return t->kind == Type::Kind::Pointer && t->base->kind == Type::Kind::Pointer && t->base->base->isInteger()
						&& t->base->base->size() == 1 && t->base->base->kind != Type::Kind::Bool;
				};
				const bool withArgs = params.size() == 2 && (params[0]->kind == Type::Kind::U32 || params[0]->kind == Type::Kind::I32) && l_isArgv(params[1]);
				if (m_funcs["main"].size() > 1 || !(params.empty() || withArgs))
					error(mainFunc->decl->pos, "main takes no parameters, or the program's arguments: sub main(var argc: u32, var argv: string*) "
						"(and returns i32 or void)");
				if (withArgs && mainFunc->requiredParams != 2)
					error(mainFunc->decl->pos, "main's parameters cannot have default values (without arguments, argc is 0 and argv null)");
				mainArgs = withArgs;
			});
		}

		genGlobalTypes(0);
		genGlobalValues(0);
		for (Node* low : m_topLows)
		{
			std::vector<std::string> lowCode;
			m_out = &lowCode;
			m_scopes.clear();
			attempt([&] { genLow(low, true); });
			m_code.push_back("");
			m_code.insert(m_code.end(), lowCode.begin(), lowCode.end());
		}
		// A sub's code; an instance's errors say which instance it is.
		auto l_gen = [&](FuncInfo* f) {
			attempt([&] {
				f->generated = true;
				try
				{
					genFunction(f);
				}
				catch (CompileError& err)
				{
					if (!f->instanceNote.empty() && err.message.find(f->instanceNote) == std::string::npos)
						err.message += f->instanceNote;
					throw;
				}
			});
		};
		for (size_t i = 0; i < m_funcList.size(); i++)
		{
			FuncInfo* f = m_funcList[i];
			if (f->instance)
				continue;		// (below: only if something uses it)
			if (!attempt([&] {
				if (!f->defined)
					error(f->decl->pos, "sub '" + f->name + "' is declared but not defined");
			}))
				continue;
			if (!f->isInline && !f->external && f->decl->constKind != 3 && (f->decl->value & 1024) == 0)
				l_gen(f);
		}
		if (m_options.noInline)
			checkInlineRecursion();
		// Until nothing new: the template instances something uses (generating one can use more),
		// the static locals they bring (their values), and the inline subs used as sub pointers
		// (real code; the other inline subs are only checked, below).
		std::set<FuncInfo*> kept;
		for (bool more = true; more; )
		{
			more = false;
			for (size_t i = 0; i < m_funcList.size(); i++)
			{
				FuncInfo* f = m_funcList[i];
				if (f->instance && f->used && !f->generated && !f->isInline && f->defined && f->decl->constKind != 3 && (f->decl->value & 1024) == 0)
				{
					l_gen(f);
					more = true;
				}
				else if (f->isInline && f->addressTaken && f->defined && !f->external && kept.insert(f).second)
				{
					l_gen(f);
					more = true;
				}
			}
			// The classes' tables the code uses (they use subs: maybe instances to generate).
			while (m_vtablesEmitted < m_vtableQueue.size())
			{
				StructInfo* info = m_vtableQueue[m_vtablesEmitted++];
				attempt([&] { emitVtables(info); });
				more = true;
			}
			if (m_globalsGenerated < m_globalDecls.size())
			{
				genGlobalTypes(m_globalsTyped);
				genGlobalValues(m_globalsGenerated);
				more = true;
			}
		}
		for (FuncInfo* f : m_funcList)
		{
			// The inline subs nothing needs as code, and the consteval subs (never code): checked.
			const bool onlyChecked = f->decl->constKind == 3 && !f->external && f->decl->a != nullptr;
			if ((!f->isInline && !onlyChecked) || kept.count(f) || !f->defined || (f->instance && !(onlyChecked && f->used)))
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
		{
			analyzeTemplates();		// (the templates nothing instantiates: with stand-in arguments)
			indexDeclarations();
			indexCompletion(program);
		}
		if (m_options.analysis)
			return {};

		// Assemble the dasm source.
		std::vector<std::string> out;
		out.push_back("## Generated by dsc (DScript compiler) -- do not edit");
		// The source files of the .loc markers (absolute paths: the debugger shows them).
		for (size_t i = 0; i < m_sourcePaths.size(); i++)
		{
			std::error_code ec;
			std::filesystem::path path = std::filesystem::weakly_canonical(std::filesystem::absolute(m_sourcePaths[i], ec), ec);
			out.push_back(".srcfile " + std::to_string(i) + " " + (ec ? m_sourcePaths[i] : path.string()));
		}
		out.push_back(m_options.library ? ".entry __ds_lib_init" : ".entry __ds_start");
		for (const auto& d : m_options.directives)
			out.push_back(d);
		// The read-only data made when compiling that the code uses.
		for (VarInfo* v : m_lazyData)
		{
			if (!v->addressUsed)
				continue;
			const std::vector<uint8_t>& bytes = m_ctBlobs[v];
			std::string line = "\t$" + v->symbol + " ";
			for (size_t i = 0; i < bytes.size(); i++)
			{
				char buf[8];
				std::snprintf(buf, sizeof(buf), "0x%02X", bytes[i]);
				line += (i > 0 ? ", " : "") + std::string(buf);
			}
			if (bytes.empty())
				line += "0x00";
			m_data.push_back(line);
		}
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
		if (m_options.shared)
		{
			// The writable globals, together: every process gets its own copy of this part (the
			// image keeps it as their initial values). The markers are one byte each (the
			// assembler has no empty data entries that code can name).
			out.push_back("\t$__ds_rw_start 0x00");
			for (const auto& d : m_dataRW)
				out.push_back(d);
			out.push_back("\t$__ds_rw_end 0x00");
		}
		out.push_back(".code");
		if (m_options.library)
		{
			// Called by the programs that import the library (before their main).
			m_pieceStarts.insert(m_pieceStarts.begin(), "__ds_lib_init");
			out.push_back("__ds_lib_init:");
			out.push_back(".loc -");
			if (!m_initCode.empty())
			{
				out.push_back("\tpush 0");
				out.push_back("\tcall $__ds_init");
			}
			out.push_back("\tret");
		}
		else
		{
			out.push_back("__ds_start:");
			out.push_back(".loc -");
			for (int i = 0; i < m_options.libraries; i++)
			{
				out.push_back("\tpush 0");
				out.push_back("\tcall $__dlib_init_" + std::to_string(i));
			}
			if (!m_initCode.empty())
			{
				out.push_back("\tpush 0");
				out.push_back("\tcall $__ds_init");
			}
			if (mainArgs)
			{
				// main(argc, argv): what the program was called with (push argc; push argv; push 2;
				// call entry), or 0, null if it was called without arguments (PP is 0) or is a raw
				// binary (started by a jump: nothing to read).
				out.push_back("\tmov R1, 0");
				out.push_back("\tmov R2, 0");
				if (!m_options.raw)
				{
					out.push_back("\tmov R3, PP");
					out.push_back("\tmov ACC, R3");
					out.push_back("\tjeq $__ds_no_args, 0");
					out.push_back("\tmovo R1, *PP, dword 0x00000000");
					out.push_back("\tmovo R2, *PP, dword 0xFFFFFFFC");
					out.push_back("__ds_no_args:");
				}
				out.push_back("\tpush R1");
				out.push_back("\tpush R2");
				out.push_back("\tpush 2");
			}
			else
				out.push_back("\tpush 0");
			out.push_back("\tcall $" + mainFunc->label);
			if (!m_finiChunks.empty())
			{
				// The globals' destructors (main's value kept: calls keep the registers).
				out.push_back("\tmov R1, RV");
				out.push_back("\tpush 0");
				out.push_back("\tcall $__ds_fini");
				out.push_back("\tmov RV, R1");
			}
			out.push_back(m_options.raw ? "\thlt" : "\tret");
		}
		if (!m_initCode.empty())
		{
			out.push_back("");
			out.push_back("__ds_init:");
			if (m_initLocalMin < 4)
			{
				out.push_back(std::string("\tmov ") + ScratchCounter + ", SP");
				out.push_back(std::string("\tsubip ") + ScratchCounter + ", " + imm(4 - m_initLocalMin));
				out.push_back(std::string("\tmov SP, ") + ScratchCounter);
			}
			out.insert(out.end(), m_initCode.begin(), m_initCode.end());
			out.push_back(".loc -");
			out.push_back("\tret");
		}
		out.insert(out.end(), m_code.begin(), m_code.end());
		if (!m_finiChunks.empty())
		{
			out.push_back("");
			out.push_back("__ds_fini:");
			out.push_back(".loc -");
			for (size_t i = m_finiChunks.size(); i-- > 0; )
				out.insert(out.end(), m_finiChunks[i].begin(), m_finiChunks[i].end());
			out.push_back("\tret");
		}
		if (m_helpers.count("__ds_dyncast") > 0)
		{
			if (m_subEmitted || m_topLows.empty())
				m_pieceStarts.push_back("__ds_dyncast");
			// __ds_dyncast(p, T): p points at one of an object's table pointers; the table has the
			// object's type and where p is in it (the object: p - that). T found up its chain of
			// classes (where each base is), or among its interfaces: the pointer to that part (RV),
			// or 0. Two types are the same if their names are (a library has its own copies).
			std::vector<std::string> h = { "", "__ds_dyncast:", ".loc -",
				"\tmovo R1, *PP, dword 0x00000000", "\tmovo R2, *PP, dword 0xFFFFFFFC",
				"\tmov R3, *R1", "\tmovo R4, *R3, dword 0x00000004", "\tmov R5, R1", "\tsubip R5, R4",
				"\tmov R6, *R3", "\tmov R7, R6", "\tmov R8, R5" };
			int n = 0;
			// (is the type in R7 T: the same address, or the same name? found: jump)
			auto l_same = [&](const std::string& found) {
				const std::string loop = "__ds_dyncast_s" + std::to_string(n), differ = "__ds_dyncast_d" + std::to_string(n);
				n++;
				for (const std::string& line : std::initializer_list<std::string> { "\tmov ACC, R7", "\tjeq $" + found + ", R2", "\tmov R10, *R7", "\tmov R11, *R2", loop + ":",
					std::string("\tmovb R12, *R10"), std::string("\tmovb R13, *R11"), std::string("\tmov ACC, R12"), "\tjne $" + differ + ", R13",
					"\tjeq $" + found + ", 0", std::string("\taddip R10, 1"), std::string("\taddip R11, 1"), "\tjmp $" + loop, differ + ":" })
					h.push_back(line);
			};
			h.push_back("__ds_dyncast_chain:");
			l_same("__ds_dyncast_class");
			for (const char* line : { "\tmovo R9, *R7, dword 0x00000004", "\tmov ACC, R9", "\tjeq $__ds_dyncast_ifaces, 0", "\tmovo R10, *R7, dword 0x00000008",
				"\taddip R8, R10", "\tmov R7, R9", "\tjmp $__ds_dyncast_chain", "__ds_dyncast_class:", "\tmov RV, R8", "\tret",
				"__ds_dyncast_ifaces:", "\tmovo R9, *R6, dword 0x0000000C", "\tmov R14, R6", "\taddip R14, 0x00000010", "__ds_dyncast_iloop:",
				"\tmov ACC, R9", "\tjeq $__ds_dyncast_none, 0", "\tmov R7, *R14" })
				h.push_back(line);
			l_same("__ds_dyncast_iface");
			for (const char* line : { "\taddip R14, 0x00000008", "\tsubip R9, 1", "\tjmp $__ds_dyncast_iloop", "__ds_dyncast_iface:",
				"\tmovo R10, *R14, dword 0x00000004", "\tmov R8, R5", "\taddip R8, R10", "\tmov RV, R8", "\tret",
				"__ds_dyncast_none:", "\tmov RV, 0", "\tret" })
				h.push_back(line);
			out.insert(out.end(), h.begin(), h.end());
		}
		if (m_helpers.count("__ds_shru") > 0)
		{
			if (m_subEmitted || m_topLows.empty())
				m_pieceStarts.push_back("__ds_shru");
			// __ds_shru(value, count): logical shift right.
			const char* helper[] = {
				"", "__ds_shru:", ".loc -",
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
