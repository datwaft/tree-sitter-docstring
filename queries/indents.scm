([
  (args_section)
  (returns_section)
  (yields_section)
  (raises_section)
  (attributes_section)
  (keyword_args_section)
  (other_parameters_section)
  (warns_section)
  (examples_section)
  (notes_section)
  (warning_section)
  (admonition_section)
  (see_also_section)
  (references_section)
  (todo_section)
  (methods_section)
] @indent.begin
  (#set! indent.immediate 1))

; Wrapped prose and literal blocks can use more than one indentation level.
(description) @indent.auto

; nvim-treesitter may choose the nested Python tree for doctest continuations;
; its indent engine currently does not account for the omitted prompt ranges.
[
  (fenced_code_block)
  (rst_code_block)
  (doctest)
] @indent.auto
