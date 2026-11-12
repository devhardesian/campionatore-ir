'use strict';

/* =========================================================================
 *  Campionatore IR — Web App
 *  Comunica con l'ESP32-S3 esclusivamente tramite le API REST JSON.
 * ========================================================================= */

// ---------------------------------------------------------------- Icone ----
const ICONS = {
  power: '<path d="M12 3v9"/><path d="M18.4 6.6a9 9 0 1 1-12.8 0"/>',
  mute: '<path d="M11 5 6 9H2v6h4l5 4z"/><path d="m22 9-6 6M16 9l6 6"/>',
  input: '<path d="M15 3h4a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-4"/><path d="m10 17 5-5-5-5"/><path d="M15 12H3"/>',
  menu: '<path d="M4 6h16M4 12h16M4 18h16"/>',
  home: '<path d="m3 10 9-7 9 7v10a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z"/>',
  back: '<path d="M9 14 4 9l5-5"/><path d="M4 9h11a5 5 0 0 1 0 10h-3"/>',
  chevUp: '<path d="m6 15 6-6 6 6"/>',
  chevDown: '<path d="m6 9 6 6 6-6"/>',
  chevLeft: '<path d="m15 6-6 6 6 6"/>',
  chevRight: '<path d="m9 6 6 6-6 6"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  minus: '<path d="M5 12h14"/>',
  edit: '<path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z"/>',
  check: '<path d="m5 12 5 5L20 7"/>',
  more: '<circle cx="12" cy="5" r="1.2" fill="currentColor"/><circle cx="12" cy="12" r="1.2" fill="currentColor"/><circle cx="12" cy="19" r="1.2" fill="currentColor"/>',
  tv: '<rect x="2" y="7" width="20" height="14" rx="2"/><path d="m17 2-5 5-5-5"/>',
  ac: '<rect x="2" y="4" width="20" height="9" rx="2"/><path d="M6 10h12"/><path d="M7 17c0 1.5-1 2-1 3M12 17v3M17 17c0 1.5 1 2 1 3"/>',
  grid: '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
  remote: '<rect x="7" y="2" width="10" height="20" rx="3"/><circle cx="12" cy="7" r="1.5"/><path d="M10 12h4M10 15.5h4"/>',
  send: '<path d="M3 12h4"/><path d="M11 8a5 5 0 0 1 0 8"/><path d="M15 5a9 9 0 0 1 0 14"/>',
  rec: '<circle cx="12" cy="12" r="5" fill="currentColor" stroke="none"/>',
  trash: '<path d="M3 6h18M8 6V4h8v2M6 6l1 14h10l1-14"/>',
  cool: '<path d="M12 2v20M3.3 7l17.4 10M3.3 17 20.7 7"/><path d="m9 4 3 2 3-2M9 20l3-2 3 2"/>',
  heat: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
  fan: '<path d="M12 12c-3-4-1-9 2-8s2 5-2 8zM12 12c4-3 9-1 8 2s-5 2-8-2zM12 12c3 4 1 9-2 8s-2-5 2-8zM12 12c-4 3-9 1-8-2s5-2 8 2z"/>',
  dry: '<path d="M12 2.7 6.3 8.4a8 8 0 1 0 11.4 0z"/>',
  download: '<path d="M12 3v12M7 10l5 5 5-5M5 21h14"/>',
  upload: '<path d="M12 21V9M7 14l5-5 5 5M5 3h14"/>',
  info: '<circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/>',
  share: '<path d="M12 3v12"/><path d="m8 7 4-4 4 4"/><path d="M8 11H6a2 2 0 0 0-2 2v7a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-7a2 2 0 0 0-2-2h-2"/>',
  addHome: '<rect x="3" y="3" width="18" height="18" rx="4"/><path d="M12 8v8M8 12h8"/>',
  wifi: '<path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><path d="M12 20h.01"/>',
  lock: '<rect x="4" y="11" width="16" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>',
  key: '<circle cx="7.5" cy="15.5" r="4.5"/><path d="m10.7 12.3 9.3-9.3M16 7l3 3M18 5l2 2"/>',
  refresh: '<path d="M21 12a9 9 0 1 1-2.64-6.36L21 8"/><path d="M21 3v5h-5"/>',
  eye: '<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
  eyeOff: '<path d="M17.94 17.94A10.07 10.07 0 0 1 12 19c-6.5 0-10-7-10-7a18.5 18.5 0 0 1 5.06-5.94"/><path d="M9.9 4.24A9.12 9.12 0 0 1 12 4c6.5 0 10 7 10 7a18.5 18.5 0 0 1-2.16 3.19"/><path d="m1 1 22 22"/><path d="M14.12 14.12a3 3 0 1 1-4.24-4.24"/>',
};

