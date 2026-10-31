'use strict';
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
// namfx web UI (no dependencies). Source language: English; translations in lang/<code>.json.

const CATS = {
  dyn: ['#22E0A0', 'Dynamics'], drive: ['#FF3B5C', 'Drive'], amp: ['#FFB300', 'Amp'], cab: ['#B8F542', 'Cab (IR)'],
  eq: ['#2F9BFF', 'EQ'], mod: ['#9B5CFF', 'Modulation'], dly: ['#00E5FF', 'Delay'], rev: ['#FF4FD8', 'Reverb'],
  util: ['#A0AEC0', 'Utility'], route: ['#FF7A1A', 'Routing'],
};
const LANE = { a: '#FF7A1A', b: '#5CC8FF' };
const SCENE_COLORS = ['#2F9BFF', '#FF4FD8', '#FF3B5C', '#9B5CFF', '#22E0A0', '#FFB300'];
const STRINGS = ['E', 'A', 'D', 'G', 'B', 'E'];

const $ = (s) => document.querySelector(s);
const el = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v == null || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k === 'style') e.style.cssText = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const k of kids.flat()) if (k != null && k !== false) e.append(k.nodeType ? k : document.createTextNode(k));
  return e;
};

const S = { state: null, rawCatalog: [], catalog: [], catalogVersion: 0, status: {}, sel: null, tab: 'rig', dragging: false, pending: null, ws: null, rid: 1, cbs: {}, lang: null, dict: {}, view: 'tiles' };
const THEMES = [['dark', 'Dark'], ['light', 'Light'], ['contrast', 'High contrast'], ['retro', 'Retro amber']];

// ------------------------------------------------------------ i18n

// t('key', 'English text', [args]) – {0}, {1} placeholders
function t(key, en, args) {
  let s = S.dict[key];
  if (typeof s !== 'string' || !s) s = en;
  if (args) args.forEach((a, i) => { s = s.split('{' + i + '}').join(a); });
  return s;
}

async function loadLang(code) {
  if (code === S.lang) return;
  S.lang = code;
  if (code === 'en') S.dict = {};
  else {
    try { S.dict = await (await fetch(`lang/${code}.json`, { cache: 'no-cache' })).json(); } catch { S.dict = {}; }
  }
  document.documentElement.lang = code;
  localizeCatalog();
  applyStatic();
  renderConn();
  renderAll();
  initTunerStrings();
}

function renderConn() {
  const on = S.ws && S.ws.readyState === 1;
  $('#conn').textContent = on ? t('ui.online', 'online') : t('ui.offline', 'offline');
  $('#conn').classList.toggle('off', !on);
}

// catalog with translated names/desc/params/options (same keys as the on-screen UI)
// type files may carry their own translations: tr.<lang>.{name, desc, params.<id>.{name, options}}
function localizeCatalog() {
  S.catalog = S.rawCatalog.map((d) => {
    const tr = (d.tr && d.tr[S.lang]) || {};
    const tp = tr.params || {};
    return {
      ...d,
      name: t('fx.' + d.type, tr.name || d.name),
      desc: t('fx.' + d.type + '.desc', tr.desc || d.desc),
      params: d.params.map((p) => ({
        ...p,
        name: t(`p.${d.type}.${p.id}`, (tp[p.id] && tp[p.id].name) || p.name),
        options: p.options && p.options.map((o, i) => t(`o.${d.type}.${p.id}.${i}`, (tp[p.id] && tp[p.id].options && tp[p.id].options[i]) || o)),
      })),
    };
  });
}

function refetchCatalog() {
  send({ cmd: 'catalog' }, (r) => {
    if (!r.ok) return;
    S.rawCatalog = r.catalog;
    S.catalogVersion = r.catalog_version;
    localizeCatalog();
    renderAll();
  });
}

// static HTML: data-i18n="key" (text), data-i18n-attr="attr:key"
function applyStatic() {
  document.querySelectorAll('[data-i18n]').forEach((e) => {
    if (!e.dataset.en) e.dataset.en = e.textContent;
    e.textContent = t(e.dataset.i18n, e.dataset.en);
  });
  document.querySelectorAll('[data-i18n-attr]').forEach((e) => {
    const [attr, key] = e.dataset.i18nAttr.split(':');
    if (!e.dataset.enAttr) e.dataset.enAttr = e.getAttribute(attr) || '';
    e.setAttribute(attr, t(key, e.dataset.enAttr));
  });
}

const errText = (m) => t(m.code || 'err.generic', m.error || 'Error') + (m.code && m.detail ? ': ' + m.detail : '');

// ------------------------------------------------------------ connection

function connect() {
  const ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
  S.ws = ws;
  ws.onopen = renderConn;
  ws.onclose = () => { renderConn(); setTimeout(connect, 1500); };
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.t === 'hello') { S.rawCatalog = m.catalog; S.catalogVersion = m.catalog_version; S.net = m.net; localizeCatalog(); setState(m); }
    else if (m.t === 'state') setState(m);
    else if (m.t === 'status') setStatus(m);
    else {
      if (m.rid && S.cbs[m.rid]) { S.cbs[m.rid](m); delete S.cbs[m.rid]; }
      if (m.ok === false) toast(errText(m));
    }
  };
}

function send(cmd, cb) {
  if (!S.ws || S.ws.readyState !== 1) { toast(t('ui.no_connection', 'No connection to the console')); return; }
  if (cb) { cmd.rid = S.rid++; S.cbs[cmd.rid] = cb; }
  S.ws.send(JSON.stringify(cmd));
}

// smooth parameter updates (max ~25x/s per parameter)
const pend = new Map();
let flushT = null;
function sendParam(id, param, value) {
  pend.set(id + '|' + param, { cmd: 'set_param', id, param, value });
  if (!flushT) flushT = setTimeout(() => { for (const c of pend.values()) send(c); pend.clear(); flushT = null; }, 40);
}

function toast(msg, ok) {
  const e = $('#toast');
  e.textContent = msg;
  e.className = 'toast' + (ok ? ' ok' : '');
  e.hidden = false;
  clearTimeout(toast.t);
  toast.t = setTimeout(() => (e.hidden = true), 2600);
}

// ------------------------------------------------------------ helpers

const def = (type) => S.catalog.find((d) => d.type === type);
const color = (b) => { const d = def(b.type); return d ? CATS[d.cat][0] : '#fff'; };
const base = (f) => (f || '').replace(/\.[^.]+$/, '');
const catName = (k) => t('cat.' + k, CATS[k][1]);
const num = (x, d) => x.toFixed(d).replace('.', t('fmt.decimal', '.'));

function fmt(p, v) {
  if (p.kind === 'bool') return v > 0.5 ? t('ui.on', 'On') : t('ui.off', 'Off');
  if (p.kind === 'enum') return p.options[Math.round(v)] ?? v;
  switch (p.unit) {
    case 'pan': return Math.abs(v) < 0.5 ? 'C' : (v < 0 ? 'L' : 'R') + Math.round(Math.abs(v));
    case 'Hz': return v >= 1000 ? num(v / 1000, 1) + ' kHz' : Math.round(v) + ' Hz';
    case 'dB': return (v > 0 ? '+' : '') + num(v, 1) + ' dB';
    case 'ms': return (v < 10 ? num(v, 1) : Math.round(v)) + ' ms';
    case '%': return Math.round(v) + ' %';
    case ':1': return num(v, 1) + ':1';
    default: return p.step >= 1 ? String(Math.round(v)) : num(v, 2);
  }
}
const fmtLoad = (v) => (v == null ? '' : `CPU ${num(v, 1)} %`);
const visParams = (d) => d.params.filter((p) => !p.hidden);
const toPos = (p, v) => (p.log && p.min > 0 ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min)) * 1000;
const fromPos = (p, x) => {
  const f = x / 1000;
  const v = p.log && p.min > 0 ? p.min * Math.pow(p.max / p.min, f) : p.min + f * (p.max - p.min);
  return p.step ? p.min + Math.round((v - p.min) / p.step) * p.step : v;
};

