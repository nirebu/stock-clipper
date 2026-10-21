/* Stock Clipper — local trim / rotate / render UI */

const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

const el = {
  sourceBtn: $('#sourceBtn'),
  sourcePath: $('#sourcePath'),
  sourceDot: $('#sourceDot'),
  btnRescan: $('#btnRescan'),
  btnRenders: $('#btnRenders'),
  btnSettings: $('#btnSettings'),
  search: $('#search'),
  libCount: $('#libCount'),
  clipList: $('#clipList'),
  libEmpty: $('#libEmpty'),
  btnRenderAll: $('#btnRenderAll'),
  btnRenderEdited: $('#btnRenderEdited'),
  stage: $('#stage'),
  video: $('#video'),
  stageEmpty: $('#stageEmpty'),
  stageHud: $('#stageHud'),
  hudName: $('#hudName'),
  hudMeta: $('#hudMeta'),
  hudRendered: $('#hudRendered'),
  bigPlay: $('#bigPlay'),
  timeline: $('#timeline'),
  inInput: $('#inInput'),
  outInput: $('#outInput'),
  lenRead: $('#lenRead'),
  btnSetIn: $('#btnSetIn'),
  btnSetOut: $('#btnSetOut'),
  btnReset: $('#btnReset'),
  shortWarn: $('#shortWarn'),
  track: $('#track'),
  filmstrip: $('#filmstrip'),
  stripLoading: $('#stripLoading'),
  shadeL: $('#shadeL'),
  shadeR: $('#shadeR'),
  selRect: $('#selRect'),
  handleIn: $('#handleIn'),
  handleOut: $('#handleOut'),
  playhead: $('#playhead'),
  ruler: $('#ruler'),
  controls: $('.controls'),
  btnPlay: $('#btnPlay'),
  btnStepBack: $('#btnStepBack'),
  btnStepFwd: $('#btnStepFwd'),
  chkLoop: $('#chkLoop'),
  rotateSeg: $('#rotateSeg'),
  btnFlipH: $('#btnFlipH'),
  btnFlipV: $('#btnFlipV'),
  audioWrap: $('#audioWrap'),
  chkAudio: $('#chkAudio'),
  btnRender: $('#btnRender'),
  queue: $('#queue'),
  queueToggle: $('#queueToggle'),
  queueChev: $('#queueChev'),
  queueBadge: $('#queueBadge'),
  queueSummary: $('#queueSummary'),
  queueBody: $('#queueBody'),
  toast: $('#toast'),
  settingsModal: $('#settingsModal'),
  browseModal: $('#browseModal'),
};

const state = {
  settings: null,
  clips: [],
  filter: '',
  currentPath: null,
  viewingRender: false,
  loop: true,
  jobs: [],
  pollTimer: null,
  saveTimer: null,
  browseCb: null,
  browsePath: null,
};

const MIN_GAP = 0.05;

/* ------------------------------------------------------------------ */
/* utils                                                               */
/* ------------------------------------------------------------------ */

async function api(url, opts = {}) {
  const init = { ...opts, headers: { ...(opts.headers || {}) } };
  if (opts.body !== undefined) {
    init.headers['content-type'] = 'application/json';
    init.body = JSON.stringify(opts.body);
  }
  const res = await fetch(url, init);
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || `${res.status} ${res.statusText}`);
  return data;
}

function clamp(n, lo, hi) { return Math.min(hi, Math.max(lo, n)); }
function round3(n) { return Math.round(n * 1000) / 1000; }

function fmtTime(t, decimals = 2) {
  if (!Number.isFinite(t) || t < 0) t = 0;
  const h = Math.floor(t / 3600);
  const m = Math.floor((t % 3600) / 60);
  const s = t % 60;
  const ss = s.toFixed(decimals).padStart(decimals > 0 ? 3 + decimals : 2, '0');
  return h > 0 ? `${h}:${String(m).padStart(2, '0')}:${ss}` : `${m}:${ss}`;
}

function parseTime(str) {
  if (typeof str !== 'string') return NaN;
  const s = str.trim();
  if (!s) return NaN;
  if (/^\d+(\.\d+)?$/.test(s)) return Number(s);
  const parts = s.split(':').map((p) => Number(p));
  if (parts.some((n) => !Number.isFinite(n))) return NaN;
  if (parts.length === 2) return parts[0] * 60 + parts[1];
  if (parts.length === 3) return parts[0] * 3600 + parts[1] * 60 + parts[2];
  return NaN;
}

function esc(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

let toastTimer = null;
function toast(msg, kind = '') {
  el.toast.textContent = msg;
  el.toast.className = `toast ${kind}`;
  el.toast.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.toast.hidden = true; }, kind === 'error' ? 5200 : 2600);
}

const cur = () => state.clips.find((c) => c.path === state.currentPath) || null;

/* ------------------------------------------------------------------ */
/* library                                                             */
/* ------------------------------------------------------------------ */

