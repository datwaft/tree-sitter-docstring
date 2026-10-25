((fenced_code_block
  (fence_start) @_language
  (code_content) @injection.content)
  (#match? @_language "^[`~]+[ \\t]*(python|py|python3|py3)[ \\t]*$")
  (#set! injection.language "python"))

((rst_code_block
  (code_directive) @_language
  (code_content) @injection.content)
  (#match? @_language "^\\.\\.[ \\t]+(code-block|sourcecode)::[ \\t]+(python|py|python3|py3)[ \\t]*$")
  (#set! injection.language "python"))

; All input lines in one doctest share a parser; prompts and output are excluded.
((doctest
  . ((doctest_prompt) (code_line) @injection.content)+
  (doctest_prompt)? .)
  (#any-match? @injection.content "[^ \t\r\n]")
  (#set! injection.language "python"))