function summary(b) {
  if (b.file) return (b.info && b.info.name) || base(b.file);
  if (b.error) return b.error;
  const d = def(b.type);
  if (!d) return '';
  if (d.type === 'nam' || d.type === 'ir') return t('ui.no_file', 'no file - pick one');
  return visParams(d).slice(0, 2).map((p) => fmt(p, b.params[p.id])).join(' · ');
}

// ------------------------------------------------------------ state

function setState(st) {
  if (S.dragging) { S.pending = st; return; }
  S.state = st;
  const ui = st.ui || {};
  document.documentElement.dataset.theme = ui.theme || 'dark';
  S.view = ui.view === 'pedals' ? 'pedals' : 'tiles';
  if (st.catalog_version && S.catalogVersion && st.catalog_version !== S.catalogVersion) refetchCatalog();
  if (st.lang && st.lang !== S.lang) { loadLang(st.lang); return; }
  renderAll();
}

function setStatus(m) {
  S.status = m;
  const cpu = m.cpu || 0;
  const c = $('#cpu');
  c.textContent = `CPU ${Math.round(cpu)} %`;
  c.style.color = cpu < 60 ? 'var(--ok)' : cpu < 85 ? 'var(--warn)' : 'var(--err)';
  const w = (db) => Math.max(0, Math.min(100, ((db + 60) / 60) * 100)) + '%';
  $('#mIn').style.width = w(m.in_db);
  $('#mOut').style.width = w(m.out_db);
  $('#mOut').style.background = m.out_db > -1 ? 'var(--err)' : '';  // clipping
  $('#stOut').style.width = w(m.out_db);
  $('#stOut').style.background = m.out_db > -1 ? 'var(--err)' : '';
  $('#stCpu').textContent = `CPU ${Math.round(cpu)} %`;
  $('#stMute').style.color = m.muted ? 'var(--err)' : '';
  $('#stMute').textContent = m.muted ? t('ui.unmute', 'Unmute') : t('ui.mute', 'Mute');
  $('#mInL').style.width = w(m.in_l_db);
  $('#mInR').style.width = w(m.in_r_db);
  const wz = m.wizard || {};
  $('#wizBtn').disabled = !!wz.on;
  $('#wizMsg').textContent = wz.on ? t('sys.wiz_play', 'Play your loudest chords…') :
    wz.result === 'ok' ? t('sys.wiz_ok', 'Done: the input gain was set.') : wz.result === 'nosignal' ? t('sys.wiz_none', 'No signal on the input.') : '';
  const mb = $('#muteBtn');
  mb.textContent = m.muted ? t('ui.unmute', 'Unmute') : t('ui.mute', 'Mute');
  mb.style.color = m.muted ? 'var(--err)' : '';
  if (m.loads) document.querySelectorAll('[data-load]').forEach((e) => { e.textContent = fmtLoad(m.loads[e.dataset.load]); });
  if (S.tab === 'tuner') renderTuner(m.tuner);
  if (S.tab === 'sys') renderSysInfo();
}

function renderAll() {
  const st = S.state;
  if (!st || !S.catalog.length) return;
  $('#stName').textContent = st.preset.name;
  $('#stScene').textContent = st.scene >= 0 ? t('ui.scene', 'Scene') + ' ' + (st.scene + 1) : '';
  $('#undoBtn').disabled = !st.can_undo;
  $('#redoBtn').disabled = !st.can_redo;
  const pl = st.files.presets;
  const idx = pl.indexOf(st.preset.file);
  $('#pnum').textContent = 'PRESET ' + (idx >= 0 ? String(idx + 1).padStart(2, '0') : '--') + ' · ' + t('ui.cpu_estimate', 'CPU estimate {0} %', [Math.round(st.cpu_est)]);
  $('#ptitle').textContent = st.preset.name;
  renderScenes();
  renderChain();
  renderFiles();
  renderSys();
}

// ------------------------------------------------------------ scenes

function renderScenes() {
  const box = $('#scenes');
  box.replaceChildren();
  const sc = S.state.scenes;
  sc.forEach((s, i) => {
    const c = SCENE_COLORS[i % SCENE_COLORS.length];
    let timer;
    const b = el('button', {
      class: i === S.state.scene ? 'on' : '', style: `--sc:${c}`,
      onclick: () => send({ cmd: 'scene_select', index: i }),
      oncontextmenu: (e) => { e.preventDefault(); storeScene(i, s.name); },
      onpointerdown: () => { timer = setTimeout(() => storeScene(i, s.name), 700); },
      onpointerup: () => clearTimeout(timer), onpointerleave: () => clearTimeout(timer),
    }, s.name);
    box.append(b);
  });
  if (!sc.length) box.append(el('span', { class: 'hint' }, t('ui.no_scenes_hint', 'No scenes - add one in System → Preset')));
}

function storeScene(i, name) {
  if (confirm(t('ui.confirm_scene_store', 'Store the current block on/off state into scene "{0}"?', [name]))) send({ cmd: 'scene_store', index: i });
}

// ------------------------------------------------------------ chain

function wire(c, grow) { return el('div', { class: 'wire' + (grow ? ' grow' : ''), style: `--wc:${c}` }); }

function blockEl(b, n, ctx) {
  const c = color(b);
  const d = def(b.type);
  const sel = S.sel === b.id;
  const box = el('div', { class: `blk${b.on ? '' : ' off'}${sel ? ' sel' : ''}${b.error ? ' err' : ''}`, style: `--c:${c}` });
  const row = el('div', { class: 'row', onclick: () => { S.sel = sel ? null : b.id; renderChain(); } },
    el('div', { class: 'num' }, n),
    el('div', { class: 'cat' }, d ? d.short : '?'),
    el('div', { class: 'nm' }, el('div', { class: 't' }, d ? (ctx.lane ? d.short : d.name) : b.type), el('div', { class: 's' }, summary(b))),
    el('button', { class: 'sw' + (b.on ? ' on' : ''), 'aria-label': t('ui.toggle', 'On/off'), onclick: (e) => { e.stopPropagation(); send({ cmd: 'set_on', id: b.id, on: !b.on }); } }));
  box.append(row);
  if (sel && !ctx.lane) box.append(editor(b, ctx));
  return box;
}

function paramRow(b, p, c) {
  const v = b.params[p.id];
  if (p.kind === 'enum' || p.kind === 'bool') {
    const opts = p.kind === 'bool' ? [t('ui.off', 'Off'), t('ui.on', 'On')] : p.options;
    return el('div', { class: 'prow', style: `--c:${c}` }, el('span', {}, p.name),
      el('div', { class: 'seg', style: 'grid-column: span 2' },
        opts.map((o, i) => el('button', { class: Math.round(v) === i ? 'on' : '', onclick: () => send({ cmd: 'set_param', id: b.id, param: p.id, value: i }) }, o))));
  }
  const out = el('output', {}, fmt(p, v));
  const r = el('input', { type: 'range', min: 0, max: 1000, step: 1, value: Math.round(toPos(p, v)), 'aria-label': p.name });
  r.addEventListener('pointerdown', () => (S.dragging = true));
  const end = () => { S.dragging = false; if (S.pending) { const st = S.pending; S.pending = null; setState(st); } };
  r.addEventListener('pointerup', end);
  r.addEventListener('change', end);
  r.addEventListener('input', () => {
    const nv = fromPos(p, +r.value);
    out.textContent = fmt(p, nv);
    b.params[p.id] = nv;
    sendParam(b.id, p.id, nv);
  });
  r.addEventListener('dblclick', () => { sendParam(b.id, p.id, p.def); });
  return el('label', { class: 'prow', style: `--c:${c}` }, el('span', {}, p.name), r, out);
}

