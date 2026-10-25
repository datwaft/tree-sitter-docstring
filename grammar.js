/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'docstring',

  extras: _ => [],

  rules: {
    document: $ => repeat(choice($.text, /\r?\n/)),

    text: _ => /[^\r\n]+/,
  },
});
