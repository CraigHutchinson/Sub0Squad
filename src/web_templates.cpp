#include "sub0squad/harness_web.hpp"

#include <map>

namespace sub0squad {

namespace {

const char* kPackage = R"JS({
  "name": "sci-calc",
  "private": true,
  "type": "module",
  "scripts": { "test": "node --test tests/", "start": "node serve.mjs 8080" }
}
)JS";

const char* kServe = R"JS(import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.json': 'application/json' };

export function startServer(port = 0, root = path.dirname(fileURLToPath(import.meta.url))) {
  const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://localhost');
    let rel = decodeURIComponent(url.pathname);
    if (rel.endsWith('/')) rel += 'index.html';
    const file = path.join(root, path.normalize(rel));
    if (!file.startsWith(root) || rel.startsWith('/tests/')) { res.writeHead(404); res.end('not found'); return; }
    fs.readFile(file, (err, data) => {
      if (err) { res.writeHead(404); res.end('not found'); return; }
      res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream' });
      res.end(data);
    });
  });
  return new Promise((resolve) => server.listen(port, '127.0.0.1', () => resolve(server)));
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const server = await startServer(Number(process.argv[2] || 8080));
  console.log('http://127.0.0.1:' + server.address().port);
}
)JS";

const char* kDomStub = R"JS(// A tiny fake DOM for UI tests (no browser, no jsdom). Provided by the harness; agents do not write it.
class Element {
  constructor(id, tag = 'div') { this.id = id; this.tagName = tag; this.value = ''; this.textContent = ''; this.checked = false; this.dataset = {}; this.listeners = {}; }
  addEventListener(type, fn) { (this.listeners[type] ||= []).push(fn); }
  dispatch(type, extra = {}) { for (const fn of this.listeners[type] || []) fn({ type, target: this, preventDefault() {}, ...extra }); }
}

export const DEFAULT_KEYS = ['7', '8', '9', '/', '4', '5', '6', '*', '1', '2', '3', '-', '0', '.', '^', '+', '(', ')', '!', 'pi', 'e',
  'sin(', 'cos(', 'tan(', 'ln(', 'log(', 'sqrt(', 'C', 'DEL', '='];

export function createDom(keys = DEFAULT_KEYS) {
  const els = { expr: new Element('expr', 'input'), result: new Element('result', 'output'), degrees: new Element('degrees', 'input') };
  const buttons = keys.map((k) => { const b = new Element('key-' + k, 'button'); b.dataset.key = k; return b; });
  const document = {
    getElementById: (id) => els[id] ?? null,
    querySelector: (sel) => (sel === '#expr' ? els.expr : sel === '#result' ? els.result : sel === '#degrees' ? els.degrees : null),
    querySelectorAll: (sel) => (sel === 'button[data-key]' ? buttons : []),
  };
  return {
    document, expr: els.expr, result: els.result, degrees: els.degrees, buttons,
    press(key) { const b = buttons.find((x) => x.dataset.key === key); if (!b) throw new Error('no key ' + key); b.dispatch('click'); },
    keydown(key) { els.expr.dispatch('keydown', { key }); },
  };
}
)JS";

const char* kStubCalc = "// harness stub: the developer replaces this file\nexport function evaluate(expr, options = {}) { throw new Error('not implemented'); }\n";
const char* kStubUi = "// harness stub: the developer replaces this file\nexport function mount(document) { throw new Error('not implemented'); }\n";
const char* kStubLexer = "// harness stub: the developer replaces this file\nexport function tokenize(text) { throw new Error('not implemented'); }\n";
const char* kStubFunctions = "// harness stub: the developer replaces this file\nexport function constant(name) { throw new Error('not implemented'); }\nexport function callFunction(name, args, degrees = false) { throw new Error('not implemented'); }\nexport function factorial(n) { throw new Error('not implemented'); }\n";
const char* kStubIndex = "<!doctype html><title>stub</title>\n";