function icon(name) {
  const t = document.createElement('template');
  t.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICONS[name] || ''}</svg>`;
  return t.content.firstChild;
}

// -------------------------------------------------------------- Utilità ----
const $ = (sel) => document.querySelector(sel);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const countLabel = (n) => (n === 1 ? '1 tasto registrato' : `${n} tasti registrati`);

// Crea un elemento DOM. I testi sono sempre inseriti come nodi di testo.
function el(tag, props = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (v == null || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k === 'dataset') Object.assign(e.dataset, v);
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const kid of kids.flat(Infinity)) {
    if (kid == null || kid === false) continue;
    e.append(kid instanceof Node ? kid : String(kid));
  }
  return e;
}

// ------------------------------------------------------------------ API ----
class ApiError extends Error {
  constructor(message, status) {
    super(message);
    this.status = status;
  }
}

// timeoutMs: utile quando il dispositivo sta cambiando rete e la richiesta
// resterebbe appesa per decine di secondi invece di fallire.
async function request(method, path, params, body, timeoutMs = 0) {
  let url = path;
  const opts = { method, headers: {} };
  if (params) {
    const qs = new URLSearchParams(params).toString();
    if (method === 'GET' || method === 'DELETE') url += '?' + qs;
    else {
      opts.body = qs;
      opts.headers['Content-Type'] = 'application/x-www-form-urlencoded';
    }
  }
  if (body !== undefined) {
    opts.body = body;
    opts.headers['Content-Type'] = 'application/json';
  }
  let res;
  let timer = null;
  if (timeoutMs && typeof AbortController === 'function') {
    const ctl = new AbortController();
    opts.signal = ctl.signal;
    timer = setTimeout(() => ctl.abort(), timeoutMs);
  }
  try {
    res = await fetch(url, opts);
  } catch {
    throw new ApiError('Dispositivo non raggiungibile', 0);
  } finally {
    clearTimeout(timer);
  }
  let data = null;
  try { data = await res.json(); } catch { /* risposta senza JSON */ }
  if (!res.ok || (data && data.ok === false)) {
    throw new ApiError((data && data.error) || `Errore HTTP ${res.status}`, res.status);
  }
  return data;
}

const api = {
  remotes: () => request('GET', '/api/remotes', { lite: 1 }),
  create: (name, type) => request('POST', '/api/remotes', { name, type }),
  learn: (remote, button, label, gap) =>
    request('POST', '/api/learn', gap ? { remote, button, label, gap } : { remote, button, label }),
  learnStatus: () => request('GET', '/api/learn'),
  learnCancel: () => request('DELETE', '/api/learn'),
  send: (remote, button) => request('POST', '/api/send', { remote, button }),
  remove: (remote, button) => request('DELETE', '/api/delete', button ? { remote, button } : { remote }),
  importDb: (json) => request('POST', '/api/import', null, json),
  info: () => request('GET', '/api/info'),
  wifi: (timeoutMs) => request('GET', '/api/wifi', null, undefined, timeoutMs),
  wifiScan: (refresh) => request('GET', '/api/wifi/scan', refresh ? { refresh: 1 } : null, undefined, 6000),
  wifiConnect: (ssid, password) => request('POST', '/api/wifi/connect', { ssid, password }),
  wifiSave: (ssid, password) => request('POST', '/api/wifi/networks', { ssid, password }),
  wifiForget: (ssid) => request('DELETE', '/api/wifi/networks', { ssid }),
  apPassword: (password) => request('POST', '/api/wifi/ap', { password }),
};

// ---------------------------------------------------------------- Stato ----
const S = { remotes: [], loaded: false, edit: false };

const getRemote = (id) => S.remotes.find((r) => r.id === id);
const getButton = (remote, id) => remote && remote.buttons.find((b) => b.id === id);

async function reload() {
  const data = await api.remotes();
  S.remotes = data.remotes || [];
  S.loaded = true;
}

// ------------------------------------------------------------ Preset ----
const TYPES = {
  tv: { label: 'TV', desc: 'Layout con i tasti standard', icon: 'tv' },
  ac: { label: 'Condizionatore', desc: 'Temperatura, modalità e ventola', icon: 'ac' },
  custom: { label: 'Personalizzato', desc: 'Crei tu i tasti, con le tue etichette', icon: 'grid' },
};

const TV_KEYS = {
  power: 'Power', input: 'Input', mute: 'Mute',
  vol_up: 'Volume +', vol_down: 'Volume −', ch_up: 'Canale +', ch_down: 'Canale −',
  up: 'Su', down: 'Giù', left: 'Sinistra', right: 'Destra', ok: 'OK',
  menu: 'Menu', home: 'Home', back: 'Indietro',
};
for (let n = 0; n <= 9; n++) TV_KEYS['num_' + n] = 'Tasto ' + n;

const AC_MODES = [
  { id: 'cool', label: 'Cool', icon: 'cool' },
  { id: 'heat', label: 'Heat', icon: 'heat' },
  { id: 'fan', label: 'Fan', icon: 'fan' },
  { id: 'dry', label: 'Dry', icon: 'dry' },
];
const AC_FANS = [
  { id: 'auto', label: 'Auto' },
  { id: 'low', label: 'Bassa' },
  { id: 'mid', label: 'Media' },
  { id: 'high', label: 'Alta' },
];
const AC_TEMP = { min: 16, max: 30 };
// I condizionatori spesso inviano più pacchetti separati da pause > 15 ms:
// per questi tasti la cattura termina dopo 50 ms di silenzio.
const AC_GAP_MS = 50;

const acKey = (s) => `${s.mode}_${s.temp}_${s.fan}`;
const acMode = (id) => AC_MODES.find((m) => m.id === id) || AC_MODES[0];
const acFan = (id) => AC_FANS.find((f) => f.id === id) || AC_FANS[0];
const acLabel = (s) => `${acMode(s.mode).label} ${s.temp}°C · ventola ${acFan(s.fan).label}`;
const acShort = (s) => `${acMode(s.mode).label} ${s.temp}° ${acFan(s.fan).label}`;

function parseAcKey(id) {
  const m = /^(cool|heat|fan|dry)_(\d{2})_(auto|low|mid|high)$/.exec(id);
  return m && { mode: m[1], temp: Number(m[2]), fan: m[3] };
}

function acState(remoteId) {
  try {
    const s = JSON.parse(localStorage.getItem('ac:' + remoteId));
    if (s && acMode(s.mode).id === s.mode && acFan(s.fan).id === s.fan &&
        s.temp >= AC_TEMP.min && s.temp <= AC_TEMP.max) return s;
  } catch { /* storage non disponibile */ }
  return { mode: 'cool', temp: 24, fan: 'auto' };
}

function saveAcState(remoteId, s) {
  try { localStorage.setItem('ac:' + remoteId, JSON.stringify(s)); } catch { /* ignora */ }
}

// ------------------------------------------------------ Notifiche/modali ----
function toast(message, type = '') {
  const t = el('div', { class: `toast ${type}` }, message);
  $('#toasts').append(t);
  setTimeout(() => t.remove(), type === 'error' ? 4000 : 2200);
}

function openSheet({ title, body, actions = [], onClose }) {
  const sheet = el('div', { class: 'sheet', role: 'dialog', 'aria-modal': 'true' });
  const overlay = el('div', { class: 'overlay' }, sheet);
  let closed = false;

  const onKey = (e) => { if (e.key === 'Escape') ctl.close(); };
  const ctl = {
    render({ title, body, actions = [] }) {
      sheet.replaceChildren();
      if (title) sheet.append(el('h2', {}, title));
      if (body) sheet.append(body);
      if (actions.length) {
        sheet.append(el('div', { class: 'actions' }, actions.map((a) =>
          el('button', {
            class: `btn ${a.cls || ''}`, type: 'button',
            onclick: () => (a.onClick ? a.onClick(ctl) : ctl.close()),
          }, a.icon ? icon(a.icon) : null, a.label))));
      }
    },
    close() {
      if (closed) return;
      closed = true;
      overlay.remove();
      document.removeEventListener('keydown', onKey);
      if (onClose) onClose();
    },
  };

  overlay.addEventListener('click', (e) => { if (e.target === overlay) ctl.close(); });
  document.addEventListener('keydown', onKey);
  ctl.render({ title, body, actions });
  document.body.append(overlay);
  return ctl;
}

function confirmDialog(title, message, okLabel = 'Conferma', danger = false) {
  return new Promise((resolve) => {
    let ok = false;
    openSheet({
      title,
      body: el('p', { class: 'muted' }, message),
      actions: [
        { label: 'Annulla', cls: 'ghost' },
        { label: okLabel, cls: danger ? 'rec' : 'primary', onClick: (s) => { ok = true; s.close(); } },
      ],
      onClose: () => resolve(ok),
    });
  });
}

function promptDialog(title, { label, placeholder = '', okLabel = 'OK', maxLength = 40 } = {}) {
  return new Promise((resolve) => {
    let value = null;
    const input = el('input', { class: 'input', placeholder, maxlength: maxLength, enterkeyhint: 'done' });
    const submit = (s) => {
      const v = input.value.trim();
      if (!v) return input.focus();
      value = v;
      s.close();
    };
    const sheet = openSheet({
      title,
      body: el('div', { class: 'field' }, label ? el('label', {}, label) : null, input),
      actions: [{ label: 'Annulla', cls: 'ghost' }, { label: okLabel, cls: 'primary', onClick: submit }],
      onClose: () => resolve(value),
    });
    input.addEventListener('keydown', (e) => { if (e.key === 'Enter') submit(sheet); });
    setTimeout(() => input.focus(), 60);
  });
}

// ---------------------------------------------------------- Invio tasti ----
async function sendKey(btn, remoteId, buttonId, silent = false) {
  if (btn) {
    btn.classList.remove('sent');
    void btn.offsetWidth;  // riavvia l'animazione
    btn.classList.add('sent');
  }
  try {
    await api.send(remoteId, buttonId);
    if (navigator.vibrate) navigator.vibrate(15);
  } catch (e) {
    if (!(silent && e.status === 503)) toast(e.message, 'error');
  }
}

// Tocco: invia se il tasto è registrato, altrimenti avvia la registrazione.
// In modalità modifica il tocco (ri)registra sempre.
function pressKey(btn, remoteId, buttonId, name, gap) {
  const remote = getRemote(remoteId);
  if (!remote) return;
  if (S.edit || !getButton(remote, buttonId)) learn(remote, buttonId, name, gap);
  else sendKey(btn, remoteId, buttonId);
}

// Tasti con autorepeat (volume, canali, frecce): tenendo premuto si ripete l'invio.
function bindKey(btn, remoteId, buttonId, name, { repeat = false, gap } = {}) {
  let timer = null;
  let repeating = false;
  const stop = () => { clearTimeout(timer); clearInterval(timer); timer = null; };

  btn.addEventListener('click', () => {
    if (repeating) { repeating = false; return; }
    pressKey(btn, remoteId, buttonId, name, gap);
  });
  btn.addEventListener('contextmenu', (e) => e.preventDefault());
  if (!repeat) return;

  btn.addEventListener('pointerdown', () => {
    repeating = false;
    stop();
    if (S.edit || !getButton(getRemote(remoteId), buttonId)) return;
    timer = setTimeout(() => {
      repeating = true;
      sendKey(btn, remoteId, buttonId, true);
      timer = setInterval(() => sendKey(btn, remoteId, buttonId, true), 260);
    }, 450);
  });
  for (const ev of ['pointerup', 'pointerleave', 'pointercancel']) btn.addEventListener(ev, stop);
}

function delBadge(onDelete) {
  return el('span', {
    class: 'badge-del', role: 'button', 'aria-label': 'Elimina',
    onclick: (e) => { e.stopPropagation(); onDelete(); },
  }, '×');
}

function keyButton(remote, id, name, { icon: ic, caption, text, cls = '', repeat = false, gap } = {}) {
  const learned = !!getButton(remote, id);
  const b = el('button', {
    class: `key ${cls} ${learned ? 'learned' : 'unlearned'}`, type: 'button', 'aria-label': name, title: name,
  }, ic ? icon(ic) : null, text ? el('span', {}, text) : null, caption ? el('small', {}, caption) : null);
  bindKey(b, remote.id, id, name, { repeat, gap });
  if (S.edit && learned) b.append(delBadge(() => deleteButton(remote.id, id, name)));
  return b;
}

// -------------------------------------------------------- Apprendimento ----
async function learn(remote, buttonId, label, gap) {
  try {
    await api.learn(remote.id, buttonId, label, gap);
  } catch (e) {
    toast(e.message, 'error');
    return;
  }

  const anim = el('div', { class: 'learn-anim' }, icon('remote'));
  const bar = el('div');
  const status = el('p', { class: 'muted' }, 'In attesa del segnale…');
  let active = true;
  const sheet = openSheet({
    title: `Registra «${label}»`,
    body: el('div', { class: 'learn' }, anim,
      el('p', { class: 'small muted' },
        'Punta il telecomando originale verso il ricevitore, a pochi centimetri, e premi il tasto una volta.'),
      el('div', { class: 'progress' }, bar), status),
    actions: [{ label: 'Annulla', cls: 'ghost' }],
    onClose: () => {
      if (!active) return;
      active = false;
      api.learnCancel().catch(() => {});
    },
  });

  let failures = 0;
  while (active) {
    await sleep(300);
    if (!active) return;
    let st;
    try {
      st = await api.learnStatus();
      failures = 0;
    } catch {
      if (++failures >= 10) {
        active = false;
        showLearnFailure(sheet, 'Connessione con il dispositivo persa.', null);
      }
      continue;
    }
    if (!active) return;

    if (st.state === 'waiting' || st.state === 'capturing') {
      const left = Math.max(0, st.timeout - st.elapsed);
      bar.style.transform = `scaleX(${st.timeout ? left / st.timeout : 0})`;
      anim.classList.toggle('capturing', st.state === 'capturing');
      status.textContent = st.state === 'capturing'
        ? 'Ricezione del segnale…'
        : `In attesa del segnale… ${Math.ceil(left / 1000)} s`;
      continue;
    }

    active = false;
    const retry = () => learn(getRemote(remote.id) || remote, buttonId, label, gap);
    if (st.state === 'done') {
      try { await reload(); } catch { /* aggiornerà al prossimo caricamento */ }
      render();
      showLearnDone(sheet, st, remote.id, buttonId, label, retry);
    } else if (st.state === 'timeout') {
      showLearnFailure(sheet, 'Nessun segnale ricevuto. Avvicina il telecomando al ricevitore e riprova.', retry);
    } else if (st.state === 'cancelled') {
      sheet.close();
    } else {
      showLearnFailure(sheet, st.error || 'Errore durante l\'apprendimento.', retry);
    }
  }
}

function showLearnDone(sheet, st, remoteId, buttonId, label, retry) {
  const canvas = el('canvas', { class: 'wave', 'aria-label': 'Forma d\'onda del segnale' });
  sheet.render({
    title: `«${label}» registrato`,
    body: el('div', { class: 'learn' },
      el('div', { class: 'learn-anim done' }, icon('check')),
      el('p', {}, 'Segnale acquisito e salvato.'),
      el('div', { class: 'stats' },
        el('span', {}, `${Math.ceil(st.len / 2)} impulsi`),
        el('span', {}, `${(st.freq / 1000).toFixed(1)} kHz`),
        el('span', {}, `${(st.duration / 1000).toFixed(1)} ms`)),
      canvas),
    actions: [
      { label: 'Rifai', cls: 'ghost', onClick: (s) => { s.close(); retry(); } },
      { label: 'Prova', icon: 'send', onClick: () => sendKey(null, remoteId, buttonId) },
      { label: 'Fatto', cls: 'primary' },
    ],
  });
  requestAnimationFrame(() => drawWave(canvas, (st.signal && st.signal.raw) || []));
}

function showLearnFailure(sheet, message, retry) {
  sheet.render({
    title: 'Registrazione non riuscita',
    body: el('div', { class: 'learn' },
      el('div', { class: 'learn-anim fail' }, icon('remote')),
      el('p', { class: 'muted' }, message)),
    actions: retry
      ? [{ label: 'Chiudi', cls: 'ghost' }, { label: 'Riprova', cls: 'rec', onClick: (s) => { s.close(); retry(); } }]
      : [{ label: 'Chiudi', cls: 'primary' }],
  });
}

// Disegna i mark (LED acceso) del segnale campionato.
function drawWave(canvas, raw) {
  const w = canvas.clientWidth;
  const h = canvas.clientHeight;
  if (!w || !raw.length) return;
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.round(w * dpr);
  canvas.height = Math.round(h * dpr);
  const ctx = canvas.getContext('2d');
  ctx.scale(dpr, dpr);

  const total = raw.reduce((a, b) => a + b, 0);
  const pad = 8, top = 12, bottom = h - 12;
  const scale = (w - pad * 2) / total;
  ctx.strokeStyle = '#2a3456';
  ctx.beginPath();
  ctx.moveTo(pad, bottom);
  ctx.lineTo(w - pad, bottom);
  ctx.stroke();
  ctx.fillStyle = '#ff5470';
  let t = 0;
  raw.forEach((d, i) => {
    if (i % 2 === 0) ctx.fillRect(pad + t * scale, top, Math.max(1, d * scale), bottom - top);
    t += d;
  });
}

// ------------------------------------------------------------ Gestione ----
async function deleteButton(remoteId, buttonId, name) {
  if (!(await confirmDialog('Elimina tasto', `Eliminare il segnale registrato per «${name}»?`, 'Elimina', true))) return;
  try {
    await api.remove(remoteId, buttonId);
    await reload();
    render();
    toast('Tasto eliminato');
  } catch (e) {
    toast(e.message, 'error');
  }
}

async function deleteRemote(remote) {
  const msg = `Eliminare «${remote.name}» e ${remote.buttons.length === 1 ? 'il tasto registrato' : `tutti i ${remote.buttons.length} tasti registrati`}? L'operazione non è reversibile.`;
  if (!(await confirmDialog('Elimina telecomando', msg, 'Elimina', true))) return;
  try {
    await api.remove(remote.id);
    await reload();
    location.hash = '';
    toast('Telecomando eliminato');
  } catch (e) {
    toast(e.message, 'error');
  }
}

