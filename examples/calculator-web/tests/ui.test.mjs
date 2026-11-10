import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { createDom } from './helpers/dom-stub.mjs';
import { mount } from '../src/ui.js';

describe('Calculator UI', () => {
  test('building an expression appends to input', () => {
    const { document, expr, press } = createDom();
    mount(document, () => {});
    press('1');
    press('2');
    press('+');
    assert.strictEqual(expr.value, '12+');
  });

  test('equals calls the injected evaluate with the expression and the degrees flag', () => {
    const { document, expr, result, degrees, press, keydown } = createDom();
    const evaluate = (expression, options) => {
      assert.strictEqual(expression, '2+2');
      assert.strictEqual(options.degrees, degrees.checked);
      return 42;
    };
    mount(document, evaluate);
    press('2');
    press('+');
    press('2');
    keydown('Enter');
    assert.strictEqual(result.textContent, '42');
  });

  test('clear button clears expression and result', () => {
    const { document, expr, result, press } = createDom();
    const evaluate = () => {};
    mount(document, evaluate);
    press('1');
    press('C');
    assert.strictEqual(expr.value, '');
    assert.strictEqual(result.textContent, '');
  });

  test('delete button removes the last character', () => {
    const { document, expr, press } = createDom();
    const evaluate = () => {};
    mount(document, evaluate);
    press('1');
    press('2');
    press('3');
    press('DEL');
    assert.strictEqual(expr.value, '12');
  });

  test('error display shows Error: message when evaluate throws', () => {
    const { document, expr, result, press, keydown } = createDom();
    const evaluate = () => {
      throw new Error('bad input');
    };
    mount(document, evaluate);
    press('1');
    keydown('Enter');
    assert.strictEqual(result.textContent, 'Error: bad input');
  });

  test('Enter key evaluates the expression', () => {
    const { document, expr, result, press, keydown } = createDom();
    const evaluate = (expression) => {
      assert.strictEqual(expression, '5');
      return 10;
    };
    mount(document, evaluate);
    press('5');
    keydown('Enter');
    assert.strictEqual(result.textContent, '10');
  });

  test('degrees mode passes checked state to evaluate', () => {
    const { document, expr, result, degrees, press, keydown } = createDom();
    const evaluate = (expression, options) => {
      assert.strictEqual(options.degrees, true);
      return 0;
    };
    mount(document, evaluate);
    degrees.checked = true;
    press('0');
    keydown('Enter');
    assert.strictEqual(result.textContent, '0');
  });
});