const char* kCalc = R"JS(const CONST = { pi: Math.PI, e: Math.E, tau: 2 * Math.PI };

function factorial(n) {
  if (!Number.isInteger(n) || n < 0 || n > 170) throw new Error('factorial needs an integer in 0..170');
  let r = 1;
  for (let i = 2; i <= n; i++) r *= i;
  return r;
}

function tokenize(s) {
  const out = [];
  let i = 0;
  while (i < s.length) {
    const c = s[i];
    if (/\s/.test(c)) { i++; continue; }
    if (/[0-9.]/.test(c)) {
      let j = i;
      while (j < s.length && /[0-9.]/.test(s[j])) j++;
      if ((s[j] === 'e' || s[j] === 'E') && /[0-9+\-]/.test(s[j + 1] || '')) {
        let k = j + 1;
        if (s[k] === '+' || s[k] === '-') k++;
        if (/[0-9]/.test(s[k] || '')) { while (/[0-9]/.test(s[k] || '')) k++; j = k; }
      }
      out.push({ t: 'num', v: parseFloat(s.slice(i, j)) });
      i = j;
      continue;
    }
    if (/[A-Za-z_]/.test(c)) {
      let j = i;
      while (j < s.length && /[A-Za-z0-9_]/.test(s[j])) j++;
      out.push({ t: 'id', v: s.slice(i, j) });
      i = j;
      continue;
    }
    if ('+-*/^%!(),'.includes(c)) { out.push({ t: c }); i++; continue; }
    throw new Error(`unexpected character '${c}'`);
  }
  out.push({ t: 'end' });
  return out;
}

export function evaluate(expr, options = {}) {
  const k = options.degrees ? Math.PI / 180 : 1;
  const t = tokenize(String(expr));
  let p = 0;
  const isOp = (o) => t[p].t === o;
  const f1 = {
    sin: (a) => Math.sin(a * k), cos: (a) => Math.cos(a * k), tan: (a) => Math.tan(a * k),
    asin: (a) => Math.asin(a) / k, acos: (a) => Math.acos(a) / k, atan: (a) => Math.atan(a) / k,
    sinh: Math.sinh, cosh: Math.cosh, tanh: Math.tanh, ln: Math.log, log: Math.log10, log2: Math.log2,
    sqrt: (a) => { if (a < 0) throw new Error('sqrt of a negative number'); return Math.sqrt(a); },
    cbrt: Math.cbrt, abs: Math.abs, exp: Math.exp, floor: Math.floor, ceil: Math.ceil, round: Math.round, fact: factorial,
  };
  const f2 = { pow: Math.pow, min: Math.min, max: Math.max };

  function expression() {
    let l = term();
    while (isOp('+') || isOp('-')) { const o = t[p++].t; const r = term(); l = o === '+' ? l + r : l - r; }
    return l;
  }
  function term() {
    let l = unary();
    while (isOp('*') || isOp('/') || isOp('%')) {
      const o = t[p++].t;
      const r = unary();
      if (o === '*') l *= r;
      else { if (r === 0) throw new Error(o === '/' ? 'division by zero' : 'modulo by zero'); l = o === '/' ? l / r : l % r; }
    }
    return l;
  }
  function unary() {
    if (isOp('-')) { p++; return -unary(); }
    if (isOp('+')) { p++; return unary(); }
    return power();
  }
  function power() {
    const b = postfix();
    if (isOp('^')) { p++; return Math.pow(b, unary()); }
    return b;
  }
  function postfix() {
    let v = primary();
    while (isOp('!')) { p++; v = factorial(v); }
    return v;
  }
  function primary() {
    const tok = t[p];
    if (tok.t === 'num') { p++; return tok.v; }
    if (tok.t === 'id') {
      p++;
      if (isOp('(')) {
        p++;
        const args = [];
        if (!isOp(')')) { args.push(expression()); while (isOp(',')) { p++; args.push(expression()); } }
        if (!isOp(')')) throw new Error("expected ')'");
        p++;
        if (f1[tok.v]) { if (args.length !== 1) throw new Error(`${tok.v} takes 1 argument`); return f1[tok.v](args[0]); }
        if (f2[tok.v]) { if (args.length !== 2) throw new Error(`${tok.v} takes 2 arguments`); return f2[tok.v](args[0], args[1]); }
        throw new Error(`unknown function '${tok.v}'`);
      }
      if (tok.v in CONST) return CONST[tok.v];
      throw new Error(`unknown name '${tok.v}'`);
    }
    if (tok.t === '(') { p++; const v = expression(); if (!isOp(')')) throw new Error("expected ')'"); p++; return v; }
    throw new Error(`unexpected token '${tok.t}'`);
  }

  const v = expression();
  if (t[p].t !== 'end') throw new Error(`unexpected token '${t[p].t}'`);
  return v;
}
)JS";

