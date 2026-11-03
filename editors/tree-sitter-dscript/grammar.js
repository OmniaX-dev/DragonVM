/**
 * Tree-sitter grammar for DScript, the C-like language of the Dragon machine.
 * The language reference is extra/info/dscript.md; the compiler's parser
 * (src/compiler/Parser.cpp) is the authority when they disagree.
 */

const PREC = {
	ASSIGN: 1,
	TERNARY: 2,
	OR: 3,
	AND: 4,
	BIT_OR: 5,
	BIT_XOR: 6,
	BIT_AND: 7,
	EQUALITY: 8,
	RELATIONAL: 9,
	SHIFT: 10,
	ADDITIVE: 11,
	MULTIPLICATIVE: 12,
	UNARY: 13,
	POSTFIX: 14,
};

const commaSep = (rule) => optional(commaSep1(rule));
const commaSep1 = (rule) => seq(rule, repeat(seq(',', rule)));

module.exports = grammar({
	name: 'dscript',

	word: $ => $.identifier,

	// Preprocessor directives can appear on any line.
	extras: $ => [/\s/, $.line_comment, $.block_comment, $.preproc_directive],

	conflicts: $ => [
		[$._expression, $._type],
	],

	rules: {
		source_file: $ => repeat($._top_level),

		_top_level: $ => choice(
			$.struct_declaration,
			$.enum_declaration,
			$.const_declaration,
			$.alias_declaration,
			seq($.variable_declaration, ';'),
			$.function_declaration,
			$.low_block,
		),

		// ===== Comments and directives =====
		line_comment: _ => token(seq('//', /[^\n]*/)),
		block_comment: _ => token(seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/')),

		// #include(file.ds)  #define(NAME = value)  #if(expr)  #else  #endif  #once ...
		// (one token: it runs to the end of the line, before a comment)
		preproc_directive: _ => token(prec(1, seq(/#[A-Za-z_]+/, /([^\n/]|\/[^\n/*])*/))),

		// ===== Declarations =====
		struct_declaration: $ => seq(
			'struct',
			field('name', alias($.identifier, $.type_identifier)),
			field('body', $.field_list),
			optional(';'),
		),
		// Fields and methods (subs in a struct, called as x.m() / p->m(), with this).
		field_list: $ => seq('{', repeat(choice($.field_declaration, $.function_declaration)), '}'),
		field_declaration: $ => seq(
			'var',
			field('name', alias($.identifier, $.field_identifier)),
			':',
			field('type', $._type),
			optional(seq('=', field('value', $._expression))),
			';',
		),

		enum_declaration: $ => seq(
			'enum',
			field('name', alias($.identifier, $.type_identifier)),
			field('body', $.enumerator_list),
			optional(';'),
		),
		enumerator_list: $ => seq('{', commaSep($.enumerator), optional(','), '}'),
		enumerator: $ => seq(
			field('name', $.identifier),
			optional(seq('=', field('value', $._expression))),
		),

		const_declaration: $ => seq(
			'const',
			field('name', $.identifier),
			optional(seq(':', field('type', $._type))),
			// Struct constants: { ... }, or no value (the fields' defaults).
			optional(seq('=', field('value', choice($._expression, $.initializer_list)))),
			';',
		),

		alias_declaration: $ => seq(
			'alias',
			field('name', alias($.identifier, $.type_identifier)),
			'=',
			field('type', $._type),
			';',
		),

		variable_declaration: $ => seq(
			'var',
			field('name', $.identifier),
			optional(seq(':', field('type', $._type))),
			optional(seq('=', field('value', choice($._expression, $.initializer_list)))),
		),

		initializer_list: $ => seq(
			'{',
			commaSep(choice($._expression, $.initializer_list)),
			optional(','),
			'}',
		),

		function_declaration: $ => seq(
			optional(choice($.interrupt_modifier, $.inline_modifier)),
			'sub',
			field('name', $.identifier),
			field('parameters', $.parameter_list),
			optional(seq('->', field('return_type', $._type))),
			choice(field('body', $.block), ';'),
		),
		interrupt_modifier: _ => 'interrupt',
		static_modifier: _ => 'static',
		// static var x: T = value;  static const X = value;  (in a sub)
		static_declaration: $ => seq(
			$.static_modifier,
			choice(seq($.variable_declaration, ';'), $.const_declaration),
		),
		inline_modifier: _ => 'inline',
		parameter_list: $ => seq('(', choice(alias('void', $.primitive_type), commaSep($.parameter)), ')'),
		parameter: $ => seq(
			optional('var'),
			field('name', $.identifier),
			':',
			field('type', $._type),
			optional(seq('=', field('default', $._expression))),
		),

		// low { raw dasm }: the body is kept as text (and highlighted as dasm by an injection).
		low_block: $ => seq('low', '{', optional($.low_content), '}'),
		low_content: $ => repeat1(choice(
			/[^{}]+/,
			seq('{', optional($.low_content), '}'),
		)),

		// ===== Types =====
		_type: $ => choice(
			$.primitive_type,
			alias($.identifier, $.type_identifier),
			$.pointer_type,
			$.array_type,
			$.function_type,
			$.parenthesized_type,
			$.struct_type,
		),
		primitive_type: _ => choice('i8', 'u8', 'i16', 'u16', 'i32', 'u32', 'bool', 'char', 'string', 'int', 'uint', 'void'),
		pointer_type: $ => prec(2, seq(field('type', $._type), '*')),
		// T[]: the size comes from the value
		array_type: $ => prec(2, seq(field('element', $._type), '[', optional(field('size', $._expression)), ']')),
		function_type: $ => prec.right(1, seq(
			'sub',
			'(',
			choice(alias('void', $.primitive_type), commaSep($._type)),
			')',
			optional(seq('->', field('return_type', $._type))),
		)),
		parenthesized_type: $ => seq('(', $._type, ')'),
		// A nameless struct used as a type: var p: struct { var x: i32; };
		struct_type: $ => seq('struct', field('body', $.field_list)),

		// ===== Statements =====
		block: $ => seq('{', repeat($._statement), '}'),

		_statement: $ => choice(
			$.block,
			seq($.variable_declaration, ';'),
			// Local constants and static variables: set once, visible in the block.
			$.const_declaration,
			$.static_declaration,
			$.if_statement,
			$.while_statement,
			$.do_statement,
			$.for_statement,
			$.return_statement,
			$.break_statement,
			$.continue_statement,
			$.low_block,
			$.expression_statement,
			$.empty_statement,
		),

		if_statement: $ => prec.right(seq(
			'if', '(', field('condition', $._expression), ')',
			field('consequence', $._statement),
			optional(seq('else', field('alternative', $._statement))),
		)),
		while_statement: $ => seq('while', '(', field('condition', $._expression), ')', field('body', $._statement)),
		do_statement: $ => seq('do', field('body', $._statement), 'while', '(', field('condition', $._expression), ')', ';'),
		for_statement: $ => seq(
			'for', '(',
			optional(field('initializer', choice($.variable_declaration, $._expression))), ';',
			optional(field('condition', $._expression)), ';',
			optional(field('update', $._expression)),
			')',
			field('body', $._statement),
		),
		return_statement: $ => seq('return', optional($._expression), ';'),
		break_statement: _ => seq('break', ';'),
		continue_statement: _ => seq('continue', ';'),
		expression_statement: $ => seq($._expression, ';'),
		empty_statement: _ => ';',

		// ===== Expressions =====
		_expression: $ => choice(
			$.assignment_expression,
			$.ternary_expression,
			$.binary_expression,
			$.unary_expression,
			$.update_expression,
			$.cast_expression,
			$.sizeof_expression,
			$.lambda_expression,
			$.call_expression,
			$.index_expression,
			$.member_expression,
			$.parenthesized_expression,
			$.scoped_identifier,
			$.identifier,
			$.number_literal,
			$.char_literal,
			$.string_literal,
			$.concatenated_string,
			$.boolean_literal,
			$.null_literal,
		),

		assignment_expression: $ => prec.right(PREC.ASSIGN, seq(
			field('left', $._expression),
			field('operator', choice('=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>=')),
			field('right', $._expression),
		)),

		ternary_expression: $ => prec.right(PREC.TERNARY, seq(
			field('condition', $._expression), '?',
			field('consequence', $._expression), ':',
			field('alternative', $._expression),
		)),

		binary_expression: $ => {
			const table = [
				[PREC.OR, choice('||', 'or')],
				[PREC.AND, choice('&&', 'and')],
				[PREC.BIT_OR, '|'],
				[PREC.BIT_XOR, '^'],
				[PREC.BIT_AND, '&'],
				[PREC.EQUALITY, choice('==', '!=')],
				[PREC.RELATIONAL, choice('<', '<=', '>', '>=')],
				[PREC.SHIFT, choice('<<', '>>')],
				[PREC.ADDITIVE, choice('+', '-')],
				[PREC.MULTIPLICATIVE, choice('*', '/', '%')],
			];
			return choice(...table.map(([p, op]) => prec.left(p, seq(
				field('left', $._expression),
				field('operator', op),
				field('right', $._expression),
			))));
		},

		unary_expression: $ => prec.left(PREC.UNARY, seq(
			field('operator', choice('-', '+', '!', 'not', '~', '*', '&')),
			field('argument', $._expression),
		)),

		update_expression: $ => {
			const op = choice('++', '--');
			return choice(
				prec.right(PREC.UNARY, seq(field('operator', op), field('argument', $._expression))),
				prec.left(PREC.POSTFIX, seq(field('argument', $._expression), field('operator', op))),
			);
		},

		cast_expression: $ => seq('cast', '<', field('type', $._type), '>', '(', field('value', $._expression), ')'),

		sizeof_expression: $ => choice(
			seq('sizeof', '<', field('type', $._type), '>'),
			seq('sizeof', '(', field('value', choice($._expression, $._type)), ')'),
		),

		// A lambda: sub(var x: u32) -> u32 { ... } (no captures), or interrupt sub() { ... }.
		lambda_expression: $ => seq(
			optional($.interrupt_modifier),
			'sub',
			field('parameters', $.parameter_list),
			optional(seq('->', field('return_type', $._type))),
			field('body', $.block),
		),

		call_expression: $ => prec(PREC.POSTFIX, seq(field('function', $._expression), field('arguments', $.argument_list))),
		argument_list: $ => seq('(', commaSep($._expression), ')'),

		index_expression: $ => prec(PREC.POSTFIX, seq(field('array', $._expression), '[', field('index', $._expression), ']')),

		member_expression: $ => prec(PREC.POSTFIX, seq(
			field('object', $._expression),
			field('operator', choice('.', '->')),
			field('member', alias($.identifier, $.field_identifier)),
		)),

		parenthesized_expression: $ => seq('(', $._expression, ')'),

		// Struct::method: a method as a sub (its first parameter is the struct pointer).
		scoped_identifier: $ => seq(
			field('scope', alias($.identifier, $.type_identifier)),
			'::',
			field('name', $.identifier),
		),

		// ===== Literals and names =====
		number_literal: _ => token(choice(
			/0[xX][0-9a-fA-F][0-9a-fA-F_]*/,
			/0[bB][01][01_]*/,
			/[0-9][0-9_]*/,
		)),
		char_literal: $ => seq("'", choice($.escape_sequence, token.immediate(/[^'\\\n]/)), token.immediate("'")),
		string_literal: $ => seq(
			'"',
			repeat(choice(token.immediate(prec(1, /[^"\\\n]+/)), $.escape_sequence)),
			token.immediate('"'),
		),
		concatenated_string: $ => seq($.string_literal, repeat1($.string_literal)),
		escape_sequence: _ => token.immediate(seq('\\', choice(/[ntr0abe\\'"]/, /x[0-9a-fA-F]{2}/))),
		boolean_literal: _ => choice('true', 'false'),
		null_literal: _ => 'null',

		identifier: _ => /[A-Za-z_][A-Za-z0-9_]*/,
	},
});