function flagsHtml(c) {
  const out = [];
  if (c.error) out.push('<span class="flag err">unreadable</span>');
  if (c.edit?.rotate) out.push(`<span class="flag rot">${c.edit.rotate}°</span>`);
  if (c.edit?.flipH || c.edit?.flipV) out.push('<span class="flag rot">flip</span>');
  if (isEdited(c)) out.push('<span class="flag trim">trimmed</span>');
  if (c.rendered) {
    out.push(c.stale
      ? '<span class="flag warn">re-render</span>'
      : '<span class="flag rendered">rendered</span>');
  }
  if (!out.length) {
    const vertical = c.displayHeight > c.displayWidth;
    if (vertical) out.push('<span class="flag">vertical</span>');
  }
  return out.join('');
}

function isEdited(c) {
  if (!c.edit) return false;
  return (
    c.edit.in > 0.002 ||
    c.edit.out < (c.duration || 0) - 0.002 ||
    c.edit.rotate !== 0 ||
    c.edit.flipH ||
    c.edit.flipV
  );
}

function rowHtml(c) {
  const sub = c.error
    ? 'could not read file'
    : `${c.displayWidth}×${c.displayHeight}${c.fps ? ` · ${c.fps.toFixed(2)}fps` : ''}`;
  return `
    <div class="clip ${c.path === state.currentPath ? 'active' : ''}" data-path="${encodeURIComponent(c.path)}">
      <div class="thumb">
        ${c.error ? '' : `<img loading="lazy" src="${thumbUrl(c)}" alt="">`}
        <span class="dur">${c.duration ? fmtTime(c.duration, 1) : '—'}</span>
      </div>
      <div class="meta">
        <div class="name" title="${esc(c.rel)}">${esc(c.name)}</div>
        <div class="sub">${esc(sub)}</div>
        <div class="flags">${flagsHtml(c)}</div>
      </div>
      <button class="row-rot ${c.edit?.rotate ? 'on' : ''}" title="Rotate 90° clockwise">⟳</button>
    </div>`;
}

/* previews follow the clip's chosen orientation, so rotated clips look upright */
function orientQuery(c) {
  return `rotate=${Number(c.edit?.rotate) || 0}&flipH=${c.edit?.flipH ? 1 : 0}&flipV=${c.edit?.flipV ? 1 : 0}`;
}

function thumbUrl(c) {
  return `/api/thumb?path=${encodeURIComponent(c.path)}&${orientQuery(c)}${c.mtimeMs ? `&v=${Math.round(c.mtimeMs)}` : ''}`;
}

function renderLibrary() {
  const q = state.filter.trim().toLowerCase();
  const clips = state.clips.filter((c) => !q || c.rel.toLowerCase().includes(q));
  el.clipList.innerHTML = clips.map(rowHtml).join('');
  el.libEmpty.hidden = state.clips.length > 0;
  el.libCount.textContent = q ? `${clips.length}/${state.clips.length}` : String(state.clips.length);
  const edited = state.clips.filter(isEdited).length;
  el.btnRenderEdited.disabled = edited === 0;
  el.btnRenderEdited.textContent = edited ? `Render edited (${edited})` : 'Render edited';
  el.btnRenderAll.disabled = state.clips.length === 0;
}

function refreshRow(path) {
  const c = state.clips.find((x) => x.path === path);
  if (!c) return;
  const row = el.clipList.querySelector(`.clip[data-path="${encodeURIComponent(path)}"]`);
  if (!row) return;
  const flags = row.querySelector('.flags');
  if (flags) flags.innerHTML = flagsHtml(c);
  const rot = row.querySelector('.row-rot');
  if (rot) rot.classList.toggle('on', Boolean(c.edit?.rotate));
  const img = row.querySelector('img');
  if (img) {
    const src = thumbUrl(c);
    if (img.getAttribute('src') !== src) img.src = src;
  }
}

async function loadLibrary({ refresh = false, silent = false } = {}) {
  try {
    const data = await api(`/api/library${refresh ? '?refresh=1' : ''}`);
    state.clips = data.clips || [];
    renderLibrary();
    if (!silent) {
      const bad = state.clips.filter((c) => c.error).length;
      if (bad) toast(`${bad} file${bad > 1 ? 's' : ''} could not be read`, 'error');
    }
    // keep the current selection if it still exists
    if (state.currentPath && !state.clips.some((c) => c.path === state.currentPath)) {
      clearSelection();
    }
  } catch (err) {
    toast(`Could not read library: ${err.message}`, 'error');
  }
}

function clearSelection() {
  state.currentPath = null;
  state.viewingRender = false;
  el.video.removeAttribute('src');
  el.video.load();
  el.stageEmpty.hidden = false;
  el.stageHud.hidden = true;
  el.bigPlay.hidden = true;
  el.timeline.classList.add('disabled');
  el.controls.classList.remove('disabled');
  el.playhead.style.left = '0%';
  el.filmstrip.style.backgroundImage = 'none';
  history.replaceState(null, '', location.pathname);
}

/* ------------------------------------------------------------------ */
/* selection + preview                                                 */
/* ------------------------------------------------------------------ */