const char* kEvalTests = R"JS(import test from 'node:test';
import assert from 'node:assert/strict';
import { evaluate } from '../src/calc.js';

const near = (a, b) => assert.ok(Math.abs(a - b) <= 1e-9 * (1 + Math.abs(b)), `${a} is not near ${b}`);

test('arithmetic and precedence', () => {
  near(evaluate('2+3*4'), 14);
  near(evaluate('(2+3)*4'), 20);
  near(evaluate('10/4'), 2.5);
  near(evaluate('10%3'), 1);
  near(evaluate('2*(3+(4-1)*2)'), 18);
});
test('power is right associative and binds tighter than unary minus', () => {
  near(evaluate('2^3^2'), 512);
  near(evaluate('-2^2'), -4);
  near(evaluate('2^-1'), 0.5);
  near(evaluate('2^10'), 1024);
});
test('factorial', () => { near(evaluate('5!'), 120); near(evaluate('0!'), 1); near(evaluate('3!!'), 720); });
test('decimals and exponents', () => { near(evaluate('.5+.5'), 1); near(evaluate('1.5e2+1'), 151); });
test('unary signs', () => { near(evaluate('--3'), 3); near(evaluate('+4'), 4); });
test('trigonometry in radians', () => {
  near(evaluate('sin(pi/6)'), 0.5); near(evaluate('cos(0)'), 1); near(evaluate('tan(pi/4)'), 1);
});
test('trigonometry in degrees', () => {
  near(evaluate('sin(30)', { degrees: true }), 0.5); near(evaluate('cos(60)', { degrees: true }), 0.5);
  near(evaluate('asin(1)', { degrees: true }), 90); near(evaluate('atan(1)', { degrees: true }), 45);
});
test('logarithms and roots', () => {
  near(evaluate('ln(e)'), 1); near(evaluate('log(1000)'), 3); near(evaluate('log2(8)'), 3);
  near(evaluate('sqrt(16)'), 4); near(evaluate('cbrt(27)'), 3);
});
test('rounding and other functions', () => {
  near(evaluate('abs(-7.5)'), 7.5); near(evaluate('floor(2.7)'), 2); near(evaluate('ceil(2.1)'), 3); near(evaluate('round(2.5)'), 3);
  near(evaluate('pow(2,8)'), 256); near(evaluate('max(3,7)'), 7); near(evaluate('min(3,7)'), 3); near(evaluate('fact(6)'), 720);
});
test('constants', () => { near(evaluate('pi*2'), 6.283185307179586); near(evaluate('tau/2'), Math.PI); });
test('nested calls', () => { near(evaluate('max(sin(pi/2),cos(0))+1'), 2); near(evaluate('sqrt(pow(3,2)+pow(4,2))'), 5); });
test('invalid input throws', () => {
  for (const bad of ['1/0', '5%0', '2+', '(1+2', 'foo(1)', 'sin(1,2)', '2 $ 3', 'sqrt(-1)', 'unknown', '1.5!', '()'])
    assert.throws(() => evaluate(bad), Error, bad);
});
)JS";

const char* kUiTests = R"JS(import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { createDom } from './helpers/dom-stub.mjs';
import { mount } from '../src/ui.js';

