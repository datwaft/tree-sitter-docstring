local root = vim.fn.getcwd()
vim.opt.runtimepath:prepend(root .. "/.tests/deps/nvim-treesitter")
vim.opt.runtimepath:prepend(root .. "/.tests/deps/nvim-treesitter/runtime")
vim.opt.runtimepath:prepend(root)
vim.opt.runtimepath:append(root .. "/after")
vim.opt.runtimepath:prepend(root .. "/.tests/site")
vim.treesitter.language.add("docstring", { path = root .. "/.tests/docstring.so" })
vim.treesitter.language.add("python", { path = root .. "/.tests/python.so" })
vim.o.more = false
vim.o.swapfile = false

---@param actual any
---@param expected any
local function equal(actual, expected)
  assert(vim.deep_equal(actual, expected), "Expected " .. vim.inspect(expected) .. ", got " .. vim.inspect(actual))
end

---@param text string
---@param language string
---@param width? integer
---@return vim.treesitter.LanguageTree
local function open(text, language, width)
  local buf = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_set_current_buf(buf)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, vim.split(text:gsub("\n$", ""), "\n", { plain = true }))
  vim.bo.filetype = language
  vim.bo.shiftwidth = width or 4
  vim.bo.tabstop = width or 4
  vim.bo.expandtab = true
  vim.bo.autoindent = true
  vim.bo.indentexpr = "v:lua.require'nvim-treesitter'.indentexpr()"
  local parser = vim.treesitter.get_parser(buf, language)
  parser:parse(true)
  return parser
end

---@param needle string
---@return integer row
---@return integer col
local function position(needle)
  for row, line in ipairs(vim.api.nvim_buf_get_lines(0, 0, -1, false)) do
    local col = line:find(needle, 1, true)
    if col then
      return row - 1, col - 1
    end
  end
  error("Text not found: " .. needle)
end