function editor(b, ctx) {
  const d = def(b.type);
  const c = color(b);
  const ed = el('div', { class: 'edit', style: `--c:${c}` });
  if (d.file) {
    const files = S.state.files[d.file] || [];
    const s = el('select', { 'aria-label': t('ui.file', 'File'), onchange: (e) => send({ cmd: 'set_file', id: b.id, file: e.target.value }) },
      el('option', { value: '' }, files.length ? t('ui.pick_file_opt', '- pick a file -') : t('ui.upload_files_opt', '- upload files ({0}) -', [d.file])),
      files.map((f) => el('option', { value: f, selected: f === b.file }, f)));
    ed.append(s);
    if (b.info) {
      const i = b.info;
      const parts = [i.gear_make && `${i.gear_make} ${i.gear_model || ''}`, i.architecture, i.slimmable ? 'Lite/Full' : '', i.ms && `${Math.round(i.ms)} ms`].filter(Boolean);
      if (parts.length) ed.append(el('div', { class: 'info' }, parts.join(' · ')));
      if (i.warning) ed.append(el('div', { class: 'info warn' }, i.warning));
    }
    if (b.error) ed.append(el('div', { class: 'info err' }, b.error));
  }
  if (d.source === 'missing') ed.append(el('div', { class: 'info warn' }, t('ui.missing_type', 'This effect type is not available here (missing type file or LV2 plugin). The block passes the signal through and keeps its settings.')));
  else if (d.source === 'lv2' || d.user) ed.append(el('div', { class: 'info' }, [d.source === 'lv2' ? 'LV2' : t('ui.user_type', 'user type'), d.pack, d.author].filter(Boolean).join(' · ')));
  for (const p of visParams(d)) ed.append(paramRow(b, p, c));
  ed.append(el('div', { class: 'info load', 'data-load': b.id }, fmtLoad(S.status.loads && S.status.loads[b.id])));
  const acts = el('div', { class: 'actions' },
    el('button', { class: 'pill', onclick: () => send({ cmd: 'move', id: b.id, dir: -1 }) }, t('ui.move_up', '↑ up')),
    el('button', { class: 'pill', onclick: () => send({ cmd: 'move', id: b.id, dir: 1 }) }, t('ui.move_down', '↓ down')),
    el('button', { class: 'pill', onclick: () => openAdd({ after: b.id }) }, t('ui.add_after', '+ after this')));
  if (ctx.lane) acts.append(el('button', { class: 'pill', onclick: () => send({ cmd: 'move_to_lane', id: b.id, lane: 'main', split: ctx.split }) }, t('ui.to_main', '→ main chain')));
  else if (ctx.splits.length) {
    const sp = ctx.splits[ctx.splits.length - 1];
    acts.append(el('button', { class: 'pill', onclick: () => send({ cmd: 'move_to_lane', id: b.id, lane: 'a', split: sp }) }, t('ui.to_path_a', '→ path A')));
    acts.append(el('button', { class: 'pill', onclick: () => send({ cmd: 'move_to_lane', id: b.id, lane: 'b', split: sp }) }, t('ui.to_path_b', '→ path B')));
  }
  if (ctx.lane) acts.append(el('button', { class: 'pill', onclick: () => send({ cmd: 'move_to_lane', id: b.id, lane: ctx.lane === 'a' ? 'b' : 'a', split: ctx.split }) }, ctx.lane === 'a' ? t('ui.to_path_b', '→ path B') : t('ui.to_path_a', '→ path A')));
  acts.append(el('button', { class: 'pill danger', onclick: () => { if (confirm(t('ui.confirm_delete_block', 'Delete this block?'))) { send({ cmd: 'remove', id: b.id }); S.sel = null; } } }, t('ui.delete', 'Delete')));
  ed.append(acts);
  return ed;
}

function splitParams(n) {
  const d = def('split');
  const P = (id) => d.params.find((p) => p.id === id);
  const mode = Math.round(n.params.mode);
  const desc = [t('ui.split_y', 'Y - both paths get the full signal'),
    t('ui.split_ab', 'A/B balance {0} %', [Math.round(n.params.balance)]),
    t('ui.split_xover', 'Crossover {0}', [fmt(P('freq'), n.params.freq)]),
    t('ui.split_dyn', 'Dynamic, threshold {0}', [fmt(P('thresh'), n.params.thresh)])][mode];
  return { d, P, mode, desc };
}

