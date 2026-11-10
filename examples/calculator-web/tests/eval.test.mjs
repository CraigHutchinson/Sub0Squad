import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { evaluate } from '../src/calc.js';

const TOLERANCE = 1e-9;

function approxEqual(actual, expected, message = '') {
  assert.ok(
    Math.abs(actual - expected) < TOLERANCE,
    `Expected ${expected} but got ${actual} (${message})`
  );
}

function expectThrows(expr, message, errorType = Error) {
  assert.throws(() => evaluate(expr), errorType, message);
}

describe('evaluate', () => {
  describe('basic arithmetic', () => {
    it('adds two numbers', () => {
      approxEqual(evaluate('1 + 2'), 3);
    });

    it('subtracts two numbers', () => {
      approxEqual(evaluate('5 - 3'), 2);
    });

    it('multiplies two numbers', () => {
      approxEqual(evaluate('4 * 3'), 12);
    });

    it('divides two numbers', () => {
      approxEqual(evaluate('10 / 2'), 5);
    });

    it('modulus two numbers', () => {
      approxEqual(evaluate('10 % 3'), 1);
    });
  });

  describe('precedence', () => {
    it('multiplication binds tighter than addition', () => {
      approxEqual(evaluate('1 + 2 * 3'), 7);
    });

    it('parentheses override precedence', () => {
      approxEqual(evaluate('(1 + 2) * 3'), 9);
    });

    it('handles nested parentheses', () => {
      approxEqual(evaluate('((1 + 2) * (3 + 4))'), 21);
    });
  });

  describe('exponentiation associativity', () => {
    it('exponentiation is right-associative: 2^3^2 is 2^(3^2) = 512', () => {
      approxEqual(evaluate('2 ^ 3 ^ 2'), 512);
    });

    it('unary minus has lower precedence than exponentiation: -2^2 is -(2^2) = -4', () => {
      approxEqual(evaluate('-2 ^ 2'), -4);
    });

    it('parenthesized unary minus: (-2)^2 is 4', () => {
      approxEqual(evaluate('(-2) ^ 2'), 4);
    });
  });

  describe('factorial', () => {
    it('factorial of 0 is 1', () => {
      approxEqual(evaluate('0!'), 1);
    });

    it('factorial of 5 is 120', () => {
      approxEqual(evaluate('5!'), 120);
    });

    it('factorial binds tighter than addition: 5! + 1 is 121', () => {
      approxEqual(evaluate('5! + 1'), 121);
    });
  });

  describe('numbers', () => {
    it('handles decimal numbers', () => {
      approxEqual(evaluate('1.5 + 2.5'), 4);
    });

    it('handles leading dot decimals', () => {
      approxEqual(evaluate('.5 + .5'), 1);
    });

    it('handles scientific notation', () => {
      approxEqual(evaluate('1.5e2'), 150);
    });
  });

  describe('constants', () => {
    it('pi is approximately 3.141592653589793', () => {
      approxEqual(evaluate('pi'), Math.PI);
    });

    it('e is approximately 2.718281828459045', () => {
      approxEqual(evaluate('e'), Math.E);
    });

    it('tau is 2 * pi', () => {
      approxEqual(evaluate('tau'), 2 * Math.PI);
    });
  });

  describe('trigonometry (radians)', () => {
    it('sin(0) is 0', () => {
      approxEqual(evaluate('sin(0)'), 0);
    });

    it('cos(0) is 1', () => {
      approxEqual(evaluate('cos(0)'), 1);
    });

    it('tan(pi/4) is approximately 1', () => {
      approxEqual(evaluate('tan(pi / 4)'), 1);
    });

    it('asin(0) is 0', () => {
      approxEqual(evaluate('asin(0)'), 0);
    });

    it('acos(1) is 0', () => {
      approxEqual(evaluate('acos(1)'), 0);
    });

    it('atan(0) is 0', () => {
      approxEqual(evaluate('atan(0)'), 0);
    });
  });

  describe('trigonometry (degrees)', () => {
    it('sin(90 degrees) is 1', () => {
      approxEqual(evaluate('sin(90)', { degrees: true }), 1);
    });

    it('cos(0 degrees) is 1', () => {
      approxEqual(evaluate('cos(0)', { degrees: true }), 1);
    });

    it('tan(45 degrees) is 1', () => {
      approxEqual(evaluate('tan(45)', { degrees: true }), 1);
    });

    it('asin(1) in degrees is 90', () => {
      approxEqual(evaluate('asin(1)', { degrees: true }), 90);
    });

    it('acos(0) in degrees is 90', () => {
      approxEqual(evaluate('acos(0)', { degrees: true }), 90);
    });

    it('atan(1) in degrees is 45', () => {
      approxEqual(evaluate('atan(1)', { degrees: true }), 45);
    });
  });

  describe('logarithms', () => {
    it('ln(e) is 1', () => {
      approxEqual(evaluate('ln(e)'), 1);
    });

    it('log(10) is 1', () => {
      approxEqual(evaluate('log(10)'), 1);
    });

    it('log2(8) is 3', () => {
      approxEqual(evaluate('log2(8)'), 3);
    });
  });

  describe('two-argument functions', () => {
    it('pow(2, 3) is 8', () => {
      approxEqual(evaluate('pow(2, 3)'), 8);
    });

    it('min(1, 2) is 1', () => {
      approxEqual(evaluate('min(1, 2)'), 1);
    });

    it('max(1, 2) is 2', () => {
      approxEqual(evaluate('max(1, 2)'), 2);
    });
  });

  describe('other functions', () => {
    it('sqrt(4) is 2', () => {
      approxEqual(evaluate('sqrt(4)'), 2);
    });

    it('cbrt(8) is 2', () => {
      approxEqual(evaluate('cbrt(8)'), 2);
    });

    it('abs(-5) is 5', () => {
      approxEqual(evaluate('abs(-5)'), 5);
    });

    it('exp(1) is e', () => {
      approxEqual(evaluate('exp(1)'), Math.E);
    });

    it('floor(1.5) is 1', () => {
      approxEqual(evaluate('floor(1.5)'), 1);
    });

    it('ceil(1.5) is 2', () => {
      approxEqual(evaluate('ceil(1.5)'), 2);
    });

    it('round(1.5) is 2', () => {
      approxEqual(evaluate('round(1.5)'), 2);
    });

    it('fact(5) is 120', () => {
      approxEqual(evaluate('fact(5)'), 120);
    });

    it('sinh(0) is 0', () => {
      approxEqual(evaluate('sinh(0)'), 0);
    });

    it('cosh(0) is 1', () => {
      approxEqual(evaluate('cosh(0)'), 1);
    });

    it('tanh(0) is 0', () => {
      approxEqual(evaluate('tanh(0)'), 0);
    });
  });

  describe('unary operators', () => {
    it('unary plus: +5 is 5', () => {
      approxEqual(evaluate('+5'), 5);
    });

    it('unary minus: -5 is -5', () => {
      approxEqual(evaluate('-5'), -5);
    });

    it('double unary minus: --5 is 5', () => {
      approxEqual(evaluate('--5'), 5);
    });
  });

  describe('error cases', () => {
    it('division by zero throws', () => {
      expectThrows('1 / 0', 'Division by zero');
    });

    it('modulo by zero throws', () => {
      expectThrows('1 % 0', 'Modulo by zero');
    });

    it('unknown name throws', () => {
      expectThrows('x', 'Unknown name');
    });

    it('unknown function throws', () => {
      expectThrows('foo()', 'Unknown function');
    });

    it('wrong argument count for sin throws', () => {
      expectThrows('sin(1, 2)', 'Wrong argument count');
    });

    it('wrong argument count for pow throws', () => {
      expectThrows('pow(1)', 'Wrong argument count');
    });

    it('sqrt of negative throws', () => {
      expectThrows('sqrt(-1)', 'Sqrt of negative');
    });

    it('unbalanced parentheses throws', () => {
      expectThrows('(1 + 2', 'Unbalanced parentheses');
    });

    it('unbalanced closing parentheses throws', () => {
      expectThrows('1 + 2)', 'Unbalanced parentheses');
    });

    it('trailing operator throws', () => {
      expectThrows('1 +', 'Trailing operator');
    });

    it('unexpected characters throw', () => {
      expectThrows('1 @ 2', 'Unexpected character');
    });
  });
});
