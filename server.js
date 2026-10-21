import express from 'express';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { probe, isVideoFile, renderArgs, buildVideoFilter } from './lib/media.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DATA_DIR = path.join(__dirname, 'data');
const CACHE_DIR = path.join(__dirname, '.cache');
const PORT = Number(process.env.PORT) || 4321;

const DEFAULT_SETTINGS = {
  root: path.join(__dirname, 'input'),
  outputDir: path.join(__dirname, 'renders'),
  suffix: '-edit',
  crf: 18,
  preset: 'medium',
  minDuration: 5,
  audio: false,
};

const PRESETS = new Set([
  'ultrafast', 'superfast', 'veryfast', 'faster', 'fast',
  'medium', 'slow', 'slower', 'veryslow',
]);

/* ------------------------------------------------------------------ */
/* tiny json store                                                     */
/* ------------------------------------------------------------------ */

async function readJson(file, fallback) {
  try {
    return JSON.parse(await fsp.readFile(file, 'utf8'));
  } catch {
    return structuredClone(fallback);
  }
}

const pendingWrites = new Map();
function writeJsonSoon(file, value, delay = 250) {
  clearTimeout(pendingWrites.get(file));
  pendingWrites.set(file, setTimeout(async () => {
    pendingWrites.delete(file);
    try {
      await fsp.mkdir(path.dirname(file), { recursive: true });
      await fsp.writeFile(file, JSON.stringify(value, null, 2));
    } catch (err) {
      console.error('Failed to write', file, err.message);
    }
  }, delay));
}

const SETTINGS_FILE = path.join(DATA_DIR, 'settings.json');
const EDITS_FILE = path.join(DATA_DIR, 'edits.json');
const PROBE_CACHE_FILE = path.join(CACHE_DIR, 'probe.json');

await fsp.mkdir(DATA_DIR, { recursive: true });
await fsp.mkdir(CACHE_DIR, { recursive: true });

let settings = { ...DEFAULT_SETTINGS, ...(await readJson(SETTINGS_FILE, {})) };
/** @type {Record<string, {in:number,out:number,rotate:number,flipH:boolean,flipV:boolean,outputPath?:string,renderedAt?:number}>} */
let edits = await readJson(EDITS_FILE, {});

/** @type {Map<string, {mtimeMs:number,size:number,meta:object}>} */
const probeCache = new Map(Object.entries(await readJson(PROBE_CACHE_FILE, {})));