function renderChain() {
  if (S.view === 'pedals') { renderPedals(); return; }
  const st = S.state;
  const box = $('#chain');
  const y = window.scrollY;
  box.replaceChildren();
  box.append(el('div', { class: 'jack' }, el('b', {}, 'IN'), st.input.source === 'wav' ? `WAV · ${st.input.file}` : t('ui.guitar_in', 'guitar · {0} kHz', [st.audio.rate / 1000])));
  let prev = '#F2F4F7';
  let n = 0;
  const splits = st.chain.filter((x) => x.type === 'split').map((x) => x.id);
  for (const node of st.chain) {
    if (node.type !== 'split') {
      box.append(wire(prev), blockEl(node, String(++n), { splits }));
      if (node.on) prev = color(node);
      continue;
    }
    const sp = splitParams(node);
    const sel = S.sel === node.id;
    box.append(wire(prev));
    const head = el('div', { class: 'split', onclick: () => { S.sel = sel ? null : node.id; renderChain(); } },
      el('span', { class: 'g' }, 'X'), el('span', { class: 'lbl' }, 'SPLIT'), el('span', { class: 'd' }, sp.desc),
      el('span', { class: 'info' }, sel ? t('ui.close', 'close') : t('ui.edit', 'edit')));
    box.append(head);
    if (sel) {
      const ed = el('div', { class: 'edit', style: `--c:${LANE.a}` });
      ed.append(paramRow(node, sp.P('mode'), LANE.a));
      if (sp.mode === 1) ed.append(paramRow(node, sp.P('balance'), LANE.a));
      if (sp.mode === 2) ed.append(paramRow(node, sp.P('freq'), LANE.a));
      if (sp.mode === 3) ed.append(paramRow(node, sp.P('thresh'), LANE.a));
      ed.append(el('div', { class: 'actions' },
        el('button', { class: 'pill', onclick: () => send({ cmd: 'move', id: node.id, dir: -1 }) }, t('ui.move_up', '↑ up')),
        el('button', { class: 'pill', onclick: () => send({ cmd: 'move', id: node.id, dir: 1 }) }, t('ui.move_down', '↓ down')),
        el('button', { class: 'pill danger', onclick: () => { if (confirm(t('ui.confirm_remove_split', 'Remove the split? Path A blocks stay, path B is deleted.'))) send({ cmd: 'remove', id: node.id }); } }, t('ui.remove_split', 'Remove split'))));
      box.append(ed);
    }
    box.append(forkSvg(prev, false));
    const lanes = el('div', { class: 'lanes' });
    for (const ln of ['a', 'b']) {
      const lc = LANE[ln];
      const lane = el('div', { class: 'lane', style: `--lc:${lc}` });
      lane.append(el('div', { class: 'lh' }, ln.toUpperCase() + (sp.mode === 2 ? (ln === 'a' ? ' · ' + t('ui.highs', 'highs') : ' · ' + t('ui.lows', 'lows')) : '')));
      let lp = lc;
      node[ln].forEach((b, i) => {
        lane.append(wire(lp), blockEl(b, ln.toUpperCase() + (i + 1), { lane: ln, split: node.id, splits: [] }));
        if (b.on) lp = color(b);
      });
      lane.append(wire(lp));
      const last = node[ln][node[ln].length - 1];
      lane.append(el('button', { class: 'addslot', onclick: () => openAdd(last ? { after: last.id } : { lane: { split: node.id, lane: ln } }) }, t('ui.add_to_path', '+ block to {0}', [ln.toUpperCase()])));
      lane.append(wire(lc, true));
      lanes.append(lane);
    }
    box.append(lanes);
    for (const ln of ['a', 'b']) {
      const i = node[ln].findIndex((b) => b.id === S.sel);
      if (i < 0) continue;
      const b = node[ln][i];
      const lc = LANE[ln];
      const wrap = el('div', { class: 'blk sel', style: `--c:${color(b)}; margin-top: 8px` },
        el('div', { class: 'row', onclick: () => { S.sel = null; renderChain(); } },
          el('div', { class: 'num', style: `background:${lc}` }, ln.toUpperCase() + (i + 1)),
          el('div', { class: 'nm' }, el('div', { class: 't' }, def(b.type).name), el('div', { class: 's' }, t('ui.path_x', 'path {0}', [ln.toUpperCase()]) + ' · ' + summary(b))),
          el('span', { class: 'info' }, t('ui.close', 'close'))),
        editor(b, { lane: ln, split: node.id, splits: [] }));
      box.append(wrap);
    }
    box.append(forkSvg(null, true), mergeEl(node));
    prev = '#F2F4F7';
  }
  box.append(wire(prev));
  box.append(el('div', { class: 'endrow' },
    el('div', { class: 'jack', style: 'flex:1' }, el('b', {}, 'OUT'), splits.length ? t('ui.out_stereo', 'headphones / line out · stereo') : t('ui.out', 'headphones / line out')),
    el('button', { class: 'pill', style: '--c:var(--eq); border-color: var(--eq); color: var(--eq)', onclick: () => openAdd({}) }, t('ui.add_block_btn', '+ block')),
    el('button', { class: 'pill', style: 'border-color: var(--laneA); color: var(--laneA)', onclick: () => send({ cmd: 'add_split' }) }, t('ui.add_split_btn', '+ split'))));
  window.scrollTo(0, y);
}

function forkSvg(c, join) {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('class', 'fork');
  svg.setAttribute('viewBox', '0 0 100 16');
  svg.setAttribute('preserveAspectRatio', 'none');
  const w = $('#chain').clientWidth || 358;
  const xa = (16.5 / w) * 100, xb = ((w / 2 + 5 + 16.5) / w) * 100;
  const mk = (d, col) => { const p = document.createElementNS(ns, 'path'); p.setAttribute('d', d); p.setAttribute('stroke', col); p.setAttribute('stroke-width', '3'); p.setAttribute('fill', 'none'); p.setAttribute('vector-effect', 'non-scaling-stroke'); svg.append(p); };
  if (!join) { mk(`M${xa} 0 V7 H${xb} V16`, LANE.b); mk(`M${xa} 0 V16`, LANE.a); }
  else { mk(`M${xb} 0 V9 H${xa}`, LANE.b); mk(`M${xa} 0 V16`, LANE.a); }
  return svg;
}

function mergeEl(n) {
  const d = def('split');
  const P = (id) => d.params.find((p) => p.id === id);
  const box = el('div', { class: 'merge' });
  const stereo = n.params.stereo > 0.5;
  box.append(el('div', { class: 'head' }, el('span', { class: 'g' }, 'Σ'), el('span', { class: 'lbl' }, 'MERGE'),
    el('div', { class: 'seg', style: '--c:var(--laneB); flex: 0 0 140px' },
      el('button', { class: stereo ? '' : 'on', onclick: () => send({ cmd: 'set_param', id: n.id, param: 'stereo', value: 0 }) }, 'Mono'),
      el('button', { class: stereo ? 'on' : '', onclick: () => send({ cmd: 'set_param', id: n.id, param: 'stereo', value: 1 }) }, 'Stereo'))));
  const ed = el('div', { class: 'edit' });
  for (const ln of ['A', 'B']) {
    const c = LANE[ln.toLowerCase()];
    ed.append(paramRow(n, P('level' + ln), c));
    if (stereo) ed.append(paramRow(n, P('pan' + ln), c));
  }
  ed.append(el('div', { class: 'actions' }, ['A', 'B'].map((ln) => {
    const on = n.params['inv' + ln] > 0.5;
    return el('button', { class: 'pill', style: on ? 'background:var(--err);color:var(--bg);border-color:var(--err)' : '', onclick: () => send({ cmd: 'set_param', id: n.id, param: 'inv' + ln, value: on ? 0 : 1 }) }, t('ui.phase_x', 'Ø phase {0}', [ln]));
  })));
  box.append(ed);
  return box;
}

// ------------------------------------------------------------ add block

let addCat = 'dyn', addSrc = 'all', addQ = '';
const SOURCES = [['all', 'src.0', 'All'], ['builtin', 'src.1', 'Built-in'], ['user', 'src.2', 'My types'], ['lv2', 'src.3', 'LV2']];
function srcOf(d) { return d.source === 'lv2' ? 'lv2' : d.user ? 'user' : 'builtin'; }