function createRemoteDialog() {
  let type = 'tv';
  const input = el('input', { class: 'input', placeholder: 'Es. TV Salotto', maxlength: 32, enterkeyhint: 'done' });
  const opts = Object.entries(TYPES).map(([id, t]) =>
    el('button', {
      class: 'type-opt' + (id === type ? ' sel' : ''), type: 'button',
      onclick: () => {
        type = id;
        opts.forEach((o, i) => o.classList.toggle('sel', Object.keys(TYPES)[i] === id));
      },
    }, el('span', { class: 'ic' }, icon(t.icon)), el('span', {}, el('b', {}, t.label), el('small', {}, t.desc))));

  const submit = async (s) => {
    const name = input.value.trim();
    if (!name) return input.focus();
    try {
      const res = await api.create(name, type);
      s.close();
      await reload();
      location.hash = '#/r/' + res.remote.id;
    } catch (e) {
      toast(e.message, 'error');
    }
  };

  const sheet = openSheet({
    title: 'Nuovo telecomando',
    body: el('div', {},
      el('div', { class: 'field' }, el('label', {}, 'Nome'), input),
      el('div', { class: 'field' }, el('label', {}, 'Tipo'), el('div', { class: 'type-grid' }, opts))),
    actions: [{ label: 'Annulla', cls: 'ghost' }, { label: 'Crea', cls: 'primary', onClick: submit }],
  });
  input.addEventListener('keydown', (e) => { if (e.key === 'Enter') submit(sheet); });
  setTimeout(() => input.focus(), 60);
}

