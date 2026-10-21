# AGENTS.md — Stock Clipper

Local, single-user web app for turning camera footage into individual stock-video
files: import clips → trim in/out → rotate/flip → preview → render MP4 per clip.
Target output is Pond5, but the app only produces clean H.264 MP4s, nothing site-specific.

User-facing usage lives in `README.md`. This file is the context an agent needs to
change the code without breaking the workflow.

## Commands

```sh
npm install          # express only dependency
npm start            # http://localhost:4321 (bound to 127.0.0.1)
npm run dev          # node --watch
PORT=4400 npm start  # alternate port
node --check server.js && node --check lib/media.js && node --check public/app.js
```

No build step, no bundler, no framework. `public/` is served as-is. Keep it that way.

Stop a running instance by PID from `ss`, **not** `pkill -f "node server.js"` — the
pattern matches the shell command running `pkill` too and kills your own shell:

```sh
kill $(ss -ltnp | grep 4321 | grep -oP 'pid=\K[0-9]+')
```

## Architecture

```
server.js          Express 5 app: settings/edits stores, library scan, previews, render queue, routes
lib/media.js       ffprobe wrapper + the ffmpeg filter/argument builders (single source of truth)
public/index.html  static shell, IDs are referenced directly from app.js
public/app.js      all UI state/interaction (~1k lines, no framework, plain DOM)
public/style.css   dark theme; CSS vars at the top
input/             source clips (created at boot, gitignored)
renders/           render output (created at boot, gitignored)
data/settings.json persisted settings
data/edits.json    { absolutePath: edit } — trims/rotation survive restarts
.cache/probe.json  ffprobe results, keyed by absolute path
.cache/thumb-*.jpg / .cache/strip-*.jpg   generated previews, content-addressed
```

### Request flow

1. `GET /api/library` walks the source folder (depth 3), ffprobes every video with at
   most 4 in flight (`mapLimit`), merges the stored edit, and returns the list.
   It re-walks on **every** request — cheap because probe results are cached by
   `(path, mtime, size)`. That is what makes the Rescan button (and dropped files)
   show up; there is no filesystem watcher.
2. The browser previews the **original** file through `GET /media` (Range requests
   handled by `res.sendFile`) and applies rotation with a CSS transform. There is no
   proxy/preview render.
3. `PUT /api/edit` persists the cut; debounced 220 ms from the client.
4. `POST /api/render` enqueues one job per path. The queue is in-memory and strictly
   serial (x264 already uses all cores). The client polls `GET /api/jobs` every 650 ms
   while anything is active.

### Data model

`edit` = `{ in, out, rotate, flipH, flipV, audio }` in seconds/degrees/booleans,
plus server-owned bookkeeping `{ outputPath, renderedAt, modifiedAt }`.

- `in`/`out` are **source-clip times**, not times inside the trimmed clip. The whole
  timeline UI is laid out against source duration. Do not "simplify" this.
- `out` is effectively exclusive; trimmed length is `out - in`; minimum gap 0.05 s.
- `rotate` ∈ {0, 90, 180, 270} clockwise; flips apply *after* rotation (output space).
- `audio` is tri-state: `null` = follow `settings.audio` (the default, `false`),
  `true`/`false` = explicit per-clip choice. Resolving it lives in
  `keepAudio(edit, settings)` on both sides; never coerce `null` to `false` in
  `normalizeEdit` or changing the default stops applying to untouched clips.
- `modifiedAt` is bumped only when the cut actually changes (`sameCut`, which includes
  `audio`), so `renderedAt < modifiedAt` means "render is stale" → UI shows
  `re-render`. Audio is deliberately *not* part of `isEdited` (it is not a trim), so
  audio-only changes never light up the `trimmed` badge or `Render edited`.

## Rendering (the part to be careful with)

`buildVideoFilter()` in `lib/media.js` is used by **renders, thumbnails and
filmstrips** — orientation can never drift between preview and output. Change it once.

