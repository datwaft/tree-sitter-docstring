import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';

export const root = resolve(import.meta.dirname, '..');
export const parser = resolve(root, '.tests/docstring.so');

export function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: root,
    encoding: 'utf8',
    timeout: 30000,
    ...options,
  });
  assert.ifError(result.error);
  assert.equal(result.status, 0, `${command} ${args.join(' ')}\n${result.stdout}\n${result.stderr}`);
  return result.stdout;
}

export function buildParser() {
  mkdirSync(resolve(root, '.tests'), { recursive: true });
  run('tree-sitter', ['build', '--output', parser]);
}