function openAdd(pos) {
  $('#sheetTitle').textContent = t('ui.add_block', 'Add block');
  const body = $('#sheetContent');
  const search = el('input', { type: 'text', class: 'search', placeholder: t('ui.search', 'Search effects…'), value: addQ, 'aria-label': t('ui.search', 'Search effects…') });
  const srcRow = el('div', { class: 'seg srcseg' });
  const cats = el('div', { class: 'cats' });
  const list = el('div', { class: 'fxlist' });
  const inPath = !!pos.lane;
  const pool = () => S.catalog.filter((d) => d.source !== 'missing' && !(inPath && d.type === 'split')
    && (addSrc === 'all' || srcOf(d) === addSrc)
    && (!addQ || `${d.name} ${d.desc} ${d.short} ${d.pack || ''} ${d.author || ''}`.toLowerCase().includes(addQ.toLowerCase())));
  const draw = () => {
    srcRow.replaceChildren(...SOURCES.filter(([k]) => k === 'all' || S.catalog.some((d) => d.source !== 'missing' && srcOf(d) === k))
      .map(([k, key, en]) => el('button', { class: k === addSrc ? 'on' : '', onclick: () => { addSrc = k; draw(); } }, t(key, en))));
    const p = pool();
    const counts = {};
    p.forEach((d) => { counts[d.cat] = (counts[d.cat] || 0) + 1; });
    if (addCat !== '*' && !counts[addCat]) addCat = Object.keys(CATS).find((k) => counts[k]) || '*';
    cats.replaceChildren(el('button', { class: addCat === '*' ? 'on' : '', style: '--c:var(--text)', onclick: () => { addCat = '*'; draw(); } }, `${t('cat.all', 'All')} ${p.length}`),
      ...Object.entries(CATS).filter(([k]) => counts[k]).map(([k, [c]]) =>
        el('button', { class: k === addCat ? 'on' : '', style: `--c:${c}`, onclick: () => { addCat = k; draw(); } }, `${catName(k)} ${counts[k]}`)));
    list.replaceChildren(...p.filter((d) => addCat === '*' || d.cat === addCat).map((d) => {
      const cpu = d.cpu_measured != null ? `${num(d.cpu_measured, 1)} % ✓` : `~${num(d.cpu, d.cpu < 1 ? 1 : 0)} %`;
      const badge = d.source === 'lv2' ? 'LV2' : d.user ? t('ui.user_short', 'USER') : '';
      return el('button', {
        class: 'fxrow', style: `--c:${(CATS[d.cat] || CATS.util)[0]}`,
        onclick: () => {
          const cmd = d.type === 'split' ? { cmd: 'add_split', ...pos } : { cmd: 'add_block', type: d.type, ...pos };
          if (d.type === 'split') delete cmd.lane;
          send(cmd, (r) => { if (r.ok) { S.sel = r.id; closeSheet(); } });
        },
      }, el('span', { class: 'sh' }, d.short),
      el('span', { class: 'tx' }, el('b', {}, d.name), el('small', {}, [d.desc, d.pack && d.source !== 'builtin' ? d.pack : ''].filter(Boolean).join(' · '))),
      el('span', { class: 'mt' }, badge ? el('i', {}, badge) : null, el('em', {}, cpu)));
    }));
    if (!list.children.length) list.append(el('div', { class: 'info' }, t('ui.no_effects', 'No effects here')));
  };
  search.addEventListener('input', () => { addQ = search.value; if (addQ) addCat = '*'; draw(); });
  draw();
  body.replaceChildren(search, srcRow, cats, list);
  $('#sheet').hidden = false;
  if (matchMedia('(pointer: fine)').matches) search.focus();
}
function closeSheet() { $('#sheet').hidden = true; }

// ------------------------------------------------------------ pedalboard view

function knobEl(p, v) {
  if (p.kind !== 'float') return el('div', { class: 'kn sw2' }, el('b', {}, fmt(p, v)), el('small', {}, p.name));
  const a = -135 + 270 * Math.max(0, Math.min(1, toPos(p, v) / 1000));
  return el('div', { class: 'kn' }, el('i', { style: `--a:${a}deg` }), el('small', {}, p.name));
}

function pedalEl(b, label, ctx) {
  const d = def(b.type);
  const c = color(b);
  const sel = S.sel === b.id;
  const ps = d ? visParams(d).slice(0, 3) : [];
  return el('div', {
    class: `pedal${b.on ? '' : ' off'}${sel ? ' sel' : ''}${b.error ? ' err' : ''}${ctx.lane ? ' small' : ''}`, style: `--c:${c}`,
    onclick: () => { S.sel = sel ? null : b.id; renderChain(); },
  }, el('div', { class: 'ped-head' }, el('span', { class: 'pn' }, label), el('span', { class: 'led' })),
  el('div', { class: 'ped-knobs' }, ps.map((p) => knobEl(p, b.params[p.id]))),
  el('div', { class: 'ped-name' }, d ? d.name : b.type),
  el('div', { class: 'ped-sub' }, b.file ? summary(b) : (d ? d.short : '')),
  el('button', { class: 'foot', 'aria-label': t('ui.toggle', 'On/off'), onclick: (e) => { e.stopPropagation(); send({ cmd: 'set_on', id: b.id, on: !b.on }); } }));
}

function renderPedals() {
  const st = S.state;
  const box = $('#chain');
  const y = window.scrollY;
  box.replaceChildren();
  box.append(el('div', { class: 'jack' }, el('b', {}, 'IN'), st.input.source === 'wav' ? `WAV · ${st.input.file}` : t('ui.guitar_in', 'guitar · {0} kHz', [st.audio.rate / 1000])));
  const board = el('div', { class: 'board' });
  const splits = st.chain.filter((x) => x.type === 'split').map((x) => x.id);
  let n = 0, selCtx = null, selBlock = null;
  for (const node of st.chain) {
    if (node.type !== 'split') {
      board.append(pedalEl(node, String(++n), { splits }));
      if (node.id === S.sel) { selBlock = node; selCtx = { splits }; }
      continue;
    }
    const sp = splitParams(node);
    const sel = S.sel === node.id;
    const ps = el('div', { class: 'psplit' });
    ps.append(el('div', { class: 'split', onclick: () => { S.sel = sel ? null : node.id; renderChain(); } },
      el('span', { class: 'g' }, 'X'), el('span', { class: 'lbl' }, 'SPLIT'), el('span', { class: 'd' }, sp.desc),
      el('span', { class: 'info' }, sel ? t('ui.close', 'close') : t('ui.edit', 'edit'))));
    if (sel) {
      const ed = el('div', { class: 'edit', style: `--c:${LANE.a}` });
      ed.append(paramRow(node, sp.P('mode'), LANE.a));
      if (sp.mode === 1) ed.append(paramRow(node, sp.P('balance'), LANE.a));
      if (sp.mode === 2) ed.append(paramRow(node, sp.P('freq'), LANE.a));
      if (sp.mode === 3) ed.append(paramRow(node, sp.P('thresh'), LANE.a));
      ed.append(el('div', { class: 'actions' },
        el('button', { class: 'pill', onclick: () => send({ cmd: 'move', id: node.id, dir: -1 }) }, t('ui.move_up', '↑ up')),
        el('button', { class: 'pill', onclick: () => send({ cmd: 'move', id: node.id, dir: 1 }) }, t('ui.move_down', '↓ down')),
        el('button', { class: 'pill danger', onclick: () => { if (confirm(t('ui.confirm_remove_split', 'Remove the split? Path A blocks stay, path B is deleted.'))) send({ cmd: 'remove', id: node.id }); } }, t('ui.remove_split', 'Remove split'))));
      ps.append(ed);
    }
    for (const ln of ['a', 'b']) {
      const row = el('div', { class: 'plane', style: `--lc:${LANE[ln]}` }, el('div', { class: 'lh' }, ln.toUpperCase()));
      node[ln].forEach((b, i) => {
        row.append(pedalEl(b, ln.toUpperCase() + (i + 1), { lane: ln }));
        if (b.id === S.sel) { selBlock = b; selCtx = { lane: ln, split: node.id, splits: [] }; }
      });
      const last = node[ln][node[ln].length - 1];
      row.append(el('button', { class: 'addslot', onclick: () => openAdd(last ? { after: last.id } : { lane: { split: node.id, lane: ln } }) }, '+'));
      ps.append(row);
    }
    ps.append(mergeEl(node));
    board.append(ps);
  }
  board.append(el('button', { class: 'addslot addped', onclick: () => openAdd({}) }, t('ui.add_block_btn', '+ block')));
  box.append(board);
  if (selBlock) {
    const d = def(selBlock.type);
    box.append(el('div', { class: 'blk sel', style: `--c:${color(selBlock)}; margin-top: 10px` },
      el('div', { class: 'row', onclick: () => { S.sel = null; renderChain(); } },
        el('div', { class: 'cat' }, d ? d.short : '?'),
        el('div', { class: 'nm' }, el('div', { class: 't' }, d ? d.name : selBlock.type), el('div', { class: 's' }, summary(selBlock))),
        el('button', { class: 'sw' + (selBlock.on ? ' on' : ''), 'aria-label': t('ui.toggle', 'On/off'), onclick: (e) => { e.stopPropagation(); send({ cmd: 'set_on', id: selBlock.id, on: !selBlock.on }); } })),
      editor(selBlock, selCtx)));
  }
  box.append(el('div', { class: 'endrow', style: 'margin-top: 10px' },
    el('div', { class: 'jack', style: 'flex:1' }, el('b', {}, 'OUT'), t('ui.out', 'headphones / line out')),
    el('button', { class: 'pill', style: 'border-color: var(--laneA); color: var(--laneA)', onclick: () => send({ cmd: 'add_split' }) }, t('ui.add_split_btn', '+ split'))));
  window.scrollTo(0, y);
}

