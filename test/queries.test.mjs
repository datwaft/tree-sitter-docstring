import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { before, test } from 'node:test';
import { buildParser, parser, root, run } from './helpers.mjs';

const fixture = `${root}/test/fixtures/docstring.txt`;
const source = readFileSync(fixture, 'utf8');

before(buildParser);

test('grammar corpus', () => run('tree-sitter', ['test']));

function captures(kind) {
  const output = run('tree-sitter', [
    'query', '--captures', '--lib-path', parser, '--lang-name', 'docstring',
    `queries/docstring/${kind}.scm`, fixture,
  ]);
  return [...output.matchAll(/capture:\s*\d+ - ([\w.]+), start: \((\d+), (\d+)\), end: \((\d+), (\d+)\)/g)]
    .map((match) => ({ name: match[1], start: [+match[2], +match[3]], end: [+match[4], +match[5]] }));
}

function range(needle, token = needle) {
  const offset = source.indexOf(needle);
  assert.notEqual(offset, -1, needle);
  const before = source.slice(0, offset).split('\n');
  const start = [before.length - 1, Buffer.byteLength(before.at(-1))];
  return { start, end: [start[0], start[1] + Buffer.byteLength(token)] };
}

function hasCapture(found, name, needle, token) {
  assert.ok(found.some((capture) => capture.name === name &&
    JSON.stringify(capture.start) === JSON.stringify(range(needle, token).start) &&
    JSON.stringify(capture.end) === JSON.stringify(range(needle, token).end)),
  `${name} should capture exactly ${JSON.stringify(token ?? needle)} in ${JSON.stringify(needle)}`);
}

function textOf(capture) {
  const lines = source.split('\n');
  return lines.slice(capture.start[0], capture.end[0] + 1).map((line, index, all) => {
    const bytes = Buffer.from(line);
    return bytes.subarray(index === 0 ? capture.start[1] : 0,
      index === all.length - 1 ? capture.end[1] : bytes.length).toString();
  }).join('\n');
}

test('highlight section headings, excluding the literal example', () => {
  assert.deepEqual(captures('highlights').filter((c) => c.name === 'markup.heading').map(textOf),
    ['Args:', 'Returns:', 'Returns:', 'Yields:', 'Raises:', 'Attributes:']);
});

test('highlight exact parameter ranges, including stars and UTF-8', () => {
  const found = captures('highlights');
  for (const [needle, token] of [['path (str)', 'path'], ['*values:', '*values'],
    ['**options (dict', '**options'], ['año (int)', 'año']]) {
    hasCapture(found, 'variable.parameter', needle, token);
  }
  assert.equal(found.filter((c) => c.name === 'variable.parameter').length, 4);
});

test('highlight attribute names separately from parameter names', () => {
  hasCapture(captures('highlights'), 'variable.member', 'enabled (bool)', 'enabled');
});

test('highlight exact types and exception names', () => {
  const found = captures('highlights');
  for (const [needle, token] of [['str): File', 'str'], ['dict[str, object]', 'dict[str, object]'],
    ['int): Año', 'int'], ['list[Record]', 'list[Record]'], [':class:`package.Record`', ':class:`package.Record`'],
    ['package.ReadError', 'package.ReadError'], ['bool): Whether', 'bool']]) {
    hasCapture(found, 'type', needle, token);
  }
  assert.equal(found.filter((c) => c.name === 'type').length, 7);
});

test('highlight field punctuation without treating prose colons as delimiters', () => {
  const found = captures('highlights');
  hasCapture(found, 'punctuation.bracket', '(str)', '(');
  hasCapture(found, 'punctuation.bracket', '): File', ')');
  hasCapture(found, 'punctuation.delimiter', ': File', ':');
  const prose = range(': value.', ':');
  assert.ok(!found.some((c) => c.name === 'punctuation.delimiter' &&
    JSON.stringify(c.start) === JSON.stringify(prose.start)));
});

test('preserve documentation highlighting as the base capture', () => {
  const found = captures('highlights');
  assert.ok(found.some((c) => c.name === 'string.documentation' &&
    JSON.stringify(c.start) === '[0,0]' && textOf(c) === source));
});

test('indent queries compile and use recognized Neovim captures', () => {
  const found = captures('indents');
  assert.ok(found.length > 0, 'indent queries should have captures');
  const allowed = new Set(['indent.auto', 'indent.begin', 'indent.end', 'indent.dedent',
    'indent.branch', 'indent.ignore', 'indent.align', 'indent.zero']);
  for (const capture of found) {
    assert.ok(capture.name.startsWith('_') || allowed.has(capture.name), capture.name);
  }
});

test('fold queries select real sections rather than literal examples', () => {
  const found = captures('folds').filter((c) => c.name === 'fold');
  assert.deepEqual(found.map((c) => textOf(c).split('\n')[0]),
    ['Args:', 'Returns:', 'Returns:', 'Yields:', 'Raises:', 'Attributes:']);
});
