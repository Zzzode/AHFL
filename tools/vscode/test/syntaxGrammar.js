const assert = require('assert');
const fs = require('fs');
const path = require('path');

const extensionRoot = path.resolve(__dirname, '..');
const grammarPath = path.join(extensionRoot, 'syntaxes', 'ahfl.tmLanguage.json');

function collectStrings(value, out = []) {
  if (typeof value === 'string') {
    out.push(value);
    return out;
  }
  if (Array.isArray(value)) {
    for (const item of value) {
      collectStrings(item, out);
    }
    return out;
  }
  if (value && typeof value === 'object') {
    for (const item of Object.values(value)) {
      collectStrings(item, out);
    }
  }
  return out;
}

function requireWord(joinedGrammar, word) {
  assert.ok(
    joinedGrammar.includes(word),
    `expected AHFL TextMate grammar to cover ${word}`
  );
}

function forbidSubstring(joinedGrammar, needle, reason) {
  assert.ok(!joinedGrammar.includes(needle), `${reason}: ${needle}`);
}

function forbidWord(joinedGrammar, word, reason) {
  const escaped = word.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  assert.ok(
    !new RegExp(`(^|[^A-Za-z0-9_])${escaped}([^A-Za-z0-9_]|$)`).test(joinedGrammar),
    `${reason}: ${word}`
  );
}

function main() {
  const grammar = JSON.parse(fs.readFileSync(grammarPath, 'utf8'));
  assert.strictEqual(grammar.scopeName, 'source.ahfl');
  assert.ok(grammar.repository, 'expected repository section');
  assert.ok(!grammar.repository.chars, 'AHFL has no char literal grammar');

  const joined = collectStrings(grammar).join('\n');
  const requiredWords = [
    'pub',
    'use',
    'as',
    'where',
    'effect',
    'Pure',
    'Nondet',
    'decreases',
    'mut',
    'self',
    'assert',
    'unwrap',
    'requires',
    'unreachable',
    'trait',
    'impl',
    'capability',
    'agent',
    'workflow',
    'contract',
    'flow',
    'input',
    'output',
    'context',
    'states',
    'initial',
    'final',
    'capabilities',
    'quota',
    'max_tool_calls',
    'max_execution_time',
    'transition',
    'node',
    'after',
    'safety',
    'liveness',
    'state',
    'with',
    'retry',
    'retry_on',
    'timeout',
    'ensures',
    'invariant',
    'forbid',
    'always',
    'eventually',
    'next',
    'until',
    'called',
    'in_state',
    'running',
    'completed',
    'domain',
    'idempotency',
    'receipt',
    'compensation',
    'policy',
    'external_side_effect',
    'durable_write',
    'financial_write',
    'safe_if_idempotent',
    'Optional',
    'List',
    'Set',
    'Map',
    'Fn',
    'Unit',
    'Bool',
    'Int',
    'Float',
    'String',
    'UUID',
    'Timestamp',
    'Duration',
    'Decimal',
    'builtin',
  ];
  for (const word of requiredWords) {
    requireWord(joined, word);
  }

  const forbiddenSubstrings = [
    ['keyword.control.loop', 'loop keywords are not AHFL syntax'],
    ['keyword.operator.bitwise', 'bitwise operators are not AHFL syntax'],
    ['keyword.other.ffi', 'extern-style FFI is not AHFL syntax'],
    ['keyword.other.doc', 'doc keyword is not AHFL syntax'],
    ['variable.language.Self', 'Self keyword is not AHFL syntax'],
    ['constant.language.null', 'null literal is not AHFL syntax'],
    ['string.quoted.single', 'single-quoted char/string literals are not AHFL syntax'],
    ['hexadecimal', 'hex integer literals are not AHFL syntax'],
    ['octal', 'octal integer literals are not AHFL syntax'],
    ['binary', 'binary integer literals are not AHFL syntax'],
    ['\\+=', 'compound assignment is not AHFL syntax'],
    ['\\*\\*', 'exponent operator is not AHFL syntax'],
    ['#\\[', 'Rust-style attributes are not AHFL syntax'],
  ];
  for (const [needle, reason] of forbiddenSubstrings) {
    forbidSubstring(joined, needle, reason);
  }
  const forbiddenWords = [
    ['extern', 'extern keyword is not AHFL syntax'],
    ['while', 'while loop is not AHFL syntax'],
    ['Never', 'Never is not a current AHFL primitive type'],
    ['Result', 'Result must be highlighted as a user/std symbol, not a primitive'],
  ];
  for (const [word, reason] of forbiddenWords) {
    forbidWord(joined, word, reason);
  }

  console.log('Verified AHFL TextMate grammar follows the current syntax surface');
}

try {
  main();
} catch (error) {
  console.error(error);
  process.exit(1);
}
