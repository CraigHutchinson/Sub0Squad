// Renders the portal page's script (web/portal.js) against a recorded state, with no browser: a small stand-in for the DOM, `fetch` answered from the
// fixture, timers that never fire. It proves that the script loads, that one state tick renders the workflow, its items and decisions without an
// exception, and that what the status JSON says about an item reaches the page: `healed`, the mends applied, and why the latest failed attempt failed
// (catalogue row 72). Run by ctest as `test_portal_page` when node is installed:   node portal_page_test.js <portal.js> <state.json>
'use strict';
const fs = require('fs');
const vm = require('vm');

const [, , scriptPath, statePath] = process.argv;
if (!scriptPath || !statePath) { console.error('usage: node portal_page_test.js <portal.js> <state.json>'); process.exit(64); }
const stateText = fs.readFileSync(statePath, 'utf8');

let failed = 0;
function check(ok, what) { if (!ok) { ++failed; console.error('CHECK failed: ' + what); } }

// ---- the stand-in DOM: elements keep attributes, children and the HTML or text they were given ----
const all = [];
function element(tag) {
  const e = {
    tag, attrs: {}, children: [], parent: null, innerHTML: '', textContent: '', style: {}, dataset: {}, scrollTop: 0, scrollHeight: 0, value: '', listeners: {},
    classList: {
      set: new Set(),
      add(c) { this.set.add(c); }, remove(c) { this.set.delete(c); }, contains(c) { return this.set.has(c); },
      toggle(c, on) { const want = on === undefined ? !this.set.has(c) : !!on; if (want) this.set.add(c); else this.set.delete(c); return want; },
    },
    getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; },
    setAttribute(k, v) { this.attrs[k] = String(v); },
    removeAttribute(k) { delete this.attrs[k]; },
    addEventListener(name, fn) { this.listeners[name] = fn; },
    appendChild(c) { return this.insertBefore(c, null); },
    insertBefore(c, ref) {
      if (c.parent) c.remove();
      const at = ref ? this.children.indexOf(ref) : -1;
      if (at < 0) this.children.push(c); else this.children.splice(at, 0, c);
      c.parent = this;
      return c;
    },
    remove() { if (!this.parent) return; const at = this.parent.children.indexOf(this); if (at >= 0) this.parent.children.splice(at, 1); this.parent = null; },
    get firstElementChild() { return this.children[0] || null; },
    get nextElementSibling() { if (!this.parent) return null; const at = this.parent.children.indexOf(this); return this.parent.children[at + 1] || null; },
    found: {},
    querySelector(sel) { if (!this.found[sel]) this.found[sel] = element('found'); return this.found[sel]; },   // a part of the markup this element was given
    querySelectorAll() { return []; },
    closest() { return null; },
    focus() {}, scrollIntoView() {}, getBoundingClientRect() { return { left: 0, top: 0, width: 800, height: 400 }; },
  };
  all.push(e);
  return e;
}
const byId = new Map();
const document = {
  hidden: false, body: element('body'), listeners: {},
  getElementById(id) { if (!byId.has(id)) { const e = element('div'); e.attrs.id = id; byId.set(id, e); } return byId.get(id); },
  createElement: (t) => element(t),
  createElementNS: (ns, t) => element(t),
  querySelector: () => null,
  querySelectorAll: () => [],
  addEventListener(name, fn) { this.listeners[name] = fn; },
};

const asked = [];
const answers = { '/api/state': stateText, '/api/live': '{"engines":[],"system":{}}' };
function fetchStub(url) {
  asked.push(String(url));
  const path = String(url).split('?')[0];
  const body = path in answers ? answers[path] : '{}';
  return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve(JSON.parse(body)), text: () => Promise.resolve(body) });
}

