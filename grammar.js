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
    $._keyword_args_header,
    $._other_parameters_header,
    $._warns_header,
    $._examples_header,
    $._notes_header,
    $._warning_header,
    $._admonition_header,
    $._see_also_header,
    $._references_header,
    $._todo_header,
    $._methods_header,
    $._section_end,
    $._entry_end,
    $.identifier,
    $.exception_name,
    $._parenthesized_type,
    $._return_type,
    $._code_end,
    $.fence_start,
    $.fence_end,
    $.code_directive,
    $.directive_option,
    $.code_content,
    $.doctest_prompt,
    $.code_line,
    $._entry_start,
    $._text_start,
    $._text_fragment,
    $._text_end,
    $._literal_start,
    $._emphasis_start,
    $._strong_start,
    $._interpreted_start,
    $._role_start,
    $._reference_start,
    $._simple_reference,
    $._inline_content,
    $._inline_close,
    $._role_suffix,
    $.url,
    $._error_sentinel,
  ],

  extras: _ => [/[ \t\r\n]+/],

  rules: {
    document: $ => repeat(choice(
      $._content,
      $.args_section,
      $.returns_section,
      $.yields_section,
      $.raises_section,
      $.attributes_section,
      $.keyword_args_section,
      $.other_parameters_section,
      $.warns_section,
      $.examples_section,
      $.notes_section,
      $.warning_section,
      $.admonition_section,
      $.see_also_section,
      $.references_section,
      $.todo_section,
      $.methods_section,
    )),

    _content: $ => choice($.text, $.fenced_code_block, $.rst_code_block, $.doctest),

    // Keep prose grouped by line, with markup exposed only inside prose.
    text: $ => seq(
      $._text_start,
      repeat1(choice(
        alias($._text_fragment, 'text_fragment'),
        $.inline_literal,
        $.emphasis,
        $.strong_emphasis,
        $.interpreted_text,
        $.reference,
        $.url,
      )),
      $._text_end,
    ),

    inline_literal: $ => seq(
      alias($._literal_start, $.markup_delimiter),
      alias($._inline_content, $.inline_content),
      alias($._inline_close, $.markup_delimiter),
    ),

    emphasis: $ => seq(
      alias($._emphasis_start, $.markup_delimiter),
      alias($._inline_content, $.inline_content),
      alias($._inline_close, $.markup_delimiter),
    ),

    strong_emphasis: $ => seq(
      alias($._strong_start, $.markup_delimiter),
      alias($._inline_content, $.inline_content),
      alias($._inline_close, $.markup_delimiter),
    ),

    interpreted_text: $ => seq(
      choice(
        alias($._interpreted_start, $.markup_delimiter),
        seq(alias($._role_start, $.role_name), alias('`', $.markup_delimiter)),
      ),
      alias($._inline_content, $.inline_content),
      alias($._inline_close, $.markup_delimiter),
      optional(alias($._role_suffix, $.role_name)),
    ),

    reference: $ => seq(
      choice(
        seq(alias($._reference_start, $.markup_delimiter),
          alias($._inline_content, $.inline_content)),
        alias($._simple_reference, $.inline_content),
      ),
      alias($._inline_close, $.markup_delimiter),
    ),

    fenced_code_block: $ => seq(
      $.fence_start,
      optional($.code_content),
      choice($.fence_end, $._code_end),
    ),

    rst_code_block: $ => seq(
      $.code_directive,
      repeat($.directive_option),
      optional($.code_content),
      $._code_end,
    ),

    doctest: $ => prec.right(repeat1(seq($.doctest_prompt, optional($.code_line)))),

    args_section: $ => seq(
      field('header', alias($._args_header, $.section_header)),
      repeat(choice($.parameter, $._content)),
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
      repeat(choice($.exception, $._content)),
      $._section_end,
    ),

    attributes_section: $ => seq(
      field('header', alias($._attributes_header, $.section_header)),
      repeat(choice($.attribute, $._content)),
      $._section_end,
    ),

    keyword_args_section: $ => seq(
      field('header', alias($._keyword_args_header, $.section_header)),
      repeat(choice($.parameter, $._content)),
      $._section_end,
    ),

    other_parameters_section: $ => seq(
      field('header', alias($._other_parameters_header, $.section_header)),
      repeat(choice($.parameter, $._content)),
      $._section_end,
    ),

    warns_section: $ => seq(
      field('header', alias($._warns_header, $.section_header)),
      repeat(choice($.exception, $._content)),
      $._section_end,
    ),

    examples_section: $ => seq(
      field('header', alias($._examples_header, $.section_header)),
      repeat($._content),
      $._section_end,
    ),

    notes_section: $ => seq(
      field('header', alias($._notes_header, $.section_header)),
      repeat($._content),
      $._section_end,
    ),

    warning_section: $ => seq(
      field('header', alias($._warning_header, $.section_header)),
      repeat($._content),
      $._section_end,
    ),

    admonition_section: $ => seq(
      field('header', alias($._admonition_header, $.section_header)),
      repeat($._content),
      $._section_end,
    ),

    see_also_section: $ => seq(
      field('header', alias($._see_also_header, $.section_header)),
      repeat($._content),
      $._section_end,
    ),

    references_section: $ => seq(
      field('header', alias($._references_header, $.section_header)),
      repeat($._content),
      $._section_end,
    ),

    todo_section: $ => seq(
      field('header', alias($._todo_header, $.section_header)),
      repeat($._content),
      $._section_end,
    ),

    methods_section: $ => seq(
      field('header', alias($._methods_header, $.section_header)),
      repeat($._content),
      $._section_end,
    ),

    parameter: $ => seq(
      $._entry_start,
      field('name', $.identifier),
      optional($._type_annotation),
      ':',
      optional(field('description', $.description)),
      $._entry_end,
    ),

    attribute: $ => seq(
      $._entry_start,
      field('name', $.identifier),
      optional($._type_annotation),
      ':',
      optional(field('description', $.description)),
      $._entry_end,
    ),

    exception: $ => seq(
      $._entry_start,
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

    description: $ => repeat1($._content),
  },
});