async function addCustomKey(remote) {
  const label = await promptDialog('Nuovo tasto', {
    label: 'Etichetta del tasto', placeholder: 'Es. Play, Luce, Netflix…', okLabel: 'Registra',
  });
  if (!label) return;
  const id = 'k' + Date.now().toString(36) + Math.random().toString(36).slice(2, 5);
  learn(remote, id, label);
}

// ---------------------------------------------------- Backup e dispositivo ----
async function exportBackup() {
  try {
    const res = await fetch('/api/remotes');
    if (!res.ok) throw new Error();
    const url = URL.createObjectURL(await res.blob());
    const a = el('a', { href: url, download: `remotes-${new Date().toISOString().slice(0, 10)}.json` });
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 5000);
  } catch {
    toast('Esportazione non riuscita', 'error');
  }
}

function importBackup() {
  const input = el('input', { type: 'file', accept: '.json,application/json' });
  input.addEventListener('change', async () => {
    const file = input.files[0];
    if (!file) return;
    const ok = await confirmDialog('Importa backup',
      `Tutti i telecomandi attuali verranno sostituiti con il contenuto di «${file.name}». Continuare?`, 'Importa', true);
    if (!ok) return;
    const wait = openSheet({
      title: 'Importazione in corso',
      body: el('p', { class: 'muted' },
        'Sto riscrivendo i segnali sulla flash: con molti tasti possono volerci alcuni secondi.'),
    });
    try {
      const res = await api.importDb(await file.text());
      wait.close();
      await reload();
      render();
      toast(`Importati ${res.remotes} telecomandi` + (res.buttons ? `, ${res.buttons} tasti` : ''), 'ok');
    } catch (e) {
      wait.close();
      toast(e.message, 'error');
    }
  });
  input.click();
}

function formatUptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return d ? `${d} g ${h} h` : h ? `${h} h ${m} min` : `${m} min`;
}

async function infoDialog() {
  try {
    const i = await api.info();
    const kb = (b) => `${Math.round(b / 1024)} KB`;
    const rows = [
      ['Firmware', 'v' + i.version],
      ['Rete', i.mode === 'sta' ? `Wi-Fi «${i.ssid}»` : `Access Point «${i.ssid}»`],
      ['Indirizzo', i.ip],
      ['mDNS', i.hostname + '.local'],
      i.rssi != null && ['Segnale', `${i.rssi} dBm`],
      ['RAM libera', kb(i.heap)],
      i.psram > 0 && ['PSRAM libera', kb(i.psram)],
      ['Flash dati', `${kb(i.fsUsed)} / ${kb(i.fsTotal)}`],
      ['Tasti registrati', String(i.buttons ?? 0)],
      i.legacy && ['Database v1', 'presente (/api/legacy)'],
      ['Acceso da', formatUptime(i.uptime)],
    ].filter(Boolean);
    openSheet({
      title: 'Dispositivo',
      body: el('dl', { class: 'kv' }, rows.map(([k, v]) => [el('dt', {}, k), el('dd', {}, v)])),
      actions: [{ label: 'Chiudi', cls: 'primary' }],
    });
  } catch (e) {
    toast(e.message, 'error');
  }
}

// ---------------------------------------------------------------- Wi-Fi ----
// Reti salvate, aggiunta di una rete (scansione o nome inserito a mano) e
// password dell'Access Point. Le reti restano nella NVS del dispositivo.
const AP_PASS_RE = /^[\x20-\x7e]{8,63}$/;
const signalLevel = (rssi) => (rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1);
const signalText = (rssi) => ['', 'debole', 'discreto', 'buono', 'ottimo'][signalLevel(rssi)];
const minutes = (s) => { const m = Math.max(1, Math.round(s / 60)); return m === 1 ? 'un minuto' : `${m} minuti`; };

function signalBars(rssi) {
  const lvl = signalLevel(rssi);
  return el('span', { class: 'bars', role: 'img', 'aria-label': `Segnale ${signalText(rssi)}`, title: `${rssi} dBm` },
    [1, 2, 3, 4].map((i) => el('i', { class: i <= lvl ? 'on' : null })));
}

