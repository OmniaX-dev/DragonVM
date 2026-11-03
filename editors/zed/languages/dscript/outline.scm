(function_declaration
	[(interrupt_modifier) (inline_modifier) (static_modifier) (priv_modifier)]* @context
	"sub" @context
	name: (identifier) @name
	parameters: (parameter_list) @context) @item

(struct_declaration
	"struct" @context
	name: (type_identifier) @name) @item

(field_declaration
	(priv_modifier)? @context
	name: (field_identifier) @name) @item

(enum_declaration
	"enum" @context
	name: (type_identifier) @name) @item

(source_file
	(const_declaration
		"const" @context
		name: (identifier) @name) @item)

(alias_declaration
	"alias" @context
	name: (type_identifier) @name) @item

(source_file
	(variable_declaration
		"var" @context
		name: (identifier) @name) @item)
