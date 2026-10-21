import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

const pExecFile = promisify(execFile);

export const VIDEO_EXTS = new Set([
  '.mp4', '.m4v', '.mov', '.mkv', '.avi', '.mts', '.m2ts', '.m2t',
  '.webm', '.mxf', '.mpeg', '.mpg', '.wmv', '.3gp', '.insv', '.lrv',
]);

export function isVideoFile(name) {
  return VIDEO_EXTS.has(name.slice(name.lastIndexOf('.')).toLowerCase());
}

function normRotation(r) {
  const n = Math.round(Number(r) / 90) * 90;
  return ((n % 360) + 360) % 360;
}

function parseRate(str) {
  if (!str || typeof str !== 'string') return null;
  const [a, b] = str.split('/').map(Number);
  if (!Number.isFinite(a)) return null;
  if (!b || !Number.isFinite(b)) return a || null;
  const v = a / b;
  return Number.isFinite(v) && v > 0 ? Math.round(v * 1000) / 1000 : null;
}

/**
 * Read the streams of a media file and return the bits the UI needs.
 * Width/height are returned both as coded dimensions and as display
 * dimensions (i.e. after any rotation metadata the player will apply).
 */
export async function probe(file) {
  const { stdout } = await pExecFile(
    'ffprobe',
    ['-v', 'error', '-print_format', 'json', '-show_format', '-show_streams', file],
    { maxBuffer: 32 * 1024 * 1024 }
  );
  const info = JSON.parse(stdout);
  const streams = info.streams || [];
  const v = streams.find((s) => s.codec_type === 'video' && s.disposition?.attached_pic !== 1)
    || streams.find((s) => s.codec_type === 'video');
  const a = streams.find((s) => s.codec_type === 'audio');

  if (!v) throw new Error('No video stream found');

  const duration = Number(
    info.format?.duration ?? v.duration ?? a?.duration ?? 0
  ) || 0;

  const rotationSrc =
    v.side_data_list?.find((d) => d.rotation !== undefined)?.rotation ??
    v.tags?.rotate ??
    v.tags?.Rotation ??
    0;
  const rotation = normRotation(rotationSrc);

  const width = Number(v.width) || 0;
  const height = Number(v.height) || 0;
  const swapped = rotation === 90 || rotation === 270;

  return {
    duration: Math.round(duration * 1000) / 1000,
    width,
    height,
    displayWidth: swapped ? height : width,
    displayHeight: swapped ? width : height,
    rotation,
    fps: parseRate(v.avg_frame_rate) || parseRate(v.r_frame_rate),
    codec: v.codec_name || null,
    pixFmt: v.pix_fmt || null,
    hasAudio: Boolean(a),
    audioCodec: a?.codec_name || null,
    sizeBytes: Number(info.format?.size) || 0,
  };
}

/** Build the ffmpeg -vf chain for the requested orientation. */
export function buildVideoFilter({ rotate = 0, flipH = false, flipV = false } = {}) {
  const r = normRotation(rotate);
  const f = [];
  if (r === 90) f.push('transpose=1');        // 90 clockwise
  else if (r === 180) f.push('hflip', 'vflip');
  else if (r === 270) f.push('transpose=2');  // 90 counter-clockwise
  if (flipH) f.push('hflip');
  if (flipV) f.push('vflip');
  // h264 + yuv420p wants even dimensions
  f.push('scale=trunc(iw/2)*2:trunc(ih/2)*2');
  f.push('setsar=1');
  return f.join(',');
}

/**
 * Audio codecs that can be stream-copied into MP4 and still play everywhere.
 * Anything else (PCM from Sony XAVC, Opus, FLAC, …) gets re-encoded: muxing it
 * unchanged produces files that players and stock sites reject.
 */
const MP4_AUDIO_COPYABLE = new Set(['aac', 'mp3', 'ac3', 'eac3', 'alac']);

/**
 * What to do with the source audio: 'none' (strip), 'copy' (remux the original
 * packets) or 'aac' (re-encode because the codec can't live in MP4).
 * Stock footage is silent by default; keeping audio is opt-in.
 */
export function audioPlan({ hasAudio = false, audioCodec = null, keepAudio = false } = {}) {
  if (!hasAudio || !keepAudio) return 'none';
  return MP4_AUDIO_COPYABLE.has(String(audioCodec || '').toLowerCase()) ? 'copy' : 'aac';
}

/**
 * Full ffmpeg argument list for one clip render.
 * Uses input-side -ss (accurate seek while re-encoding) plus -t for the length.
 */
export function renderArgs({
  input, output, inPoint = 0, duration, rotate = 0, flipH = false, flipV = false,
  hasAudio = false, audioCodec = null, keepAudio = false, crf = 18, preset = 'medium',
}) {
  const audio = audioPlan({ hasAudio, audioCodec, keepAudio });
  const args = ['-hide_banner', '-loglevel', 'error', '-nostdin', '-y'];
  if (inPoint > 0) args.push('-ss', inPoint.toFixed(3));
  args.push('-i', input);
  args.push('-t', Math.max(0.05, duration).toFixed(3));
  args.push('-map', '0:v:0');
  if (audio !== 'none') args.push('-map', '0:a:0');
  args.push('-vf', buildVideoFilter({ rotate, flipH, flipV }));
  args.push('-c:v', 'libx264', '-preset', preset, '-crf', String(crf), '-pix_fmt', 'yuv420p');
  args.push('-profile:v', 'high');
  if (audio === 'copy') args.push('-c:a', 'copy', '-shortest');
  else if (audio === 'aac') args.push('-c:a', 'aac', '-b:a', '192k', '-ar', '48000');
  else args.push('-an');
  args.push('-map_metadata', '-1', '-movflags', '+faststart');
  args.push('-progress', 'pipe:1', '-nostats');
  args.push(output);
  return args;
}