Order: rotation → flips → `scale=trunc(iw/2)*2:trunc(ih/2)*2` → `setsar=1`.

`renderArgs()` produces:

- `-ss <in>` before `-i` (fast, but still frame-accurate while re-encoding) and
  `-t <out-in>` after `-i`
- `-map 0:v:0`, plus `-map 0:a:0` only when audio is kept. Sony/GoPro files carry
  extra timed-metadata streams; only the first video/audio streams are mapped on purpose.
- `libx264 -preset <s> -crf <n> -pix_fmt yuv420p -profile:v high`
- audio: `audioPlan()` in `lib/media.js` decides `none` / `copy` / `aac`.
  `-c:a copy -shortest` for MP4-safe codecs (AAC, MP3, AC3/E-AC3, ALAC — `-shortest`
  stops the copied track running ~20 ms past the video); AAC 192k/48k otherwise;
  `-an` when stripped or the source is silent. Silent is the default.
- `-map_metadata -1`, `-movflags +faststart`
- `-progress pipe:1 -nostats` for progress

Deliberate non-features: no scaling, no fps conversion, no letterboxing, no color
management. Output keeps native resolution and frame rate, only rotated. That is what
stock sites expect, and it keeps the render lossless-ish apart from encoding.

## Gotchas found the hard way

**ffmpeg / probe**

- ffmpeg applies a file's display-matrix rotation automatically. `probe()` therefore
  returns both coded `width/height` and `displayWidth/displayHeight` (swapped for
  ±90° metadata rotation). The UI sizes the stage from display dims and does *not*
  re-apply file rotation; manual `rotate` is layered on top. Double-applying is the
  classic bug here.
- ffmpeg 9 ignores `-metadata:s:v rotate=90` when writing; it no longer emits a
  display matrix. To fabricate a "shot vertical, flagged" fixture use the input
  option: `ffmpeg -display_rotation 90 -i in.mp4 -c copy out.mp4`.
- `-progress pipe:1` reports `out_time_us` / `out_time_ms`; both are **microseconds**
  despite the name. Divide by 1e6.
- `yuv420p` + H.264 requires even dimensions. Odd source sizes (and some rotations)
  produce odd dims, hence the `trunc(...)` scale. Removing it reintroduces encoder
  failures on odd-sized footage.
- Do not set `-level`. It was `4.2` initially and is wrong for 4K input; x264 picks
  a valid level itself.
- `tile=10x1` emits nothing until it has 10 frames. The filmstrip generator derives
  `fps = 10 / duration` (capped at 30) precisely so a full tile always arrives.
- PCM audio in Sony XAVC MP4s (`pcm_s16be`) must not be stream-copied into MP4 —
  ffmpeg will mux it, but players and stock sites reject the result. `audioPlan()`
  only copies codecs MP4 players accept and falls back to AAC for everything else.
  Copying also needs `-shortest`: `-t` cuts the video on a frame boundary, and a
  copied AAC packet can otherwise leave the audio ~20 ms longer than the video.

**Express / Node**

- `res.sendFile` rejects dotfile paths by default (404 "Not Found" even though the
  file exists). Cache previews live in `.cache/`, so those routes need
  `{ dotfiles: 'allow' }`. Same flag on `/media` and `/renders` in case a source or
  output folder has a dot segment in its path.
- Express 5 + ESM: `require()` does not exist. `express@5.x` is installed.
- Never trust client-supplied `outputPath`/`renderedAt`/`modifiedAt`.
  `normalizeEdit()` strips them; endpoints re-attach them from the stored edit. If
  you let the request body win, a debounced save can roll back a finished render's
  timestamp and the stale badge lies.
- Re-rendering overwrites a previous output **only** when that path is in the current
  output folder and present in `edits`; otherwise `uniqueOutputPath()` appends `-2`,
  `-3`… so unrelated files are never clobbered.
