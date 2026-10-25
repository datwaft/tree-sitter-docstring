(document) @string.documentation

; Python also highlights the enclosing string at priority 100.
((section_header) @markup.heading
  (#set! priority 110))

((parameter
  name: (identifier) @variable.parameter)
  (#set! priority 110))

((attribute
  name: (identifier) @variable.member)
  (#set! priority 110))

([
  (type)
  (exception_name)
] @type
  (#set! priority 110))

(":" @punctuation.delimiter
  (#set! priority 110))

(["(" ")"] @punctuation.bracket
  (#set! priority 110))

(text) @spell

([
  (fence_start)
  (fence_end)
  (code_directive)
  (directive_option)
  (doctest_prompt)
] @punctuation.special
  (#set! priority 110))

; Inline markup overrides the enclosing Python string and documentation prose.
((inline_literal (inline_content) @markup.raw @nospell)
  (#set! priority 120))

((emphasis (inline_content) @markup.italic)
  (#set! priority 120))

((strong_emphasis (inline_content) @markup.strong)
  (#set! priority 120))

((interpreted_text (inline_content) @markup.link @nospell)
  (#set! priority 120))

((role_name) @function.builtin
  (#set! priority 120))

((reference (inline_content) @markup.link @nospell)
  (#set! priority 120))

((url) @markup.link.url @nospell
  (#set! priority 120))

((markup_delimiter) @punctuation.delimiter @conceal
  (#set! priority 120)
  (#set! conceal ""))
