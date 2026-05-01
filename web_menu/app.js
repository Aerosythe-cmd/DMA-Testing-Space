// ─────────────────────────────────────────────────────────
//  BF6 DMA — Web Menu (app.js)
//
//  Layout:
//   - Hero rows host the master toggle for each feature; the
//     wrapping `.subgroup[data-master=...]` dims when off.
//   - Every input auto-applies on change (debounced for ranges
//     and text fields). No manual "Apply" button — server-side
//     `POST /api/config` does a partial-merge.
// ─────────────────────────────────────────────────────────

const API = '';
let cfg = {};
let selectedConfig = null;
let pendingPushTimer = null;

// ── Field schemas (single source of truth) ─────────────
const CHECKBOXES = [
  'aimbotEnabled', 'aimPrediction', 'aimOnlyVisible',
  'espEnabled', 'espBox', 'espSkeleton', 'espName', 'espWeapon',
  'espHealth', 'espSnaplines', 'espTeammates', 'fuserMode',
  'antiRecoilEnabled', 'offsetAutoDiscover',
];
const RANGES = [
  'aimFov', 'aimSmoothness', 'aimSmoothSteps', 'espMaxDistance',
  'antiRecoilStrength', 'antiRecoilResetMs',
];
const NUMBERS = [
  'aimBone', 'inputDevice', 'kmboxPort', 'screenW', 'screenH',
  'gameFov', 'mouseSensitivity',
  'aimbotHotkey', 'aimbotHotkeyMode',
  'espHotkey', 'espHotkeyMode',
  'antiRecoilHotkey', 'antiRecoilHotkeyMode',
];
const STRINGS = ['kmboxIP', 'arduinoPort'];

// Master → subgroup map: master toggle dims its dependent subgroup
const MASTER_TOGGLES = ['aimbotEnabled', 'espEnabled', 'antiRecoilEnabled'];

// ── Tab switching ─────────────────────────────────────
document.querySelectorAll('.tab').forEach(btn => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
    btn.classList.add('active');
    document.getElementById('tab-' + btn.dataset.tab).classList.add('active');

    if (btn.dataset.tab === 'configs') loadConfigList();
    if (btn.dataset.tab === 'players') startPlayerPoll();
    else stopPlayerPoll();
  });
});

// ── Range value display ───────────────────────────────
function bindRangeDisplay(id) {
  const el  = document.getElementById(id);
  const val = document.getElementById('val-' + id);
  if (!el || !val) return;
  const update = () => { val.textContent = formatRangeValue(id, el.value); };
  el.addEventListener('input', update);
  update();
}
function formatRangeValue(id, v) {
  const n = parseFloat(v);
  if (id === 'antiRecoilStrength') return n.toFixed(2);
  if (id === 'aimSmoothness')      return n.toFixed(1);
  if (id === 'aimFov')             return n.toFixed(1);
  return String(n);
}
RANGES.forEach(bindRangeDisplay);