// ------------------------------------------------------------ files

const FILE_KINDS = [['nam', 'files.nam', 'NAM models', '#FFB300', '.nam'], ['ir', 'files.ir', 'IRs (WAV)', '#B8F542', '.wav'],
  ['wav', 'files.wav', 'DI recordings (WAV)', '#5CC8FF', '.wav'], ['presets', 'files.presets', 'Presets', '#9B5CFF', '.json'],
  ['fx', 'files.fx', 'Effect types (JSON packs)', '#00E5FF', '.json']];

function renderFiles() {
  const box = $('#files');
  box.replaceChildren();
  for (const [kind, key, en, c, ext] of FILE_KINDS) {
    const up = el('input', { type: 'file', multiple: true, accept: ext, onchange: (e) => upload(kind, e.target.files) });
    box.append(el('h3', { style: `--c:${c}` }, el('span', { class: 'dot' }), t(key, en), el('label', { class: 'upload' }, up, t('ui.upload', '+ upload'))));
    const list = S.state.files[kind] || [];
    if (!list.length) box.append(el('div', { class: 'info' }, t('ui.empty', 'empty')));
    for (const f of list) {
      const item = el('div', { class: 'fitem' }, el('span', {}, f));
      if (kind === 'presets') item.append(el('button', { class: 'load', onclick: () => send({ cmd: 'preset_load', file: f }, (r) => r.ok && toast(t('ui.loaded_x', 'Loaded: {0}', [f]), true)) }, t('ui.load', 'load')));
      if (kind === 'wav') item.append(el('button', { class: 'load', onclick: () => send({ cmd: 'input', source: 'wav', file: f, play: true, loop: true }) }, t('ui.use_as_input', 'use as input')));
      item.append(el('button', { onclick: () => del(kind, f) }, t('ui.delete_lc', 'delete')));
      box.append(item);
    }
  }
  box.append(el('p', { class: 'info' }, t('ui.files_hint', 'Get NAM models and IRs e.g. from TONE3000 (tone3000.com). On the R36S use NAM A2 (Lite).')));
}

async function upload(kind, files) {
  for (const f of files) {
    const r = await fetch(`/api/upload?kind=${kind}&name=${encodeURIComponent(f.name)}`, { method: 'POST', body: f });
    const j = await r.json().catch(() => ({}));
    toast(j.ok ? t('ui.uploaded_x', 'Uploaded: {0}', [f.name]) : t('ui.upload_failed_x', 'Upload failed: {0}', [f.name]), j.ok);
  }
}

async function del(kind, f) {
  if (!confirm(t('ui.confirm_delete_file', 'Delete {0}?', [f]))) return;
  await fetch(`/api/delete?kind=${kind}&name=${encodeURIComponent(f)}`, { method: 'POST' });
}

// ------------------------------------------------------------ tuner

const noteName = (n) => t('note.' + n, n);  // e.g. B -> H in Czech

function renderTuner(tu) {
  const valid = !!tu;
  const cents = valid ? tu.cents : 0;
  const col = !valid ? 'var(--faint)' : Math.abs(cents) < 3 ? 'var(--ok)' : Math.abs(cents) < 15 ? 'var(--warn)' : 'var(--err)';
  $('#tNote').textContent = valid ? noteName(tu.note) : '·';
  $('#tNote').parentElement.style.color = col;
  $('#tOct').textContent = valid ? tu.octave : '';
  $('#tFreq').textContent = valid ? `${num(tu.freq, 1)} Hz · ${cents > 0 ? '+' : ''}${Math.round(cents)} ${t('ui.cents', 'cents')}` : t('ui.play_string', 'play a string');
  const pos = valid ? Math.max(0, Math.min(20, Math.round(cents / 5) + 10)) : -1;
  [...$('#tBar').children].forEach((c, i) => c.classList.toggle('lit', i === pos || i === 10));
  [...$('#tStrings').children].forEach((s) => s.classList.toggle('hit', valid && s.dataset.n === tu.note));
}

function initTunerStrings() {
  $('#tStrings').replaceChildren(...STRINGS.map((s, i) => el('span', { 'data-n': s }, i === 5 ? 'e' : noteName(s))));
}

function initTuner() {
  const bar = $('#tBar');
  for (let i = 0; i < 21; i++) {
    const d = Math.abs(i - 10);
    bar.append(el('i', { style: `background:${d <= 1 ? 'var(--ok)' : d <= 4 ? 'var(--warn)' : d <= 7 ? 'var(--amp)' : 'var(--err)'}` }));
  }
  initTunerStrings();
  $('#tMute').onchange = (e) => send({ cmd: 'set_global', key: 'tuner_mute', value: e.target.checked });
  $('#tA4').onchange = (e) => send({ cmd: 'set_global', key: 'a4', value: +e.target.value });
}

// ------------------------------------------------------------ system

function renderSys() {
  const st = S.state;
  const wav = st.input.source === 'wav';
  [...$('#srcSeg').children].forEach((b) => b.classList.toggle('on', b.dataset.src === st.input.source));
  $('#wavBox').style.display = wav ? '' : 'none';
  $('#wavSel').replaceChildren(...st.files.wav.map((f) => el('option', { value: f, selected: f === st.input.file }, f)));
  $('#wavLoop').checked = st.input.loop;
  if (!S.dragging) {
    $('#gIn').value = st.globals.input_gain;
    $('#gOut').value = st.globals.output_gain;
  }
  $('#gInV').textContent = fmt({ unit: 'dB' }, st.globals.input_gain);
  $('#gOutV').textContent = fmt({ unit: 'dB' }, st.globals.output_gain);
  $('#tMute').checked = st.globals.tuner_mute;
  $('#tA4').value = st.globals.a4;
  $('#langSel').replaceChildren(...(st.langs || []).map((l) => el('option', { value: l.code, selected: l.code === st.lang }, l.name)));
  renderNet();
  renderAudio();
  const lp = st.looper || { state: 'empty', seconds: 0, max_seconds: 30 };
  const lpNames = { empty: t('sys.loop_s_empty', 'empty'), rec: t('sys.loop_s_rec', 'recording'), play: t('sys.loop_s_play', 'playing'), overdub: t('sys.loop_s_over', 'overdubbing'), stopped: t('sys.loop_s_stop', 'stopped') };
  $('#loopInfo').replaceChildren(el('div', {}, `${lpNames[lp.state] || lp.state} · ${lp.seconds.toFixed(1)} / ${Math.round(lp.max_seconds)} s`));
  ['rec', 'play', 'over', 'stop'].forEach((k) => $('#loop' + k[0].toUpperCase() + k.slice(1)).classList.toggle('on', (k === 'over' ? 'overdub' : k) === lp.state));
  const me = st.metronome || { on: false, bpm: 100, volume: -12 };
  if (document.activeElement !== $('#metBpm')) $('#metBpm').value = Math.round(me.bpm);
  $('#metVol').value = me.volume;
  $('#metVolV').textContent = me.volume + ' dB';
  $('#metOn').textContent = me.on ? t('sys.metro_stop', 'Stop') : t('sys.metro_start', 'Start');
  $('#metOn').classList.toggle('on', me.on);
  const ui = st.ui || {};
  $('#themeSel').replaceChildren(...THEMES.map(([k, en]) => el('option', { value: k, selected: k === (ui.theme || 'dark') }, t('theme.' + k, en))));
  [...$('#viewSeg').children].forEach((b) => b.classList.toggle('on', b.dataset.view === S.view));
  const cnt = (f) => S.catalog.filter(f).length;
  $('#fxInfo').replaceChildren(...[
    [t('sys.fx_builtin', 'Built-in + bundled types'), cnt((d) => d.source === 'builtin' || (d.source === 'fx' && !d.user))],
    [t('sys.fx_user', 'Your effect types (data/fx)'), cnt((d) => d.source === 'fx' && d.user)],
    ['LV2', st.lv2 ? cnt((d) => d.source === 'lv2') : t('sys.lv2_off', 'not available in this build')],
    [t('sys.fx_missing', 'Missing types in this preset'), cnt((d) => d.source === 'missing')],
  ].flatMap(([k, v]) => [el('span', {}, k), el('b', {}, String(v))]));
}