function setup() { const dom = createDom(); mount(dom.document); return dom; }
function type(dom, keys) { for (const k of keys) dom.press(k); }

test('keys build the expression', () => {
  const dom = setup();
  type(dom, ['2', '+', '3']);
  assert.equal(dom.expr.value, '2+3');
});
test('equals shows the result', () => {
  const dom = setup();
  type(dom, ['2', '+', '3', '*', '4', '=']);
  assert.equal(dom.result.textContent, '14');
});
test('C clears the expression and the result', () => {
  const dom = setup();
  type(dom, ['7', '=', 'C']);
  assert.equal(dom.expr.value, '');
  assert.equal(dom.result.textContent, '');
});
test('DEL removes the last character', () => {
  const dom = setup();
  type(dom, ['1', '2', 'DEL']);
  assert.equal(dom.expr.value, '1');
});
test('errors are shown, not thrown', () => {
  const dom = setup();
  type(dom, ['1', '/', '0', '=']);
  assert.ok(dom.result.textContent.startsWith('Error'));
});
test('Enter evaluates the typed expression', () => {
  const dom = setup();
  dom.expr.value = '2^3^2';
  dom.keydown('Enter');
  assert.equal(dom.result.textContent, '512');
});
test('degrees mode follows the checkbox', () => {
  const dom = setup();
  dom.degrees.checked = true;
  type(dom, ['sin(', '3', '0', ')', '=']);
  assert.equal(dom.result.textContent, '0.5');
});
test('the page has the required elements', () => {
  const html = fs.readFileSync(new URL('../index.html', import.meta.url), 'utf8');
  for (const needle of ['id="expr"', 'id="result"', 'id="degrees"', 'data-key="="', '<script type="module"'])
    assert.ok(html.includes(needle), needle);
});
)JS";

const char* kUi = R"JS(import { evaluate } from './calc.js';

export function mount(document) {
  const expr = document.getElementById('expr');
  const result = document.getElementById('result');
  const degrees = document.getElementById('degrees');
  const show = () => {
    try {
      const v = evaluate(expr.value, { degrees: !!(degrees && degrees.checked) });
      result.textContent = String(Number(v.toPrecision(12)));
    } catch (e) {
      result.textContent = 'Error: ' + e.message;
    }
  };
  for (const b of document.querySelectorAll('button[data-key]')) {
    b.addEventListener('click', () => {
      const k = b.dataset.key;
      if (k === 'C') { expr.value = ''; result.textContent = ''; }
      else if (k === 'DEL') expr.value = expr.value.slice(0, -1);
      else if (k === '=') show();
      else expr.value += k;
    });
  }
  expr.addEventListener('keydown', (e) => { if (e.key === 'Enter') show(); });
}

if (typeof document !== 'undefined') mount(document);
)JS";

