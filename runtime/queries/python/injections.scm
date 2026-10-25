; extends

; Only the first statement of a module, class, or function is a docstring.
; Comments can precede that statement without changing its meaning.
([
  (module
    . (comment)*
    . (expression_statement
      (string
        (string_start) @_docstring_start
        (string_content) @injection.content)))
  (class_definition
    body: (block
      . (comment)*
      . (expression_statement
        (string
          (string_start) @_docstring_start
          (string_content) @injection.content))))
  (function_definition
    body: (block
      . (comment)*
      . (expression_statement
        (string
          (string_start) @_docstring_start
          (string_content) @injection.content))))
]
  (#match? @_docstring_start "^[rRuU]?[\"']")
  (#set! injection.language "docstring")
  (#set! injection.include-children))
