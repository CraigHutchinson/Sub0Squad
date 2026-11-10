// A tiny fake DOM for UI tests (no browser, no jsdom). Provided by the harness; agents do not write it.
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
