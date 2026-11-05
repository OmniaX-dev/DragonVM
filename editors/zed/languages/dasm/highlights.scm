; Generic captures first: later patterns win in Zed.

(identifier) @variable
(symbol) @label

((identifier) @constant
	(#match? @constant "^[A-Z][A-Z0-9_]*(\\.[A-Za-z0-9_]+)*$"))

; Registers: R1..R20 (and their halves R1L / R1H), ACC, FP, SP, IP, PP, RV, FL; reg(n)
((identifier) @variable.special
	(#match? @variable.special "^(?i:r([1-9]|1[0-9]|20)[lh]?|acc|fp|sp|ip|pp|rv|fl)$"))

(register_call
	.
	(identifier) @function.builtin)

; Instructions and labels
(instruction
	mnemonic: (identifier) @keyword)

(low_mnemonic) @keyword

(offset_size
	[
		"word"
		"dword"
	] @keyword)

(label
	name: (identifier) @label)

; Data
(data_entry
	name: (symbol) @variable)

(data_entry
	struct: (identifier) @type)

; Directives
(comment) @comment
(section_name) @preproc
(directive_name) @preproc
(export_name) @preproc
(system_path) @string.special
(source_path) @string.special

(define_directive
	name: (identifier) @constant)

(group_block
	name: (identifier) @type)

(struct_block
	name: (identifier) @type)

(group_entry
	name: (identifier) @constant)

(struct_member
	name: (identifier) @property)

; Literals and punctuation
(number) @number
(string) @string
(operator) @operator

[
	"*"
	"&"
	"<"
	">"
	"="
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
	":"
] @punctuation.delimiter
