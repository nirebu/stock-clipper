# AGENTS.md — Stock Clipper

Local, single-user app for turning camera footage into individual stock-video
files: import clips → trim in/out → rotate/flip → preview → render MP4 per clip.
Target output is Pond5, but the app only produces clean H.264 MP4s, nothing
site-specific.

Native **Qt 6 / QML** app: one binary that talks to `ffmpeg`/`ffprobe` and nothing
else. User-facing usage lives in `README.md`; this file is the context an agent
needs to change the code without breaking the workflow.

## Commands

```sh
cmake -S . -B build -G Ninja && cmake --build build   # build
./build/stock-clipper                                 # run

# headless verification (renders one clip, prints the output path)
cmake --build build --target stock-clipper-selftest
STOCK_HOME=/tmp/sc-test QT_QPA_PLATFORM=offscreen ./build/stock-clipper-selftest

# layout screenshot
STOCK_HOME=/tmp/sc-test SC_SHOT=/tmp/shot.png SC_OPEN=settings ./build/stock-clipper
```

ffmpeg and ffprobe must be on `PATH` (not bundled). Qt 6.5+ with Core, Gui, Quick,
Multimedia and the **FFmpeg** multimedia backend.

## Architecture

```
CMakeLists.txt      app + selftest targets (qt_add_qml_module URI StockClipper)
src/core.*          Edit/Meta/Settings/Paths, resolveInside, normalizeEdit, isEdited,
                    sameCut, fmtTime/parseTime, cacheKey, JSON store
src/media.*         ffprobe arg builder, parseProbe, buildVideoFilter, audioPlan,
                    renderArgs — the single source of truth for the ffmpeg pipeline
src/library.*       Prober, Thumbnailer, ClipModel + ClipFilterModel, listVideos (depth 3)
src/render.*        RenderQueue (itself the QAbstractListModel of jobs), progress parsing
src/backend.*       QML facade: state, selection, edits, playback/seek/trim, render
                    enqueue, settings/browse, priming, trim loop
src/main.cpp        app wiring, QML message handler, SC_* test hooks
qml/                Main, Theme (singleton), ClipRow, Stage, Timeline, QueuePanel,
                    Sheet + dialogs, Btn, TickBox, Toast — plain Items, no
                    QtQuick.Controls on purpose
tests/selftest.cpp  headless driver
```

State is per-user, not in the checkout:

| what | where |
| --- | --- |
| settings, edits | `QStandardPaths` AppData `stock-clipper/` |
| probe cache, thumbnails, filmstrips | `QStandardPaths` Cache `stock-clipper/` |

`STOCK_HOME` overrides both (`dir/data`, `dir/.cache`). On first run
`Paths::discover()` copies `./data/settings.json` and `./data/edits.json` from the
cwd once, so a fresh state dir inherits existing trims and folder settings. The
files are plain JSON; unknown fields are ignored on load.

### Signal flow

1. `Backend::rescan()` walks the source folder (depth 3, skips dot entries), reuses
   the probe cache when `(mtime, size)` match, and otherwise queues a `Prober`
   request (4 concurrent `ffprobe` processes). Clips appear immediately as
   `loading`, then fill in as probes finish. There is **no filesystem watcher**;
   Rescan re-walks.
2. `ClipModel` rows are decorated with the stored edit; edit changes are applied to
   the model in place and persisted by a 220 ms single-shot `QTimer` (~"debounce").
3. Selecting a clip sets `QMediaPlayer::setSource` on the original file and
   **primes** a frame (see gotchas). Previews show the original; rotation and flips
   are QML transforms. There is no proxy/preview render.
4. Rendering enqueues jobs into `RenderQueue`, which runs exactly one `ffmpeg`
   `QProcess` at a time and parses `-progress pipe:1`; the drawer is the queue's
   model. Jobs are in-memory; edits and rendered files are the durable state.

### Data model

