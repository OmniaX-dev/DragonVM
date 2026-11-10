; Generic captures first: later patterns win in Zed.

(identifier) @variable

((identifier) @constant
	(#match? @constant "^[A-Z][A-Z0-9_]+$"))

; Comments and directives
(line_comment) @comment
(block_comment) @comment
(preproc_directive) @preproc

; Keywords
[
	"var"
	"const"
	"sub"
	"struct"
	"enum"
	"alias"
	"namespace"
	"low"
	"cast"
	"sizeof"
] @keyword

[
	"return"
	"if"
	"else"
	"while"
	"do"
	"for"
	"break"
	"continue"
	"assert"
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
(static_modifier) @keyword
(priv_modifier) @keyword

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

; Literals
(number_literal) @number
(char_literal) @string
(string_literal) @string
(escape_sequence) @string.escape
(boolean_literal) @boolean
(null_literal) @constant.builtin

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
