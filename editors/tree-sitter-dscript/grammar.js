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
// if constexpr (condition) { items } [else { items } | else if constexpr ...] around declarations.
const conditional = ($, self, item) => seq(
	'if', 'constexpr', '(', field('condition', $._expression), ')',
	'{', repeat(item), '}',
	optional(seq('else', choice(self, seq('{', repeat(item), '}')))),
);

module.exports = grammar({
	name: 'dscript',

	word: $ => $.identifier,

	// Preprocessor directives can appear on any line.
	extras: $ => [/\s/, $.line_comment, $.block_comment, $.preproc_include, $.preproc_directive],

	conflicts: $ => [
		[$._expression, $._type],
		[$.scoped_identifier, $.scoped_type_identifier],
		// (Name<...>: a template's arguments or a comparison: the parse that works wins)
		[$.member_expression],
		[$._expression, $.generic_expression],
		[$.generic_type, $.generic_expression],
		[$.generic_type, $._expression, $.generic_expression],
		// (priv constexpr NAME = ... / priv constexpr sub: told by what follows)
		[$.struct_constant, $.function_declaration],
		// (Name<16>: a template's value argument or comparisons: the parse that works wins)
		[$.template_arguments, $._expression],
	],

	rules: {
		source_file: $ => repeat($._top_level),

		_top_level: $ => choice(
			$.template_declaration,
			$.namespace_declaration,
			$.struct_declaration,
			$.enum_declaration,
			$.const_declaration,
			$.alias_declaration,
			seq($.variable_declaration, ';'),
			$.function_declaration,
			$.low_block,
			$.conditional_declaration,
			$.static_assert,
		),
		conditional_declaration: $ => conditional($, $.conditional_declaration, $._top_level),
		// static_assert(condition[, "message"]): checked when compiling (no code)
		static_assert: $ => seq('static_assert', '(', field('condition', $._expression), optional(seq(',', field('message', $.string_literal))), ')', ';'),

		// ===== Comments and directives =====
		line_comment: _ => token(seq('//', /[^\n]*/)),
		block_comment: _ => token(seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/')),

		// #include("file.ds")  #import("name")  #import("name", dynamic): the path is a string
		// (also the old <name> and unquoted forms).
		preproc_include: $ => seq(
			alias(token(prec(2, /#(include|import)/)), $.preproc_keyword),
			token.immediate('('),
			field('path', choice(
				$.string_literal,
				// (a negative precedence: these match almost anything, so in error recovery, where
				// every token is possible, any other token wins; they are only taken after '(')
				alias(token(prec(-1, /<[^>\n]*>/)), $.string_literal),
				alias(token(prec(-1, /[^\s(),"<>]+/)), $.string_literal),
			)),
			optional(seq(',', field('mode', $.identifier))),
			')',
		),

		// #define(NAME = value)  #if(expr)  #else  #endif  #once ...
		// (one token: it runs to the end of the line, before a comment)
		preproc_directive: _ => token(prec(1, seq(/#[A-Za-z_]+/, /([^\n/]|\/[^\n/*])*/))),

		// ===== Declarations =====
		// namespace A { ... } / namespace A::B { ... } (nested, reopened anywhere)
		namespace_declaration: $ => seq(
			'namespace',
			field('name', $.namespace_name),
			field('body', $.declaration_list),
			optional(';'),
		),
		namespace_name: $ => seq(
			alias($.identifier, $.namespace_identifier),
			repeat(seq('::', alias($.identifier, $.namespace_identifier))),
		),
		declaration_list: $ => seq('{', repeat($._top_level), '}'),

		struct_declaration: $ => seq(
			'struct',
			field('name', alias($.identifier, $.type_identifier)),
			field('body', $.field_list),
			optional(';'),
		),
		// Fields and methods (subs in a struct, called as x.m() / p->m(), with this).
		// Nested structs / enums (Outer::Inner; priv: only usable in the struct).
		// constexpr NAME = value;: a constant of the struct.
		field_list: $ => seq('{', repeat($._member), '}'),
		_member: $ => choice($.field_declaration, $.function_declaration, $.nested_type_declaration, $.template_declaration,
			$.struct_constant, $.conditional_member, $.static_assert),
		conditional_member: $ => conditional($, $.conditional_member, $._member),
		struct_constant: $ => seq(
			optional($.priv_modifier),
			'constexpr',
			field('name', $.identifier),
			optional(seq(':', field('type', $._type))),
			'=', field('value', choice($._expression, $.initializer_list)),
			';',
		),

		// template(T1, optional T2) where <condition> { structs and subs }
		template_declaration: $ => seq(
			'template', '(', commaSep1($.template_parameter), ')',
			optional(seq('where', field('condition', $._expression))),
			field('body', $.template_body),
			optional(';'),
		),
		// T (a type), N: u32 (a value), = default (a type, or a value)
		template_parameter: $ => seq(
			optional('optional'),
			field('name', alias($.identifier, $.type_identifier)),
			optional(seq(':', field('type', $._type))),
			optional(seq('=', field('default', choice($._type, $._expression)))),
		),
		template_body: $ => seq('{', repeat($._template_item), '}'),
		_template_item: $ => choice($.struct_declaration, $.function_declaration, $.conditional_template_item, $.static_assert),
		conditional_template_item: $ => conditional($, $.conditional_template_item, $._template_item),
		// <u32, Point*, 16, CONFIG> after a template's name (in a type or an expression)
		template_arguments: $ => prec.dynamic(1, seq('<', commaSep1(choice($._type, $.number_literal, $.member_expression, $.call_expression,
			$.parenthesized_expression, $.unary_expression, $.boolean_literal, $.binary_expression)), '>')),
		nested_type_declaration: $ => seq(
			optional($.priv_modifier),
			choice($.struct_declaration, $.enum_declaration),
		),
		// const: set when the struct is built (its default, { ... }, in new), then never changed
		field_declaration: $ => seq(
			optional(choice($.priv_modifier, $.readonly_modifier)),
			choice('var', 'const'),
			field('name', alias($.identifier, $.field_identifier)),
			':',
			field('type', $._type),
			optional(seq('=', field('value', $._expression))),
			';',
		),

		enum_declaration: $ => seq(
			'enum',
			field('name', alias($.identifier, $.type_identifier)),
			// The integer type of the members (i32 without one).
			optional(seq(':', field('type', $._type))),
			field('body', $.enumerator_list),
			optional(';'),
		),
		enumerator_list: $ => seq('{', commaSep($.enumerator), optional(','), '}'),
		enumerator: $ => seq(
			field('name', $.identifier),
			optional(seq('=', field('value', $._expression))),
		),

		// const: a compile-time constant if the value is one, otherwise a variable that never changes;
		// constexpr: a compile-time constant.
		const_declaration: $ => seq(
			choice('const', 'constexpr'),
			field('name', $.identifier),
			optional(seq(':', field('type', $._type),
				// const p: Point(1, 2): built by the struct's constructor
				optional(field('arguments', $.argument_list)))),
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

		variable_declaration: $ => choice(
			seq(
				'var',
				field('name', $.identifier),
				optional(seq(':', field('type', $._type),
					// var p: Point(1, 2): the struct's constructor (sub new)
					optional(field('arguments', $.argument_list)))),
				optional(seq('=', field('value', choice($._expression, $.initializer_list)))),
			),
			// auto x = value: the type comes from the value (no ': type')
			seq('auto', field('name', $.identifier), '=', field('value', $._expression)),
			// constinit x: T = value: a global whose value is known when compiling
			seq(
				'constinit',
				field('name', $.identifier),
				optional(seq(':', field('type', $._type))),
				optional(seq('=', field('value', choice($._expression, $.initializer_list)))),
			),
		),

		initializer_list: $ => seq(
			'{',
			commaSep(choice($._expression, $.initializer_list)),
			optional(','),
			'}',
		),

		function_declaration: $ => seq(
			// static: in a struct, a sub without this (static inline / inline static)
			// readonly: this is a const pointer; constexpr / consteval: evaluated when compiling
			repeat(choice($.atomic_modifier, $.interrupt_modifier, $.inline_modifier, $.static_modifier, $.priv_modifier, $.readonly_modifier,
				$.constexpr_modifier, $.consteval_modifier)),
			'sub',
			field('name', $.identifier),
			field('parameters', $.parameter_list),
			optional(seq('->', field('return_type', $._type))),
			choice(field('body', $.block), ';'),
		),
		interrupt_modifier: _ => 'interrupt',
		constexpr_modifier: _ => 'constexpr',
		consteval_modifier: _ => 'consteval',
		// atomic sub: its body runs with interrupts disabled (like atomic { ... })
		atomic_modifier: _ => 'atomic',
		static_modifier: _ => 'static',
		// priv: a struct field or sub only the struct's own subs can use
		priv_modifier: _ => 'priv',
		// readonly: a struct field everyone can read, but only the struct's own subs can change
		readonly_modifier: _ => 'readonly',
		// static var x: T = value;  static const X = value;  (in a sub)
		static_declaration: $ => seq(
			$.static_modifier,
			choice(seq($.variable_declaration, ';'), $.const_declaration),
		),
		inline_modifier: _ => 'inline',
		// ... / ...typed: more arguments (the last "parameter").
		parameter_list: $ => seq('(', choice(
			alias('void', $.primitive_type),
			commaSep($.parameter),
			seq(commaSep1($.parameter), ',', $.variadic_parameter),
			$.variadic_parameter,
		), ')'),
		variadic_parameter: _ => seq('...', optional('typed')),
		parameter: $ => seq(
			optional(choice('var', 'const')),
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
			$.scoped_type_identifier,
			$.generic_type,
			$.pointer_type,
			$.array_type,
			$.function_type,
			$.parenthesized_type,
			$.struct_type,
			$.const_type,
			$.value_type,
		),
		// A::B::Type, ::Type (the global one)
		scoped_type_identifier: $ => seq(
			optional(field('scope', alias($.identifier, $.namespace_identifier))),
			'::',
			field('name', choice(alias($.identifier, $.type_identifier), $.scoped_type_identifier)),
		),
		// Array<u32>, Ns::Map<u32, Point*>
		generic_type: $ => seq(
			field('name', choice(alias($.identifier, $.type_identifier), $.scoped_type_identifier)),
			field('arguments', $.template_arguments),
		),
		primitive_type: _ => choice('i8', 'u8', 'i16', 'u16', 'i32', 'u32', 'f32', 'bool', 'char', 'string', 'int', 'uint', 'float', 'void'),
		pointer_type: $ => prec(2, seq(field('type', $._type), '*')),
		// T[]: the size comes from the value
		array_type: $ => prec(2, seq(field('element', $._type), '[', optional(field('size', $._expression)), ']')),
		function_type: $ => prec.right(1, seq(
			'sub',
			'(',
			choice(
				alias('void', $.primitive_type),
				commaSep($._type),
				seq(commaSep1($._type), ',', $.variadic_parameter),
				$.variadic_parameter,
			),
			')',
			optional(seq('->', field('return_type', $._type))),
		)),
		parenthesized_type: $ => seq('(', $._type, ')'),
		// const T*: what the pointer points at is constant (const u8* groups as const (u8*))
		const_type: $ => prec(1, seq('const', field('type', $._type))),
		// value_type(N): the type of a value (of a template parameter given one)
		value_type: $ => seq('value_type', '(', field('value', $._expression), ')'),
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
			$.for_in_statement,
			$.return_statement,
			$.break_statement,
			$.continue_statement,
			$.low_block,
			$.assert_statement,
			$.debug_print_statement,
			$.static_assert,
			$.defer_statement,
			$.atomic_statement,
			$.expression_statement,
			$.empty_statement,
		),
		assert_statement: $ => seq(
			'assert', '(', field('condition', $._expression),
			optional(seq(',', field('message', $._expression))), ')', ';',
		),

		// Run when the block is left (in the reverse order), on every way out of it.
		defer_statement: $ => seq('defer', field('body', $._statement)),

		// Interrupts disabled in the block, put back as they were on every way out of it.
		atomic_statement: $ => seq('atomic', field('body', $.block)),

		debug_print_statement: $ => seq('debug_print', '(', field('value', $._expression), ')', ';'),

		if_statement: $ => prec.right(seq(
			'if', optional('constexpr'), '(', field('condition', $._expression), ')',
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
		// for (auto x in range) / for (auto& x in range) / for (var x: T in range) / ... in a..b
		for_in_statement: $ => seq(
			'for', '(',
			choice(
				seq('auto', optional(field('reference', '&')), field('name', $.identifier)),
				seq('var', field('name', $.identifier), ':', field('type', $._type)),
			),
			'in',
			field('range', choice($.range_expression, $._expression)),
			')',
			field('body', $._statement),
		),
		// a..b (b excluded). A number right before '..' is one token with it (0..10: the lexer can't
		// tell "0." from "0" followed by "..", and "2." is a float).
		range_expression: $ => choice(
			seq(field('start', $._expression), '..', field('end', $._expression)),
			seq(field('start', alias($._number_dotdot, $.number_literal)), field('end', $._expression)),
		),
		_number_dotdot: _ => token(prec(1, /[0-9][0-9_]*[uU]?\.\./)),
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
			$.generic_expression,
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
		// (the built-ins of compile-time conditions take types: is_type(T, u32), is_pointer_to(T, u8*))
		argument_list: $ => seq('(', commaSep(choice($._expression, $.primitive_type, $.pointer_type, $.generic_type, $.function_type, $.const_type, $.value_type)), ')'),

		// max<u32>, Array<u32> (before (...) or .name): a template's instance
		generic_expression: $ => seq(
			field('name', choice($.identifier, $.scoped_identifier)),
			field('arguments', $.template_arguments),
		),

		index_expression: $ => prec(PREC.POSTFIX, seq(field('array', $._expression), '[', field('index', $._expression), ']')),

		member_expression: $ => prec(PREC.POSTFIX, seq(
			field('object', $._expression),
			field('operator', choice('.', '->')),
			field('member', alias($.identifier, $.field_identifier)),
			optional(field('arguments', $.template_arguments)),		// x.map<f32>(...)
		)),

		parenthesized_expression: $ => seq('(', $._expression, ')'),

		// A::B::name: in a namespace, a struct's static sub / method (Struct::method: a method as
		// a sub, its first parameter is the struct pointer) or nested type; ::name: the global one.
		scoped_identifier: $ => seq(
			optional(field('scope', alias($.identifier, $.namespace_identifier))),
			'::',
			field('name', choice($.identifier, $.scoped_identifier)),
		),

		// ===== Literals and names =====
		number_literal: _ => token(choice(
			// (u / U: unsigned, 123u is a u32)
			/0[xX][0-9a-fA-F][0-9a-fA-F_]*[uU]?/,
			/0[bB][01][01_]*[uU]?/,
			/[0-9][0-9_]*[uU]?/,
			// f32: 1.5, 2., 1e-3, 2.5e+4, 3f, 0.5f
			/[0-9][0-9_]*\.[0-9_]*([eE][+-]?[0-9]+)?[fF]?/,
			/[0-9][0-9_]*[eE][+-]?[0-9]+[fF]?/,
			/[0-9][0-9_]*[fF]/,
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