---@param parser vim.treesitter.LanguageTree
---@return TSNode[]
local function doc_roots(parser)
  local roots = {}
  parser:for_each_tree(function(tree, language_tree)
    if language_tree:lang() == "docstring" then
      assert(not tree:root():has_error(), "Injected docstring has a parse error")
      roots[#roots + 1] = tree:root()
    end
  end)
  return roots
end

---@param needle string
---@param capture string
---@return table
local function highlight(needle, capture)
  vim.treesitter.start(0, vim.bo.filetype)
  local row, col = position(needle)
  col = col + #(needle:match("^%s*"))
  for _, item in ipairs(vim.treesitter.get_captures_at_pos(0, row, col)) do
    if item.lang == "docstring" and item.capture == capture then
      return item
    end
  end
  error("Missing docstring capture " .. capture .. " at " .. needle)
end

---@return vim.treesitter.LanguageTree
local function python_fixture()
  return open(table.concat(vim.fn.readfile(root .. "/test/fixtures/python.py"), "\n"), "python")
end

---@param width integer
---@param python boolean
local function indent_fixture(width, python)
  local base = python and string.rep(" ", width) or ""
  local step = string.rep(" ", width)
  local lines = python and { "def fetch():", base .. '"""Fetch records.' } or { "Fetch records." }
  vim.list_extend(lines, {
    "",
    base .. "Args:",
    base .. step .. "key: A record key.",
    base .. step .. step .. "A continuation.",
    base .. "Returns:",
    base .. step .. "The matching record.",
  })
  if python then
    vim.list_extend(lines, { base .. '"""', base .. "return None" })
  end
  local parser = open(table.concat(lines, "\n"), python and "python" or "docstring", width)
  if python then
    equal(#doc_roots(parser), 1)
  end
  vim.cmd("silent normal! gg=G")
  equal(vim.api.nvim_buf_get_lines(0, 0, -1, false), lines)
end

---@param injected boolean
local function fold_fixture(injected)
  local lines = injected and { "def fetch():", '    """Fetch records.', "" } or { "Fetch records.", "" }
  local base = injected and "    " or ""
  vim.list_extend(lines, {
    base .. "Args:",
    base .. "    key: A record key.",
    "",
    base .. "Returns:",
    base .. "    The matching record.",
  })
  if injected then
    vim.list_extend(lines, { '    """', "    return None" })
  end
  local parser = open(table.concat(lines, "\n"), injected and "python" or "docstring")
  vim.wo.foldmethod = "expr"
  vim.wo.foldexpr = "v:lua.vim.treesitter.foldexpr()"
  vim.wo.foldminlines = 0
  vim.wo.foldlevel = 99
  parser:parse(true)
  vim.cmd("silent normal! zx")
  assert(
    vim.wait(2000, function()
      local row = position("Args:")
      return vim.fn.foldlevel(row + 1) > 0
    end),
    "No folds appeared"
  )
  return parser
end

---@param needle string
---@param end_needle string
local function fold_range(needle, end_needle)
  local row, col = position(needle)
  local end_row = position(end_needle)
  vim.cmd("silent normal! zR")
  vim.api.nvim_win_set_cursor(0, { row + 1, col })
  vim.cmd("silent normal! zc")
  equal(vim.fn.foldclosed(row + 1), row + 1)
  equal(vim.fn.foldclosedend(row + 1), end_row + 1)
end

local cases = {}

function cases.query_loading()
  for _, kind in ipairs({ "highlights", "indents", "folds" }) do
    assert(vim.treesitter.query.get("docstring", kind), "Missing docstring " .. kind .. " query")
  end
  local query = assert(vim.treesitter.query.get("python", "injections"))
  assert(vim.tbl_contains(query.captures, "injection.content"))
  equal(#doc_roots(python_fixture()), 3)
end

function cases.injection()
  local parser = python_fixture()
  equal(#doc_roots(parser), 3)
  for _, needle in ipairs({ "version (str)", "cache (dict", "key (str)" }) do
    local row, col = position(needle)
    equal(parser:language_for_range({ row, col, row, col + 1 }):lang(), "docstring")
  end
end

function cases.injection_boundaries()
  local parser = python_fixture()
  equal(#doc_roots(parser), 3)
  for _, needle in ipairs({ "fake:", "another_fake:", "block_fake:", '        """', "return []" }) do
    local row, col = position(needle)
    if needle:find('"""', 1, true) then
      col = col + 8
    end
    equal(parser:language_for_range({ row, col, row, col + 1 }):lang(), "python")
  end
end

function cases.prefixes()
  local parser = open(
    [=[
def raw():
    r"""Raw documentation.

    Args:
        path: A path.
    """
def unicode():
    u"Summary."
def binary():
    b"""Bytes are not documentation."""
def formatted():
    f"""An f-string is not documentation."""
]=],
    "python"
  )
  equal(#doc_roots(parser), 2)
end

function cases.escapes()
  local parser = open(
    [=[
def fetch():
    """A quoted \"value\" and a backslash \\.

    Args:
        key: A record key.
    """
]=],
    "python"
  )
  equal(#doc_roots(parser), 1)
  local child = assert(parser:children().docstring)
  local regions = child:included_regions()
  equal(#regions, 1)
  equal(#regions[1], 1)
  highlight("key:", "variable.parameter")
end

function cases.highlights()
  python_fixture()
  vim.treesitter.start(0, "python")
  highlight("        Args:", "markup.heading")
  highlight("key (str)", "variable.parameter")
  highlight("str): Record key", "type")
  highlight("cache (dict", "variable.member")
  highlight("ValueError:", "type")
end

function cases.highlight_priority()
  python_fixture()
  vim.treesitter.start(0, "python")
  local capture = highlight("key (str)", "variable.parameter")
  local row, col = position("key (str)")
  local host_priority = 100
  for _, item in ipairs(vim.treesitter.get_captures_at_pos(0, row, col)) do
    if item.lang == "python" then
      host_priority = math.max(host_priority, tonumber(item.metadata.priority) or 100)
    end
  end
  assert(
    (tonumber(capture.metadata.priority) or 100) > host_priority,
    "The structural highlight should override Python string highlighting"
  )
end

function cases.highlight_edits()
  local parser = python_fixture()
  highlight("key (str)", "variable.parameter")
  local row, col = position("key (str)")
  vim.api.nvim_buf_set_text(0, row, col, row, col + 3, { "identifier" })
  parser:parse(true)
  highlight("identifier (str)", "variable.parameter")
  equal(#doc_roots(parser), 3)
end

function cases.standalone_indent_2()
  indent_fixture(2, false)
end
function cases.standalone_indent_4()
  indent_fixture(4, false)
end
function cases.python_indent_2()
  indent_fixture(2, true)
end
function cases.python_indent_4()
  indent_fixture(4, true)
end

---@param width integer
local function newline_indent(width)
  open("Args:", "docstring", width)
  vim.api.nvim_win_set_cursor(0, { 1, 5 })
  vim.cmd("normal! okey: A record key.")
  equal(vim.fn.indent(2), width)
  open("Args:", "docstring", width)
  vim.api.nvim_win_set_cursor(0, { 1, 5 })
  vim.cmd("normal! A" .. vim.api.nvim_replace_termcodes("<CR>", true, false, true) .. "key: A record key.")
  equal(vim.fn.indent(2), width)
end

function cases.newline_indent_2()
  newline_indent(2)
end
function cases.newline_indent_4()
  newline_indent(4)
end

function cases.literal_indent()
  local text =
    "Args:\n    example: Literal text::\n\n        Args:\n            fake: An example.\n    actual: A real argument."
  open(text, "docstring", 4)
  vim.cmd("silent normal! gg=G")
  equal(table.concat(vim.api.nvim_buf_get_lines(0, 0, -1, false), "\n"), text)
end

function cases.standalone_folds()
  fold_fixture(false)
  fold_range("Args:", "key: A record key.")
  fold_range("Returns:", "The matching record.")
end

function cases.python_folds()
  fold_fixture(true)
  fold_range("Args:", "key: A record key.")
  fold_range("Returns:", "The matching record.")
end

function cases.fold_edits()
  local parser = fold_fixture(false)
  local row, col = position("The matching record.")
  vim.api.nvim_buf_set_lines(0, row + 1, row + 1, false, { "    Another line." })
  parser:parse(true)
  vim.cmd("silent normal! zx")
  assert(vim.wait(2000, function()
    vim.cmd("silent normal! zx")
    return vim.fn.foldlevel(row + 2) > 0
  end))
  fold_range("Returns:", "Another line.")
end

---@return vim.treesitter.LanguageTree
local function extension_fixture()
  return open(table.concat(vim.fn.readfile(root .. "/test/fixtures/extensions.txt"), "\n"), "docstring")
end

function cases.extension_highlights()
  extension_fixture()
  for _, heading in ipairs({ "Keyword Args:", "Warns:", "Examples:", "Notes:", "See Also:" }) do
    highlight(heading, "markup.heading")
  end
  highlight("value (list", "variable.parameter")
  highlight("list[str]", "type")
  highlight("package.SomeWarning", "type")
end

function cases.extension_indent()
  extension_fixture()
  local before = vim.api.nvim_buf_get_lines(0, 0, -1, false)
  vim.cmd("silent normal! gg=G")
  equal(vim.api.nvim_buf_get_lines(0, 0, -1, false), before)
end

function cases.extension_folds()
  local parser = extension_fixture()
  vim.wo.foldmethod = "expr"
  vim.wo.foldexpr = "v:lua.vim.treesitter.foldexpr()"
  vim.wo.foldminlines = 0
  vim.wo.foldlevel = 99
  parser:parse(true)
  vim.cmd("silent normal! zx")
  fold_range("Examples:", "A second example.")
  fold_range("Notes:", "A second note.")
  fold_range("See Also:", "Another function.")
end

---@return vim.treesitter.LanguageTree
local function code_fixture()
  return open(table.concat(vim.fn.readfile(root .. "/test/fixtures/code_blocks.txt"), "\n"), "docstring")
end

function cases.code_injection()
  local parser = code_fixture()
  for _, needle in ipairs({ "fenced_value", "directive_value", "def double", "return value", "double(3)" }) do
    local row, col = position(needle)
    equal(parser:language_for_range({ row, col, row, col + 1 }):lang(), "python")
  end
  for _, needle in ipairs({ ">>>", "...", "    6", '"code":', "shell_command" }) do
    local row, col = position(needle)
    col = col + #(needle:match("^%s*"))
    equal(parser:language_for_range({ row, col, row, col + 1 }):lang(), "docstring")
  end
  local count = 0
  parser:for_each_tree(function(tree, language_tree)
    if language_tree:lang() == "python" then
      assert(not tree:root():has_error(), tree:root():sexpr())
      count = count + 1
    end
  end)
  equal(count, 3)
end

function cases.code_aliases()
  for _, language in ipairs({ "python", "py", "python3", "py3" }) do
    for _, opener in ipairs({
      "```" .. language,
      "~~~" .. language,
      ".. code-block:: " .. language,
      ".. sourcecode:: " .. language,
    }) do
      local ending = opener:sub(1, 3) == "```" and "```" or opener:sub(1, 3) == "~~~" and "~~~" or ""
      local indent = ending == "" and "    " or ""
      local parser = open(opener .. "\n" .. indent .. "alias_value = 1\n" .. ending, "docstring")
      local row, col = position("alias_value")
      equal(parser:language_for_range({ row, col, row, col + 1 }):lang(), "python")
    end
  end
end

function cases.code_nested()
  local text =
    'def outer():\n    """A function.\n\n    Examples:\n        ```python\n        def double(value):\n            return value * 2\n        ```\n    """\n    return None'
  local parser = open(text, "python")
  vim.treesitter.start(0, "python")
  local row, col = position("return value")
  equal(parser:language_for_range({ row, col, row, col + 6 }):lang(), "python")
  local root = parser:language_for_range({ row, col, row, col + 1 }):parent()
  assert(root and root:lang() == "docstring")
  local found = false
  for _, capture in ipairs(vim.treesitter.get_captures_at_pos(0, row, col)) do
    if capture.lang == "python" and capture.capture:match("^keyword") then
      found = true
    end
  end
  assert(found, "Missing Python keyword highlighting inside the docstring example")
end

function cases.code_indent()
  -- nvim-treesitter currently applies Python indentation to doctest prompt lines.
  -- Keep this check on blocks whose source is directly valid Python.
  open(
    [=[
Examples:
    ```python
    def double(value):
        return value * 2
    ```

    .. code-block:: python
        :linenos:

        directive_value = 2

    ```json
    {"code": "is data"}
    ```

Notes:
    A final paragraph.
]=],
    "docstring"
  )
  local before = vim.api.nvim_buf_get_lines(0, 0, -1, false)
  vim.cmd("silent normal! gg=G")
  equal(vim.api.nvim_buf_get_lines(0, 0, -1, false), before)
end

function cases.code_edits()
  local parser = open("```python\neditable_value = 1\n```", "docstring")
  local row, col = position("editable_value")
  equal(parser:language_for_range({ row, col, row, col + 1 }):lang(), "python")
  vim.api.nvim_buf_set_text(0, 0, 3, 0, 9, { "json" })
  parser:parse(true)
  equal(parser:language_for_range({ row, col, row, col + 1 }):lang(), "docstring")
  vim.api.nvim_buf_set_text(0, 0, 3, 0, 7, { "python" })
  parser:parse(true)
  equal(parser:language_for_range({ row, col, row, col + 1 }):lang(), "python")
end

function cases.code_folds()
  local parser = open(
    [=[
Examples:
    ~~~python
    fenced_value = 1
    ~~~~

    .. code-block:: python

        directive_value = 2

    >>> total = 1 + 2
    >>> print(total)
    3

Notes:
    A final paragraph.
]=],
    "docstring"
  )
  vim.wo.foldmethod = "expr"
  vim.wo.foldexpr = "v:lua.vim.treesitter.foldexpr()"
  vim.wo.foldminlines = 1
  vim.wo.foldlevel = 99
  parser:parse(true)
  vim.cmd("silent normal! zx")
  fold_range("~~~python", "~~~~")
  fold_range(".. code-block:: python", "directive_value")
  fold_range(">>> total =", ">>> print(total)")
end

function cases.code_multiline()
  local parser = open(
    "Examples:\n    >>> values = [\n    ...     1,\n    ...     2,\n    ... ]\n    >>> sum(values)\n    3\n    >>> ",
    "docstring"
  )
  local count = 0
  parser:for_each_tree(function(tree, language_tree)
    if language_tree:lang() == "python" then
      assert(not tree:root():has_error(), tree:root():sexpr())
      count = count + 1
    end
  end)
  equal(count, 1)
  local row, col = position("sum(values)")
  equal(parser:language_for_range({ row, col, row, col + 1 }):lang(), "python")
end

function cases.inline_highlights()
  local parser = open(
    [=[def demo(*args):
    """Use ``value`` with *care* and **attention**.

    See :py:class:`Path`, `details <https://example.org>`_, and https://example.org.

    Args:
        *args (tuple[str, ...]): The ``inputs``.
    """
    return args
]=],
    "python"
  )
  equal(#doc_roots(parser), 1)
  for _, item in ipairs({
    { "value", "markup.raw" },
    { "care", "markup.italic" },
    { "attention", "markup.strong" },
    { "Path", "markup.link" },
    { "details", "markup.link" },
    { "https://example.org.", "markup.link.url" },
    { "*args (tuple", "variable.parameter" },
    { "inputs", "markup.raw" },
  }) do
    local capture = highlight(item[1], item[2])
    assert(tonumber(capture.metadata.priority) > 100, item[1])
  end
  highlight("value", "nospell")
end

function cases.inline_conceal()
  local original_level, original_cursor = vim.wo.conceallevel, vim.wo.concealcursor
  local parser = open("Use ``value`` with *care* and **attention**; see :class:`Path`.", "docstring")
  local query = assert(vim.treesitter.query.get("docstring", "highlights"))
  local ranges = {}
  for id, node, metadata in query:iter_captures(parser:parse()[1]:root(), 0) do
    if query.captures[id] == "conceal" then
      equal(metadata.conceal or (metadata[id] or {}).conceal, "")
      ranges[#ranges + 1] = vim.treesitter.get_node_text(node, 0)
    end
  end
  equal(ranges, { "``", "``", "*", "*", "**", "**", "`", "`" })
  equal(vim.wo.conceallevel, original_level)
  equal(vim.wo.concealcursor, original_cursor)
end

function cases.inline_edits()
  local parser = open("Use *care* here.", "docstring")
  highlight("care", "markup.italic")
  vim.api.nvim_buf_set_lines(0, 0, -1, false, { "Use *care here." })
  local tree = parser:parse(true)[1]
  assert(not tree:root():has_error(), tree:root():sexpr())
  assert(not tree:root():sexpr():find("emphasis", 1, true))
  vim.api.nvim_buf_set_lines(0, 0, -1, false, { "Use **care** here." })
  parser:parse(true)
  highlight("care", "markup.strong")
end

---@param injected boolean
local function spell_fixture(injected)
  local lines = {
    "This is wrngsummary.",
    "",
    "Args:",
    "    qzxparam (QzxType): This is wrngdescription.",
    "",
    "Attributes:",
    "    qzxattribute (QzxAttributeType): This is wrngattribute.",
    "",
    "Returns:",
    "    QzxReturnType: This is wrngreturn.",
    "",
    "Raises:",
    "    QzxException: This is wrngexception.",
    "",
    "Notes:",
    "    Use :qzxrole:`qzxtarget` and ``qzxliteral``.",
    "    Use *wrngemphasis* and **wrngstrong**.",
    "    See `qzxreference`_ and https://example.org/qzxurl.",
    "",
    "Examples:",
    "    ```python",
    "    qzxfenced = 1",
    "    # qzxcomment",
    "    ```",
    "",
    "    ```qzxlanguage",
    "    qzxunknown",
    "    ```",
    "",
    "    ```",
    "    qzxunlabelled",
    "    ```",
    "",
    "    .. code-block:: python",
    "        :qzxoption:",
    "",
    "        qzxdirective = 2",
    "",
    "    >>> qzxdoctest = 3",
    "",
    "This is wrngfinal.",
  }
  if injected then
    for i, line in ipairs(lines) do
      lines[i] = "    " .. line
    end
    lines[1] = '    """' .. lines[1]:sub(5)
    table.insert(lines, 1, "def demo():")
    lines[#lines + 1] = '    """'
    lines[#lines + 1] = "    pass"
  end
  local parser = open(table.concat(lines, "\n"), injected and "python" or "docstring")
  equal(#doc_roots(parser), 1)
  vim.wo.spell = true
  vim.bo.spelllang = "en"
  vim.bo.spellcapcheck = ""
  vim.treesitter.start(0, vim.bo.filetype)
  for _, item in ipairs({
    { "wrngsummary", "wrngsummary" },
    { "qzxparam", "wrngdescription" },
    { "QzxType", "wrngdescription" },
    { "qzxattribute", "wrngattribute" },
    { "QzxAttributeType", "wrngattribute" },
    { "QzxReturnType", "wrngreturn" },
    { "QzxException", "wrngexception" },
    { "qzxrole", "" },
    { "qzxtarget", "" },
    { "qzxliteral", "" },
    { "wrngemphasis", "wrngemphasis" },
    { "wrngstrong", "wrngstrong" },
    { "qzxreference", "" },
    { "qzxurl", "" },
    { "qzxfenced", "" },
    { "qzxcomment", "" },
    { "qzxlanguage", "" },
    { "qzxunknown", "" },
    { "qzxunlabelled", "" },
    { "qzxoption", "" },
    { "qzxdirective", "" },
    { "qzxdoctest", "" },
    { "wrngfinal", "wrngfinal" },
  }) do
    local row, col = position(item[1])
    vim.api.nvim_win_set_cursor(0, { row + 1, col })
    local found = vim.fn.spellbadword()[1]
    assert(found == item[2], item[1] .. ": expected " .. vim.inspect(item[2]) .. ", got " .. vim.inspect(found))
  end
end

function cases.spell_standalone()
  spell_fixture(false)
end

function cases.spell_python()
  spell_fixture(true)
end

local name = assert(arg[1], "Expected a test case name")
assert(cases[name], "Unknown case: " .. name)()
local messages = vim.fn.execute("messages")
assert(not messages:find("stack traceback", 1, true), messages)
assert(not messages:match("\nE%d+:"), messages)
vim.cmd("qa!")