// ── Hero subtitle + master/sub disable ────────────────
function refreshMasterStates() {
  // Subgroup dimming
  document.querySelectorAll('.subgroup[data-master]').forEach(g => {
    const masterId = g.dataset.master;
    const master = document.getElementById(masterId);
    if (!master) return;
    g.dataset.disabled = master.checked ? 'false' : 'true';
  });

  // Hero subtitles
  setHero('hero-aimbot-sub', 'aimbotEnabled', heroAimbotSub);
  setHero('hero-esp-sub',    'espEnabled',    heroEspSub);
  setHero('hero-recoil-sub', 'antiRecoilEnabled', heroRecoilSub);
}
function setHero(subId, masterId, builder) {
  const sub = document.getElementById(subId);
  const master = document.getElementById(masterId);
  if (!sub || !master) return;
  const hero = sub.closest('.hero');
  if (hero) hero.dataset.on = master.checked ? 'true' : 'false';
  sub.textContent = master.checked ? builder() : 'Off';
}
function heroAimbotSub() {
  const fov = numVal('aimFov').toFixed(1);
  const bone = (numVal('aimBone') === 0) ? 'head' : 'chest';
  const modeIdx = parseInt(document.getElementById('aimbotHotkeyMode')?.value ?? '1', 10);
  const modeLbl = ['always', 'hold', 'toggle'][modeIdx] ?? 'hold';
  const vk      = parseInt(document.getElementById('aimbotHotkey')?.value ?? '0', 10);
  const hk      = modeIdx === 0 ? 'always-on' : `${modeLbl} ${hotkeyName(vk)}`;
  return `${fov}° · ${bone} · ${hk}`;
}
function heroEspSub() {
  const layers = [];
  if (boolVal('espBox'))       layers.push('box');
  if (boolVal('espSkeleton'))  layers.push('skel');
  if (boolVal('espName'))      layers.push('names');
  if (boolVal('espHealth'))    layers.push('hp');
  if (boolVal('espSnaplines')) layers.push('lines');
  if (!layers.length) return 'No layers active';
  return layers.join(' · ');
}
function heroRecoilSub() {
  const strength = numVal('antiRecoilStrength').toFixed(2);
  return `${strength}× · resets in ${numVal('antiRecoilResetMs')}ms`;
}
function hotkeyName(vk) {
  vk = parseInt(vk, 10) || 0;
  if (vk === 0) return '—';
  const map = {
    0x01: 'LMB',  0x02: 'RMB',  0x04: 'MMB',  0x05: 'XMB1', 0x06: 'XMB2',
    0x08: 'Backspace', 0x09: 'Tab', 0x0D: 'Enter',
    0x10: 'Shift', 0x11: 'Ctrl', 0x12: 'Alt',
    0x14: 'CapsLock', 0x1B: 'Esc', 0x20: 'Space',
    0x21: 'PgUp', 0x22: 'PgDn', 0x23: 'End', 0x24: 'Home',
    0x25: '←', 0x26: '↑', 0x27: '→', 0x28: '↓',
    0x2D: 'Insert', 0x2E: 'Delete',
    0xA0: 'LShift', 0xA1: 'RShift', 0xA2: 'LCtrl', 0xA3: 'RCtrl',
    0xA4: 'LAlt',   0xA5: 'RAlt',
  };
  if (map[vk]) return map[vk];
  if (vk >= 0x30 && vk <= 0x39) return String.fromCharCode(vk);          // 0-9
  if (vk >= 0x41 && vk <= 0x5A) return String.fromCharCode(vk);          // A-Z
  if (vk >= 0x70 && vk <= 0x7B) return 'F' + (vk - 0x6F);                // F1..F12
  if (vk >= 0x60 && vk <= 0x69) return 'Num' + (vk - 0x60);              // Num0..Num9
  return 'VK 0x' + vk.toString(16).toUpperCase();
}

function boolVal(id) { const el = document.getElementById(id); return el ? !!el.checked : false; }
function numVal(id)  { const el = document.getElementById(id); return el ? parseFloat(el.value) : 0; }

// ── Pull config from server ───────────────────────────
async function loadConfig() {
  try {
    const res = await fetch(API + '/api/config');
    cfg = await res.json();
    applyToUI(cfg);
  } catch (e) {
    // status pills are driven by /api/status
  }
}

function applyToUI(cfg) {
  CHECKBOXES.forEach(k => {
    const el = document.getElementById(k);
    if (el && k in cfg) el.checked = !!cfg[k];
  });
  RANGES.forEach(k => {
    const el  = document.getElementById(k);
    const val = document.getElementById('val-' + k);
    if (el && k in cfg) el.value = cfg[k];
    if (val && k in cfg) val.textContent = formatRangeValue(k, cfg[k]);
  });
  NUMBERS.forEach(k => {
    const el = document.getElementById(k);
    if (el && k in cfg) el.value = cfg[k];
  });
  STRINGS.forEach(k => {
    const el = document.getElementById(k);
    if (el && k in cfg) el.value = cfg[k];
  });
  updateDeviceVisibility();
  refreshHotkeyLabels();
  refreshMasterStates();
}

