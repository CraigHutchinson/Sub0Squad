import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { constant, factorial, callFunction } from '../src/functions.js';

describe('constant', () => {
  it('returns pi for "pi"', () => {
    assert.strictEqual(constant('pi'), Math.PI);
  });

  it('returns e for "e"', () => {
    assert.strictEqual(constant('e'), Math.E);
  });

  it('returns tau for "tau"', () => {
    assert.strictEqual(constant('tau'), Math.PI * 2);
  });

  it('returns undefined for unknown name', () => {
    assert.strictEqual(constant('unknown'), undefined);
  });
});

describe('factorial', () => {
  it('returns 1 for 0', () => {
    assert.strictEqual(factorial(0), 1);
  });

  it('returns 1 for 1', () => {
    assert.strictEqual(factorial(1), 1);
  });

  it('returns 120 for 5', () => {
    assert.strictEqual(factorial(5), 120);
  });

  it('throws for negative numbers', () => {
    assert.throws(() => factorial(-1), Error);
  });

  it('throws for non-integers', () => {
    assert.throws(() => factorial(1.5), Error);
  });
});

describe('callFunction', () => {
  it('handles one-argument functions: sqrt', () => {
    assert.strictEqual(callFunction('sqrt', [4]), 2);
  });

  it('handles one-argument functions: abs', () => {
    assert.strictEqual(callFunction('abs', [-5]), 5);
  });

  it('handles one-argument functions: round', () => {
    assert.strictEqual(callFunction('round', [3.5]), 4);
  });

  it('handles two-argument functions: pow', () => {
    assert.strictEqual(callFunction('pow', [2, 3]), 8);
  });

  it('handles two-argument functions: min', () => {
    assert.strictEqual(callFunction('min', [1, 5]), 1);
  });

  it('handles two-argument functions: max', () => {
    assert.strictEqual(callFunction('max', [1, 5]), 5);
  });

  it('handles trig functions in radians', () => {
    const sinZero = callFunction('sin', [0]);
    assert.strictEqual(sinZero, 0);
  });

  it('handles trig functions in degrees', () => {
    const sin30 = callFunction('sin', [30], true);
    assert.ok(Math.abs(sin30 - 0.5) < 1e-9, `Expected sin(30deg) ≈ 0.5, got ${sin30}`);
  });

  it('handles inverse trig functions in degrees', () => {
    const asin05 = callFunction('asin', [0.5], true);
    assert.ok(Math.abs(asin05 - 30) < 1e-9, `Expected asin(0.5) in deg ≈ 30, got ${asin05}`);
  });

  it('handles log functions', () => {
    assert.strictEqual(callFunction('log', [100]), 2);
    assert.ok(Math.abs(callFunction('ln', [Math.E]) - 1) < 1e-9);
    assert.strictEqual(callFunction('log2', [8]), 3);
  });

  it('throws for unknown function name', () => {
    assert.throws(() => callFunction('unknown', []), Error);
  });

  it('throws for wrong argument count (one-arg function with two args)', () => {
    assert.throws(() => callFunction('sqrt', [4, 5]), Error);
  });

  it('throws for wrong argument count (two-arg function with one arg)', () => {
    assert.throws(() => callFunction('pow', [2]), Error);
  });

  it('throws for wrong argument count (two-arg function with three args)', () => {
    assert.throws(() => callFunction('pow', [2, 3, 4]), Error);
  });

  it('throws for sqrt of negative number', () => {
    assert.throws(() => callFunction('sqrt', [-1]), Error);
  });

  it('throws for ln of non-positive number', () => {
    assert.throws(() => callFunction('ln', [0]), Error);
    assert.throws(() => callFunction('ln', [-1]), Error);
  });

  it('throws for log of non-positive number', () => {
    assert.throws(() => callFunction('log', [0]), Error);
    assert.throws(() => callFunction('log', [-1]), Error);
  });

  it('throws for log2 of non-positive number', () => {
    assert.throws(() => callFunction('log2', [0]), Error);
    assert.throws(() => callFunction('log2', [-1]), Error);
  });

  it('handles floating point precision for trig', () => {
    const sinPi = callFunction('sin', [Math.PI]);
    assert.ok(Math.abs(sinPi) < 1e-9, `Expected sin(π) ≈ 0, got ${sinPi}`);
  });

  it('handles floating point precision for exp', () => {
    const exp1 = callFunction('exp', [1]);
    assert.ok(Math.abs(exp1 - Math.E) < 1e-9, `Expected e ≈ ${Math.E}, got ${exp1}`);
  });

  it('handles hyperbolic functions', () => {
    const sinh0 = callFunction('sinh', [0]);
    assert.strictEqual(sinh0, 0);
    
    const cosh0 = callFunction('cosh', [0]);
    assert.strictEqual(cosh0, 1);
    
    const tanh0 = callFunction('tanh', [0]);
    assert.strictEqual(tanh0, 0);
  });

  it('handles floor and ceil', () => {
    assert.strictEqual(callFunction('floor', [3.7]), 3);
    assert.strictEqual(callFunction('ceil', [3.2]), 4);
  });

  it('handles cbrt', () => {
    assert.strictEqual(callFunction('cbrt', [27]), 3);
    assert.strictEqual(callFunction('cbrt', [-8]), -2);
  });

  it('throws for wrong argument count (one-arg function with zero args)', () => {
    assert.throws(() => callFunction('sqrt', []), Error);
  });

  it('throws for wrong argument count (two-arg function with zero args)', () => {
    assert.throws(() => callFunction('pow', []), Error);
  });

  it('throws for wrong argument count (one-arg function with three args)', () => {
    assert.throws(() => callFunction('sin', [1, 2, 3]), Error);
  });

  it('handles tan in radians', () => {
    const tan0 = callFunction('tan', [0]);
    assert.strictEqual(tan0, 0);
  });

  it('handles tan in degrees', () => {
    const tan45 = callFunction('tan', [45], true);
    assert.ok(Math.abs(tan45 - 1) < 1e-9, `Expected tan(45deg) ≈ 1, got ${tan45}`);
  });

  it('handles acos in radians', () => {
    const acos1 = callFunction('acos', [1]);
    assert.strictEqual(acos1, 0);
  });

  it('handles acos in degrees', () => {
    const acos1 = callFunction('acos', [1], true);
    assert.strictEqual(acos1, 0);
  });

  it('handles atan in radians', () => {
    const atan0 = callFunction('atan', [0]);
    assert.strictEqual(atan0, 0);
  });

  it('handles atan in degrees', () => {
    const atan0 = callFunction('atan', [0], true);
    assert.strictEqual(atan0, 0);
  });

  it('handles cos in radians', () => {
    const cos0 = callFunction('cos', [0]);
    assert.strictEqual(cos0, 1);
  });

  it('handles cos in degrees', () => {
    const cos90 = callFunction('cos', [90], true);
    assert.ok(Math.abs(cos90) < 1e-9, `Expected cos(90deg) ≈ 0, got ${cos90}`);
  });

  it('handles asin in radians', () => {
    const asin0 = callFunction('asin', [0]);
    assert.strictEqual(asin0, 0);
  });

  it('handles asin in degrees', () => {
    const asin0 = callFunction('asin', [0], true);
    assert.strictEqual(asin0, 0);
  });
});