function passwordToggle(...inputs) {
  const b = el('button', { class: 'icon-btn pw-toggle', type: 'button', 'aria-label': 'Mostra password' }, icon('eye'));
  b.addEventListener('click', () => {
    const show = inputs[0].type === 'password';
    inputs.forEach((i) => { i.type = show ? 'text' : 'password'; });
    b.replaceChildren(icon(show ? 'eyeOff' : 'eye'));
    b.setAttribute('aria-label', show ? 'Nascondi password' : 'Mostra password');
  });
  return b;
}

const passwordInput = (props = {}) => el('input', {
  class: 'input', type: 'password', autocomplete: 'off', autocapitalize: 'off', autocorrect: 'off', spellcheck: 'false', ...props,
});

async function wifiDialog() {
  let w;
  try {
    w = await api.wifi(6000);
  } catch (e) {
    toast(e.message, 'error');
    return;
  }
  const sta = w.mode === 'sta';
  const go = (fn) => () => { sheet.close(); fn(); };

  const status = el('div', { class: 'net-status' },
    el('span', { class: 'ic' + (sta ? '' : ' warn') }, icon('wifi')),
    el('div', {},
      el('b', {}, sta ? `Connesso a «${w.ssid}»`
        : w.mode === 'connecting' ? `Connessione a «${w.ssid}»…` : `Access Point «${w.ap.ssid}»`),
      el('div', { class: 'small muted' }, sta
        ? `${w.ip} · segnale ${signalText(w.rssi)} (${w.rssi} dBm)`
        : 'Nessuna rete salvata raggiungibile'),
      sta && w.ap.active && el('div', { class: 'small muted' },
        `Access Point ancora attivo${w.ap.offIn != null ? ` per ${minutes(w.ap.offIn)}` : ''}`)));

  const saved = w.networks.length
    ? el('div', { class: 'net-list' }, w.networks.map((n) => el('div', { class: 'net-row' },
      icon(n.open ? 'wifi' : 'lock'),
      el('span', { class: 'net-name' }, n.ssid),
      sta && n.ssid === w.ssid && el('span', { class: 'tag ok' }, 'in uso'),
      n.firmware
        ? el('span', { class: 'tag', title: 'Definita in config.h' }, 'firmware')
        : el('button', {
          class: 'icon-btn sm', type: 'button', 'aria-label': `Dimentica ${n.ssid}`,
          onclick: () => forgetNetwork(sheet, n.ssid, sta && n.ssid === w.ssid),
        }, icon('trash')))))
    : el('p', { class: 'small muted' }, 'Nessuna rete salvata: il dispositivo resta in modalità Access Point.');

  const sheet = openSheet({
    title: 'Wi-Fi',
    body: el('div', {},
      status,
      el('div', { class: 'net-section' },
        el('h3', { class: 'section-title' }, 'Reti salvate'),
        saved,
        el('button', { class: 'btn block', type: 'button', onclick: go(() => addNetworkDialog(w)) },
          icon('plus'), 'Aggiungi rete')),
      el('div', { class: 'net-section' },
        el('h3', { class: 'section-title' }, 'Access Point'),
        el('p', { class: 'small muted' },
          `La rete «${w.ap.ssid}» si accende quando nessuna rete salvata è raggiungibile: collegati e apri http://192.168.4.1.`),
        w.ap.defaultPassword && el('div', { class: 'note warn' }, icon('lock'),
          'Usa ancora la password predefinita: chiunque la conosca può collegarsi.'),
        el('button', { class: 'btn block', type: 'button', onclick: go(() => apPasswordDialog(w)) },
          icon('key'), 'Cambia password'))),
    actions: [{ label: 'Chiudi', cls: 'primary' }],
  });
}

async function forgetNetwork(sheet, ssid, inUse) {
  const msg = inUse
    ? `Il dispositivo resta collegato a «${ssid}» fino al prossimo riavvio, poi non la userà più.`
    : `Il dispositivo non si collegherà più a «${ssid}».`;
  if (!(await confirmDialog('Dimentica rete', msg, 'Dimentica', true))) return;
  try {
    await api.wifiForget(ssid);
    toast(`Rete «${ssid}» dimenticata`);
    sheet.close();
    wifiDialog();
  } catch (e) {
    toast(e.message, 'error');
  }
}

function addNetworkDialog(w) {
  const hint = el('p', { class: 'small muted' }, 'Ricerca delle reti…');
  const list = el('div', { class: 'net-list' });
  let active = true;
  let scanning = false;

  const pick = (opts) => { sheet.close(); credentialsDialog(w, opts); };
  const otherRow = () => el('button', { class: 'net-row', type: 'button', onclick: () => pick({ manual: true }) },
    icon('plus'), el('span', { class: 'net-name' }, 'Altra rete…'));

  function show(nets) {
    list.replaceChildren(...nets.map((n) => el('button', {
      class: 'net-row', type: 'button', onclick: () => pick({ ssid: n.ssid, secure: n.secure }),
    }, signalBars(n.rssi), el('span', { class: 'net-name' }, n.ssid),
    n.saved && el('span', { class: 'tag' }, 'salvata'),
    n.secure && icon('lock'))), otherRow());
  }

  // La scansione dura qualche secondo; durante la ricerca alcune richieste
  // possono andare perse (la radio cambia canale), quindi si riprova.
  async function scan(refresh) {
    if (scanning) return;
    scanning = true;
    hint.textContent = 'Ricerca delle reti…';
    let failures = 0;
    let first = true;
    const t0 = Date.now();
    try {
      while (active && Date.now() - t0 < 25000) {
        let r;
        try {
          r = await api.wifiScan(first && refresh);
          first = false;
          failures = 0;
        } catch (e) {
          if (++failures >= 5) throw e;
          await sleep(800);
          continue;
        }
        if (!active) return;
        if (r.networks.length || !r.scanning) show(r.networks);
        if (!r.scanning) {
          hint.textContent = r.networks.length
            ? 'Scegli la rete. Il dispositivo vede solo le reti a 2,4 GHz.'
            : 'Nessuna rete trovata. Il dispositivo vede solo le reti a 2,4 GHz.';
          return;
        }
        await sleep(800);
      }
      if (active) hint.textContent = 'La ricerca non ha risposto in tempo: riprova.';
    } catch (e) {
      if (active) hint.textContent = e.message;
    } finally {
      scanning = false;
      if (active && !list.childElementCount) list.append(otherRow());
    }
  }

  const sheet = openSheet({
    title: 'Aggiungi rete',
    body: el('div', {}, hint, list),
    actions: [
      { label: 'Indietro', cls: 'ghost', onClick: (s) => { s.close(); wifiDialog(); } },
      { label: 'Aggiorna', icon: 'refresh', onClick: () => scan(true) },
    ],
    onClose: () => { active = false; },
  });
  scan(false);
}