function selectClip(path, { autoplay = false } = {}) {
  const c = state.clips.find((x) => x.path === path);
  if (!c) return;
  if (c.error) { toast(c.error, 'error'); return; }

  state.currentPath = path;
  state.viewingRender = false;
  el.video.loop = false;

  $$('.clip').forEach((row) => {
    row.classList.toggle('active', row.dataset.path === encodeURIComponent(path));
  });

  el.video.src = `/media?path=${encodeURIComponent(path)}`;
  el.video.load();
  el.stageEmpty.hidden = true;
  el.stageHud.hidden = false;
  el.timeline.classList.remove('disabled');
  el.controls.classList.remove('disabled');
  el.hudRendered.hidden = true;
  el.hudName.textContent = c.name;

  const bits = [ `${c.displayWidth}×${c.displayHeight}` ];
  if (c.fps) bits.push(`${c.fps.toFixed(2)} fps`);
  bits.push(fmtTime(c.duration));
  if (c.rotation) bits.push(`file rotation ${c.rotation}°`);
  el.hudMeta.textContent = bits.join(' · ');

  loadFilmstrip(c);
  buildRuler(c);
  syncControls();
  updateTimeline();
  layoutStage();

  history.replaceState(null, '', `#${encodeURIComponent(path)}`);

  if (autoplay) el.video.play().catch(() => {});
}

let stripToken = 0;

function loadFilmstrip(c, { keep = false } = {}) {
  const url = `/api/filmstrip?path=${encodeURIComponent(c.path)}&${orientQuery(c)}${c.mtimeMs ? `&v=${Math.round(c.mtimeMs)}` : ''}`;
  if (keep && el.filmstrip.dataset.url === url) return;
  if (!keep) {
    el.stripLoading.style.opacity = '1';
    el.filmstrip.style.backgroundImage = 'none';
    el.filmstrip.dataset.url = '';
  }
  const token = ++stripToken;
  const img = new Image();
  img.onload = () => {
    // ignore responses that were superseded by a newer request
    if (token !== stripToken || state.currentPath !== c.path) return;
    el.filmstrip.style.backgroundImage = `url("${url}")`;
    el.filmstrip.dataset.url = url;
    el.stripLoading.style.opacity = '0';
  };
  img.onerror = () => {
    if (token !== stripToken) return;
    el.stripLoading.style.opacity = '0';
  };
  img.src = url;
}

let stripTimer = null;
function scheduleFilmstrip() {
  clearTimeout(stripTimer);
  stripTimer = setTimeout(() => {
    const c = cur();
    if (c && !state.viewingRender) loadFilmstrip(c, { keep: true });
  }, 400);
}

/**
 * Size the video box for the current rotation/flip.
 * displayWidth/Height already account for rotation stored in the file,
 * because the browser applies that metadata itself.
 */
function layoutStage() {
  const c = cur();
  if (!c) return;
  const rot = ((Number(c.edit?.rotate) || 0) % 360 + 360) % 360;
  const dw = c.displayWidth || c.width || 16;
  const dh = c.displayHeight || c.height || 9;
  const swapped = rot === 90 || rot === 270;
  const effW = swapped ? dh : dw;
  const effH = swapped ? dw : dh;

  const box = el.stage.getBoundingClientRect();
  const pad = 24;
  const scale = Math.max(0.01, Math.min((box.width - pad) / effW, (box.height - pad) / effH));
  el.video.style.width = `${Math.max(1, Math.round(dw * scale))}px`;
  el.video.style.height = `${Math.max(1, Math.round(dh * scale))}px`;
  // scale() before rotate() means the flip happens in output space, like ffmpeg's filter order
  el.video.style.transform =
    `scale(${c.edit?.flipH ? -1 : 1}, ${c.edit?.flipV ? -1 : 1}) rotate(${rot}deg)`;
}

function togglePlay() {
  const c = cur();
  if (!c || state.viewingRender) {
    if (state.viewingRender) {
      if (el.video.paused) el.video.play().catch(() => {}); else el.video.pause();
    }
    return;
  }
  if (el.video.paused) {
    if (el.video.currentTime < c.edit.in - 0.02 || el.video.currentTime >= c.edit.out - 0.02) {
      el.video.currentTime = c.edit.in;
    }
    el.video.play().catch(() => {});
  } else {
    el.video.pause();
  }
}

function seekTo(t) {
  const c = cur();
  if (!c) return;
  el.video.currentTime = clamp(t, 0, Math.max(0, (c.duration || 0) - 0.001));
  updatePlayhead();
}

function stepFrame(dir) {
  const c = cur();
  if (!c) return;
  if (!el.video.paused) el.video.pause();
  const f = 1 / (c.fps || 30);
  const t = clamp(el.video.currentTime + dir * f, 0, c.duration);
  seekTo(t);
}

/* playback loop that enforces the out point */
function tick() {
  if (!state.playing) { state.rafActive = false; return; }
  const c = cur();
  if (c && !state.viewingRender) {
    const t = el.video.currentTime;
    if (t >= c.edit.out - 0.004) {
      if (state.loop) {
        el.video.currentTime = c.edit.in;
      } else {
        el.video.pause();
        el.video.currentTime = Math.max(c.edit.in, c.edit.out - 0.02);
      }
    }
    updatePlayhead();
  }
  requestAnimationFrame(tick);
}

