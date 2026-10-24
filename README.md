# Stock Clipper

A local, single-user app for turning camera footage into individual stock-video
files: import clips → trim in/out → rotate/flip → preview → render one MP4 per clip.
Target output is Pond5-style stock footage, but the app only produces clean H.264
MP4s — nothing site-specific.

Native Qt 6 / QML: a single self-contained binary, local files only.

ffmpeg is **not** bundled; `ffmpeg` and `ffprobe` must be on `PATH`.

## Build

Needs Qt 6.5+ with Core, Gui, Quick, Multimedia and the **FFmpeg** multimedia
backend (`qt6-multimedia-ffmpeg`). On Arch:

```sh
sudo pacman -S qt6-base qt6-declarative qt6-multimedia-ffmpeg
```

Then:

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/stock-clipper
```

## Where things live

State is per-user, not in the checkout:

| what | where |
| --- | --- |
| settings, trims | `~/.local/share/stock-clipper/` (`settings.json`, `edits.json`) |
| probe cache, thumbnails, filmstrips | `~/.cache/stock-clipper/` |

On first launch the app imports `./data/settings.json` and `./data/edits.json` from
the directory it was started in, so existing trims and the input/output folders
carry over on a fresh install.

Set `STOCK_HOME=/some/dir` to use a throwaway state directory (`dir/data`,
`dir/.cache`) — handy for testing.

Source clips live in the settings' source folder (default `./input`, depth 3),
renders go to the output folder (default `./renders`). Both are editable in
Settings, which also has a folder browser.

## Keys

| key | action |
| --- | --- |
| `Space` | play / pause the trimmed range |
| `I` / `O` | set in / out at the playhead |
| `←` / `→` | step one frame (with `Shift`: one second) |
| `[` / `]` | nudge in / out by 0.1 s (with `Shift`: 1 s) |
| `Ctrl+Enter` | render the current clip |
| `Esc` | close a dialog |

Trim handles can be dragged on the filmstrip; clicking the strip seeks. `Loop`
(default on) replays the selection while previewing.

## Workflow

1. Clips in the source folder appear in the list (Rescan re-walks it; there is no
   filesystem watcher).
2. Select a clip, drag the in/out handles or type exact times, rotate/flip as
   needed. Edits are saved automatically (~220 ms after the cut changes) and survive
   restarts.
3. Tick `Keep audio` if the clip should keep its sound. Silent is the default.
4. `Render` writes one MP4. `Render edited` does every trimmed clip, `Render all`
   does the whole library; jobs run one at a time in the bottom drawer and can be
   cancelled or retried.

A rendered clip whose cut changes again shows `re-render` — the file on disk is
stale until it is rendered again.

## Rendering

Only rotation and flips are applied: output keeps the clip's native resolution and
frame rate, H.264 High / `yuv420p` with `+faststart`. No scaling, no fps
conversion, no letterboxing, no colour management.

Audio is stripped unless "Keep audio" is ticked; a source codec that MP4 players
accept is stream-copied, anything else (e.g. Sony's `pcm_s16be`) is re-encoded to
AAC 192k.

## Development

```sh
cmake --build build
```

A headless driver for verification is built alongside the app:

```sh
cmake --build build --target stock-clipper-selftest
STOCK_HOME=/tmp/sc-test QT_QPA_PLATFORM=offscreen ./build/stock-clipper-selftest
```

It scans the library, applies `in=0.5 out=2.0 rotate=90` to a clip, renders it and
prints the output path — then `ffprobe` the result to check duration, rotated
dimensions, fps, pixel format and `moov`-before-`mdat`.

The app itself takes a few development env vars:

| variable | effect |
| --- | --- |
| `SC_DEBUG=1` | frame/loop/player-state tracing on stderr |
| `SC_SHOT=/tmp/out.png` | select a clip, seek, grab the window, exit |
| `SC_SHOT_DELAY=ms` | delay before the grab (default 5000) |
| `SC_CLIP=C0016` | clip name filter for `SC_SHOT` |
| `SC_OPEN=settings\|browse` | open a dialog before grabbing |
| `SC_RENDER=1` | render the selected clip before grabbing |
| `SC_AUTOPLAY=1` | start playback before grabbing |

QML warnings are swallowed by Qt's default handler; `src/main.cpp` installs one
that prints to stderr. Keep it — otherwise load errors are silent.

## Known limitations

- Library scan is depth-3 and polled, not watched: use Rescan after adding files.
- Edits are keyed by absolute path: moving or renaming footage orphans its
  trim/rotation (and its render bookkeeping).
- One render at a time; no hardware encoder path (no `h264_nvenc`/`vaapi`).
- Preview audio follows the source and is never muted, even when the render strips
  it (the default). A kept audio track is only audible from the rendered file.
- No accounts, no telemetry, no upload.
