import { tokenize } from './lexer.js';
import { constant, callFunction, factorial } from './functions.js';

export function evaluate(expr, options = {}) {
  const tokens = tokenize(expr);
  let pos = 0;
  const degrees = options.degrees === true;

  function peek() {
    return pos < tokens.length ? tokens[pos] : null;
  }

  function consume() {
    return tokens[pos++];
  }

  function expectType(type) {
    const token = peek();
    if (!token || token.type !== type) {
      throw new Error(`Expected ${type} but got ${token ? token.type : 'end of input'}`);
    }
    return consume();
  }

  // Parse addition and subtraction (lowest precedence, left-associative)
  function parseAddSub() {
    let left = parseMulDiv();

    while (peek() && peek().type === 'op' && (peek().value === '+' || peek().value === '-')) {
      const op = consume().value;
      const right = parseMulDiv();
      if (op === '+') {
        left = left + right;
      } else {
        left = left - right;
      }
    }

    return left;
  }

  // Parse multiplication, division, modulo (left-associative)
  function parseMulDiv() {
    let left = parseUnary();

    while (peek() && peek().type === 'op' && (peek().value === '*' || peek().value === '/' || peek().value === '%')) {
      const op = consume().value;
      const right = parseUnary();
      if (op === '*') {
        left = left * right;
      } else if (op === '/') {
        if (right === 0) {
          throw new Error('Division by zero');
        }
        left = left / right;
      } else if (op === '%') {
        if (right === 0) {
          throw new Error('Modulo by zero');
        }
        left = left % right;
      }
    }

    return left;
  }

  // Parse unary + and - (right-associative in the sense that they bind to the next term)
  function parseUnary() {
    if (peek() && peek().type === 'op' && (peek().value === '+' || peek().value === '-')) {
      const op = consume().value;
      const operand = parseUnary();
      if (op === '-') {
        return -operand;
      } else {
        return +operand;
      }
    }

    return parseExponent();
  }

  // Parse exponentiation (right-associative, binds tighter than unary minus)
  function parseExponent() {
    let base = parsePostfix();

    if (peek() && peek().type === 'op' && peek().value === '^') {
      consume();
      const exponent = parseExponent(); // Right-associative: recurse here
      return Math.pow(base, exponent);
    }

    return base;
  }

  // Parse postfix operators (like !)
  function parsePostfix() {
    let value = parsePrimary();

    while (peek() && peek().type === 'op' && peek().value === '!') {
      consume();
      if (value < 0 || !Number.isInteger(value)) {
        throw new Error('Factorial of non-integer or negative number');
      }
      value = factorial(value);
    }

    return value;
  }

  // Parse primary: numbers, constants, parenthesized expressions, function calls, identifiers
  function parsePrimary() {
    const token = peek();

    if (!token) {
      throw new Error('Unexpected end of input');
    }

    if (token.type === 'num') {
      consume();
      return token.value;
    }

    if (token.type === 'lp') {
      consume(); // consume '('
      const value = parseAddSub();
      if (!peek() || peek().type !== 'rp') {
        throw new Error('Unbalanced parentheses');
      }
      consume(); // consume ')'
      return value;
    }

    if (token.type === 'id') {
      const name = consume().value;

      // Check if it's a function call
      if (peek() && peek().type === 'lp') {
        consume(); // consume '('
        const args = [];
        if (peek() && peek().type !== 'rp') {
          args.push(parseAddSub());
          while (peek() && peek().type === 'comma') {
            consume(); // consume ','
            args.push(parseAddSub());
          }
        }
        if (!peek() || peek().type !== 'rp') {
          throw new Error('Unbalanced parentheses');
        }
        consume(); // consume ')'
        return callFunction(name, args, degrees);
      }

      // It's a constant
      const value = constant(name);
      if (value === undefined) {
        throw new Error('Unknown name');
      }
      return value;
    }

    throw new Error(`Unexpected token: ${token.type}`);
  }

  const result = parseAddSub();

  // Check for trailing tokens
  if (pos < tokens.length) {
    throw new Error('Trailing tokens');
  }

  return result;
}
