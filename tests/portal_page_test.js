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
    tag, attrs: {}, children: [], parent: null, _html: '', textContent: '', style: {}, dataset: {}, scrollTop: 0, scrollHeight: 0, value: '', listeners: {},
    classList: {
      set: new Set(),
      add(c) { this.set.add(c); }, remove(c) { this.set.delete(c); }, contains(c) { return this.set.has(c); },
      toggle(c, on) { const want = on === undefined ? !this.set.has(c) : !!on; if (want) this.set.add(c); else this.set.delete(c); return want; },
    },
    get innerHTML() { return this._html; },
    set innerHTML(v) { this._html = v; this.found = {}; },   // new markup replaces the children: the parts looked up before are rebuilt too
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
const fileAnswers = {};   // /api/file?p=<path>: a body, or null for a 404
function fetchStub(url) {
  asked.push(String(url));
  const path = String(url).split('?')[0];
  const file = (/[?&]p=([^&]+)/.exec(String(url)) || [])[1];   // a sandbox file answered by its own path, when the test gave one
  const byFile = path === '/api/file' && file !== undefined ? fileAnswers[decodeURIComponent(file)] : undefined;
  if (byFile === null) return Promise.resolve({ ok: false, status: 404, json: () => Promise.reject(new Error('404')), text: () => Promise.resolve('') });
  const body = byFile !== undefined ? byFile : path in answers ? answers[path] : '{}';
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
const source = fs.readFileSync(scriptPath, 'utf8') + '\n;globalThis.__page = { render, openDrawer, tickState, tickLive, loadPkg, loadPlan, setView, tickCanvas, openFile, loadFile, setFollow };\n';
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

  // The planning card: the layers before any item, from planner.json and the files the planner leaves (a repaired plan, a mend, a refused turn, the lint).
  answers['/api/files'] = JSON.stringify({ sandbox: '/x', deliverables: null, files: ['plan.json', 'planner.json', 'planner/prose.md', 'planner/conversation.md', 'planner/turn-1.txt', 'planner/turn-2.txt', 'planner/doc/turn-1/plan.json'].map((p) => ({ p, s: 10 })) });
  answers['/api/file'] = JSON.stringify({ schema: 'planner-1', request: 'r', valid: true, turns: 2, problems: [['the plan is refused: <b>not markup</b> bad bracket'], []], kinds: ['plan', 'edit'],
    mends: [{ turn: 1, text: "item 'dev-app': depends on 'dev-engine'" }], prose: true, slots: 4, lint: 'slot fill on 4 slots: 41 % busy' });
  try { await sandbox.__page.loadPkg(true); await sandbox.__page.loadPlan(true); } catch (err) { check(false, 'the planning card threw: ' + err); }
  for (let i = 0; i < 20; ++i) await new Promise((resolve) => setImmediate(resolve));
  const planning = byId.get('planning');
  check(planning && planning.innerHTML.includes('Planning') && planning.innerHTML.includes('plan prose') && planning.innerHTML.includes('planner/prose.md'), 'the planning card shows the prose layer');
  check(planning && planning.innerHTML.includes('no problems: accepted') && planning.innerHTML.includes('&lt;b&gt;not markup&lt;/b&gt; bad bracket') && !planning.innerHTML.includes('<b>not markup</b>'), 'the planning card shows each turn and escapes the problems');
  check(planning && planning.innerHTML.includes('depends on') && planning.innerHTML.includes('mechanical mend') && planning.innerHTML.includes('slot fill on 4 slots'), 'the planning card shows the mends and the lint');
  check(planning && planning.innerHTML.includes('planner/doc/turn-1/plan.json') && planning.innerHTML.includes('planner/turn-2.txt'), 'the planning card links each reply and document');

  // The planning node of the work graph: the root the items wait on, with its turns and mends; its panel is the planning card.
  try { sandbox.__page.render(); } catch (err) { check(false, 'render with a planning node threw: ' + (err && err.stack ? err.stack.split('\n').slice(0, 3).join(' | ') : err)); }
  page = shown();
  check(page.includes('Planning: the plan from the request') && page.includes('2 turns') && page.includes('1 mend'), 'the work graph has a planning node saying turns and mends');
  try { sandbox.__page.openDrawer({ type: 'node', key: 'planning' }); } catch (err) { check(false, 'opening the planning node panel threw: ' + (err && err.stack ? err.stack : err)); }
  check(byId.get('dA').innerHTML.includes('mechanical mend') && byId.get('dA').innerHTML.includes('planner/turn-2.txt'), 'the planning node panel shows the turns and the mends');

  // The canvas view: the newest attempt of the running item, its file at the latest revision, the lines that revision changed marked, the log newest
  // first, the turn in flight, and text from the document escaped.
  const dir = 'items/dev-app/attempt-2';
  answers['/api/files'] = JSON.stringify({ sandbox: '/x', deliverables: null, files: ['items/dev-app/attempt-1/canvas.json', dir + '/canvas.json', dir + '/canvas/src_main.cpp.r1', dir + '/canvas/src_main.cpp.r2', dir + '/conversation.md'].map((p) => ({ p, s: 10 })) });
  fileAnswers[dir + '/canvas.json'] = JSON.stringify({ files: { 'src/main.cpp': 2 }, log: [{ path: 'src/main.cpp', by: 'harness', revision: 2, base: 1, from: 1, to: 3, delta: 1 }] });   // a whole-file replacement
  fileAnswers[dir + '/canvas/src_main.cpp.r1'] = ['int main()', '{', '}', ''].join('\n');
  fileAnswers[dir + '/canvas/src_main.cpp.r2'] = ['int main()', '{', '    return 0; // <b>not markup</b>', '}', ''].join('\n');
  fileAnswers[dir + '/live.txt'] = 'streaming the next edit';
  const running = JSON.parse(stateText);
  running.workflows.workflows[running.workflows.workflows.length - 1].items.find((i) => i.id === 'dev-app').status = 'running';
  answers['/api/state'] = JSON.stringify(running);
  try {
    sandbox.__page.tickState();
    for (let i = 0; i < 10; ++i) await new Promise((resolve) => setImmediate(resolve));
    await sandbox.__page.loadPkg(true);
    sandbox.__page.setView('canvas');
    for (let i = 0; i < 20; ++i) await new Promise((resolve) => setImmediate(resolve));
    await sandbox.__page.tickCanvas();
  } catch (err) { check(false, 'the canvas view threw: ' + (err && err.stack ? err.stack : err)); }
  const code = byId.get('cv-code').innerHTML;
  check(byId.get('v-canvas').style.display === '' && byId.get('v-overview').style.display === 'none', 'the canvas tab shows the canvas view and hides the overview');
  check(byId.get('cv-pick').innerHTML.includes('dev-app · attempt 2') && byId.get('cv-pick').innerHTML.includes('auto: the running item (dev-app/attempt-2)'), 'the canvas view follows the newest attempt of the running item');
  check(code.includes('<i>3</i>') && code.includes('&lt;b&gt;not markup&lt;/b&gt;') && !code.includes('<b>not markup</b>'), 'the canvas view shows the file with line numbers, escaped');
  check((code.match(/class="ln ch"/g) || []).length === 1 && code.includes('<div class="ln ch"><i>3</i>'), 'only the line that differs from the base revision is marked, though the log says the whole file was replaced');
  check(byId.get('cv-log').innerHTML.includes('r2') && byId.get('cv-log').innerHTML.includes('harness'), 'the revision log names the revision and who made it');
  check(byId.get('cv-live').textContent === 'streaming the next edit' && byId.get('cv-convs').innerHTML.includes('conversation.md'), 'the turn in flight and the transcript are shown');
  check(byId.get('cv-note').textContent.includes('revision 2 of src/main.cpp, by harness: lines 3–3 changed'), 'the note says what the shown revision changed');
  sandbox.__page.setView('overview');
  check(byId.get('v-overview').style.display === '' && byId.get('v-canvas').style.display === 'none', 'the overview tab brings the overview back');

  // The file viewer: a refresh that changes the text keeps the place of a reader who is up the transcript, and a followed view that is at the end
  // moves with the stream.
  const transcript = dir + '/conversation.md';
  fileAnswers[transcript] = 'first\n';
  const flush = async () => { for (let i = 0; i < 10; ++i) await new Promise((resolve) => setImmediate(resolve)); };
  try {
    await sandbox.__page.openFile(transcript);
    await flush();
    sandbox.__page.setFollow(false);
    await flush();
    let viewer = byId.get('dA').querySelector('pre.viewer');
    viewer.scrollHeight = 1000; viewer.clientHeight = 400; viewer.scrollTop = 100;   // the reader is up the transcript
    fileAnswers[transcript] = 'first\nsecond\n';
    await sandbox.__page.loadFile();
    viewer = byId.get('dA').querySelector('pre.viewer');
    check(viewer.textContent === 'first\nsecond\n', 'the viewer shows the refreshed transcript');
    check(viewer.scrollTop === 100, 'a refresh keeps the place of a reader who is up the transcript (scrollTop ' + viewer.scrollTop + ')');
    fileAnswers[transcript] = 'first\nsecond\nthird\n';
    sandbox.__page.setFollow(true);   // the follow box is checked, and the text is new: the header and its viewer are rebuilt once
    await flush();
    viewer = byId.get('dA').querySelector('pre.viewer');
    viewer.scrollHeight = 1000; viewer.clientHeight = 400; viewer.scrollTop = 600;   // at the end
    fileAnswers[transcript] = 'first\nsecond\nthird\nfourth\n';
    await sandbox.__page.loadFile();
    viewer = byId.get('dA').querySelector('pre.viewer');
    check(viewer.textContent === 'first\nsecond\nthird\nfourth\n', 'the followed viewer shows the new text');
    check(viewer.scrollTop === 1000, 'a followed viewer at the end moves to the end (scrollTop ' + viewer.scrollTop + ')');
  } catch (err) { check(false, 'the file viewer threw: ' + (err && err.stack ? err.stack : err)); }

  // An empty state renders the invitation, not an exception.
  answers['/api/state'] = '{"workflows":{"asOf":1,"source":"daemon","workflows":[]},"jobs":{"jobs":[]}}';
  try { sandbox.__page.tickState(); } catch (err) { check(false, 'an empty state threw: ' + err); }
  for (let i = 0; i < 10; ++i) await new Promise((resolve) => setImmediate(resolve));
  check(shown().includes('No workflows yet'), 'an empty state shows the invitation');

  const workState = JSON.parse(answers['/api/state']);
  workState.workOverview = {rows:[
    {id:'w-000001', project:'Calculator', kind:'workflow', title:'Running calculator', lifecycle:'running', freshness:'live', workflow:'w-000001', source:'east'},
    {id:'w-000001', project:'Store', kind:'workflow', title:'Archived store', lifecycle:'finished', freshness:'archive', workflow:'w-000001', source:'west', verdict:'fail'},
    {id:'case', project:'Store', kind:'scenario', title:'Staged scenario', lifecycle:'staged', freshness:'recorded', report:'brief-id', reportLabel:'Open brief'}
  ]};
  answers['/api/state'] = JSON.stringify(workState);
  sandbox.__page.setView('work');
  sandbox.__page.tickState();
  await flush();
  check(shown().includes('Staged scenario') && shown().includes('Archived store') && shown().includes('Running calculator'), 'work view separates live, archived and staged work');
  check(shown().includes('source=east') && shown().includes('source=west') && shown().includes('Open brief'), 'same short workflow IDs and staged briefs link to their owners');
  const project = byId.get('work-project');
  project.value = 'Store';
  project.listeners.change();
  check(byId.get('work-rows').children.length === 2 && byId.get('work-rows').children.every(row => !row.innerHTML.includes('Running calculator')), 'project filter applies to work rows');
  sandbox.__page.tickState();
  await flush();
  check(project.value === 'Store' && byId.get('work-rows').children.length === 2, 'refresh preserves project selection');

  if (failed) { console.error(failed + ' check(s) failed'); process.exit(1); }
  console.log('portal page: rendered ' + wf.items.length + ' items and ' + wf.decisions.length + ' decisions from the recorded state');
})().catch((err) => { console.error('the page test threw: ' + (err && err.stack ? err.stack : err)); process.exit(1); });
