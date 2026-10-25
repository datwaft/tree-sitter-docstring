/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'docstring',

  externals: $ => [
    $._args_header,
    $._returns_header,
    $._yields_header,
    $._raises_header,
    $._attributes_header,
    $._section_end,
    $._entry_end,
    $.identifier,
    $.exception_name,
    $._parenthesized_type,
    $._return_type,
    $.text,
    $._error_sentinel,
  ],

  extras: _ => [/[ \t\r\n]+/],

  rules: {
    document: $ => repeat(choice(
      $.text,
      $.args_section,
      $.returns_section,
      $.yields_section,
      $.raises_section,
      $.attributes_section,
    )),

    args_section: $ => seq(
      field('header', alias($._args_header, $.section_header)),
      repeat(choice($.parameter, $.text)),
      $._section_end,
    ),

    returns_section: $ => seq(
      field('header', alias($._returns_header, $.section_header)),
      optional($.return_value),
      $._section_end,
    ),

    yields_section: $ => seq(
      field('header', alias($._yields_header, $.section_header)),
      optional($.return_value),
      $._section_end,
    ),

    raises_section: $ => seq(
      field('header', alias($._raises_header, $.section_header)),
      repeat(choice($.exception, $.text)),
      $._section_end,
    ),

    attributes_section: $ => seq(
      field('header', alias($._attributes_header, $.section_header)),
      repeat(choice($.attribute, $.text)),
      $._section_end,
    ),

    parameter: $ => seq(
      field('name', $.identifier),
      optional($._type_annotation),
      ':',
      optional(field('description', $.description)),
      $._entry_end,
    ),

    attribute: $ => seq(
      field('name', $.identifier),
      optional($._type_annotation),
      ':',
      optional(field('description', $.description)),
      $._entry_end,
    ),

    exception: $ => seq(
      field('name', $.exception_name),
      ':',
      optional(field('description', $.description)),
      $._entry_end,
    ),

    _type_annotation: $ => seq(
      '(',
      optional(field('type', alias($._parenthesized_type, $.type))),
      ')',
    ),

    return_value: $ => choice(
      seq(
        field('type', alias($._return_type, $.type)),
        ':',
        optional(field('description', $.description)),
      ),
      field('description', $.description),
    ),

    description: $ => repeat1($.text),
  },
});