// ── Hotkey bind UI ────────────────────────────────────
// Each .hotkey-bind button has data-vk-field pointing to a hidden
// <input> that holds the VK code. The label inside the button shows
// the human-readable name.
function refreshHotkeyLabels() {
  document.querySelectorAll('.hotkey-bind').forEach(btn => {
    if (btn.dataset.binding === '1') return;   // mid-capture, leave alone
    const fieldId = btn.dataset.vkField;
    const hidden  = document.getElementById(fieldId);
    const label   = btn.querySelector('.hotkey-label');
    if (hidden && label) label.textContent = hotkeyName(hidden.value || 0);
  });
}

let bindPollTimer = null;
function startBind(btn) {
  if (btn.dataset.binding === '1') { cancelBind(btn); return; }
  btn.dataset.binding = '1';
  const label = btn.querySelector('.hotkey-label');
  if (label) label.textContent = 'Press a key…';
  btn.classList.add('btn-running');

  fetch(API + '/api/input/bind/start', { method: 'POST' }).catch(() => {});

  let elapsed = 0;
  if (bindPollTimer) clearInterval(bindPollTimer);
  bindPollTimer = setInterval(async () => {
    elapsed += 100;
    if (elapsed > 8000) { cancelBind(btn); showToast('Bind timed out', 'error'); return; }
    try {
      const res = await fetch(API + '/api/input/bind/status');
      const j   = await res.json();
      if (j.capturedVk && j.capturedVk > 0) {
        commitBind(btn, j.capturedVk);
      }
    } catch (e) { /* keep polling */ }
  }, 100);

  // Esc cancels
  const escHandler = (e) => {
    if (e.key === 'Escape') { cancelBind(btn); window.removeEventListener('keydown', escHandler); }
  };
  window.addEventListener('keydown', escHandler);
  btn._escHandler = escHandler;
}

function commitBind(btn, vk) {
  if (bindPollTimer) { clearInterval(bindPollTimer); bindPollTimer = null; }
  const fieldId = btn.dataset.vkField;
  const hidden  = document.getElementById(fieldId);
  if (hidden) hidden.value = vk;
  const label = btn.querySelector('.hotkey-label');
  if (label) label.textContent = hotkeyName(vk);
  btn.dataset.binding = '0';
  btn.classList.remove('btn-running');
  fetch(API + '/api/input/bind/cancel', { method: 'POST' }).catch(() => {});
  if (btn._escHandler) window.removeEventListener('keydown', btn._escHandler);
  schedulePush(true);
  refreshMasterStates();
  showToast(`Bound ${hotkeyName(vk)}`, 'ok');
}

function cancelBind(btn) {
  if (bindPollTimer) { clearInterval(bindPollTimer); bindPollTimer = null; }
  btn.dataset.binding = '0';
  btn.classList.remove('btn-running');
  refreshHotkeyLabels();
  fetch(API + '/api/input/bind/cancel', { method: 'POST' }).catch(() => {});
  if (btn._escHandler) window.removeEventListener('keydown', btn._escHandler);
}

// ── Collect UI → config object ────────────────────────
function collectFromUI() {
  const c = {};
  CHECKBOXES.forEach(k => { const el = document.getElementById(k); if (el) c[k] = !!el.checked; });
  RANGES.forEach(k => {    const el = document.getElementById(k); if (el) c[k] = parseFloat(el.value); });
  NUMBERS.forEach(k => {   const el = document.getElementById(k); if (el && el.value !== '') c[k] = parseFloat(el.value); });
  STRINGS.forEach(k => {   const el = document.getElementById(k); if (el) c[k] = el.value.trim(); });
  return c;
}

// ── Push config to server (auto-apply, debounced) ─────
function schedulePush(immediate = false) {
  if (pendingPushTimer) clearTimeout(pendingPushTimer);
  const delay = immediate ? 0 : 180;
  pendingPushTimer = setTimeout(async () => {
    pendingPushTimer = null;
    const c = collectFromUI();
    try {
      const res = await fetch(API + '/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(c),
      });
      if (!res.ok) throw new Error('bad status');
      // Quietly silent on success — toast only on errors / explicit actions
    } catch (e) {
      showToast('Sync failed', 'error');
    }
  }, delay);
}