function credentialsDialog(w, { ssid = '', secure = true, manual = false } = {}) {
  const sta = w.mode === 'sta';
  const ssidInput = manual && el('input', {
    class: 'input', value: ssid, placeholder: 'Nome della rete (SSID)', maxlength: 32,
    autocapitalize: 'off', autocorrect: 'off', spellcheck: 'false',
  });
  const askPass = manual || secure;
  const pass = passwordInput({ maxlength: 64, enterkeyhint: 'go', placeholder: manual ? 'Vuota se la rete è aperta' : '' });

  const read = () => {
    const s = manual ? ssidInput.value.trim() : ssid;
    const p = askPass ? pass.value : '';
    if (!s) { ssidInput.focus(); return null; }
    if (!manual && secure && !p) { pass.focus(); return null; }
    if (p && (p.length < 8 || p.length > 64)) {
      toast('La password Wi-Fi deve avere da 8 a 63 caratteri', 'error');
      pass.focus();
      return null;
    }
    return { ssid: s, password: p, secure: !!p, manual };
  };
  const doConnect = (s) => {
    const c = read();
    if (!c) return;
    s.close();
    connectFlow(w, c);
  };
  const doSave = async (s) => {
    const c = read();
    if (!c) return;
    try {
      await api.wifiSave(c.ssid, c.password);
      s.close();
      toast(`Rete «${c.ssid}» salvata`, 'ok');
      wifiDialog();
    } catch (e) {
      toast(e.message, 'error');
    }
  };

  const sheet = openSheet({
    title: manual ? 'Altra rete' : `Rete «${ssid}»`,
    body: el('div', {},
      manual && el('div', { class: 'field' }, el('label', {}, 'Nome della rete'), ssidInput),
      askPass && el('div', { class: 'field' }, el('label', {}, 'Password'),
        el('div', { class: 'pw' }, pass, passwordToggle(pass))),
      el('p', { class: 'small muted' }, sta
        ? `«Connetti» sposta subito il dispositivo su questa rete; se non ci riesce torna su «${w.ssid}». «Salva» la memorizza per quando sarà a portata.`
        : 'Il dispositivo prova a collegarsi tenendo acceso l’Access Point, così puoi leggere qui il risultato.')),
    actions: [
      { label: 'Annulla', cls: 'ghost', onClick: (s) => { s.close(); addNetworkDialog(w); } },
      sta && { label: 'Salva', onClick: doSave },
      { label: 'Connetti', cls: 'primary', onClick: doConnect },
    ].filter(Boolean),
  });
  pass.addEventListener('keydown', (e) => { if (e.key === 'Enter') doConnect(sheet); });
  setTimeout(() => (manual && !ssid ? ssidInput : askPass ? pass : null)?.focus(), 60);
}

async function connectFlow(w, cred) {
  let res;
  try {
    res = await api.wifiConnect(cred.ssid, cred.password);
  } catch (e) {
    toast(e.message, 'error');
    return;
  }
  const fromSta = w.mode === 'sta';
  const status = el('p', {}, `Connessione a «${cred.ssid}»…`);
  let active = true;
  const sheet = openSheet({
    title: 'Connessione in corso',
    body: el('div', { class: 'learn' },
      el('div', { class: 'learn-anim net' }, icon('wifi')),
      status,
      el('p', { class: 'small muted' }, fromSta
        ? 'Mentre cambia rete il dispositivo non risponde: attendi qualche secondo.'
        : 'Il telefono potrebbe perdere per un momento il collegamento all’Access Point.')),
    actions: [{ label: 'Chiudi', cls: 'ghost' }],
    onClose: () => { active = false; },
  });

  const t0 = Date.now();
  // Da Station il dispositivo sparisce: se dopo un errore torna sulla rete di
  // prima lo rivediamo entro ~30 s, altrimenti è rimasto sulla nuova rete.
  const limit = (fromSta ? 45 : 60) * 1000;
  const retry = (s) => { s.close(); credentialsDialog(w, cred); };
  while (active) {
    await sleep(1000);
    let st = null;
    try { st = await api.wifi(4000); } catch { /* sta cambiando rete */ }
    if (!active) return;
    const a = st && st.attempt && st.attempt.id === res.attempt ? st.attempt : null;
    if (a && a.state === 'ok') return showConnectDone(sheet, st, a, fromSta);
    if (a && a.state === 'failed') return showConnectFailure(sheet, a, retry);
    if (Date.now() - t0 > limit) return showConnectLost(sheet, w, cred.ssid, fromSta);
    status.textContent = `Connessione a «${cred.ssid}»… ${Math.round((Date.now() - t0) / 1000)} s`;
  }
}

function showConnectDone(sheet, st, a, fromSta) {
  const url = `http://${a.ip}/`;
  sheet.render({
    title: 'Connesso',
    body: el('div', { class: 'learn' },
      el('div', { class: 'learn-anim done' }, icon('check')),
      el('p', {}, `Il dispositivo è collegato a «${a.ssid}» e la rete è stata salvata.`),
      el('p', { class: 'addr' }, el('a', { href: url }, url)),
      !fromSta && el('p', { class: 'small muted' },
        `L’Access Point si spegnerà tra ${minutes((st.ap && st.ap.offIn) || 120)}: collega il telefono a «${a.ssid}» e apri l’indirizzo qui sopra (oppure http://${st.hostname}.local).`)),
    actions: [{ label: 'Fatto', cls: 'primary' }],
  });
}

function showConnectFailure(sheet, a, retry) {
  sheet.render({
    title: 'Connessione non riuscita',
    body: el('div', { class: 'learn' },
      el('div', { class: 'learn-anim fail' }, icon('wifi')),
      el('p', {}, a.error || 'Connessione non riuscita.'),
      el('p', { class: 'small muted' }, a.fallback
        ? `Il dispositivo è tornato su «${a.fallback}». La rete non è stata salvata.`
        : 'L’Access Point resta attivo. La rete non è stata salvata.')),
    actions: [{ label: 'Chiudi', cls: 'ghost' }, { label: 'Riprova', cls: 'primary', onClick: retry }],
  });
}

function showConnectLost(sheet, w, ssid, fromSta) {
  const where = `http://${w.hostname}.local`;
  sheet.render({
    title: fromSta ? 'Dispositivo su un’altra rete' : 'Collegamento perso',
    body: el('div', { class: 'learn' },
      el('div', { class: 'learn-anim fail' }, icon('wifi')),
      el('p', {}, fromSta
        ? `Il dispositivo non risponde più su questa rete: con ogni probabilità si è collegato a «${ssid}».`
        : 'Il telefono si è scollegato dall’Access Point: succede quando il dispositivo si collega alla nuova rete e cambia canale.'),
      el('p', { class: 'small muted' }, fromSta
        ? `Collega il telefono a «${ssid}» e apri ${where} o l’indirizzo assegnato dal router.`
        : `Ricollegati a «${w.ap.ssid}» per vedere il risultato, oppure collega il telefono a «${ssid}» e apri ${where}.`)),
    actions: [{ label: 'Chiudi', cls: 'primary' }],
  });
}