function updatePlayhead() {
  const c = cur();
  if (!c || state.viewingRender) return;
  const d = c.duration || 1;
  const pct = clamp(el.video.currentTime / d, 0, 1) * 100;
  el.playhead.style.left = `${pct}%`;
}

function buildRuler(c) {
  const ticks = 6;
  const dur = c.duration || 0;
  const parts = [];
  for (let i = 0; i <= ticks; i++) {
    const t = (dur * i) / ticks;
    parts.push(`<span style="left:${(i / ticks) * 100}%">${fmtTime(t, 1)}</span>`);
  }
  el.ruler.innerHTML = parts.join('');
}

/* ------------------------------------------------------------------ */
/* edits                                                              */
/* ------------------------------------------------------------------ */

function normalizeEdit(edit, duration) {
  let inP = Number(edit.in);
  let outP = Number(edit.out);
  if (!Number.isFinite(inP)) inP = 0;
  if (!Number.isFinite(outP)) outP = duration;
  inP = clamp(inP, 0, Math.max(0, duration - MIN_GAP));
  outP = clamp(outP, inP + MIN_GAP, duration);
  return {
    ...edit,
    in: round3(inP),
    out: round3(outP),
    rotate: [0, 90, 180, 270].includes(Number(edit.rotate)) ? Number(edit.rotate) : 0,
    flipH: Boolean(edit.flipH),
    flipV: Boolean(edit.flipV),
    // null = inherit the default from settings; true/false = explicit choice
    audio: typeof edit.audio === 'boolean' ? edit.audio : null,
  };
}

/** Effective "keep audio" for a clip: its own choice, else the settings default. */
function keepAudio(c) {
  if (typeof c?.edit?.audio === 'boolean') return c.edit.audio;
  return Boolean(state.settings?.audio);
}

function setEdit(patch, { seek = null } = {}) {
  const c = cur();
  if (!c || !c.edit || state.viewingRender) return;
  c.edit = normalizeEdit({ ...c.edit, ...patch }, c.duration);
  updateTimeline();
  syncControls();
  layoutStage();
  refreshRow(c.path);
  scheduleSave();
  if ('rotate' in patch || 'flipH' in patch || 'flipV' in patch) scheduleFilmstrip();

  if (seek === 'in') seekTo(c.edit.in);
  else if (seek === 'out') seekTo(Math.max(c.edit.in, c.edit.out - 0.001));
}

function scheduleSave() {
  clearTimeout(state.saveTimer);
  state.saveTimer = setTimeout(saveEdit, 220);
}

async function saveEdit() {
  const c = cur();
  if (!c || !c.edit) return;
  try {
    const saved = await api('/api/edit', { method: 'PUT', body: { path: c.path, edit: c.edit } });
    if (c.edit) {
      c.edit = { ...c.edit, ...saved };
      c.edited = isEdited(c);
      refreshRow(c.path);
    }
  } catch (err) {
    toast(`Could not save edit: ${err.message}`, 'error');
  }
}

function syncControls() {
  const c = cur();
  if (!c || !c.edit) return;
  const rot = c.edit.rotate;
  $$('#rotateSeg button').forEach((b) => b.classList.toggle('on', Number(b.dataset.rotate) === rot));
  el.btnFlipH.classList.toggle('on', c.edit.flipH);
  el.btnFlipV.classList.toggle('on', c.edit.flipV);

  const audioOn = keepAudio(c);
  el.chkAudio.checked = c.hasAudio ? audioOn : false;
  el.chkAudio.disabled = !c.hasAudio;
  el.audioWrap.title = c.hasAudio
    ? (audioOn
      ? 'Audio is kept (source codec copied when MP4 supports it)'
      : 'Audio is stripped from the render')
    : 'This clip has no audio stream';
  el.audioWrap.classList.toggle('on', c.hasAudio && audioOn);

  const len = c.edit.out - c.edit.in;
  const min = state.settings?.minDuration ?? 5;
  if (min > 0 && len < min) {
    el.shortWarn.hidden = false;
    el.shortWarn.textContent = `${len.toFixed(1)}s — under ${min}s`;
  } else {
    el.shortWarn.hidden = true;
  }
}

function updateTimeline() {
  const c = cur();
  if (!c || !c.edit) return;
  const d = c.duration || 1;
  const inPct = clamp(c.edit.in / d, 0, 1) * 100;
  const outPct = clamp(c.edit.out / d, 0, 1) * 100;

  el.shadeL.style.left = '0%';
  el.shadeL.style.width = `${inPct}%`;
  el.shadeR.style.left = `${outPct}%`;
  el.shadeR.style.width = `${100 - outPct}%`;
  el.selRect.style.left = `${inPct}%`;
  el.selRect.style.width = `${Math.max(0, outPct - inPct)}%`;
  el.handleIn.style.left = `${inPct}%`;
  el.handleOut.style.left = `${outPct}%`;

  if (document.activeElement !== el.inInput) el.inInput.value = fmtTime(c.edit.in);
  if (document.activeElement !== el.outInput) el.outInput.value = fmtTime(c.edit.out);
  el.lenRead.textContent = fmtTime(c.edit.out - c.edit.in);
  updatePlayhead();
}

