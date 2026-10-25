import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { buildParser, root, run } from './helpers.mjs';

const dependencies = [
  ['python', 'tree-sitter/tree-sitter-python', 'v0.25.0'],
  ['nvim-treesitter', 'nvim-treesitter/nvim-treesitter', 'a2f694478be07f89c2d03cc17858a8745a400c4b'],
];

for (const [name, repository, revision] of dependencies) {
  const directory = resolve(root, '.tests/deps', name);
  const stamp = resolve(directory, '.revision');
  if (existsSync(stamp) && readFileSync(stamp, 'utf8') === revision) continue;
  const archive = resolve(root, '.tests', `${name}.tar.gz`);
  mkdirSync(directory, { recursive: true });
  run('curl', ['--fail', '--location', '--silent', '--show-error',
    `https://codeload.github.com/${repository}/tar.gz/${revision}`, '--output', archive], { timeout: 120000 });
  run('tar', ['-xzf', archive, '--strip-components=1', '-C', directory]);
  writeFileSync(stamp, revision);
}
buildParser();
run('tree-sitter', ['build', '--output', resolve(root, '.tests/python.so'), resolve(root, '.tests/deps/python')]);
console.log('Test dependencies and parsers are ready.');
