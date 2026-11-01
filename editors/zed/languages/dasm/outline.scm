(label
	name: (identifier) @name) @item

(data_entry
	name: (symbol) @name) @item

(group_block
	(directive_name) @context
	name: (identifier) @name) @item

(struct_block
	(directive_name) @context
	name: (identifier) @name) @item

(define_directive
	(directive_name) @context
	name: (identifier) @name) @item