/* ------------------------------------------------------------------ */
/* timeline interaction                                                */
/* ------------------------------------------------------------------ */

function timeFromClientX(clientX) {
  const c = cur();
  if (!c) return 0;
  const r = el.track.getBoundingClientRect();
  return clamp((clientX - r.left) / r.width, 0, 1) * (c.duration || 0);
}

el.track.addEventListener('pointerdown', (e) => {
  if (!cur() || state.viewingRender) return;
  if (e.target.closest('.handle')) return;
  const t = timeFromClientX(e.clientX);
  if (!el.video.paused) el.video.pause();
  seekTo(t);
});

function bindHandle(node, which) {
  node.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    e.stopPropagation();
    const c = cur();
    if (!c || state.viewingRender) return;
    node.setPointerCapture(e.pointerId);
    if (!el.video.paused) el.video.pause();

    const move = (ev) => {
      const t = timeFromClientX(ev.clientX);
      if (which === 'in') {
        setEdit({ in: clamp(t, 0, c.edit.out - MIN_GAP) }, { seek: 'in' });
      } else {
        setEdit({ out: clamp(t, c.edit.in + MIN_GAP, c.duration) }, { seek: 'out' });
      }
    };
    const up = () => {
      node.removeEventListener('pointermove', move);
      node.removeEventListener('pointerup', up);
      node.removeEventListener('pointercancel', up);
    };
    node.addEventListener('pointermove', move);
    node.addEventListener('pointerup', up);
    node.addEventListener('pointercancel', up);
    move(e);
  });
}
bindHandle(el.handleIn, 'in');
bindHandle(el.handleOut, 'out');

el.inInput.addEventListener('change', () => {
  const c = cur();
  const t = parseTime(el.inInput.value);
  if (!c || !Number.isFinite(t)) { updateTimeline(); return; }
  setEdit({ in: clamp(t, 0, c.edit.out - MIN_GAP) }, { seek: 'in' });
});
el.inInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') el.inInput.blur(); });
el.outInput.addEventListener('change', () => {
  const c = cur();
  const t = parseTime(el.outInput.value);
  if (!c || !Number.isFinite(t)) { updateTimeline(); return; }
  setEdit({ out: clamp(t, c.edit.in + MIN_GAP, c.duration) }, { seek: 'out' });
});
el.outInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') el.outInput.blur(); });

/* ------------------------------------------------------------------ */
/* video events                                                        */
/* ------------------------------------------------------------------ */

state.playing = false;

el.video.addEventListener('loadedmetadata', () => {
  const c = cur();
  if (!c) return;
  if (!state.viewingRender && Number.isFinite(c.edit?.in)) {
    el.video.currentTime = c.edit.in;
  }
  layoutStage();
  updatePlayhead();
});

el.video.addEventListener('play', () => {
  state.playing = true;
  el.btnPlay.textContent = '❚❚';
  el.bigPlay.hidden = true;
  if (!state.rafActive) {
    state.rafActive = true;
    requestAnimationFrame(tick);
  }
});
el.video.addEventListener('pause', () => {
  state.playing = false;
  el.btnPlay.textContent = '▶';
  if (state.currentPath) el.bigPlay.hidden = false;
});
el.video.addEventListener('timeupdate', updatePlayhead);
el.video.addEventListener('seeking', updatePlayhead);

el.stage.addEventListener('click', (e) => {
  if (e.target.closest('#hudRendered')) return;
  togglePlay();
});

new ResizeObserver(() => layoutStage()).observe(el.stage);
window.addEventListener('resize', layoutStage);

/* ------------------------------------------------------------------ */
/* controls                                                           */
/* ------------------------------------------------------------------ */

el.btnPlay.addEventListener('click', togglePlay);
el.btnStepBack.addEventListener('click', () => stepFrame(-1));
el.btnStepFwd.addEventListener('click', () => stepFrame(1));
el.chkLoop.addEventListener('change', () => { state.loop = el.chkLoop.checked; });

el.btnSetIn.addEventListener('click', () => {
  const c = cur();
  if (!c) return;
  setEdit({ in: clamp(el.video.currentTime, 0, c.edit.out - MIN_GAP) });
});
el.btnSetOut.addEventListener('click', () => {
  const c = cur();
  if (!c) return;
  setEdit({ out: clamp(el.video.currentTime, c.edit.in + MIN_GAP, c.duration) });
});
el.btnReset.addEventListener('click', () => {
  const c = cur();
  if (!c) return;
  setEdit({ in: 0, out: c.duration, rotate: 0, flipH: false, flipV: false, audio: null }, { seek: 'in' });
  toast('Reset trim, orientation and audio');
});