- All filesystem paths from the client go through `resolveInside()`; render output
  must stay inside `settings.outputDir`, media inside `settings.root`.

**Browser / UI**

- `[hidden]` does not work against rules that set `display:`. A global
  `[hidden] { display: none !important; }` is at the top of `style.css`; do not
  remove it or the empty-state, play overlay and queue drawer all reappear.
- Flip + rotate order matters. Use `transform: scale(sx, sy) rotate(deg)`
  (rightmost transform applies first) so flips happen in output space, matching the
  ffmpeg filter order `transpose` → `hflip/vflip`.
- The trim loop is enforced with `requestAnimationFrame`, not `timeupdate`
  (which only fires ~4–10×/s and overshoots the out point). `state.rafActive` guards
  against stacking rAF loops on repeated `play` events.
- Filmstrip and thumbnails are generated **with** the clip's orientation, keyed by
  `(path, mtime, size, orientation)`. If you drop orientation from the cache key,
  rotating a clip keeps showing sideways frames. `loadFilmstrip()` also uses a
  monotonically increasing `stripToken` so a slow response for an older orientation
  cannot overwrite a newer one.
- Cache-bust with `mtimeMs`, never `Date.now()` — the latter defeats the whole
  preview cache.
- Headless Chromium in this environment cannot decode H.264, so screenshots show a
  black stage. That is not a bug: verify layout from the video element's computed
  box. A VP9 `.webm` fixture does decode if you need a visible frame.

## Verifying a change

Minimum bar for anything touching rendering:

```sh
curl -s localhost:4321/api/health
# render one clip with a trim + rotation, then assert
ffprobe -v error -print_format json -show_format -show_streams renders/<name>.mp4
# expect: duration == out-in, rotated dims (e.g. 1080x1920), 50/1 fps, aac, yuv420p,
#         and 'moov' before 'mdat' (verify with a box walk)
```

Test fixtures without touching real footage (writes are ~MBs, delete afterwards):

```sh
ffmpeg -f lavfi -i "testsrc2=size=1920x1080:rate=30:duration=8" -f lavfi -i "sine=duration=8" \
  -c:v libx264 -pix_fmt yuv420p -c:a aac short.mp4
ffmpeg -f lavfi -i "testsrc2=size=1080x1920:rate=30:duration=6" -c:v libx264 -pix_fmt yuv420p vertical.mp4
ffmpeg -display_rotation 90 -i short.mp4 -c copy flagged.mp4   # metadata rotation
```

`input/` currently holds real Sony a6300 material (1080p50 XAVC, PCM audio, no
rotation flags — vertical shots are physically sideways pixels). Do not delete or
re-encode anything in `input/`; use fixtures for experiments and clean them up.

## Conventions / non-goals

- The UI must stay usable during renders; never block the request that starts a job.
- Keep the preview instantaneous. Anything that requires a render to preview is a
  regression (CSS transform + native video is the whole trick).
- No accounts, no network calls, no telemetry, no upload. Local only.
- Editing `edit` shape means updating: `normalizeEdit`, `CUT_KEYS`/`sameCut`,
  `defaultEdit`, the client's `normalizeEdit`, and `isEdited` — plus existing
  `data/edits.json` entries keep working via normalization.
- Jobs are intentionally ephemeral (in-memory). Edits and rendered files are the
  durable state.

## Known limitations

- Library scan is depth-3 and filesystem-polled, not watched.
- Edits are keyed by absolute path: moving or renaming footage in `input/` orphans its
  trim/rotation (the render bookkeeping goes with it).
- One render at a time; no hardware encoder path (no `h264_nvenc`/`vaapi`).
- Preview audio follows the source and is never muted, even when the render strips it
  (default). The output track — if kept — is only audible from the queue's `play` link.
- Rendered-file playback is served from the flat output folder only.
- No batch rotation UI beyond the per-row `⟳` button.