`Edit` = `{ in, out, rotate, flipH, flipV, audio }` in seconds/degrees/booleans,
plus app-owned bookkeeping `{ outputPath, renderedAt, modifiedAt }`.

- `in`/`out` are **source-clip times**, not times inside the trimmed clip. The whole
  timeline UI is laid out against source duration. Do not "simplify" this.
- `out` is effectively exclusive; trimmed length is `out - in`; minimum gap 0.05 s.
- `rotate` ∈ {0, 90, 180, 270} clockwise; flips apply *after* rotation (output space).
- `audio` is tri-state: `null` = follow `settings.audio` (default `false`),
  `true`/`false` = explicit per-clip choice. Resolving it is
  `edit.audio.value_or(settings.audio)` (`Backend::keepAudio`); never coerce `null`
  to `false` in `normalizeEdit`, or changing the default stops applying to untouched
  clips.
- `modifiedAt` is bumped only when the cut actually changes (`sameCut`, which includes
  `audio`), so `renderedAt < modifiedAt` means "render is stale" → UI shows
  `re-render`. Audio is deliberately *not* part of `isEdited` (it is not a trim), so
  audio-only changes never light up the `trimmed` badge or `Render edited`.
- Bookkeeping is never accepted from the UI: `normalizeEdit` drops it and the app
  re-attaches it from the stored edit. Letting a request win would roll back a
  finished render's timestamp and make the stale badge lie.

## Rendering (the part to be careful with)

`buildVideoFilter()` in `src/media.cpp` is used by **renders, thumbnails and
filmstrips** — orientation can never drift between preview and output. Change it once.

Order: rotation → flips → `scale=trunc(iw/2)*2:trunc(ih/2)*2` → `setsar=1`.

`renderArgs()` produces:

- `-ss <in>` before `-i` (fast, but still frame-accurate while re-encoding) and
  `-t <out-in>` after `-i`
- `-map 0:v:0`, plus `-map 0:a:0` only when audio is kept. Sony/GoPro files carry
  extra timed-metadata streams; only the first video/audio streams are mapped on purpose.
- `libx264 -preset <s> -crf <n> -pix_fmt yuv420p -profile:v high`
- audio: `audioPlan()` decides `none` / `copy` / `aac`. `-c:a copy -shortest` for
  MP4-safe codecs (AAC, MP3, AC3/E-AC3, ALAC — `-shortest` stops the copied track
  running ~20 ms past the video); AAC 192k/48k otherwise; `-an` when stripped or the
  source is silent. Silent is the default.
- `-map_metadata -1`, `-movflags +faststart`
- `-progress pipe:1 -nostats` for progress

Deliberate non-features: no scaling, no fps conversion, no letterboxing, no colour
management. Output keeps native resolution and frame rate, only rotated. That is
what stock sites expect, and it keeps the render lossless-ish apart from encoding.

`uniqueOutputPath()` reuses a previous output only when that path is in the current
output folder and belongs to a stored edit; otherwise it appends `-2`, `-3`… so
unrelated files are never clobbered and a re-render of our own clip can overwrite it.

## Gotchas found the hard way

**ffmpeg / probe**

- ffmpeg applies a file's display-matrix rotation automatically. `parseProbe()`
  therefore returns both coded `width/height` and `displayWidth/displayHeight`
  (swapped for ±90° metadata rotation). The UI sizes the stage from display dims and
  does *not* re-apply file rotation; manual `rotate` is layered on top.
  Double-applying is the classic bug here.
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

**Qt / QML**

- **`VideoOutput` has no `source` in Qt 6.12+.** It exposes a read-only `videoSink`;
  the app hands that sink to `QMediaPlayer::setVideoSink()` from QML
  (`App.attachVideoSink(vo.videoSink)`).
- **A paused `QMediaPlayer` never emits a frame** — not after `setSource`, not after
  `setPosition`. Selecting a clip or seeking while paused must *prime*: play muted
  for exactly one frame, then pause (`Backend::primeFrame`). Without it the stage
  stays black. `m_wantPlay` exists so priming never cancels a real play request.
