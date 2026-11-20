; Generic captures first: later patterns win in Zed.

(identifier) @variable

((identifier) @constant
	(#match? @constant "^[A-Z][A-Z0-9_]+$"))

; Comments and directives
(line_comment) @comment
(block_comment) @comment
(preproc_directive) @preproc
(preproc_keyword) @preproc
(preproc_include path: (string_literal) @string)
(preproc_include mode: (identifier) @keyword)

; Keywords
[
	"var"
	"auto"
	"const"
	"constinit"
	"sub"
	"struct"
	"class"
	"interface"
	"ex"
	"impl"
	"friend"
	"enum"
	"alias"
	"namespace"
	"low"
	"cast"
	"bitcast"
	"static_cast"
	"sizeof"
	"value_type"
	"base_type"
	"absolute_base_type"
] @keyword

[
	"return"
	"if"
	"else"
	"while"
	"do"
	"for"
	"in"
	"template"
	"where"
	"optional"
	"constexpr"
	"break"
	"continue"
	"assert"
	"static_assert"
	"debug_print"
	"defer"
	"atomic"
] @keyword

[
	"and"
	"or"
	"not"
] @keyword

(interrupt_modifier) @keyword
(atomic_modifier) @keyword
(inline_modifier) @keyword
(variadic_parameter) @keyword
(static_modifier) @keyword
(priv_modifier) @keyword
(readonly_modifier) @keyword
(prot_modifier) @keyword
(virt_modifier) @keyword
(pure_modifier) @keyword
(override_modifier) @keyword
(final_modifier) @keyword
(abstract_modifier) @keyword
(constexpr_modifier) @keyword
(consteval_modifier) @keyword

; Types
(primitive_type) @type.builtin
(type_identifier) @type
(namespace_identifier) @type

; Declarations and uses
(function_declaration
	name: (identifier) @function)

; A struct's constructor (sub new)
(field_list
	(function_declaration
		name: (identifier) @constructor
		(#eq? @constructor "new")))

(call_expression
	function: (identifier) @function)

(call_expression
	function: (member_expression
		member: (field_identifier) @function.method))

; A::f(), A::B::f(), A::B::C::f(), ::f()
(call_expression
	function: (scoped_identifier
		name: (identifier) @function))
(call_expression
	function: (scoped_identifier
		name: (scoped_identifier
			name: (identifier) @function)))
(call_expression
	function: (scoped_identifier
		name: (scoped_identifier
			name: (scoped_identifier
				name: (identifier) @function))))

((identifier) @variable.special
	(#eq? @variable.special "this"))

(parameter
	name: (identifier) @variable.parameter)

(field_identifier) @property

(enumerator
	name: (identifier) @constant)

(const_declaration
	name: (identifier) @constant)
(struct_constant
	name: (identifier) @constant)

; Literals
(number_literal) @number
(char_literal) @string
(string_literal) @string
(escape_sequence) @string.escape
(boolean_literal) @boolean
(null_literal) @constant.builtin

; The built-ins of variadic subs.
(call_expression
	function: (identifier) @function.builtin
	(#match? @function.builtin "^va_(count|start|types)$"))

; Operators and punctuation
[
	"="
	"+="
	"-="
	"*="
	"/="
	"%="
	"&="
	"|="
	"^="
	"<<="
	">>="
	"+"
	"-"
	"*"
	"/"
	"%"
	"&"
	"|"
	"^"
	"~"
	"!"
	"<<"
	">>"
	"=="
	"!="
	"<"
	"<="
	">"
	">="
	"&&"
	"||"
	"++"
	"--"
	"->"
	"."
	".."
	"::"
	"?"
] @operator

[
	"("
	")"
	"["
	"]"
	"{"
	"}"
] @punctuation.bracket

[
	","
	";"
	":"
] @punctuation.delimiter

; Templates: max<u32>(...), x.map<f32>(...)
(call_expression
	function: (generic_expression name: (identifier) @function))
(call_expression
	function: (generic_expression name: (scoped_identifier name: (identifier) @function)))