function apPasswordDialog(w) {
  const p1 = passwordInput({ maxlength: 63, autocomplete: 'new-password' });
  const p2 = passwordInput({ maxlength: 63, autocomplete: 'new-password', enterkeyhint: 'done' });

  const submit = async (s) => {
    if (!AP_PASS_RE.test(p1.value)) {
      toast('La password deve avere da 8 a 63 caratteri, senza lettere accentate né emoji', 'error');
      p1.focus();
      return;
    }
    if (p1.value !== p2.value) {
      toast('Le due password non coincidono', 'error');
      p2.focus();
      return;
    }
    try {
      const r = await api.apPassword(p1.value);
      s.close();
      if (r.restart) {
        openSheet({
          title: 'Password cambiata',
          body: el('p', { class: 'muted' },
            `L’Access Point si sta riavviando: ricollega il telefono a «${w.ap.ssid}» con la nuova password.`),
          actions: [{ label: 'OK', cls: 'primary' }],
        });
      } else {
        toast('Password dell’Access Point cambiata', 'ok');
        wifiDialog();
      }
    } catch (e) {
      toast(e.message, 'error');
    }
  };

  const sheet = openSheet({
    title: 'Password dell’Access Point',
    body: el('div', {},
      el('p', { class: 'small muted' },
        `Protegge la rete «${w.ap.ssid}», che il dispositivo accende quando non trova reti note.`),
      el('div', { class: 'field' }, el('label', {}, 'Nuova password (8-63 caratteri)'),
        el('div', { class: 'pw' }, p1, passwordToggle(p1, p2))),
      el('div', { class: 'field' }, el('label', {}, 'Ripeti la password'), p2),
      w.ap.active && el('p', { class: 'small muted' },
        'Il telefono collegato all’Access Point verrà disconnesso: ricollegalo con la nuova password.'),
      w.ap.resetButton && el('p', { class: 'small muted' },
        'Se la dimentichi, tieni premuto il tasto BOOT del dispositivo per 5 secondi: torna la password predefinita.')),
    actions: [
      { label: 'Annulla', cls: 'ghost', onClick: (s) => { s.close(); wifiDialog(); } },
      { label: 'Salva', cls: 'primary', onClick: submit },
    ],
  });
  p2.addEventListener('keydown', (e) => { if (e.key === 'Enter') submit(sheet); });
  setTimeout(() => p1.focus(), 60);
}

// ------------------------------------------------- Installazione su iOS ----
// Su iPhone e iPad il browser non propone da solo l'installazione (non esiste
// l'evento beforeinstallprompt): alla prima visita spieghiamo come aggiungere
// l'app alla schermata Home. iPadOS si presenta come un Mac, ma ha il touch.
const isIos = /iPhone|iPad|iPod/.test(navigator.userAgent)
  || (/Macintosh/.test(navigator.userAgent) && navigator.maxTouchPoints > 1);
const isStandalone = () => navigator.standalone === true || matchMedia('(display-mode: standalone)').matches;
const canInstallIos = () => isIos && !isStandalone();

function installDialog() {
  const step = (ic, ...text) => el('li', {}, el('span', { class: 'ic' }, icon(ic)), el('span', {}, ...text));
  openSheet({
    title: 'Installa come app',
    body: el('div', {},
      el('p', { class: 'muted' }, 'Aggiungi il telecomando alla schermata Home: si aprirà a schermo intero, come un’app.'),
      el('ol', { class: 'steps' },
        step('share', 'Tocca ', el('b', {}, 'Condividi'), '. Su Safari con iOS 26 si trova nel menu ', el('b', {}, '•••'), '.'),
        step('addHome', 'Scorri l’elenco e scegli ', el('b', {}, 'Aggiungi alla schermata Home'), '.'),
        step('check', 'Lascia attivo ', el('b', {}, 'Apri come app web'), ', se presente, e tocca ', el('b', {}, 'Aggiungi'), '.'))),
    actions: [{ label: 'Ho capito', cls: 'primary' }],
  });
}

function maybeShowInstallHint() {
  if (!canInstallIos()) return;
  try {
    if (localStorage.getItem('installHint')) return;
    localStorage.setItem('installHint', '1');
  } catch {
    return;  // senza storage il popup ricomparirebbe a ogni apertura
  }
  installDialog();
}

function homeMenu() {
  const item = (ic, text, fn) =>
    el('button', { class: 'menu-item', type: 'button', onclick: () => { sheet.close(); fn(); } }, icon(ic), text);
  const sheet = openSheet({
    title: 'Menu',
    body: el('div', { class: 'menu-list' },
      item('download', 'Esporta backup', exportBackup),
      item('upload', 'Importa backup', importBackup),
      item('wifi', 'Wi-Fi e Access Point', wifiDialog),
      item('info', 'Informazioni dispositivo', infoDialog),
      canInstallIos() && item('addHome', 'Installa come app', installDialog)),
  });
}

// ---------------------------------------------------------------- Viste ----
function setTopbar({ title, back = false, edit = false, menu = false }) {
  $('#title').textContent = title;
  document.title = back ? `${title} · Campionatore IR` : 'Campionatore IR';
  $('#btnBack').hidden = !back;
  $('#btnMenu').hidden = !menu;
  const be = $('#btnEdit');
  be.hidden = !edit;
  be.classList.toggle('on', S.edit);
  be.replaceChildren(icon(S.edit ? 'check' : 'edit'));
  be.setAttribute('aria-label', S.edit ? 'Fine modifica' : 'Modifica tasti');
}

function renderHome() {
  setTopbar({ title: 'Telecomandi', menu: true });
  const view = $('#view');
  view.className = 'view';
  const kids = [];

  if (!S.loaded) {
    kids.push(el('p', { class: 'muted center' }, 'Caricamento…'));
  } else if (!S.remotes.length) {
    kids.push(el('div', { class: 'empty' },
      el('div', { class: 'big-ic' }, icon('remote')),
      el('h2', {}, 'Nessun telecomando'),
      el('p', {}, 'Crea un telecomando e registra i tasti puntando quello originale verso il ricevitore.')));
  } else {
    kids.push(el('div', { class: 'remote-list' }, S.remotes.map((r) => {
      const t = TYPES[r.type] || TYPES.custom;
      return el('button', { class: 'remote-card', type: 'button', onclick: () => { location.hash = '#/r/' + r.id; } },
        el('span', { class: 'ic' }, icon(t.icon)),
        el('span', { class: 'txt' },
          el('div', { class: 'name' }, r.name),
          el('div', { class: 'meta' }, `${t.label} · ${countLabel(r.buttons.length)}`)),
        el('span', { class: 'chev' }, icon('chevRight')));
    })));
  }

  const footer = el('div', { class: 'footer-info' });
  kids.push(footer);
  kids.push(el('button', { class: 'fab', type: 'button', 'aria-label': 'Nuovo telecomando', onclick: createRemoteDialog }, icon('plus')));
  view.replaceChildren(...kids);

  api.info().then((i) => { footer.textContent = `Campionatore IR v${i.version} · ${i.ip}`; }).catch(() => {});
}