function renderAudio() {
  const au = (S.state && S.state.audio_dev) || {}, act = au.active || {}, ch = au.choice || {};
  const cards = (au.cards || []).filter((c) => c.capture && c.playback && c.name !== 'Loopback');
  const rows = [[t('sys.aud_active', 'In use'), act.label || act.capture || '-'], [t('sys.aud_channel', 'Input channel'), [t('aud.left', 'Left'), t('aud.right', 'Right'), t('aud.sum', 'Sum')][act.channel] || '-']];
  if (act.missing) rows.push([t('sys.net_err', 'Error'), t('sys.aud_missing', 'card "{0}" is not connected', [act.missing])]);
  if (ch.output === 'bluetooth') rows.push([t('sys.aud_output', 'Output'), act.bt_route ? t('sys.aud_bt_on', 'Bluetooth route is active') : t('sys.aud_bt_off', 'Bluetooth headphones are not connected, playing on the card')]);
  if (au.error) rows.push([t('sys.net_err', 'Error'), au.error]);
  if (au.manual) rows.push([t('sys.aud_manual', 'Note'), t('sys.aud_manual_val', 'started with --capture / --playback / --in-channel: the choice below is ignored')]);
  $('#audInfo').replaceChildren(...rows.flatMap(([k, v]) => [el('span', {}, k), el('b', {}, String(v))]));
  const opts = [['auto', t('aud.auto', 'Auto')], ['internal', t('aud.internal', 'Internal')], ...cards.filter((c) => c.usb).map((c) => ['card:' + c.id, `USB: ${c.name}`])];
  const cur = ch.device || 'auto';
  if (!opts.some(([v]) => v === cur)) opts.push([cur, cur.replace(/^card:/, '') + ' (' + t('aud.unplugged', 'not connected') + ')']);
  if (document.activeElement !== $('#audDev')) $('#audDev').replaceChildren(...opts.map(([v, n]) => el('option', { value: v, selected: v === cur }, n)));
  [...$('#audCh').children].forEach((b) => b.classList.toggle('on', b.dataset.ch === (ch.channel || 'auto')));
  $('#audOut').value = ch.output || 'same';
}

function renderNet() {
  const st = S.state, n = st.network || {}, cfg = st.hotspot_cfg || {};
  const port = n.web_port && n.web_port !== 80 ? ':' + n.web_port : '';
  let rows;
  if (!n.available) rows = [[t('sys.net_status', 'Status'), t('sys.net_na', 'not available') + (n.error ? ` (${n.error})` : '')]];
  else if (n.hotspot) rows = [[t('sys.net_status', 'Status'), t('sys.net_ap', 'own network active')], [t('sys.ssid', 'Network name'), n.ap ? n.ap.ssid : ''],
    [t('sys.password', 'Password'), n.ap && n.ap.password ? n.ap.password : t('sys.net_open', 'none (open network)')], [t('sys.net_addr', 'Web UI'), `http://${n.ip || '10.42.0.1'}${port}`]];
  else rows = [[t('sys.net_status', 'Status'), n.wifi_state && n.wifi_state.startsWith('connected') ? t('sys.net_wifi', 'WiFi {0}', [n.connection]) : (n.wifi_state || '-')],
    [t('sys.net_addr', 'Web UI'), n.ip ? `http://${n.ip}${port}` : '-']];
  if (n.error && n.available) rows.push([t('sys.net_err', 'Error'), n.error]);
  $('#netInfo').replaceChildren(...rows.flatMap(([k, v]) => [el('span', {}, k), el('b', {}, String(v))]));
  const mode = cfg.mode || 'auto';
  [...$('#netMode').children].forEach((b) => b.classList.toggle('on', b.dataset.mode === mode));
  if (document.activeElement !== $('#netSsid')) $('#netSsid').value = cfg.ssid || 'namfx';
  if (document.activeElement !== $('#netPass')) $('#netPass').value = cfg.password || '';
  $('#netOpenHint').hidden = !(n.hotspot && n.ap && !n.ap.password);
  $('#netStart').hidden = !n.available || !!n.hotspot;
  $('#netStop').hidden = !n.hotspot;
}

function renderSysInfo() {
  const st = S.state, m = S.status;
  if (!st) return;
  $('#wavPlay').textContent = m.playing ? t('ui.stop', '■ Stop') : t('ui.play', '▶ Play');
  $('#wavPos').textContent = m.wav_len ? `${num(m.wav_pos, 1)} / ${num(m.wav_len, 1)} s` : '';
  const kv = [[t('sys.audio', 'Audio'), t('sys.audio_val', '{0} Hz, period {1} ({2} ms)', [st.audio.rate, st.audio.block, num(1000 * st.audio.block / st.audio.rate, 1)])],
    [t('sys.cpu', 'CPU (audio thread)'), `${Math.round(m.cpu || 0)} %`], [t('sys.cpu_est', 'Chain CPU estimate'), `${Math.round(st.cpu_est)} %`],
    [t('sys.xruns', 'Dropouts (xruns)'), m.xruns ?? 0], [t('sys.network', 'Network'), S.net || location.host]];
  $('#sysInfo').replaceChildren(...kv.flatMap(([k, v]) => [el('span', {}, k), el('b', {}, String(v))]));
}