// ── Hook up all auto-apply listeners ──────────────────
function bindAutoApply() {
  CHECKBOXES.forEach(k => {
    const el = document.getElementById(k);
    if (!el) return;
    el.addEventListener('change', () => { refreshMasterStates(); schedulePush(true); });
  });
  RANGES.forEach(k => {
    const el = document.getElementById(k);
    if (!el) return;
    el.addEventListener('input', () => { refreshMasterStates(); schedulePush(false); });
  });
  NUMBERS.forEach(k => {
    const el = document.getElementById(k);
    if (!el) return;
    el.addEventListener('change', () => { refreshMasterStates(); schedulePush(true); });
  });
  STRINGS.forEach(k => {
    const el = document.getElementById(k);
    if (!el) return;
    el.addEventListener('blur', () => schedulePush(true));
  });

  // Input-device select needs to also re-show/hide the right row
  const inputDev = document.getElementById('inputDevice');
  if (inputDev) inputDev.addEventListener('change', updateDeviceVisibility);

  // Hotkey bind buttons
  document.querySelectorAll('.hotkey-bind').forEach(btn => {
    btn.addEventListener('click', (e) => {
      e.preventDefault();
      startBind(btn);
    });
  });
}

// ── Input-device row visibility ───────────────────────
function updateDeviceVisibility() {
  const isKMBox = parseInt(document.getElementById('inputDevice').value) === 0;
  document.getElementById('row-kmboxIP').style.display     = isKMBox ? '' : 'none';
  document.getElementById('row-kmboxPort').style.display   = isKMBox ? '' : 'none';
  document.getElementById('row-arduinoPort').style.display = isKMBox ? 'none' : '';
}

// ── Saved configs ─────────────────────────────────────
async function loadConfigList() {
  try {
    const res  = await fetch(API + '/api/configs');
    const list = await res.json();
    const cont = document.getElementById('configList');
    cont.innerHTML = '';
    if (!list.length) {
      cont.innerHTML = '<div class="config-empty">No saved configs yet.</div>';
      return;
    }
    list.forEach(name => {
      const item = document.createElement('div');
      item.className = 'config-item';
      item.innerHTML = `<span>${escapeHtml(name)}</span>`;
      item.addEventListener('click', () => {
        document.querySelectorAll('.config-item').forEach(i => i.classList.remove('selected'));
        item.classList.add('selected');
        selectedConfig = name;
        document.getElementById('configNameInput').value = name;
      });
      cont.appendChild(item);
    });
  } catch (e) { /* offline; keep empty */ }
}

async function saveConfig() {
  const name = document.getElementById('configNameInput').value.trim();
  if (!name) return showToast('Enter a name first', 'error');

  const c = collectFromUI();
  c.configName = name;

  try {
    await fetch(API + '/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(c),
    });
    await fetch(API + '/api/configs/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name }),
    });
    showToast(`Saved "${name}"`, 'ok');
    loadConfigList();
  } catch (e) {
    showToast('Save failed', 'error');
  }
}

async function loadSelectedConfig() {
  if (!selectedConfig) return showToast('Pick a config first', 'error');
  try {
    const res = await fetch(API + '/api/configs/load', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: selectedConfig }),
    });
    if (!res.ok) throw new Error();
    await loadConfig();
    showToast(`Loaded "${selectedConfig}"`, 'ok');
  } catch (e) {
    showToast('Load failed', 'error');
  }
}

async function deleteSelectedConfig() {
  if (!selectedConfig) return showToast('Pick a config first', 'error');
  try {
    await fetch(API + '/api/configs/' + encodeURIComponent(selectedConfig), { method: 'DELETE' });
    showToast(`Deleted "${selectedConfig}"`, 'info');
    selectedConfig = null;
    loadConfigList();
  } catch (e) {
    showToast('Delete failed', 'error');
  }
}

// ── Players poll ──────────────────────────────────────
let playerPollInterval = null;