function renderTv(remote) {
  const k = (id, opts = {}) => keyButton(remote, id, TV_KEYS[id], opts);
  const arrow = (id, ic) => k(id, { icon: ic, cls: id, repeat: true });
  return el('div', { class: 'tv' },
    el('div', { class: 'row3' },
      k('power', { icon: 'power', caption: 'Power', cls: 'key-power' }),
      k('input', { icon: 'input', caption: 'Input' }),
      k('mute', { icon: 'mute', caption: 'Mute' })),
    el('div', { class: 'nav' },
      el('div', { class: 'rocker' },
        k('vol_up', { icon: 'plus', repeat: true }),
        el('span', { class: 'rocker-label' }, 'VOL'),
        k('vol_down', { icon: 'minus', repeat: true })),
      el('div', { class: 'dpad' },
        arrow('up', 'chevUp'), arrow('left', 'chevLeft'),
        k('ok', { text: 'OK', cls: 'ok' }),
        arrow('right', 'chevRight'), arrow('down', 'chevDown')),
      el('div', { class: 'rocker' },
        k('ch_up', { icon: 'chevUp', repeat: true }),
        el('span', { class: 'rocker-label' }, 'CH'),
        k('ch_down', { icon: 'chevDown', repeat: true }))),
    el('div', { class: 'row3' },
      k('menu', { icon: 'menu', caption: 'Menu' }),
      k('home', { icon: 'home', caption: 'Home' }),
      k('back', { icon: 'back', caption: 'Indietro' })),
    el('div', { class: 'numpad' },
      [1, 2, 3, 4, 5, 6, 7, 8, 9].map((n) => k('num_' + n, { text: String(n) })),
      el('span'), k('num_0', { text: '0' }), el('span')));
}

function renderAc(remote) {
  const st = acState(remote.id);
  const key = acKey(st);
  const learned = !!getButton(remote, key);
  const mode = acMode(st.mode);
  const fan = acFan(st.fan);
  const update = (patch) => {
    Object.assign(st, patch);
    saveAcState(remote.id, st);
    render();
  };
  const section = (title, content) => el('div', {}, el('h3', { class: 'section-title' }, title), content);

  const display = el('div', { class: 'ac-display' },
    el('div', { class: 'ac-temp' }, String(st.temp), el('sup', {}, '°C')),
    el('div', { class: 'ac-sub' },
      el('span', {}, icon(mode.icon), mode.label),
      el('span', {}, icon('fan'), 'Ventola ' + fan.label)),
    el('div', { class: `ac-state ${learned ? 'ok' : 'todo'}` },
      learned ? 'Combinazione registrata' : 'Combinazione da registrare'));

  const modes = el('div', { class: 'seg' }, AC_MODES.map((m) =>
    el('button', { type: 'button', class: m.id === st.mode ? 'sel' : '', onclick: () => update({ mode: m.id }) },
      icon(m.icon), m.label)));
  const fans = el('div', { class: 'seg' }, AC_FANS.map((f) =>
    el('button', { type: 'button', class: f.id === st.fan ? 'sel' : '', onclick: () => update({ fan: f.id }) }, f.label)));
  const temp = el('div', { class: 'temp-ctl' },
    el('button', { type: 'button', 'aria-label': 'Diminuisci temperatura',
      onclick: () => st.temp > AC_TEMP.min && update({ temp: st.temp - 1 }) }, icon('minus')),
    el('div', { class: 'val' }, `${st.temp}°C`),
    el('button', { type: 'button', 'aria-label': 'Aumenta temperatura',
      onclick: () => st.temp < AC_TEMP.max && update({ temp: st.temp + 1 }) }, icon('plus')));

  const primary = learned && !S.edit
    ? el('button', { class: 'btn primary', type: 'button', onclick: (e) => sendKey(e.currentTarget, remote.id, key) },
      icon('send'), 'Invia')
    : el('button', { class: 'btn rec', type: 'button', onclick: () => learn(remote, key, acLabel(st), AC_GAP_MS) },
      icon('rec'), 'Registra');
  const off = keyButton(remote, 'off', 'Spegni', { icon: 'power', caption: 'Spegni', cls: 'key-power', gap: AC_GAP_MS });

  const saved = remote.buttons
    .map((b) => ({ b, s: parseAcKey(b.id) }))
    .filter((x) => x.s)
    .sort((a, b) => a.b.id.localeCompare(b.b.id));
  const chips = saved.length
    ? section(`Combinazioni registrate (${saved.length})`, el('div', { class: 'chips' }, saved.map(({ b, s }) => {
      const c = el('button', { type: 'button', class: 'chip' + (b.id === key ? ' sel' : ''), onclick: () => update(s) }, acShort(s));
      if (S.edit) c.append(delBadge(() => deleteButton(remote.id, b.id, b.label)));
      return c;
    })))
    : el('p', { class: 'small muted' },
      'Ogni combinazione di modalità, temperatura e ventola è un segnale diverso. ' +
      'Impostala qui e sul telecomando originale, poi premi «Registra».');

  return el('div', { class: 'ac' }, display,
    section('Modalità', modes), section('Temperatura', temp), section('Ventola', fans),
    el('div', { class: 'ac-actions' }, primary, off), chips);
}

function renderCustom(remote) {
  const keys = remote.buttons.map((b) => keyButton(remote, b.id, b.label, { text: b.label }));
  const add = el('button', { class: 'key add', type: 'button', onclick: () => addCustomKey(remote) },
    icon('plus'), el('small', {}, 'Nuovo tasto'));
  const grid = el('div', { class: 'custom-grid' }, keys, add);
  if (remote.buttons.length) return grid;
  return el('div', {},
    el('p', { class: 'muted small' },
      'Aggiungi i tasti che ti servono: dai un nome a ciascuno e registralo dal telecomando originale.'),
    grid);
}

function renderRemote(remote) {
  setTopbar({ title: remote.name, back: true, edit: true });
  const body = remote.type === 'tv' ? renderTv(remote)
    : remote.type === 'ac' ? renderAc(remote)
      : renderCustom(remote);

  const view = $('#view');
  view.className = 'view' + (S.edit ? ' editing' : '');
  view.replaceChildren(...[
    S.edit && el('div', { class: 'edit-hint' }, icon('rec'),
      'Modalità registrazione: tocca un tasto per registrarlo di nuovo, × per eliminarlo.'),
    body,
    S.edit && el('div', { class: 'danger-zone' },
      el('button', { class: 'btn danger block', type: 'button', onclick: () => deleteRemote(remote) },
        icon('trash'), 'Elimina telecomando')),
  ].filter(Boolean));
}

function route() {
  const m = /^#\/r\/([\w-]+)/.exec(location.hash);
  return m ? { view: 'remote', id: m[1] } : { view: 'home' };
}

function render() {
  const r = route();
  if (r.view !== 'remote') return renderHome();
  const remote = getRemote(r.id);
  if (remote) return renderRemote(remote);
  if (S.loaded) {
    location.hash = '';  // telecomando inesistente
  } else {
    setTopbar({ title: 'Caricamento…', back: true });
    $('#view').replaceChildren(el('p', { class: 'muted center' }, 'Caricamento…'));
  }
}

// ----------------------------------------------------------------- Avvio ----
async function init() {
  $('#btnBack').append(icon('chevLeft'));
  $('#btnMenu').append(icon('more'));
  $('#btnBack').addEventListener('click', () => { location.hash = ''; });
  $('#btnMenu').addEventListener('click', homeMenu);
  $('#btnEdit').addEventListener('click', () => { S.edit = !S.edit; render(); });
  window.addEventListener('hashchange', () => { S.edit = false; render(); });
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') reload().then(render).catch(() => {});
  });

  render();
  try {
    await reload();
  } catch (e) {
    toast(e.message, 'error');
  }
  render();
  maybeShowInstallHint();

  if ('serviceWorker' in navigator) navigator.serviceWorker.register('sw.js').catch(() => {});
}

init();