- **QML colours are `#AARRGGBB`**, not CSS `#RRGGBBAA`. `"#ffffff14"` is opaque
  yellow, not 8 % white. All translucency in `qml/` is written `#AARRGGBB`.
- **`font.pixelSize` is an int**; fractional values fail at load.
- Stage sizing: the video item is sized by the **unrotated** frame dimensions and the
  rotation is applied inside a flip wrapper (flips outside, rotation inside, matching
  `transpose` → `hflip/vflip`). Sizing it by the rotated dimensions makes
  `VideoOutput.Stretch` distort the frame.
- The trim loop is a 16 ms `QTimer` started from `playbackStateChanged`, with a
  ~90 ms `m_seekGuard` after a loop seek, and it stops at `out - 4 ms`.
- QML warnings are swallowed by the default handler in this environment; `main.cpp`
  installs one that prints to stderr. Keep it — otherwise load errors are silent.
- Thumbnails and filmstrips are generated with the clip's orientation and cached by
  `(path, mtime, size, orientation)`; `Thumbnailer` dedupes in-flight work and
  `Backend::loadStrip()` tracks `m_stripWant` so a slow response for an older
  orientation cannot overwrite a newer one. Do not key the cache without orientation.
- Folder selection uses `QtQuick.Dialogs`' `FolderDialog` (Settings → Browse…), which
  asks the platform theme for its native picker (GTK3/portal here). There is no
  hand-rolled folder navigator any more; don't add one back. Settings store plain
  paths, the dialog speaks `file://` URLs, hence `Backend::pathToUrl/pathFromUrl`.
- Clean up test renders: the selftest writes into the configured output folder
  (default `./renders`), not into the state dir.

**State**

- `resolveInside(root, path)` still guards every UI-supplied path (library, output,
  browse) even without a network; keep it.
- Edits are keyed by absolute path, so moving footage orphaned its trim.
- Never write `data/` from code except the one-time import; runtime state belongs in
  `QStandardPaths` (or `STOCK_HOME`).

## Verifying a change

Minimum bar for anything touching rendering:

```sh
STOCK_HOME=/tmp/sc-test QT_QPA_PLATFORM=offscreen ./build/stock-clipper-selftest
# then ffprobe the printed output path and assert
ffprobe -v error -print_format json -show_format -show_streams renders/<name>.mp4
# expect: duration == out-in, rotated dims (e.g. 1080x1920), 50/1 fps, aac when kept,
#         yuv420p, High profile, and 'moov' before 'mdat' (walk the boxes)
```

Test fixtures without touching real footage (delete afterwards):

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

- The UI must stay usable during renders; never block the event loop that starts
  a job. Rendering is `QProcess`, never `waitForFinished`.
- Keep the preview instantaneous. Anything that requires a render to preview is a
  regression (QMediaPlayer + QML transform is the whole trick).
- No accounts, no network calls, no telemetry, no upload. Local only.
- Editing the `edit` shape means updating: `normalizeEdit`, `Edit::sameCut`,
  `Edit::toJson/fromJson`, `Backend::applyEditTo`, `Backend::decorate` and
  `isEdited` — plus existing `edits.json` entries keep working via normalization.
- Jobs are intentionally ephemeral (in-memory). Edits and rendered files are the
  durable state.

## Known limitations

- Library scan is depth-3 and filesystem-polled, not watched.
- Edits are keyed by absolute path: moving or renaming footage in the source folder
  orphans its trim/rotation (the render bookkeeping goes with it).
- One render at a time; no hardware encoder path (no `h264_nvenc`/`vaapi`).
- Preview audio follows the source and is never muted, even when the render strips it
  (default). The output track — if kept — is only audible from the rendered file.
- Rendered-file playback is served from the flat output folder only.
- No batch rotation UI beyond the per-row `⟳` button.
- No scripting surface beyond `selftest`: automation goes through that driver.