function startPlayerPoll() {
  stopPlayerPoll();
  pollPlayers();
  playerPollInterval = setInterval(pollPlayers, 1000);
}
function stopPlayerPoll() {
  if (playerPollInterval) {
    clearInterval(playerPollInterval);
    playerPollInterval = null;
  }
}
function hpClass(hp) {
  if (hp <= 0)   return '';
  if (hp < 30)   return 'hp-low';
  if (hp < 70)   return 'hp-mid';
  return 'hp-full';
}
async function pollPlayers() {
  try {
    const res     = await fetch(API + '/api/players');
    const players = await res.json();
    const tbody   = document.getElementById('playerTableBody');
    tbody.innerHTML = '';

    if (!players.length) {
      tbody.innerHTML = '<tr><td colspan="4" class="empty">No enemies in match.</td></tr>';
      return;  // header pills are driven by /api/status, not this
    }
    players.forEach(p => {
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td>${escapeHtml(p.name)}</td>
        <td>${escapeHtml(p.weapon)}</td>
        <td class="num ${hpClass(p.health)}">${Math.round(p.health)}</td>
        <td class="num">${p.team}</td>
      `;
      tbody.appendChild(tr);
    });
  } catch (e) { /* network blip */ }
}

// ── Status pills ──────────────────────────────────────
function setStatus(id, state, text) {
  const el = document.getElementById(id);
  if (!el) return;
  el.dataset.state = state;
  // Replace text node after the dot
  el.innerHTML = `<span class="dot"></span>${text}`;
}

// ── Toast ─────────────────────────────────────────────
let toastTimer;
function showToast(msg, kind = 'info') {
  let toast = document.getElementById('toast');
  if (!toast) {
    toast = document.createElement('div');
    toast.id = 'toast';
    toast.className = 'toast';
    document.body.appendChild(toast);
  }
  toast.dataset.kind = kind;
  toast.textContent = msg;
  // Force reflow → re-trigger transition
  void toast.offsetWidth;
  toast.classList.add('visible');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.remove('visible'), 1800);
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// ── Diagnostics: status pills (poll /api/status) ─────
async function pollStatus() {
  try {
    const res = await fetch(API + '/api/status');
    if (!res.ok) throw new Error();
    const s = await res.json();
    setStatus('statusConn',
      s.dmaReady && s.cr3Fixed ? 'online' : 'offline',
      s.dmaReady ? (s.cr3Fixed ? 'CONNECTED' : 'NO CR3') : 'OFFLINE');
    setStatus('statusMatch',
      s.inMatch ? 'active' : 'idle',
      s.inMatch ? `IN MATCH · ${s.enemyCount}` : 'IDLE');
  } catch (e) {
    setStatus('statusConn', 'offline', 'OFFLINE');
    setStatus('statusMatch', 'idle', 'IDLE');
  }
}

// ── Diagnostics: DMA self-test ───────────────────────
async function runDmaTest() {
  const btn = document.getElementById('btn-dma');
  const out = document.getElementById('result-dma');
  setRunning(btn, out, 'Running chain validation…');

  try {
    const res = await fetch(API + '/api/test/dma', { method: 'POST' });
    const r   = await res.json();
    out.classList.remove('hidden');
    out.innerHTML = renderDmaResult(r);
  } catch (e) {
    out.classList.remove('hidden');
    out.innerHTML = `<div class="verdict" data-kind="fail">Failed</div>
                     <div class="kv-list"><div class="k">error</div><div class="v bad">network / server unreachable</div></div>`;
  } finally {
    btn.classList.remove('btn-running');
    btn.textContent = 'Run';
  }
}

function renderDmaResult(r) {
  const allGood = r.dmaReady && r.cr3Fixed && r.chainHealthy;
  const partial = r.dmaReady && (r.cr3Fixed || r.gameCtx);
  const verdictKind = allGood ? 'ok' : (partial ? 'warn' : 'fail');
  const verdictText = allGood ? 'All checks passed' : (partial ? 'Partial — see details' : 'Failed');

  const hex = (n) => '0x' + (Number(n) >>> 0).toString(16).padStart(8, '0').padStart(12, '0');
  const hexBig = (n) => {
    const v = Number(n);
    if (!v) return '0x0';
    return '0x' + v.toString(16);
  };

  const row = (k, v, cls = '') => `<div class="k">${k}</div><div class="v ${cls}">${v}</div>`;
  const yn  = (b) => b ? '<span class="v good">yes</span>' : '<span class="v bad">no</span>';

  let kv = `<div class="kv-list">`;
  kv += row('DMA ready',       r.dmaReady    ? 'yes' : 'no',         r.dmaReady    ? 'good' : 'bad');
  kv += row('CR3 fixed',       r.cr3Fixed    ? 'yes' : 'no',         r.cr3Fixed    ? 'good' : 'bad');
  kv += row('Chain healthy',   r.chainHealthy? 'yes' : 'no',         r.chainHealthy? 'good' : 'bad');
  kv += row('PID',             r.pid || '—');
  kv += row('bf6.exe base',    hexBig(r.moduleBase));
  kv += row('bf6.exe size',    r.moduleSize ? `${(r.moduleSize/1048576).toFixed(1)} MB` : '—');
  kv += row('gameCtx VA',      hexBig(r.gameCtxStatic));
  kv += row('gameCtx value',   hexBig(r.gameCtx));
  kv += row('Enemies cached',  r.enemyCount);
  kv += row('Single read',     `${r.readLatencyUs.toFixed(1)} µs`);
  kv += row('Scatter (64×)',   `${r.scatterLatencyUs.toFixed(1)} µs`);
  kv += `</div>`;

  let notes = '';
  if (r.notes && r.notes.length) {
    notes = `<div class="test-notes"><ul>` +
      r.notes.map(n => `<li>${escapeHtml(n)}</li>`).join('') +
      `</ul></div>`;
  }

  return `<div class="verdict" data-kind="${verdictKind}">${verdictText}</div>${kv}${notes}`;
}

// ── Diagnostics: input device tests ──────────────────
async function runInputTest(kind) {
  const btnId = { wiggle: 'btn-wiggle', circle: 'btn-circle', recoil: 'btn-recoil' }[kind];
  const labelByKind = { wiggle: 'Wiggling…', circle: 'Tracing circle…', recoil: 'Firing dry pulses…' };
  const btn = document.getElementById(btnId);
  const out = document.getElementById('result-input');
  setRunning(btn, out, labelByKind[kind] ?? 'Running…');

  try {
    const res = await fetch(API + `/api/test/${kind}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    });
    const r = await res.json();
    out.classList.remove('hidden');
    out.innerHTML = renderInputResult(kind, r);
  } catch (e) {
    out.classList.remove('hidden');
    out.innerHTML = `<div class="verdict" data-kind="fail">Failed</div>
                     <div class="kv-list"><div class="k">error</div><div class="v bad">network / server unreachable</div></div>`;
  } finally {
    btn.classList.remove('btn-running');
    btn.textContent = 'Run';
  }
}

