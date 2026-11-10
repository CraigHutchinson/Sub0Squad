import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { tokenize } from '../src/lexer.js';

describe('tokenize', () => {
  test('returns empty array for empty string', () => {
    const result = tokenize('');
    assert.deepStrictEqual(result, []);
  });

  test('tokenizes integer numbers', () => {
    const result = tokenize('12');
    assert.deepStrictEqual(result, [{ type: 'num', value: 12, pos: 0 }]);
  });

  test('tokenizes decimal numbers', () => {
    const result = tokenize('1.5');
    assert.deepStrictEqual(result, [{ type: 'num', value: 1.5, pos: 0 }]);
  });

  test('tokenizes numbers with exponent', () => {
    const result = tokenize('1.5e2');
    assert.deepStrictEqual(result, [{ type: 'num', value: 150, pos: 0 }]);
  });

  test('tokenizes numbers starting with dot', () => {
    const result = tokenize('.5');
    assert.deepStrictEqual(result, [{ type: 'num', value: 0.5, pos: 0 }]);
  });

  test('tokenizes identifiers', () => {
    const result = tokenize('foo_bar123');
    assert.deepStrictEqual(result, [{ type: 'id', value: 'foo_bar123', pos: 0 }]);
  });

  test('tokenizes operators', () => {
    const ops = ['+', '-', '*', '/', '%', '^', '!'];
    for (const op of ops) {
      const result = tokenize(op);
      assert.deepStrictEqual(result, [{ type: 'op', value: op, pos: 0 }]);
    }
  });

  test('tokenizes left and right parentheses', () => {
    const resultLp = tokenize('(');
    assert.deepStrictEqual(resultLp, [{ type: 'lp', value: '(', pos: 0 }]);

    const resultRp = tokenize(')');
    assert.deepStrictEqual(resultRp, [{ type: 'rp', value: ')', pos: 0 }]);
  });

  test('tokenizes comma', () => {
    const result = tokenize(',');
    assert.deepStrictEqual(result, [{ type: 'comma', value: ',', pos: 0 }]);
  });

  test('skips whitespace', () => {
    const result = tokenize('  12  ');
    assert.deepStrictEqual(result, [{ type: 'num', value: 12, pos: 2 }]);
  });

  test('tokenizes multiple tokens with positions', () => {
    const result = tokenize('1 + 2');
    assert.deepStrictEqual(result, [
      { type: 'num', value: 1, pos: 0 },
      { type: 'op', value: '+', pos: 2 },
      { type: 'num', value: 2, pos: 4 }
    ]);
  });

  test('throws Error for unexpected character', () => {
    assert.throws(() => tokenize('@'), (err) => {
      assert.ok(err instanceof Error);
      assert.ok(err.message.includes('@'));
      assert.ok(err.message.includes('0'));
      return true;
    });
  });
});
