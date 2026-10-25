import assert from 'node:assert/strict';
import { existsSync, mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { before, test } from 'node:test';
import { buildParser, root, run } from './helpers.mjs';

before(() => {
  assert.ok(existsSync(resolve(root, '.tests/python.so')) &&
    existsSync(resolve(root, '.tests/deps/nvim-treesitter/lua/nvim-treesitter/indent.lua')),
  'Run npm run test:setup to install the pinned test dependencies.');
  buildParser();
});

const cases = {
  query_loading: 'load all query files in Neovim',
  injection: 'inject separate module, class, and async-function docstrings',
  injection_boundaries: 'exclude delimiters, ordinary strings, and arbitrary blocks',
  prefixes: 'support raw and Unicode strings, excluding bytes and f-strings',
  escapes: 'keep escape sequences inside the injection range',
  highlights: 'highlight structural captures inside a Python buffer',
  highlight_priority: 'give docstring structure priority over host string highlighting',
  highlight_edits: 'update captures after edits without reopening the buffer',
  standalone_indent_2: 'preserve valid standalone indentation with shiftwidth=2',
  standalone_indent_4: 'preserve valid standalone indentation with shiftwidth=4',
  python_indent_2: 'preserve Python docstrings and closing quotes with shiftwidth=2',
  python_indent_4: 'preserve Python docstrings and closing quotes with shiftwidth=4',
  newline_indent_2: 'indent a new section body with shiftwidth=2',
  newline_indent_4: 'indent a new section body with shiftwidth=4',
  literal_indent: 'preserve a literal block inside a field description',
  standalone_folds: 'fold standalone sections without the next heading or blank separator',
  python_folds: 'fold injected sections without the closing Python quotes',
  fold_edits: 'update section folds after edits',
};

for (const [name, title] of Object.entries(cases)) {
  test(title, () => {
    const state = resolve(root, '.tests/state', name);
    mkdirSync(state, { recursive: true });
    run(process.env.NVIM ?? 'nvim', ['--clean', '--headless', '-n', '-i', 'NONE', '-l', 'test/neovim.lua', name], {
      env: { ...process.env,
        XDG_CONFIG_HOME: `${state}/config`, XDG_DATA_HOME: `${state}/data`,
        XDG_CACHE_HOME: `${state}/cache`, XDG_STATE_HOME: `${state}/state`,
        NVIM_LOG_FILE: `${state}/nvim.log`,
      },
      timeout: 15000,
    });
  });
}
