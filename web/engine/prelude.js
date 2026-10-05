// web/engine/prelude.js -- browser API surface for Weave, layered on the C
// primitives in js.c (Node.prototype methods + the __weave host object).
// Regenerate js_prelude.h after editing: web/host/gen_prelude.sh
(function () {
'use strict';
const W = __weave, P = W.NodeProto, doc = W.document, g = globalThis;
g.window = g; g.self = g; g.top = g; g.parent = g; g.frames = g; g.document = doc;

const NODE = { ELEMENT_NODE: 1, TEXT_NODE: 3, COMMENT_NODE: 8, DOCUMENT_NODE: 9, DOCUMENT_FRAGMENT_NODE: 11 };
function Node() { throw new TypeError('Illegal constructor'); }
Node.prototype = P; Object.assign(Node, NODE); Object.assign(P, NODE);
for (const n of ['Node', 'Element', 'HTMLElement', 'Document', 'HTMLDocument', 'Text', 'Comment', 'CharacterData',
  'DocumentFragment', 'SVGElement', 'HTMLDivElement', 'HTMLAnchorElement', 'HTMLInputElement', 'HTMLButtonElement',
  'HTMLFormElement', 'HTMLImageElement', 'HTMLScriptElement', 'HTMLSelectElement', 'HTMLTextAreaElement',
  'HTMLIFrameElement', 'HTMLCanvasElement', 'HTMLVideoElement', 'HTMLMediaElement', 'HTMLTemplateElement',
  'HTMLBodyElement', 'HTMLSpanElement', 'HTMLUListElement', 'HTMLLIElement', 'HTMLTableElement', 'HTMLLinkElement',
  'HTMLStyleElement', 'HTMLMetaElement', 'HTMLParagraphElement', 'HTMLHeadingElement', 'HTMLLabelElement',
  'HTMLOptionElement', 'HTMLDetailsElement', 'HTMLDialogElement', 'ShadowRoot']) g[n] = Node;
g.EventTarget = function EventTarget() {};

const def = (o, name, get, set) => Object.defineProperty(o, name, { get, set, configurable: true });
const attrProp = (prop, attr) => def(P, prop, function () { return this.nodeType === 1 ? (this.getAttribute(attr) || '') : ''; },
  function (v) { this.setAttribute(attr, String(v)); });
const boolProp = (prop, attr) => def(P, prop, function () { return this.nodeType === 1 && this.hasAttribute(attr); },
  function (v) { v ? this.setAttribute(attr, '') : this.removeAttribute(attr); });

def(P, 'childNodes', function () { return this._childNodes(); });
def(P, 'children', function () { return this._children(); });
def(P, 'childElementCount', function () { return this._children().length; });
attrProp('id', 'id'); attrProp('className', 'class'); attrProp('name', 'name'); attrProp('type', 'type');
attrProp('alt', 'alt'); attrProp('rel', 'rel'); attrProp('lang', 'lang'); attrProp('dir', 'dir');
attrProp('placeholder', 'placeholder'); attrProp('target', 'target'); attrProp('role', 'role');
attrProp('method', 'method'); attrProp('htmlFor', 'for'); attrProp('width', 'width'); attrProp('height', 'height');
for (const a of ['hidden', 'disabled', 'checked', 'selected', 'required', 'multiple', 'autofocus', 'defer', 'async', 'open'])
  boolProp(a, a);
boolProp('readOnly', 'readonly');
for (const a of ['href', 'src', 'action']) def(P, a, function () {
  const v = this.nodeType === 1 ? this.getAttribute(a) : null;
  if (v == null) return '';
  try { return new URL(v, W.url).href; } catch (e) { return v; }
}, function (v) { this.setAttribute(a, String(v)); });
def(P, 'value', function () {
  if (this.localName === 'textarea' && !this.hasAttribute('value')) return this.textContent;
  if (this.localName === 'select') {
    const o = this.querySelector('option[selected]') || this.querySelector('option');
    return o ? (o.getAttribute('value') ?? o.textContent) : '';
  }
  return this.nodeType === 1 ? (this.getAttribute('value') ?? '') : '';
}, function (v) {
  if (this.localName === 'select') {
    let found = false;
    for (const o of this.querySelectorAll('option')) {
      const match = !found && (o.getAttribute('value') ?? o.textContent) === String(v);
      o.selected = match; found ||= match;
    }
  } else this.setAttribute('value', String(v));
});
def(P, 'tabIndex', () => 0, () => {});
def(P, 'classList', function () { return new DOMTokenList(this); });
const camelToData = k => 'data-' + k.replace(/[A-Z]/g, m => '-' + m.toLowerCase());
def(P, 'dataset', function () {
  const el = this;
  return new Proxy({}, {
    get(_, k) { return typeof k === 'string' ? (el.getAttribute(camelToData(k)) ?? undefined) : undefined; },
    set(_, k, v) { el.setAttribute(camelToData(k), String(v)); return true; },
    deleteProperty(_, k) { el.removeAttribute(camelToData(k)); return true; },
    has(_, k) { return el.hasAttribute(camelToData(k)); },
  });
});
def(P, 'style', function () { return styleProxy(this); }, function (v) { this.setAttribute('style', String(v)); });
const rect = el => el.nodeType === 1 ? el.getBoundingClientRect() : { width: 0, height: 0, top: 0, left: 0 };
def(P, 'offsetWidth', function () { return rect(this).width | 0; });
def(P, 'offsetHeight', function () { return rect(this).height | 0; });
def(P, 'clientWidth', function () { return rect(this).width | 0; });
def(P, 'clientHeight', function () { return rect(this).height | 0; });
def(P, 'offsetTop', function () { return (rect(this).top + g.scrollY) | 0; });
def(P, 'offsetLeft', function () { return rect(this).left | 0; });
def(P, 'offsetParent', function () { return this.parentElement; });
def(P, 'scrollHeight', function () { return rect(this).height | 0; });
def(P, 'scrollWidth', function () { return rect(this).width | 0; });
def(P, 'scrollTop', () => 0, () => {}); def(P, 'scrollLeft', () => 0, () => {});
def(P, 'readyState', () => 'complete');
def(P, 'cookie', () => '', () => {});
def(P, 'defaultView', () => g);
def(P, 'activeElement', () => doc.body);
def(P, 'forms', function () { return this.querySelectorAll('form'); });
def(P, 'images', function () { return this.querySelectorAll('img'); });
def(P, 'links', function () { return this.querySelectorAll('a[href]'); });
def(P, 'scripts', function () { return this.querySelectorAll('script'); });
def(P, 'URL', () => W.url);
def(P, 'documentURI', () => W.url);
def(P, 'referrer', () => '');
def(P, 'visibilityState', () => 'visible');
def(P, 'currentScript', () => null);
def(P, 'characterSet', () => 'UTF-8');
def(P, 'compatMode', () => 'CSS1Compat');
def(P, 'options', function () { return this.querySelectorAll('option'); });
def(P, 'selectedIndex', function () {
  const o = this.querySelectorAll('option');
  for (let i = 0; i < o.length; i++) if (o[i].hasAttribute('selected')) return i;
  return o.length ? 0 : -1;
}, function (i) { this.querySelectorAll('option').forEach((x, k) => k === i ? x.setAttribute('selected', '') : x.removeAttribute('selected')); });
def(P, 'content', function () {
  const f = doc.createDocumentFragment();
  for (const c of this.childNodes) f.appendChild(c.cloneNode(true));
  return f;
});
def(P, 'shadowRoot', () => null);
def(P, 'namespaceURI', () => 'http://www.w3.org/1999/xhtml');

const qsa = P.querySelectorAll;
P.querySelectorAll = function (s) { const r = qsa.call(this, s); r.item = i => r[i] ?? null; return r; };
P.getElementsByTagName = function (t) { return this.querySelectorAll(t); };
P.getElementsByClassName = function (c) {
  const sel = String(c).trim().split(/\s+/).filter(Boolean).map(x => '.' + g.CSS.escape(x)).join('');
  return sel ? this.querySelectorAll(sel) : [];
};
P.getElementsByName = function (n) { return this.querySelectorAll('[name="' + String(n).replace(/"/g, '\\"') + '"]'); };
const toNode = x => typeof x === 'string' ? doc.createTextNode(x) : x;
P.append = function (...ns) { for (const x of ns) this.appendChild(toNode(x)); };
P.prepend = function (...ns) { const f = this.firstChild; for (const x of ns) this.insertBefore(toNode(x), f); };
P.before = function (...ns) { const p = this.parentNode; if (p) for (const x of ns) p.insertBefore(toNode(x), this); };
P.after = function (...ns) { const p = this.parentNode; if (!p) return; const nx = this.nextSibling; for (const x of ns) p.insertBefore(toNode(x), nx); };
P.replaceWith = function (...ns) { if (this.parentNode) { this.before(...ns); this.remove(); } };
P.replaceChildren = function (...ns) { while (this.firstChild) this.removeChild(this.firstChild); this.append(...ns); };
P.toggleAttribute = function (a, f) { const on = f === undefined ? !this.hasAttribute(a) : !!f; on ? this.setAttribute(a, '') : this.removeAttribute(a); return on; };
P.focus = P.blur = P.scrollIntoView = P.select = P.normalize = P.reset = function () {};
P.submit = P.requestSubmit = function () { const b = this.querySelector('[type=submit],button'); if (b) b.click(); };
P.showModal = P.show = function () { this.setAttribute('open', ''); };
P.close = function () { this.removeAttribute('open'); };
P.getClientRects = function () { return [this.getBoundingClientRect()]; };
P.attachShadow = function () { return this; };
P.animate = function () { return { finished: Promise.resolve(), cancel() {}, play() {}, pause() {}, finish() {} }; };
P.isEqualNode = P.isSameNode = function (o) { return o === this; };
P.compareDocumentPosition = function (o) { return this.contains(o) ? 20 : (o && o.contains && o.contains(this)) ? 10 : 4; };
P.createComment = function () { return doc.createTextNode(''); };
P.createEvent = function () { return new Event(''); };
P.createElementNS = function (ns, n) { return doc.createElement(n); };
P.importNode = function (n, deep) { return n.cloneNode(deep); };
P.adoptNode = function (n) { return n; };
P.hasFocus = function () { return true; };
P.execCommand = function () { return false; };
P.setAttributeNS = function (ns, n, v) { this.setAttribute(n, v); };
P.getAttributeNS = function (ns, n) { return this.getAttribute(n); };
P.removeAttributeNS = function (ns, n) { this.removeAttribute(n); };
P.setPointerCapture = P.releasePointerCapture = function () {};

class DOMTokenList {
  constructor(el) { this._el = el; }
  _get() { return (this._el.getAttribute('class') || '').split(/\s+/).filter(Boolean); }
  _set(a) { this._el.setAttribute('class', a.join(' ')); }
  get length() { return this._get().length; }
  get value() { return this._el.getAttribute('class') || ''; }
  item(i) { return this._get()[i] ?? null; }
  contains(c) { return this._get().includes(c); }
  add(...cs) { const a = this._get(); let ch = false; for (const c of cs) if (!a.includes(c)) { a.push(c); ch = true; } if (ch) this._set(a); }
  remove(...cs) { const a = this._get(), b = a.filter(x => !cs.includes(x)); if (b.length !== a.length) this._set(b); }
  toggle(c, f) { const has = this.contains(c), on = f === undefined ? !has : !!f; if (on && !has) this.add(c); else if (!on && has) this.remove(c); return on; }
  replace(a, b) { if (!this.contains(a)) return false; this._set(this._get().map(x => x === a ? b : x)); return true; }
  forEach(fn, t) { this._get().forEach(fn, t); }
  [Symbol.iterator]() { return this._get()[Symbol.iterator](); }
  toString() { return this.value; }
}
g.DOMTokenList = DOMTokenList;

const kebab = k => k.startsWith('--') ? k : k.replace(/^(webkit|moz|ms)(?=[A-Z])/, '-$1').replace(/[A-Z]/g, m => '-' + m.toLowerCase());
function parseStyle(s) {
  const m = new Map();
  if (s) for (const part of s.split(';')) {
    const i = part.indexOf(':');
    if (i < 0) continue;
    const k = part.slice(0, i).trim().toLowerCase(), v = part.slice(i + 1).trim();
    if (k) m.set(k, v);
  }
  return m;
}
function writeStyle(el, m) { let s = ''; for (const [k, v] of m) if (v !== '') s += k + ': ' + v + '; '; el.setAttribute('style', s.trim()); }
function styleProxy(el) {
  return new Proxy({}, {
    get(_, k) {
      if (k === 'setProperty') return (n, v) => { const m = parseStyle(el.getAttribute('style')); m.set(n, v == null ? '' : String(v)); writeStyle(el, m); };
      if (k === 'getPropertyValue') return n => parseStyle(el.getAttribute('style')).get(n) || '';
      if (k === 'removeProperty') return n => { const m = parseStyle(el.getAttribute('style')), v = m.get(n) || ''; m.delete(n); writeStyle(el, m); return v; };
      if (k === 'cssText') return el.getAttribute('style') || '';
      if (typeof k !== 'string') return undefined;
      return parseStyle(el.getAttribute('style')).get(kebab(k)) || '';
    },
    set(_, k, v) {
      if (k === 'cssText') { el.setAttribute('style', String(v)); return true; }
      const m = parseStyle(el.getAttribute('style')), kk = kebab(String(k));
      if (v == null || v === '') m.delete(kk); else m.set(kk, String(v));
      writeStyle(el, m);
      return true;
    },
  });
}

// ---- events ----
class Event {
  constructor(type, o = {}) {
    this.type = String(type); this.bubbles = !!o.bubbles; this.cancelable = !!o.cancelable;
    this.composed = !!o.composed; this.defaultPrevented = false; this.target = null; this.currentTarget = null;
    this.timeStamp = W.now(); this.isTrusted = false; this._stop = false; this._stopNow = false;
  }
  preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
  stopPropagation() { this._stop = true; }
  stopImmediatePropagation() { this._stop = this._stopNow = true; }
  composedPath() { const p = []; for (let n = this.target; n; n = n.parentNode) p.push(n); p.push(g); return p; }
}
class CustomEvent extends Event { constructor(t, o = {}) { super(t, o); this.detail = o.detail ?? null; } }
g.Event = Event; g.CustomEvent = CustomEvent;
for (const n of ['UIEvent', 'MouseEvent', 'KeyboardEvent', 'FocusEvent', 'InputEvent', 'PointerEvent', 'TouchEvent',
  'WheelEvent', 'SubmitEvent', 'ErrorEvent', 'PopStateEvent', 'HashChangeEvent', 'MessageEvent', 'ProgressEvent',
  'AnimationEvent', 'TransitionEvent', 'StorageEvent', 'PageTransitionEvent'])
  g[n] = class extends Event { constructor(t, o = {}) { super(t, o); Object.assign(this, o); } };

function addL(t, fn, opt) {
  if (!fn) return;
  const l = this.__l || (this.__l = {});
  const list = l[t] || (l[t] = []);
  if (!list.some(x => x.fn === fn)) list.push({ fn, once: !!(opt && opt.once) });
}
function remL(t, fn) { const l = this.__l && this.__l[t]; if (!l) return; const i = l.findIndex(x => x.fn === fn); if (i >= 0) l.splice(i, 1); }
const report = e => console.error(e && e.stack ? e.stack : String(e));
function fire(node, ev) {
  ev.currentTarget = node;
  const ls = node.__l && node.__l[ev.type];
  if (ls) for (const x of ls.slice()) {
    try { typeof x.fn === 'function' ? x.fn.call(node, ev) : x.fn.handleEvent(ev); } catch (e) { report(e); }
    if (x.once) remL.call(node, ev.type, x.fn);
    if (ev._stopNow) return;
  }
  const h = node['on' + ev.type];
  if (typeof h === 'function') {
    try { if (h.call(node, ev) === false) ev.preventDefault(); } catch (e) { report(e); }
  } else if (node !== g && node.nodeType === 1 && node.hasAttribute('on' + ev.type)) {
    try { if (new Function('event', node.getAttribute('on' + ev.type)).call(node, ev) === false) ev.preventDefault(); } catch (e) { report(e); }
  }
}
function dispatch(ev) {
  if (!ev.target) ev.target = this;
  const path = [];
  for (let n = this; n; n = n.parentNode) path.push(n);
  path.push(g);
  fire(path[0], ev);
  if (ev.bubbles) for (let i = 1; i < path.length && !ev._stop; i++) fire(path[i], ev);
  return !ev.defaultPrevented;
}
P.addEventListener = addL; P.removeEventListener = remL; P.dispatchEvent = dispatch;
g.addEventListener = addL; g.removeEventListener = remL;
g.dispatchEvent = function (ev) { if (!ev.target) ev.target = g; fire(g, ev); return !ev.defaultPrevented; };
P.click = function () { this.dispatchEvent(new g.MouseEvent('click', { bubbles: true, cancelable: true })); };
W.dispatch = function (node, type) {
  const ev = new (type === 'click' ? g.MouseEvent : Event)(type, { bubbles: true, cancelable: true });
  ev.isTrusted = true; ev.button = 0;
  node.dispatchEvent(ev);
  return ev.defaultPrevented;
};
W.fireLoad = function () {
  doc.dispatchEvent(new Event('readystatechange'));
  doc.dispatchEvent(new Event('DOMContentLoaded', { bubbles: true }));
  g.dispatchEvent(new Event('load'));
  g.dispatchEvent(new Event('pageshow'));
};

// ---- timers ----
const bind = (fn, a) => typeof fn === 'function' ? (a.length ? () => fn(...a) : fn) : () => {};
g.setTimeout = (fn, ms, ...a) => W.setTimer(bind(fn, a), ms | 0, 0);
g.setInterval = (fn, ms, ...a) => W.setTimer(bind(fn, a), ms | 0, 1);
g.clearTimeout = g.clearInterval = id => W.clearTimer(id | 0);
g.requestAnimationFrame = fn => W.setTimer(() => fn(W.now()), 16, 0);
g.cancelAnimationFrame = id => W.clearTimer(id | 0);
g.requestIdleCallback = fn => W.setTimer(() => fn({ didTimeout: false, timeRemaining: () => 10 }), 1, 0);
g.cancelIdleCallback = id => W.clearTimer(id | 0);
g.queueMicrotask = fn => { Promise.resolve().then(fn); };

// ---- console ----
const fmt = a => a.map(x => typeof x === 'string' ? x : x instanceof Error ? x.message : (() => {
  try { return JSON.stringify(x); } catch (e) { return String(x); } })()).join(' ');
g.console = {
  log: (...a) => W.log(fmt(a)), info: (...a) => W.log(fmt(a)), warn: (...a) => W.log('warn: ' + fmt(a)),
  error: (...a) => W.log('error: ' + fmt(a)), debug() {}, trace() {}, group() {}, groupEnd() {}, groupCollapsed() {},
  table() {}, time() {}, timeEnd() {}, timeLog() {}, assert() {}, dir() {}, count() {}, countReset() {},
};

// ---- URL / location / navigator ----
class URLSearchParams {
  constructor(s = '') {
    this._p = [];
    if (typeof s === 'string') {
      s = s.replace(/^\?/, '');
      const d = x => { try { return decodeURIComponent(x.replace(/\+/g, ' ')); } catch (e) { return x; } };
      if (s) for (const kv of s.split('&')) { const i = kv.indexOf('='); this._p.push(i < 0 ? [d(kv), ''] : [d(kv.slice(0, i)), d(kv.slice(i + 1))]); }
    } else if (s && typeof s === 'object') {
      for (const e of (Symbol.iterator in s ? s : Object.entries(s))) this._p.push([String(e[0]), String(e[1])]);
    }
  }
  get size() { return this._p.length; }
  get(k) { const e = this._p.find(x => x[0] === k); return e ? e[1] : null; }
  getAll(k) { return this._p.filter(x => x[0] === k).map(x => x[1]); }
  has(k) { return this._p.some(x => x[0] === k); }
  set(k, v) { this.delete(k); this._p.push([k, String(v)]); }
  append(k, v) { this._p.push([k, String(v)]); }
  delete(k) { this._p = this._p.filter(x => x[0] !== k); }
  sort() { this._p.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0); }
  forEach(fn) { this._p.forEach(([k, v]) => fn(v, k, this)); }
  entries() { return this._p[Symbol.iterator](); }
  keys() { return this._p.map(x => x[0])[Symbol.iterator](); }
  values() { return this._p.map(x => x[1])[Symbol.iterator](); }
  [Symbol.iterator]() { return this.entries(); }
  toString() { return this._p.map(([k, v]) => encodeURIComponent(k) + '=' + encodeURIComponent(v)).join('&'); }
}
g.URLSearchParams = URLSearchParams;
const URL_RE = /^([a-zA-Z][a-zA-Z0-9+.-]*:)(?:\/\/(?:([^@\/?#]*)@)?([^\/?#:]*)(?::(\d+))?)?([^?#]*)(\?[^#]*)?(#.*)?$/;
function resolveURL(base, u) {
  if (/^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(u)) return u;
  const b = parseURL(base);
  if (u.startsWith('//')) return b.protocol + u;
  if (u.startsWith('/')) return b.protocol + '//' + b.host + u;
  if (u.startsWith('?')) return b.protocol + '//' + b.host + b.pathname + u;
  if (u.startsWith('#')) return b.protocol + '//' + b.host + b.pathname + b.search + u;
  const out = [];
  for (const p of (b.pathname.replace(/[^\/]*$/, '') + u).split('/')) { if (p === '..') { if (out.length > 1) out.pop(); } else if (p !== '.') out.push(p); }
  return b.protocol + '//' + b.host + out.join('/');
}
function parseURL(u, base) {
  if (base !== undefined) u = resolveURL(String(base), String(u));
  const m = URL_RE.exec(String(u));
  if (!m) throw new TypeError('Invalid URL: ' + u);
  return { protocol: m[1].toLowerCase(), username: m[2] || '', password: '', hostname: (m[3] || '').toLowerCase(),
    port: m[4] || '', get host() { return this.hostname + (this.port ? ':' + this.port : ''); },
    pathname: m[5] || (m[3] !== undefined ? '/' : ''), search: m[6] && m[6] !== '?' ? m[6] : '', hash: m[7] && m[7] !== '#' ? m[7] : '' };
}
class URL {
  constructor(u, base) { const p = parseURL(u, base); for (const k of ['protocol', 'username', 'password', 'hostname', 'port', 'pathname', 'hash']) this[k] = p[k]; this.searchParams = new URLSearchParams(p.search); }
  get host() { return this.hostname + (this.port ? ':' + this.port : ''); }
  get origin() { return this.protocol + '//' + this.host; }
  get search() { const q = this.searchParams.toString(); return q ? '?' + q : ''; }
  set search(v) { this.searchParams = new URLSearchParams(String(v)); }
  get href() { return this.protocol + '//' + this.host + this.pathname + this.search + this.hash; }
  set href(v) { const p = parseURL(v); for (const k of ['protocol', 'username', 'password', 'hostname', 'port', 'pathname', 'hash']) this[k] = p[k]; this.searchParams = new URLSearchParams(p.search); }
  toString() { return this.href; } toJSON() { return this.href; }
  static createObjectURL() { return 'blob:weave'; } static revokeObjectURL() {}
  static canParse(u, b) { try { parseURL(u, b); return true; } catch (e) { return false; } }
}
g.URL = URL; g.webkitURL = URL;
const loc = {
  get href() { return W.url; }, set href(v) { W.navigate(String(v)); },
  get protocol() { return parseURL(W.url).protocol; }, get host() { return parseURL(W.url).host; },
  get hostname() { return parseURL(W.url).hostname; }, get port() { return parseURL(W.url).port; },
  get pathname() { return parseURL(W.url).pathname; }, get search() { return parseURL(W.url).search; },
  get hash() { return parseURL(W.url).hash; }, set hash(v) {}, get origin() { const p = parseURL(W.url); return p.protocol + '//' + p.host; },
  assign(u) { W.navigate(String(u)); }, replace(u) { W.navigate(String(u)); }, reload() { W.navigate(W.url); },
  toString() { return W.url; },
};
Object.defineProperty(g, 'location', { get: () => loc, set: v => W.navigate(String(v)), configurable: true });
def(P, 'location', () => loc, v => W.navigate(String(v)));
g.history = { length: 1, state: null, scrollRestoration: 'auto', pushState(s) { this.state = s; }, replaceState(s) { this.state = s; }, back() {}, forward() {}, go() {} };
g.navigator = {
  userAgent: 'Mozilla/5.0 (X11; CareOS x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Weave/1.0 Chrome/124.0 Safari/537.36',
  appName: 'Netscape', appVersion: '5.0', appCodeName: 'Mozilla', product: 'Gecko', platform: 'CareOS', language: 'en-US', languages: ['en-US', 'en'],
  cookieEnabled: false, onLine: true, vendor: '', hardwareConcurrency: 1, maxTouchPoints: 0, doNotTrack: '1',
  webdriver: false, plugins: [], mimeTypes: [], sendBeacon() { return true; }, javaEnabled() { return false; },
  clipboard: { writeText() { return Promise.resolve(); }, readText() { return Promise.resolve(''); } },
  permissions: { query() { return Promise.resolve({ state: 'denied' }); } },
  storage: { estimate() { return Promise.resolve({ quota: 0, usage: 0 }); } },
};
g.screen = { width: W.width, height: W.height, availWidth: W.width, availHeight: W.height, colorDepth: 24, pixelDepth: 24, orientation: { type: 'landscape-primary', angle: 0 } };
g.innerWidth = g.outerWidth = W.width; g.innerHeight = g.outerHeight = W.height;
g.devicePixelRatio = 1; g.scrollX = g.scrollY = g.pageXOffset = g.pageYOffset = 0;
g.scrollTo = g.scroll = (x, y) => { if (x && typeof x === 'object') y = x.top; W.scrollTo(0, y | 0); };
g.scrollBy = () => {};
g.alert = m => W.log('alert: ' + m); g.confirm = () => false; g.prompt = () => null; g.print = () => {};
g.open = u => { if (u) W.navigate(String(u)); return null; };
g.close = g.focus = g.blur = g.stop = g.postMessage = () => {};
g.getComputedStyle = el => {
  const o = el && el._computed ? el._computed() : {};
  o.getPropertyValue = n => o[String(n).replace(/-([a-z])/g, (_, c) => c.toUpperCase())] || '';
  return o;
};
g.matchMedia = q => ({ matches: W.mediaMatches(String(q)), media: String(q), onchange: null,
  addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {} });
g.performance = { now: () => W.now(), timeOrigin: 0, timing: {}, navigation: {}, mark() {}, measure() {}, clearMarks() {},
  getEntries() { return []; }, getEntriesByName() { return []; }, getEntriesByType() { return []; } };
class Storage {
  constructor() { this._m = new Map(); }
  get length() { return this._m.size; }
  key(i) { return [...this._m.keys()][i] ?? null; }
  getItem(k) { k = String(k); return this._m.has(k) ? this._m.get(k) : null; }
  setItem(k, v) { this._m.set(String(k), String(v)); }
  removeItem(k) { this._m.delete(String(k)); }
  clear() { this._m.clear(); }
}
g.Storage = Storage; g.localStorage = new Storage(); g.sessionStorage = new Storage();
const NoopObserver = class { constructor(cb) { this.cb = cb; } observe() {} unobserve() {} disconnect() {} takeRecords() { return []; } };
g.MutationObserver = g.ResizeObserver = g.PerformanceObserver = g.ReportingObserver = NoopObserver;
g.IntersectionObserver = class {
  constructor(cb) { this.cb = cb; }
  observe(el) {
    const cb = this.cb, self = this;
    g.setTimeout(() => { const r = el.getBoundingClientRect(); cb([{ target: el, isIntersecting: true, intersectionRatio: 1,
      boundingClientRect: r, intersectionRect: r, rootBounds: null, time: W.now() }], self); }, 0);
  }
  unobserve() {} disconnect() {} takeRecords() { return []; }
};
g.CSS = { supports: () => false, escape: s => String(s).replace(/([^\w-])/g, '\\$1') };
g.fetch = () => Promise.reject(new TypeError('fetch is not supported in Weave yet'));
g.XMLHttpRequest = class {
  constructor() { this.readyState = 0; this.status = 0; this.responseText = ''; }
  open() { this.readyState = 1; }
  send() { g.setTimeout(() => { this.readyState = 4; if (this.onreadystatechange) this.onreadystatechange(); if (this.onerror) this.onerror(new Event('error')); }, 0); }
  setRequestHeader() {} abort() {} addEventListener(t, f) { this['on' + t] = f; } removeEventListener() {}
  getAllResponseHeaders() { return ''; } getResponseHeader() { return null; } overrideMimeType() {}
};
g.WebSocket = g.EventSource = class { constructor() { g.setTimeout(() => { if (this.onerror) this.onerror(new Event('error')); }, 0); } send() {} close() {} addEventListener() {} removeEventListener() {} };
g.Worker = g.SharedWorker = class { postMessage() {} terminate() {} addEventListener() {} };
g.BroadcastChannel = class { postMessage() {} close() {} addEventListener() {} };
g.AbortController = class { constructor() { this.signal = { aborted: false, addEventListener() {}, removeEventListener() {} }; } abort() { this.signal.aborted = true; } };
g.Image = function () { return doc.createElement('img'); };
g.customElements = { define() {}, get() { return undefined; }, whenDefined() { return Promise.resolve(); }, upgrade() {} };
g.crypto = {
  getRandomValues() { throw new Error('Secure randomness is unavailable'); },
  randomUUID() { throw new Error('Secure randomness is unavailable'); },
  subtle: {},
};
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
g.atob = s => { let o = '', b = 0, n = 0; for (const c of String(s).replace(/[^A-Za-z0-9+\/]/g, '')) { b = (b << 6) | B64.indexOf(c); n += 6; if (n >= 8) { n -= 8; o += String.fromCharCode((b >> n) & 255); } } return o; };
g.btoa = s => { s = String(s); let o = ''; for (let i = 0; i < s.length; i += 3) { const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2); const t = (a << 16) | ((b || 0) << 8) | (c || 0); o += B64[t >> 18 & 63] + B64[t >> 12 & 63] + (i + 1 < s.length ? B64[t >> 6 & 63] : '=') + (i + 2 < s.length ? B64[t & 63] : '='); } return o; };
g.TextEncoder = class { encode(s) { return new Uint8Array(unescape(encodeURIComponent(String(s))).split('').map(c => c.charCodeAt(0))); } };
g.TextDecoder = class { decode(a) { try { return decodeURIComponent(escape(String.fromCharCode(...(a || [])))); } catch (e) { return ''; } } };
g.getSelection = () => ({ rangeCount: 0, removeAllRanges() {}, addRange() {}, toString() { return ''; } });
if (!g.Intl) g.Intl = {
  DateTimeFormat: class { format(d) { return new Date(d).toLocaleString(); } formatToParts() { return []; } resolvedOptions() { return { timeZone: 'UTC', locale: 'en-US' }; } },
  NumberFormat: class { format(n) { return String(n); } resolvedOptions() { return { locale: 'en-US' }; } },
  PluralRules: class { select(n) { return n === 1 ? 'one' : 'other'; } },
  Collator: class { compare(a, b) { return a < b ? -1 : a > b ? 1 : 0; } },
  RelativeTimeFormat: class { format(v, u) { return v + ' ' + u; } },
};
})();