el.rotateSeg.addEventListener('click', (e) => {
  const btn = e.target.closest('button[data-rotate]');
  if (!btn) return;
  setEdit({ rotate: Number(btn.dataset.rotate) });
});
el.btnFlipH.addEventListener('click', () => {
  const c = cur();
  if (c) setEdit({ flipH: !c.edit.flipH });
});
el.btnFlipV.addEventListener('click', () => {
  const c = cur();
  if (c) setEdit({ flipV: !c.edit.flipV });
});

el.chkAudio.addEventListener('change', () => {
  const c = cur();
  if (!c || !c.edit || !c.hasAudio) return;
  setEdit({ audio: el.chkAudio.checked });
});

el.clipList.addEventListener('click', (e) => {
  const row = e.target.closest('.clip');
  if (!row) return;
  const path = decodeURIComponent(row.dataset.path);
  const rotBtn = e.target.closest('.row-rot');
  if (rotBtn) {
    const c = state.clips.find((x) => x.path === path);
    if (!c?.edit) return;
    const next = (Number(c.edit.rotate) + 90) % 360;
    c.edit = normalizeEdit({ ...c.edit, rotate: next }, c.duration);
    refreshRow(path);
    if (state.currentPath === path) {
      syncControls();
      layoutStage();
      updateTimeline();
      scheduleFilmstrip();
    }
    saveEdit();
    return;
  }
  selectClip(path);
});

el.hudRendered.addEventListener('click', () => {
  if (state.currentPath) selectClip(state.currentPath);
});

el.search.addEventListener('input', () => {
  state.filter = el.search.value;
  renderLibrary();
});

/* ------------------------------------------------------------------ */
/* render + queue                                                     */
/* ------------------------------------------------------------------ */

async function renderPaths(paths) {
  if (!paths.length) return;
  const edits = {};
  for (const p of paths) {
    const c = state.clips.find((x) => x.path === p);
    if (c?.edit) edits[p] = c.edit;
  }
  try {
    await api('/api/render', { method: 'POST', body: { paths, edits } });
    openQueue(true);
    pollJobs();
  } catch (err) {
    toast(`Render failed to start: ${err.message}`, 'error');
  }
}

el.btnRender.addEventListener('click', () => {
  if (!state.currentPath) return;
  saveEdit();
  renderPaths([state.currentPath]);
});

el.btnRenderAll.addEventListener('click', () => {
  const paths = state.clips.filter((c) => !c.error).map((c) => c.path);
  if (!paths.length) return;
  if (paths.length > 2 && !confirm(`Render ${paths.length} clips to ${state.settings?.outputDir}?`)) return;
  renderPaths(paths);
});

el.btnRenderEdited.addEventListener('click', () => {
  const paths = state.clips.filter((c) => !c.error && isEdited(c)).map((c) => c.path);
  if (!paths.length) return;
  renderPaths(paths);
});

function statusIcon(status) {
  return { queued: '•', running: '▶', done: '✓', error: '!', canceled: '✕' }[status] || '•';
}

function jobHtml(j) {
  const pct = Math.round((j.progress || 0) * 100);
  const meta = j.status === 'running'
    ? `${pct}%${j.speed ? ` · ${j.speed}` : ''}`
    : j.status === 'queued' ? 'waiting'
    : j.status === 'done' ? fmtTime(j.duration)
    : j.status === 'error' ? 'failed'
    : 'canceled';
  const active = j.status === 'running' || j.status === 'queued';
  return `
    <div class="job ${j.status}">
      <span class="st">${statusIcon(j.status)}</span>
      <span class="jname" title="${esc(j.name)}">${esc(j.outName)}</span>
      <span class="jbar"><i style="width:${j.status === 'done' ? 100 : pct}%"></i></span>
      <span class="jmeta">${esc(meta)}</span>
      <span>
        ${j.status === 'done' ? `<button class="jbtn" data-play="${encodeURIComponent(j.outName)}">play</button>` : ''}
        ${active ? `<button class="jbtn" data-cancel="${j.id}">cancel</button>` : ''}
        ${j.status === 'error' ? `<button class="jbtn" data-retry="${encodeURIComponent(j.path)}">retry</button>` : ''}
      </span>
      ${j.error ? `<span class="jerr" title="${esc(j.error)}">${esc(j.error.slice(0, 220))}</span>` : ''}
    </div>`;
}

function renderQueue() {
  const jobs = state.jobs;
  const active = jobs.filter((j) => j.status === 'running' || j.status === 'queued');
  el.queueBadge.hidden = active.length === 0;
  el.queueBadge.textContent = String(active.length);

  if (active.length) {
    const running = jobs.find((j) => j.status === 'running');
    el.queueSummary.textContent = running
      ? `rendering ${running.outName} · ${Math.round((running.progress || 0) * 100)}%`
      : `${active.length} queued`;
  } else {
    const done = jobs.filter((j) => j.status === 'done').length;
    const failed = jobs.filter((j) => j.status === 'error').length;
    el.queueSummary.textContent = jobs.length
      ? `${done} done${failed ? ` · ${failed} failed` : ''}`
      : '';
  }

  if (el.queueBody.hidden) return;
  el.queueBody.innerHTML = jobs.length
    ? jobs.slice(0, 40).map(jobHtml).join('')
    : '<div class="queue-empty">Nothing rendered in this session yet.</div>';
}