function initSys() {
  [...$('#srcSeg').children].forEach((b) => (b.onclick = () => {
    if (b.dataset.src === 'wav') {
      const f = $('#wavSel').value || S.state.files.wav[0];
      if (!f) { toast(t('ui.upload_wav_first', 'Upload a WAV in the Files tab')); return; }
      send({ cmd: 'input', source: 'wav', file: f, loop: $('#wavLoop').checked, play: true });
    } else send({ cmd: 'input', source: 'live' });
  }));
  $('#wavSel').onchange = (e) => send({ cmd: 'input', source: 'wav', file: e.target.value, loop: $('#wavLoop').checked, play: true });
  $('#wavLoop').onchange = (e) => send({ cmd: 'input', source: 'wav', loop: e.target.checked });
  $('#wavPlay').onclick = () => send({ cmd: 'transport', play: !S.status.playing });
  $('#wavRew').onclick = () => send({ cmd: 'transport', rewind: true });
  for (const [id, key] of [['#gIn', 'input_gain'], ['#gOut', 'output_gain']]) {
    const r = $(id);
    r.addEventListener('pointerdown', () => (S.dragging = true));
    r.addEventListener('change', () => { S.dragging = false; if (S.pending) { const s = S.pending; S.pending = null; setState(s); } });
    r.addEventListener('input', () => { send({ cmd: 'set_global', key, value: +r.value }); $(id + 'V').textContent = fmt({ unit: 'dB' }, +r.value); });
  }
  $('#presetNew').onclick = () => { if (confirm(t('ui.confirm_new_preset', 'Start a new empty preset? Unsaved changes will be lost.'))) send({ cmd: 'preset_new', name: t('ui.new_preset', 'New preset') }); };
  $('#presetSaveAs').onclick = () => {
    const name = prompt(t('ui.preset_name', 'Preset name'), S.state.preset.name);
    if (name) send({ cmd: 'preset_save', name, file: '' }, (r) => r.ok && toast(t('ui.saved_x', 'Saved: {0}', [r.file]), true));
  };
  $('#sceneAdd').onclick = () => {
    const name = prompt(t('ui.scene_name', 'Scene name'), t('ui.scene_n', 'Scene {0}', [S.state.scenes.length + 1]));
    if (name) send({ cmd: 'scene_store', index: -1, name });
  };
  $('#langSel').onchange = (e) => send({ cmd: 'set_lang', lang: e.target.value });
  $('#themeSel').onchange = (e) => send({ cmd: 'set_ui', theme: e.target.value });
  $('#audDev').onchange = (e) => send({ cmd: 'audio_set', device: e.target.value });
  [...$('#audCh').children].forEach((b) => (b.onclick = () => send({ cmd: 'audio_set', channel: b.dataset.ch })));
  $('#audOut').onchange = (e) => send({ cmd: 'audio_set', output: e.target.value });
  $('#audScan').onclick = () => send({ cmd: 'audio_scan' });
  $('#muteBtn').onclick = () => send({ cmd: 'mute' });
  for (const [id, action] of [['loopRec', 'rec'], ['loopPlay', 'play'], ['loopOver', 'overdub'], ['loopStop', 'stop'], ['loopClear', 'clear']])
    $('#' + id).onclick = () => send({ cmd: 'looper', action });
  $('#wizBtn').onclick = () => send({ cmd: 'gain_wizard', on: true });
  $('#metBpm').onchange = (e) => send({ cmd: 'metronome', bpm: +e.target.value });
  $('#metVol').oninput = (e) => send({ cmd: 'metronome', volume: +e.target.value });
  $('#metOn').onclick = () => send({ cmd: 'metronome', on: !(S.state.metronome && S.state.metronome.on) });
  const taps = [];
  $('#metTap').onclick = () => {  // tap tempo: average of the last intervals, restarts after 2 s of silence
    const now = performance.now();
    if (taps.length && now - taps[taps.length - 1] > 2000) taps.length = 0;
    taps.push(now);
    if (taps.length > 6) taps.shift();
    if (taps.length >= 2) send({ cmd: 'metronome', bpm: Math.round(60000 / ((taps[taps.length - 1] - taps[0]) / (taps.length - 1))) });
  };
  $('#undoBtn').onclick = () => send({ cmd: 'undo' });
  $('#redoBtn').onclick = () => send({ cmd: 'redo' });
  [...$('#netMode').children].forEach((b) => (b.onclick = () => send({ cmd: 'net_set', mode: b.dataset.mode })));
  $('#netSave').onclick = () => send({ cmd: 'net_set', ssid: $('#netSsid').value, password: $('#netPass').value }, (r) => r.ok && toast(t('ui.saved', 'Saved'), true));
  $('#netStart').onclick = () => {
    if (confirm(t('sys.net_confirm', 'The console leaves the current WiFi and opens its own network "{0}". Connect your phone to it and open http://10.42.0.1. Continue?', [$('#netSsid').value])))
      send({ cmd: 'net_hotspot', on: true });
  };
  $('#netStop').onclick = () => send({ cmd: 'net_hotspot', on: false });
  [...$('#viewSeg').children].forEach((b) => (b.onclick = () => send({ cmd: 'set_ui', view: b.dataset.view })));
  $('#fxRescan').onclick = () => send({ cmd: 'fx_rescan' }, (r) => r.ok && toast(t('ui.fx_reloaded', 'Effect types reloaded'), true));
  $('#fxLogBtn').onclick = () => send({ cmd: 'catalog_log' }, (r) => {
    if (!r.ok) return;
    const box = $('#fxLog');
    box.hidden = false;
    box.replaceChildren(el('div', { class: 'info' }, 'LV2_PATH: ' + (r.lv2_path || '')),
      ...r.log.map((e) => el('div', { class: 'info' + (e.error || e.skipped ? ' warn' : '') },
        e.file ? `${e.file}: ${e.error || t('ui.n_types', '{0} types', [e.types])}` : `LV2 ${e.lv2}: ${e.skipped}`)));
  });
}

// ------------------------------------------------------------ header, tabs

let wakeLock = null;
async function keepAwake(on) {  // the stage view keeps the phone screen on
  try {
    if (on && 'wakeLock' in navigator) wakeLock = await navigator.wakeLock.request('screen');
    else if (wakeLock) { await wakeLock.release(); wakeLock = null; }
  } catch (e) { /* not supported or refused */ }
}

function initTop() {
  const step = (d) => {
    const pl = S.state.files.presets;
    if (!pl.length) return;
    let i = pl.indexOf(S.state.preset.file);
    i = (i + d + pl.length) % pl.length;
    send({ cmd: 'preset_load', file: pl[i] });
  };
  $('#viewBtn').onclick = () => send({ cmd: 'set_ui', view: S.view === 'pedals' ? 'tiles' : 'pedals' });
  $('#stPrev').onclick = () => step(-1);
  $('#stNext').onclick = () => step(1);
  $('#stMute').onclick = () => send({ cmd: 'mute' });
  $('#prevPreset').onclick = () => step(-1);
  $('#nextPreset').onclick = () => step(1);
  $('#ptitle').onclick = () => { const n = prompt(t('ui.preset_name', 'Preset name'), S.state.preset.name); if (n) send({ cmd: 'preset_rename', name: n }); };
  $('#savePreset').onclick = () => {
    if (!S.state.preset.file) { $('#presetSaveAs').click(); return; }
    send({ cmd: 'preset_save' }, (r) => r.ok && toast(t('ui.saved', 'Saved'), true));
  };
  document.querySelectorAll('.tabs button').forEach((b) => (b.onclick = () => {
    const prev = S.tab;
    S.tab = b.dataset.tab;
    document.querySelectorAll('.tabs button').forEach((x) => x.classList.toggle('active', x === b));
    document.querySelectorAll('.tab').forEach((x) => x.classList.toggle('active', x.id === 'tab-' + S.tab));
    keepAwake(S.tab === 'stage');
    if (S.tab === 'tuner') send({ cmd: 'tuner', on: true });
    else if (prev === 'tuner') send({ cmd: 'tuner', on: false });
  }));
  $('#sheetClose').onclick = closeSheet;
  $('#sheet').onclick = (e) => { if (e.target.id === 'sheet') closeSheet(); };
}

initTop();
initTuner();
initSys();
applyStatic();
connect();
