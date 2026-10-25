([
  (args_section)
  (returns_section)
  (yields_section)
  (raises_section)
  (attributes_section)
] @indent.begin
  (#set! indent.immediate 1))

; Wrapped prose and literal blocks can use more than one indentation level.
(description) @indent.auto