function openQueue(open = true) {
  el.queueBody.hidden = !open;
  el.queueChev.textContent = open ? '▾' : '▴';
  if (open) renderQueue();
}

el.queueToggle.addEventListener('click', () => openQueue(el.queueBody.hidden));

el.queueBody.addEventListener('click', async (e) => {
  const play = e.target.closest('[data-play]');
  if (play) {
    showRendered(decodeURIComponent(play.dataset.play));
    return;
  }
  const cancel = e.target.closest('[data-cancel]');
  if (cancel) {
    try { await api(`/api/jobs/${cancel.dataset.cancel}/cancel`, { method: 'POST' }); pollJobs(); }
    catch (err) { toast(err.message, 'error'); }
    return;
  }
  const retry = e.target.closest('[data-retry]');
  if (retry) renderPaths([decodeURIComponent(retry.dataset.retry)]);
});

function showRendered(outName) {
  state.viewingRender = true;
  el.video.pause();
  el.video.loop = true;
  el.video.src = `/renders/${encodeURIComponent(outName)}`;
  el.video.load();
  el.timeline.classList.add('disabled');
  el.controls.classList.add('disabled');
  el.stageEmpty.hidden = true;
  el.stageHud.hidden = false;
  el.hudRendered.hidden = false;
  el.hudName.textContent = outName;
  el.hudMeta.textContent = 'rendered output';
  el.bigPlay.hidden = true;
  el.playhead.style.left = '0%';
  el.video.play().catch(() => {});
}

let pollTimer = null;
async function pollJobs() {
  clearTimeout(pollTimer);
  pollTimer = null;
  try {
    const { jobs } = await api('/api/jobs');
    const prevActive = state.jobs.some((j) => j.status === 'running' || j.status === 'queued');
    state.jobs = jobs || [];
    renderQueue();
    const active = state.jobs.some((j) => j.status === 'running' || j.status === 'queued');
    if (active) {
      pollTimer = setTimeout(pollJobs, 650);
    } else if (prevActive) {
      // a batch just finished — refresh so "rendered" badges appear
      loadLibrary({ silent: true });
    }
  } catch {
    pollTimer = setTimeout(pollJobs, 2500);
  }
}

el.video.addEventListener('ended', () => {
  if (!state.viewingRender) return;
  el.video.currentTime = 0;
  el.video.play().catch(() => {});
});

/* ------------------------------------------------------------------ */
/* settings + folder picker                                           */
/* ------------------------------------------------------------------ */

function updateSourceUI() {
  const s = state.settings;
  if (!s) return;
  el.sourcePath.textContent = s.root;
  el.sourceBtn.title = `Source: ${s.root}\nClick to change`;
}

async function openSettings() {
  const s = state.settings || (await api('/api/settings'));
  state.settings = s;
  $('#setRoot').value = s.root;
  $('#setOut').value = s.outputDir;
  $('#setSuffix').value = s.suffix;
  $('#setCrf').value = s.crf;
  $('#crfVal').textContent = s.crf;
  $('#setPreset').value = s.preset;
  $('#setMinDur').value = s.minDuration;
  $('#setAudio').checked = Boolean(s.audio);
  $('#ffmpegStatus').textContent = s.ffmpeg ? 'ffmpeg ready' : 'ffmpeg not found';
  el.settingsModal.hidden = false;
}

$('#setCrf').addEventListener('input', () => { $('#crfVal').textContent = $('#setCrf').value; });

$('#saveSettings').addEventListener('click', async () => {
  const body = {
    root: $('#setRoot').value.trim(),
    outputDir: $('#setOut').value.trim(),
    suffix: $('#setSuffix').value,
    crf: Number($('#setCrf').value),
    preset: $('#setPreset').value,
    minDuration: Number($('#setMinDur').value),
    audio: $('#setAudio').checked,
  };
  try {
    state.settings = await api('/api/settings', { method: 'PUT', body });
    el.settingsModal.hidden = true;
    updateSourceUI();
    await loadLibrary({ refresh: true });
    if (state.currentPath) {
      const c = cur();
      if (c) selectClip(state.currentPath);
    }
    toast('Settings saved');
  } catch (err) {
    toast(err.message, 'error');
  }
});

el.btnSettings.addEventListener('click', openSettings);
el.sourceBtn.addEventListener('click', openSettings);
$('#browseRoot').addEventListener('click', () => openBrowse($('#setRoot').value, (p) => { $('#setRoot').value = p; }));
$('#browseOut').addEventListener('click', () => openBrowse($('#setOut').value, (p) => { $('#setOut').value = p; }));

async function openBrowse(startPath, cb) {
  state.browseCb = cb;
  state.browsePath = startPath || state.settings?.root || '';
  el.browseModal.hidden = false;
  await loadBrowse(state.browsePath);
}

