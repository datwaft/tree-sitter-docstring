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