const sandbox = {
  document, console, fetch: fetchStub, Date, Math, JSON, Promise, Map, Set, RegExp, encodeURIComponent, decodeURIComponent, AbortController, isNaN, String, Number, Object, Array,
  location: { protocol: 'http:', hostname: '127.0.0.1', port: '8780' },
  setInterval: () => 0, setTimeout: () => 0, clearTimeout: () => {}, clearInterval: () => {},
};
sandbox.window = sandbox;
sandbox.getSelection = () => ({ isCollapsed: true, toString: () => '' });
vm.createContext(sandbox);

// The script, plus one line that hands its own functions to this test (its top-level `const`s are not properties of the global object).
const source = fs.readFileSync(scriptPath, 'utf8') + '\n;globalThis.__page = { render, openDrawer, tickState, tickLive };\n';
try {
  vm.runInContext(source, sandbox, { filename: 'portal.js' });
} catch (err) {
  console.error('the page script failed to load: ' + (err && err.stack ? err.stack : err));
  process.exit(1);
}

const shown = () => all.map((e) => e.innerHTML + '\n' + e.textContent).join('\n');

(async () => {
  for (let i = 0; i < 20; ++i) await new Promise((resolve) => setImmediate(resolve));   // let the first state and live ticks (started by the script) finish
  check(asked.some((u) => u.startsWith('/api/state')) && asked.some((u) => u.startsWith('/api/live')), 'the script polls /api/state and /api/live');
  const state = JSON.parse(stateText);
  try { sandbox.__page.render(); } catch (err) { check(false, 'render threw: ' + (err && err.stack ? err.stack.split('\n').slice(0, 3).join(' | ') : err)); }
  const wf = state.workflows.workflows[state.workflows.workflows.length - 1];
  let page = shown();
  check(page.includes(wf.id), 'the workflow id is shown');
  for (const item of wf.items) check(page.includes(item.id), 'item ' + item.id + ' is shown');
  check(page.includes('items accepted') && page.includes('1 / ' + wf.items.length), 'the accepted count is shown');
  check(page.includes('healed: undefined-declared-member'), 'a healed item says of what');
  check(byId.get('nodes').children.length === wf.items.length, 'one row per item in the node table');
  check(byId.get('decisions').children.length === wf.decisions.length, 'one row per decision');

  // The node panel of the item whose attempt failed: the reason its gate recorded is on the page.
  try { sandbox.__page.openDrawer({ type: 'node', key: 'dev-app' }); } catch (err) { check(false, 'opening the node panel threw: ' + (err && err.stack ? err.stack : err)); }
  for (let i = 0; i < 5; ++i) await new Promise((resolve) => setImmediate(resolve));
  page = shown();
  check(page.includes('attempt 2 failed') && page.includes('in its gate (gate): GATE FAIL: 1 of 6 tests failed'), 'the node panel says why the latest failed attempt failed');
  check(page.includes('&lt;b&gt;') && !page.includes('<b>not markup</b>'), 'text from the status is escaped, never markup');

  // The healed item's panel says the harness repaired it.
  try { sandbox.__page.openDrawer({ type: 'node', key: 'dev-engine' }); } catch (err) { check(false, 'opening the healed node panel threw: ' + (err && err.stack ? err.stack : err)); }
  page = shown();
  check(page.includes('repaired by the harness, outside the ladder'), 'the healed panel names the mend');

  // An empty state renders the invitation, not an exception.
  answers['/api/state'] = '{"workflows":{"asOf":1,"source":"daemon","workflows":[]},"jobs":{"jobs":[]}}';
  try { sandbox.__page.tickState(); } catch (err) { check(false, 'an empty state threw: ' + err); }
  for (let i = 0; i < 10; ++i) await new Promise((resolve) => setImmediate(resolve));
  check(shown().includes('No workflows yet'), 'an empty state shows the invitation');

  if (failed) { console.error(failed + ' check(s) failed'); process.exit(1); }
  console.log('portal page: rendered ' + wf.items.length + ' items and ' + wf.decisions.length + ' decisions from the recorded state');
})().catch((err) => { console.error('the page test threw: ' + (err && err.stack ? err.stack : err)); process.exit(1); });