function saveSettings() { writeJsonSoon(SETTINGS_FILE, settings); }
function saveEdits() { writeJsonSoon(EDITS_FILE, edits); }
function saveProbeCache() {
  writeJsonSoon(PROBE_CACHE_FILE, Object.fromEntries(probeCache), 1500);
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

function sha1(s) {
  return crypto.createHash('sha1').update(s).digest('hex');
}

async function exists(p) {
  try { await fsp.access(p); return true; } catch { return false; }
}

function clamp(n, lo, hi) { return Math.min(hi, Math.max(lo, n)); }

/** Resolve a user-supplied path and make sure it stays inside `base`. */
function resolveInside(base, p) {
  if (typeof p !== 'string' || !p) throw new HttpError(400, 'Missing path');
  const abs = path.resolve(base, p);
  const rel = path.relative(base, abs);
  if (rel.startsWith('..') || path.isAbsolute(rel)) {
    throw new HttpError(403, 'Path is outside the allowed folder');
  }
  return abs;
}

class HttpError extends Error {
  constructor(status, message) { super(message); this.status = status; }
}

/** Run `fn` over `items` with at most `limit` in flight. */
async function mapLimit(items, limit, fn) {
  const out = new Array(items.length);
  let next = 0;
  const workers = Array.from({ length: Math.min(limit, items.length) }, async () => {
    while (true) {
      const i = next++;
      if (i >= items.length) return;
      out[i] = await fn(items[i], i);
    }
  });
  await Promise.all(workers);
  return out;
}

function defaultEdit(meta) {
  return {
    in: 0,
    out: Math.round(meta.duration * 1000) / 1000,
    rotate: 0,
    flipH: false,
    flipV: false,
    // null = follow settings.audio; true/false = explicit per-clip choice
    audio: null,
  };
}

function normalizeEdit(raw, meta) {
  const base = defaultEdit(meta);
  if (!raw || typeof raw !== 'object') return base;
  const dur = meta.duration > 0 ? meta.duration : 0.05;
  let inP = Number(raw.in);
  let outP = Number(raw.out);
  if (!Number.isFinite(inP)) inP = 0;
  if (!Number.isFinite(outP)) outP = dur;
  inP = clamp(inP, 0, Math.max(0, dur - 0.05));
  outP = clamp(outP, inP + 0.05, dur);
  return {
    in: Math.round(inP * 1000) / 1000,
    out: Math.round(outP * 1000) / 1000,
    rotate: [0, 90, 180, 270].includes(Number(raw.rotate)) ? Number(raw.rotate) : 0,
    flipH: Boolean(raw.flipH),
    flipV: Boolean(raw.flipV),
    audio: typeof raw.audio === 'boolean' ? raw.audio : null,
  };
}

const CUT_KEYS = ['in', 'out', 'rotate', 'flipH', 'flipV', 'audio'];
function sameCut(a, b) {
  if (!a || !b) return false;
  return CUT_KEYS.every((k) => a[k] === b[k]);
}

/** Stored edit for a file: cut values plus render bookkeeping. */
function editFor(file, meta) {
  const stored = edits[file];
  const e = normalizeEdit(stored, meta);
  if (stored) {
    if (stored.outputPath) e.outputPath = stored.outputPath;
    if (stored.renderedAt) e.renderedAt = stored.renderedAt;
    if (stored.modifiedAt) e.modifiedAt = stored.modifiedAt;
  }
  return e;
}

/* ------------------------------------------------------------------ */
/* probing + thumbnails                                                */
/* ------------------------------------------------------------------ */

async function probeCached(file) {
  const st = await fsp.stat(file);
  const cached = probeCache.get(file);
  if (cached && cached.mtimeMs === st.mtimeMs && cached.size === st.size) {
    return { meta: cached.meta, stat: st };
  }
  let meta;
  try {
    meta = await probe(file);
  } catch (err) {
    throw new HttpError(422, `Could not read ${path.basename(file)}: ${err.message}`);
  }
  probeCache.set(file, { mtimeMs: st.mtimeMs, size: st.size, meta });
  saveProbeCache();
  return { meta, stat: st };
}

const inflight = new Map();

function readOrient(q) {
  const rotate = [0, 90, 180, 270].includes(Number(q.rotate)) ? Number(q.rotate) : 0;
  const flag = (v) => v === '1' || v === 'true';
  return { rotate, flipH: flag(q.flipH), flipV: flag(q.flipV) };
}

async function ensureThumb(file, stat, kind, duration, orient = { rotate: 0, flipH: false, flipV: false }) {
  const tag = `${orient.rotate || 0}-${orient.flipH ? 1 : 0}-${orient.flipV ? 1 : 0}`;
  const key = sha1(`${file}|${stat.mtimeMs}|${stat.size}|${kind}|${tag}`).slice(0, 24);
  const out = path.join(CACHE_DIR, `${kind}-${key}.jpg`);
  if (await exists(out)) return out;
  if (inflight.has(out)) return inflight.get(out);

  const job = (async () => {
    const orientation = buildVideoFilter(orient);
    const args = ['-hide_banner', '-loglevel', 'error', '-nostdin', '-y'];
    if (kind === 'thumb') {
      const at = Math.max(0, Math.min(1.5, (duration || 2) * 0.15));
      args.push('-ss', at.toFixed(3));
    }
    args.push('-i', file);
    if (kind === 'thumb') {
      args.push('-frames:v', '1', '-vf', `${orientation},scale=480:-2`, '-q:v', '4');
    } else {
      // one tiled filmstrip of ~10 upright frames spread across the clip
      const fps = Math.min(30, Math.max(0.02, 10 / Math.max(duration || 1, 0.2)));
      args.push(
        '-vf', `${orientation},fps=${fps.toFixed(6)},scale=260:-2,crop=260:146,tile=10x1`,
        '-frames:v', '1', '-q:v', '5', '-an', '-sn'
      );
    }
    args.push(out);
    await new Promise((resolve, reject) => {
      const child = spawn('ffmpeg', args, { stdio: ['ignore', 'ignore', 'pipe'] });
      let err = '';
      child.stderr.on('data', (d) => { err += d; });
      child.on('error', reject);
      child.on('close', (code) => code === 0 ? resolve() : reject(new Error(err.trim() || `ffmpeg ${code}`)));
    });
    return out;
  })().finally(() => inflight.delete(out));

  inflight.set(out, job);
  return job;
}

/* ------------------------------------------------------------------ */
/* render queue                                                        */
/* ------------------------------------------------------------------ */

/** @type {Array<any>} */
const queue = [];
const jobs = new Map();
let pumping = false;

function publicJob(j) {
  return {
    id: j.id,
    path: j.path,
    name: path.basename(j.path),
    outPath: j.outPath,
    outName: path.basename(j.outPath),
    status: j.status,
    progress: Math.round((j.progress || 0) * 1000) / 1000,
    speed: j.speed || null,
    error: j.error || null,
    duration: j.duration,
    createdAt: j.createdAt,
    endedAt: j.endedAt || null,
  };
}

/**
 * Pick an output path that doesn't clobber unrelated files.
 * Files this app rendered before (tracked in `edits`) may be overwritten, so
 * re-rendering a clip after changing the trim updates the same file.
 */
function uniqueOutputPath(dir, baseName, suffix) {
  const clean = baseName.replace(/[\\/:*?"<>|]+/g, '_').trim() || 'clip';
  const reserved = new Set(
    [...jobs.values()]
      .filter((j) => j.status === 'queued' || j.status === 'running')
      .map((j) => j.outPath)
  );
  const isOurs = (p) => Object.values(edits).some((e) => e.outputPath === p);

  let n = 1;
  for (;;) {
    const candidate = path.join(dir, `${clean}${suffix}${n === 1 ? '' : `-${n}`}.mp4`);
    n += 1;
    if (reserved.has(candidate)) continue;
    if (isOurs(candidate)) return candidate;
    if (!fs.existsSync(candidate)) return candidate;
    if (n > 999) return path.join(dir, `${clean}${suffix}-${Date.now()}.mp4`);
  }
}

function pump() {
  if (pumping) return;
  const job = queue.shift();
  if (!job) return;
  pumping = true;
  runJob(job).finally(() => { pumping = false; pump(); });
}

async function runJob(job) {
  job.status = 'running';
  job.startedAt = Date.now();
  await fsp.mkdir(path.dirname(job.outPath), { recursive: true });

  const args = renderArgs({
    input: job.path,
    output: job.outPath,
    inPoint: job.edit.in,
    duration: job.edit.out - job.edit.in,
    rotate: job.edit.rotate,
    flipH: job.edit.flipH,
    flipV: job.edit.flipV,
    hasAudio: job.hasAudio,
    audioCodec: job.audioCodec,
    keepAudio: job.keepAudio,
    crf: settings.crf,
    preset: settings.preset,
  });

  await new Promise((resolve) => {
    const child = spawn('ffmpeg', args, { stdio: ['ignore', 'pipe', 'pipe'] });
    job._child = child;
    let stderr = '';
    let buf = '';

    child.stdout.on('data', (d) => {
      buf += d.toString();
      let idx;
      while ((idx = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, idx).trim();
        buf = buf.slice(idx + 1);
        const eq = line.indexOf('=');
        if (eq < 0) continue;
        const k = line.slice(0, eq);
        const v = line.slice(eq + 1);
        if (k === 'out_time_us' || k === 'out_time_ms') {
          const sec = Number(v) / 1e6;
          if (Number.isFinite(sec) && job.duration > 0) {
            job.progress = Math.min(1, Math.max(job.progress, sec / job.duration));
          }
        } else if (k === 'speed') {
          job.speed = v;
        } else if (k === 'progress' && v === 'end') {
          job.progress = 1;
        }
      }
    });

    child.stderr.on('data', (d) => {
      stderr += d.toString();
      if (stderr.length > 12000) stderr = stderr.slice(-12000);
    });

    child.on('error', async (err) => {
      job.status = 'error';
      job.error = err.message;
      job.endedAt = Date.now();
      resolve();
    });

    child.on('close', async (code) => {
      job._child = null;
      if (job.canceled) {
        job.status = 'canceled';
        job.endedAt = Date.now();
        await fsp.rm(job.outPath, { force: true }).catch(() => {});
      } else if (code === 0) {
        job.status = 'done';
        job.progress = 1;
        job.endedAt = Date.now();
        const e = edits[job.path];
        if (e) {
          e.outputPath = job.outPath;
          e.renderedAt = job.endedAt;
          saveEdits();
        }
      } else {
        job.status = 'error';
        job.endedAt = Date.now();
        const tail = stderr.trim().split('\n').filter(Boolean).slice(-4).join(' ');
        job.error = tail || `ffmpeg exited with code ${code}`;
        await fsp.rm(job.outPath, { force: true }).catch(() => {});
      }
      resolve();
    });
  });
}

/* ------------------------------------------------------------------ */
/* library scan                                                        */
/* ------------------------------------------------------------------ */

async function scanLibrary() {
  const root = settings.root;
  await fsp.mkdir(root, { recursive: true }).catch(() => {});

  const files = [];
  async function walk(dir, depth) {
    let entries;
    try { entries = await fsp.readdir(dir, { withFileTypes: true }); } catch { return; }
    for (const ent of entries.sort((a, b) => a.name.localeCompare(b.name))) {
      if (ent.name.startsWith('.')) continue;
      const full = path.join(dir, ent.name);
      if (ent.isDirectory()) {
        if (depth < 3) await walk(full, depth + 1);
      } else if (ent.isFile() && isVideoFile(ent.name)) {
        files.push(full);
      }
    }
  }
  await walk(root, 0);

  const clips = await mapLimit(files, 4, async (file) => {
    try {
      const { meta, stat } = await probeCached(file);
      const edit = editFor(file, meta);
      const renderedOk = Boolean(edit.outputPath && await exists(edit.outputPath));
      const rel = path.relative(root, file);
      const sub = path.dirname(rel);
      return {
        path: file,
        name: path.basename(file),
        rel,
        dir: sub === '.' ? '' : sub,
        mtimeMs: stat.mtimeMs,
        ...meta,
        edit,
        edited: isEdited(edit, meta),
        rendered: renderedOk,
        stale: renderedOk && (edit.modifiedAt || 0) > (edit.renderedAt || 0),
      };
    } catch (err) {
      return {
        path: file,
        name: path.basename(file),
        rel: path.relative(root, file),
        dir: '',
        error: err.message,
        edit: null,
      };
    }
  });

  return { root, clips, scannedAt: Date.now() };
}

function isEdited(edit, meta) {
  if (!edit || !meta) return false;
  return (
    edit.in > 0.002 ||
    edit.out < (meta.duration || 0) - 0.002 ||
    edit.rotate !== 0 ||
    edit.flipH ||
    edit.flipV
  );
}

/* ------------------------------------------------------------------ */
/* app                                                                 */
/* ------------------------------------------------------------------ */

const app = express();
app.use(express.json({ limit: '1mb' }));
app.use(express.static(path.join(__dirname, 'public'), { extensions: ['html'] }));

const wrap = (fn) => (req, res, next) => Promise.resolve(fn(req, res, next)).catch((err) => {
  const status = err.status || 500;
  if (status >= 500) console.error(err);
  res.status(status).json({ error: err.message || 'Server error' });
});

app.get('/api/health', wrap(async (_req, res) => {
  res.json({ ok: true, ffmpeg: await ffmpegOk() });
}));

let ffmpegChecked = null;
async function ffmpegOk() {
  if (ffmpegChecked !== null) return ffmpegChecked;
  ffmpegChecked = await new Promise((resolve) => {
    const child = spawn('ffmpeg', ['-version'], { stdio: 'ignore' });
    child.on('error', () => resolve(false));
    child.on('close', (c) => resolve(c === 0));
  });
  return ffmpegChecked;
}

app.get('/api/settings', wrap(async (_req, res) => {
  res.json({ ...settings, ffmpeg: await ffmpegOk() });
}));

app.put('/api/settings', wrap(async (req, res) => {
  const body = req.body || {};
  const next = { ...settings };

  if (typeof body.root === 'string' && body.root.trim()) {
    const root = path.resolve(body.root.trim());
    await fsp.mkdir(root, { recursive: true });
    next.root = root;
  }
  if (typeof body.outputDir === 'string' && body.outputDir.trim()) {
    const out = path.resolve(body.outputDir.trim());
    await fsp.mkdir(out, { recursive: true });
    next.outputDir = out;
  }
  if (typeof body.suffix === 'string') {
    next.suffix = body.suffix.trim().slice(0, 40).replace(/[\\/:*?"<>|]+/g, '');
  }
  if (Number.isFinite(Number(body.crf))) next.crf = clamp(Math.round(Number(body.crf)), 0, 51);
  if (PRESETS.has(body.preset)) next.preset = body.preset;
  if (Number.isFinite(Number(body.minDuration))) next.minDuration = clamp(Number(body.minDuration), 0, 120);
  if (typeof body.audio === 'boolean') next.audio = body.audio;

  settings = next;
  saveSettings();
  res.json(settings);
}));

app.get('/api/library', wrap(async (_req, res) => {
  const data = await scanLibrary();
  res.json(data);
}));

app.get('/api/clip', wrap(async (req, res) => {
  const file = resolveInside(settings.root, req.query.path);
  const { meta } = await probeCached(file);
  res.json({ path: file, name: path.basename(file), ...meta, edit: editFor(file, meta) });
}));

app.put('/api/edit', wrap(async (req, res) => {
  const file = resolveInside(settings.root, req.body?.path);
  const { meta } = await probeCached(file);
  const existing = edits[file] || null;
  const merged = normalizeEdit({ ...existing, ...req.body?.edit }, meta);

  // keep render bookkeeping server-side, and flag the render stale after a re-cut
  if (existing?.outputPath) merged.outputPath = existing.outputPath;
  if (existing?.renderedAt) merged.renderedAt = existing.renderedAt;
  merged.modifiedAt = existing && sameCut(existing, merged)
    ? (existing.modifiedAt || 0)
    : Date.now();

  edits[file] = merged;
  saveEdits();
  res.json(merged);
}));

app.get('/api/thumb', wrap(async (req, res) => {
  const file = resolveInside(settings.root, req.query.path);
  const { meta, stat } = await probeCached(file);
  const thumb = await ensureThumb(file, stat, 'thumb', meta.duration, readOrient(req.query));
  res.set('Cache-Control', 'private, max-age=86400');
  // dotfiles must be allowed: thumbnails live in a .cache directory
  res.sendFile(thumb, { dotfiles: 'allow' });
}));

app.get('/api/filmstrip', wrap(async (req, res) => {
  const file = resolveInside(settings.root, req.query.path);
  const { meta, stat } = await probeCached(file);
  const strip = await ensureThumb(file, stat, 'strip', meta.duration, readOrient(req.query));
  res.set('Cache-Control', 'private, max-age=86400');
  res.sendFile(strip, { dotfiles: 'allow' });
}));

// Stream an original clip (Range requests handled by sendFile).
app.get('/media', wrap(async (req, res) => {
  const file = resolveInside(settings.root, req.query.path);
  await fsp.access(file);
  res.sendFile(file, { acceptRanges: true, cacheControl: true, maxAge: '1h', dotfiles: 'allow' });
}));

// Stream a rendered file from the output folder.
app.get('/renders/:name', wrap(async (req, res) => {
  const file = resolveInside(settings.outputDir, req.params.name);
  await fsp.access(file);
  res.sendFile(file, { acceptRanges: true, cacheControl: true, maxAge: '1h', dotfiles: 'allow' });
}));

app.post('/api/render', wrap(async (req, res) => {
  const paths = Array.isArray(req.body?.paths) ? req.body.paths : [];
  if (!paths.length) throw new HttpError(400, 'No clips given');
  if (!(await ffmpegOk())) throw new HttpError(500, 'ffmpeg was not found on this machine');

  const created = [];
  for (const p of paths) {
    const file = resolveInside(settings.root, p);
    const { meta } = await probeCached(file);
    const existing = edits[file] || null;
    const edit = normalizeEdit({ ...existing, ...(req.body?.edits?.[p] || {}) }, meta);
    if (existing?.outputPath) edit.outputPath = existing.outputPath;
    if (existing?.renderedAt) edit.renderedAt = existing.renderedAt;
    if (existing?.modifiedAt) edit.modifiedAt = existing.modifiedAt;
    if (!existing || !sameCut(existing, edit)) edit.modifiedAt = Date.now();
    edits[file] = edit;
    saveEdits();

    let outPath = edit.outputPath;
    // Only reuse a previous output that lives in the current output folder.
    if (!outPath || path.dirname(outPath) !== settings.outputDir) {
      outPath = uniqueOutputPath(
        settings.outputDir,
        path.parse(file).name,
        settings.suffix || '-edit'
      );
    }

    const job = {
      id: crypto.randomUUID(),
      path: file,
      outPath,
      edit,
      hasAudio: meta.hasAudio,
      audioCodec: meta.audioCodec,
      keepAudio: edit.audio ?? settings.audio,
      duration: Math.max(0.05, edit.out - edit.in),
      progress: 0,
      status: 'queued',
      createdAt: Date.now(),
    };
    jobs.set(job.id, job);
    queue.push(job);
    created.push(publicJob(job));
  }
  pump();
  res.json({ jobs: created });
}));

app.get('/api/jobs', wrap(async (_req, res) => {
  // keep memory bounded
  if (jobs.size > 300) {
    const done = [...jobs.values()]
      .filter((j) => j.status === 'done' || j.status === 'error' || j.status === 'canceled')
      .sort((a, b) => (a.endedAt || 0) - (b.endedAt || 0));
    for (const j of done.slice(0, jobs.size - 300)) jobs.delete(j.id);
  }
  res.json({ jobs: [...jobs.values()].map(publicJob).sort((a, b) => b.createdAt - a.createdAt) });
}));

app.post('/api/jobs/:id/cancel', wrap(async (req, res) => {
  const job = jobs.get(req.params.id);
  if (!job) throw new HttpError(404, 'Job not found');
  if (job.status === 'queued') {
    const i = queue.indexOf(job);
    if (i >= 0) queue.splice(i, 1);
    job.status = 'canceled';
    job.endedAt = Date.now();
  } else if (job.status === 'running') {
    job.canceled = true;
    job._child?.kill('SIGTERM');
    setTimeout(() => job._child?.kill('SIGKILL'), 2000);
  }
  res.json(publicJob(job));
}));

// Open a folder in the desktop file manager.
app.post('/api/reveal', wrap(async (req, res) => {
  const target = req.body?.path;
  const dir = req.body?.dir || (target ? path.dirname(path.resolve(target)) : null);
  if (!dir) throw new HttpError(400, 'No folder given');
  const abs = path.resolve(dir);
  const allowed = [settings.outputDir, settings.root].some((b) => {
    const rel = path.relative(b, abs);
    return !rel.startsWith('..') && !path.isAbsolute(rel);
  });
  if (!allowed) throw new HttpError(403, 'Folder is outside the project');
  const child = spawn('xdg-open', [abs], { stdio: 'ignore', detached: true });
  child.on('error', () => {});
  child.unref();
  res.json({ ok: true, dir: abs });
}));

// Minimal folder browser for picking the source folder.
app.get('/api/browse', wrap(async (req, res) => {
  const start = typeof req.query.path === 'string' && req.query.path.trim()
    ? path.resolve(req.query.path.trim())
    : settings.root;
  let dir = start;
  try {
    const st = await fsp.stat(dir);
    if (!st.isDirectory()) dir = path.dirname(dir);
  } catch {
    dir = path.dirname(dir);
  }
  const entries = await fsp.readdir(dir, { withFileTypes: true }).catch(() => []);
  const dirs = entries
    .filter((e) => e.isDirectory() && !e.name.startsWith('.'))
    .map((e) => e.name)
    .sort((a, b) => a.localeCompare(b));
  const videoCount = entries.filter((e) => e.isFile() && isVideoFile(e.name)).length;
  res.json({
    path: dir,
    parent: path.dirname(dir) === dir ? null : path.dirname(dir),
    dirs,
    videoCount,
    home: process.env.HOME,
  });
}));

app.use((err, _req, res, _next) => {
  const status = err.status || 500;
  if (status >= 500) console.error(err);
  res.status(status).json({ error: err.message || 'Server error' });
});

app.listen(PORT, '127.0.0.1', async () => {
  await fsp.mkdir(settings.root, { recursive: true }).catch(() => {});
  await fsp.mkdir(settings.outputDir, { recursive: true }).catch(() => {});
  const ok = await ffmpegOk();
  console.log(`\n  Stock Clipper running at http://localhost:${PORT}`);
  console.log(`  source: ${settings.root}`);
  console.log(`  renders: ${settings.outputDir}`);
  if (!ok) console.log('  !! ffmpeg not found on PATH — renders will fail\n');
  else console.log('');
});
