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
] @keyword

[
	"and"
	"or"
	"not"
] @keyword

(interrupt_modifier) @keyword
(inline_modifier) @keyword
(static_modifier) @keyword

; Types
(primitive_type) @type.builtin
(type_identifier) @type

; Declarations and uses
(function_declaration
	name: (identifier) @function)

(call_expression
	function: (identifier) @function)

(call_expression
	function: (member_expression
		member: (field_identifier) @function.method))

(scoped_identifier
	name: (identifier) @function)

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
