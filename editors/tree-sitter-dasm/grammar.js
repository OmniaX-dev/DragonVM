/**
 * Tree-sitter grammar for dasm, the assembly language of the Dragon machine (.dss).
 * Line based: a line is a label, an instruction, a data entry or a directive.
 * Also used for the low { ... } blocks of DScript (so it accepts // comments and &$var).
 */

const commaSep1 = (rule) => seq(rule, repeat(seq(',', rule)));
const NAME = /[A-Za-z_][A-Za-z0-9_]*/;

module.exports = grammar({
	name: 'dasm',

	extras: $ => [/[ \t\r]/, $.comment],

	word: $ => $.identifier,

	rules: {
		source_file: $ => seq(
			repeat(seq(optional($._line), $._newline)),
			optional($._line),
		),

		_newline: _ => '\n',

		_line: $ => choice(
			$.label,
			$.instruction,
			$.data_entry,
			$.source_marker,
			$.section_directive,
			$.include_directive,
			$.define_directive,
			$.directive,
			$.group_block,
			$.struct_block,
		),

		// ## comment (dasm), // comment (DScript low blocks)
		comment: _ => token(choice(seq('##', /[^\n]*/), seq('//', /[^\n]*/))),

		// ===== Labels and instructions =====
		label: $ => seq(field('name', $.identifier), token.immediate(':')),

		instruction: $ => choice(
			seq(field('mnemonic', $.identifier), optional(field('operands', $.operand_list))),
			seq(field('mnemonic', $.low_mnemonic), repeat1(choice($.number, $.identifier))),		// %low 0x02 0x00: raw bytes
		),
		low_mnemonic: _ => '%low',
		operand_list: $ => commaSep1($._operand),

		_operand: $ => choice(
			$.register_call,
			$.dereference,
			$.memory,
			$.expression_block,
			$.offset_size,
			$.address_of,
			$._value,
		),
		register_call: $ => seq(alias('reg', $.identifier), token.immediate('('), choice($.number, $.identifier), ')'),
		dereference: $ => seq('*', choice($.register_call, $.identifier, $.symbol)),
		memory: $ => seq('[', choice($.expression_block, $._value), ']'),
		offset_size: $ => seq(choice('word', 'dword'), $._operand),
		address_of: $ => seq('&', $.symbol),

		_value: $ => choice($.number, $.symbol, $.identifier, $.string),

		// { MemoryAddresses.INT_VEC + (0x20 * 5) + 1 }
		expression_block: $ => seq('{', repeat(choice($._value, $.operator, '(', ')')), '}'),
		operator: _ => token(choice('+', '-', '*', '/', '%', '&', '|', '^', '~', '<<', '>>')),

		// ===== Data =====
		// $name 0x01, 0x02     $name "text"     $name:16     $name <Struct> = (0x01, 0x02)
		data_entry: $ => seq(
			field('name', $.symbol),
			choice(
				seq(token.immediate(':'), field('size', $.number)),
				seq('<', field('struct', $.identifier), '>', optional(seq('=', '(', optional(commaSep1($._value)), ')'))),
				// $symbol: the address of a data symbol (4 bytes)
				commaSep1(choice($.number, $.string, $.identifier, $.symbol, $.expression_block)),
			),
		),

		// ===== Directives =====
		// .load 0x00010000   .entry main   .fixed 512, 0xFF   .header KERNEL0_BOOT   .data   .code
		section_directive: $ => seq(
			field('name', $.section_name),
			optional(commaSep1(choice($.number, $.identifier))),
		),
		section_name: _ => token(seq('.', NAME)),

		// Source markers (dsc's output, for the debugger): .loc <file> <line> <inline depth>
		// .loc -   .srcfile <index> <path>
		source_marker: $ => choice(
			seq(alias($._loc, $.section_name), choice('-', repeat1($.number))),
			seq(alias($._srcfile, $.section_name), $.number, field('path', $.source_path)),
		),
		_loc: _ => token(prec(2, '.loc')),
		_srcfile: _ => token(prec(2, '.srcfile')),
		source_path: _ => /[^\s][^\n]*/,

		include_directive: $ => seq(alias('@include', $.directive_name), field('path', $.system_path)),
		system_path: _ => /<[^>\n]*>/,

		// @define [/EXPORT] NAME value
		define_directive: $ => seq(
			alias('@define', $.directive_name),
			optional($.export_name),
			field('name', $.identifier),
			field('value', repeat1(choice($._value, $.expression_block, $.operator, '(', ')'))),
		),

		// @guard NAME   @export NAME path   @export_comment NAME "text"   @raw_export_start NAME ...
		directive: $ => seq(
			field('name', $.directive_name),
			repeat(choice($._value, $.system_path, $.export_name, /[^\s"$<>{}()\[\],*&#\/]+(\/[^\s"$<>{}()\[\],*&#\/]+)+/)),
		),
		directive_name: _ => token(seq('@', choice('guard', 'export', 'export_comment', 'raw_export_start', 'raw_export_end'))),
		export_name: _ => token(seq('/', NAME)),

		// @group [/EXPORT] Name  (NAME value lines)  @end
		group_block: $ => seq(
			alias('@group', $.directive_name),
			optional($.export_name),
			field('name', $.identifier),
			$._newline,
			repeat(seq(optional($.group_entry), $._newline)),
			alias('@end', $.directive_name),
		),
		group_entry: $ => seq(field('name', $.identifier), field('value', repeat1(choice($._value, $.expression_block, $.operator)))),

		// @struct Name  (Member:size [> values] lines)  @end
		struct_block: $ => seq(
			alias('@struct', $.directive_name),
			field('name', $.identifier),
			$._newline,
			repeat(seq(optional($.struct_member), $._newline)),
			alias('@end', $.directive_name),
		),
		struct_member: $ => seq(
			field('name', $.identifier),
			token.immediate(':'),
			field('size', $.number),
			optional(seq('>', commaSep1($.number))),
		),

		// ===== Tokens =====
		number: _ => token(seq(optional('-'), choice(/0[xX][0-9a-fA-F]+/, /0[bB][01]+/, /[0-9]+/))),
		string: _ => /"[^"\n]*"/,
		symbol: _ => token(seq('$', NAME, repeat(seq('.', NAME)))),
		identifier: _ => token(seq(NAME, repeat(seq('.', NAME)))),
	},
});