async function loadBrowse(p) {
  try {
    const data = await api(`/api/browse?path=${encodeURIComponent(p || '')}`);
    state.browsePath = data.path;
    $('#browsePath').textContent = data.path;
    $('#browsePath').title = data.path;
    $('#browseUp').disabled = !data.parent;
    $('#browseUp').dataset.parent = data.parent || '';
    const items = data.dirs.map((d) => `
      <button class="browse-item" data-dir="${encodeURIComponent(d)}">
        <span class="ico">▸</span><span>${esc(d)}</span>
      </button>`).join('');
    $('#browseList').innerHTML = items || '<div class="browse-none">No subfolders</div>';
    $('#browseHint').textContent = `${data.videoCount} video file${data.videoCount === 1 ? '' : 's'} directly in this folder`;
  } catch (err) {
    toast(err.message, 'error');
  }
}

$('#browseList').addEventListener('click', (e) => {
  const btn = e.target.closest('[data-dir]');
  if (!btn) return;
  loadBrowse(`${state.browsePath.replace(/\/$/, '')}/${decodeURIComponent(btn.dataset.dir)}`);
});
$('#browseUp').addEventListener('click', (e) => {
  const p = e.currentTarget.dataset.parent;
  if (p) loadBrowse(p);
});
$('#browseUse').addEventListener('click', () => {
  if (state.browseCb) state.browseCb(state.browsePath);
  el.browseModal.hidden = true;
});

$$('[data-close]').forEach((b) => {
  b.addEventListener('click', () => {
    b.closest('.modal-backdrop').hidden = true;
  });
});
$$('.modal-backdrop').forEach((bd) => {
  bd.addEventListener('click', (e) => { if (e.target === bd) bd.hidden = true; });
});

el.btnRescan.addEventListener('click', async () => {
  el.btnRescan.disabled = true;
  await loadLibrary({ refresh: true });
  el.btnRescan.disabled = false;
  toast('Library refreshed');
});

el.btnRenders.addEventListener('click', async () => {
  try { await api('/api/reveal', { method: 'POST', body: { dir: state.settings.outputDir } }); }
  catch (err) { toast(err.message, 'error'); }
});

/* ------------------------------------------------------------------ */
/* keyboard                                                           */
/* ------------------------------------------------------------------ */

document.addEventListener('keydown', (e) => {
  const inField = /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName);
  if (inField) return;
  if (e.metaKey || e.ctrlKey) {
    if (e.key === 'Enter') { e.preventDefault(); if (state.currentPath) renderPaths([state.currentPath]); }
    return;
  }
  if (!state.currentPath) return;
  const c = cur();
  if (!c) return;

  switch (e.key) {
    case ' ':
      e.preventDefault();
      togglePlay();
      break;
    case 'i': case 'I':
      e.preventDefault();
      setEdit({ in: clamp(el.video.currentTime, 0, c.edit.out - MIN_GAP) });
      break;
    case 'o': case 'O':
      e.preventDefault();
      setEdit({ out: clamp(el.video.currentTime, c.edit.in + MIN_GAP, c.duration) });
      break;
    case 'ArrowLeft': {
      e.preventDefault();
      const step = e.shiftKey ? 1 : 1 / (c.fps || 30);
      if (!el.video.paused) el.video.pause();
      seekTo(el.video.currentTime - step);
      break;
    }
    case 'ArrowRight': {
      e.preventDefault();
      const step = e.shiftKey ? 1 : 1 / (c.fps || 30);
      if (!el.video.paused) el.video.pause();
      seekTo(el.video.currentTime + step);
      break;
    }
    case '[': {
      e.preventDefault();
      setEdit({ in: clamp(c.edit.in - (e.shiftKey ? 1 : 0.1), 0, c.edit.out - MIN_GAP) }, { seek: 'in' });
      break;
    }
    case ']': {
      e.preventDefault();
      setEdit({ out: clamp(c.edit.out + (e.shiftKey ? 1 : 0.1), c.edit.in + MIN_GAP, c.duration) }, { seek: 'out' });
      break;
    }
    case 'r': case 'R':
      e.preventDefault();
      setEdit({ rotate: (Number(c.edit.rotate) + 90) % 360 });
      break;
    case 'Escape':
      $$('.modal-backdrop').forEach((m) => { m.hidden = true; });
      break;
  }
});

/* ------------------------------------------------------------------ */
/* boot                                                               */
/* ------------------------------------------------------------------ */

(async function init() {
  try {
    state.settings = await api('/api/settings');
  } catch (err) {
    toast('Server not reachable', 'error');
    return;
  }
  updateSourceUI();
  if (!state.settings.ffmpeg) toast('ffmpeg was not found on PATH', 'error');
  await loadLibrary();

  const hashPath = location.hash ? decodeURIComponent(location.hash.slice(1)) : '';
  if (hashPath && state.clips.some((c) => c.path === hashPath)) selectClip(hashPath);
  else if (state.clips.length) selectClip(state.clips[0].path);

  await pollJobs();
})();