function renderInputResult(kind, r) {
  let verdictKind, verdictText;
  if (!r.deviceReady)      { verdictKind = 'fail'; verdictText = 'Device not connected'; }
  else if (!r.ok)          { verdictKind = 'warn'; verdictText = 'Sent with errors'; }
  else                     { verdictKind = 'ok';   verdictText = 'Sent successfully'; }

  const row = (k, v, cls = '') => `<div class="k">${k}</div><div class="v ${cls}">${v}</div>`;
  let kv = `<div class="kv-list">`;
  kv += row('Test',         kind);
  kv += row('Device',       r.deviceName);
  kv += row('Connected',    r.deviceReady ? 'yes' : 'no', r.deviceReady ? 'good' : 'bad');
  kv += row('Moves sent',   r.movesSent);
  kv += row('Duration',     `${r.durationMs.toFixed(0)} ms`);
  if (r.error) kv += row('Error', escapeHtml(r.error), 'bad');
  kv += `</div>`;

  return `<div class="verdict" data-kind="${verdictKind}">${verdictText}</div>${kv}`;
}

function setRunning(btn, panel, msg) {
  btn.classList.add('btn-running');
  btn.textContent = '…';
  panel.classList.remove('hidden');
  panel.innerHTML = `<div class="verdict" data-kind="run">${msg}</div>`;
}

// ── Init ──────────────────────────────────────────────
bindAutoApply();
loadConfig();
pollStatus();
setInterval(loadConfig, 5000);   // heartbeat / reconnect / external-change pickup
setInterval(pollStatus, 2000);   // header pills
