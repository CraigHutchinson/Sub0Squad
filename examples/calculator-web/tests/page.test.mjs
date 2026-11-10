import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const rootDir = join(__dirname, '..');

function readFile(relativePath) {
  return readFileSync(join(rootDir, relativePath), 'utf-8');
}

const REQUIRED_IDS = ['expr', 'result', 'degrees'];
const REQUIRED_KEYS = [
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  '.', '+', '-', '*', '/', '%', '^', '(', ')', '!',
  'pi', 'e', 'sin(', 'cos(', 'tan(', 'asin(', 'acos(', 'atan(',
  'ln(', 'log(', 'sqrt(', 'C', 'DEL', '='
];

test('index.html exists', () => {
  const html = readFile('index.html');
  assert.ok(html.length > 0, 'index.html must exist and not be empty');
});

test('index.html contains required input elements', () => {
  const html = readFile('index.html');
  
  for (const id of REQUIRED_IDS) {
    assert.ok(
      html.includes(`id="${id}"`),
      `index.html must contain an element with id="${id}"`
    );
  }
});

test('index.html contains button for every required key', () => {
  const html = readFile('index.html');
  
  for (const key of REQUIRED_KEYS) {
    assert.ok(
      html.includes(`data-key="${key}"`),
      `index.html must contain a button with data-key="${key}"`
    );
  }
});

test('index.html links to style.css', () => {
  const html = readFile('index.html');
  assert.ok(
    html.includes('<link rel="stylesheet" href="style.css">'),
    'index.html must contain <link rel="stylesheet" href="style.css">'
  );
});

test('index.html contains inline module script with correct imports and mount call', () => {
  const html = readFile('index.html');
  
  assert.ok(
    html.includes('<script type="module">'),
    'index.html must contain an inline <script type="module">'
  );
  assert.ok(
    html.includes("import { evaluate } from './src/calc.js'"),
    'Script must import { evaluate } from ./src/calc.js'
  );
  assert.ok(
    html.includes("import { mount } from './src/ui.js'"),
    'Script must import { mount } from ./src/ui.js'
  );
  assert.ok(
    html.includes('mount(document, evaluate)'),
    'Script must call mount(document, evaluate)'
  );
});

test('style.css exists and is non-empty', () => {
  const css = readFile('style.css');
  assert.ok(css.length > 0, 'style.css must not be empty');
});