const char* kCss = R"JS(body { font-family: system-ui, sans-serif; background: #f4f4f6; margin: 0; display: grid; place-items: center; min-height: 100vh; }
main { background: #fff; padding: 1.2rem; border-radius: 12px; box-shadow: 0 4px 18px rgba(0,0,0,.12); width: min(26rem, 94vw); }
h1 { font-size: 1.1rem; margin: 0 0 .8rem; }
#expr { width: 100%; box-sizing: border-box; font-size: 1.3rem; padding: .5rem; }
#result { display: block; min-height: 2rem; font-size: 1.6rem; text-align: right; margin: .4rem 0; }
#keys { display: grid; grid-template-columns: repeat(5, 1fr); gap: .35rem; margin-top: .6rem; }
button { font-size: 1rem; padding: .6rem 0; border: 1px solid #ccd; border-radius: 6px; background: #eef; cursor: pointer; }
button:hover { background: #dde; }
)JS";

std::string indexHtml()
{
    std::string keys;
    for (const char* k : {"7", "8", "9", "/", "C", "4", "5", "6", "*", "DEL", "1", "2", "3", "-", "(", "0", ".", "^", "+", ")", "!", "pi", "e", "sin(", "cos(", "tan(", "ln(", "log(", "sqrt(", "="}) {
        keys += std::string{"    <button data-key=\""} + k + "\">" + k + "</button>\n";
    }
    return std::string{"<!doctype html>\n<html lang=\"en\">\n<head>\n  <meta charset=\"utf-8\">\n  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                       "  <title>Scientific Calculator</title>\n  <link rel=\"stylesheet\" href=\"style.css\">\n</head>\n<body>\n<main>\n  <h1>Scientific Calculator</h1>\n"
                       "  <input id=\"expr\" autofocus autocomplete=\"off\" aria-label=\"expression\">\n  <output id=\"result\" aria-live=\"polite\"></output>\n"
                       "  <label><input type=\"checkbox\" id=\"degrees\"> degrees</label>\n  <div id=\"keys\">\n"} + keys +
           "  </div>\n</main>\n<script type=\"module\" src=\"src/ui.js\"></script>\n</body>\n</html>\n";
}

struct Bug { const char* from; const char* to; };

const std::map<std::string, Bug>& bugs()
{
    static const std::map<std::string, Bug> m = {
        {"src/calc.js", {"if (isOp('^')) { p++; return Math.pow(b, unary()); }", "if (isOp('^')) { p++; return Math.pow(b, postfix()); }"}},
        {"tests/eval.test.mjs", {"from '../src/calc.js'", "from '../src/calcs.js'"}},
        {"src/ui.js", {"else if (k === '=') show();", "else if (k === '=') { /* TODO */ }"}},
        {"tests/ui.test.mjs", {"from './helpers/dom-stub.mjs'", "from './helpers/dom-stubs.mjs'"}},
    };
    return m;
}

}  // namespace

const std::vector<HarnessFile>& webScaffold()
{
    static const std::vector<HarnessFile> files = {
        {"package.json", kPackage}, {"serve.mjs", kServe}, {"tests/helpers/dom-stub.mjs", kDomStub},
        {"src/calc.js", kStubCalc}, {"src/lexer.js", kStubLexer}, {"src/functions.js", kStubFunctions}, {"src/ui.js", kStubUi}, {"index.html", kStubIndex},
    };
    return files;
}

std::size_t webScaffoldBytes()
{
    std::size_t n = 0;
    for (const auto& f : webScaffold()) n += f.content.size();
    return n;
}

std::string webHarnessBrief()
{
    return "Project harness (already in the workspace, do not recreate): package.json (ES modules, tests run with `node --test`), serve.mjs (static server), "
           "tests/helpers/dom-stub.mjs (exports createDom(): a fake DOM for UI tests), and stub versions of src/calc.js, src/ui.js and index.html that throw 'not implemented'. "
           "Plain browser JavaScript (ES modules), no dependencies, no build step.";
}

std::string webMockFile(const std::string& path, bool buggy)
{
    std::string text;
    if (path == "src/calc.js") text = kCalc;
    else if (path == "tests/eval.test.mjs") text = kEvalTests;
    else if (path == "tests/ui.test.mjs") text = kUiTests;
    else if (path == "src/ui.js") text = kUi;
    else if (path == "style.css") text = kCss;
    else if (path == "index.html") text = indexHtml();
    else return {};
    if (buggy) {
        auto b = bugs().find(path);
        if (b != bugs().end()) {
            const auto pos = text.find(b->second.from);
            if (pos == std::string::npos) return {};
            text.replace(pos, std::string{b->second.from}.size(), b->second.to);
        }
    }
    return text;
}

std::string webMockBugTarget(const std::string& item)
{
    static const std::map<std::string, std::string> m = {{"eval-tests", "tests/eval.test.mjs"}, {"engine", "src/calc.js"}, {"ui-tests", "tests/ui.test.mjs"}, {"ui", "src/ui.js"}};
    auto it = m.find(item);
    return it == m.end() ? std::string{} : it->second;
}

}  // namespace sub0squad
