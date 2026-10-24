# Stock Clipper (desktop)

Native Qt 6 / QML build of the stock clipper. Same workflow as the web app —
import clips, trim in/out, rotate/flip, preview, render one MP4 per clip — but as a
single local binary with no server and no browser.

ffmpeg is **not** bundled; `ffmpeg` and `ffprobe` must be on `PATH`.

## Build

```sh
cmake -S desktop -B desktop/build -G Ninja
cmake --build desktop/build
./desktop/build/stock-clipper
```

Needs Qt 6.5+ with Core, Gui, Quick, Multimedia and the **FFmpeg** multimedia
backend (`qt6-multimedia-ffmpeg`). On Arch:

```sh
sudo pacman -S qt6-base qt6-declarative qt6-multimedia-ffmpeg
```

## Where things live

State is per-user, not in the checkout:

| what | where |
| --- | --- |
| settings, trims | `~/.local/share/stock-clipper/` (`settings.json`, `edits.json`) |
| probe cache, thumbnails, filmstrips | `~/.cache/stock-clipper/` |

On first launch the app imports `./data/settings.json` and `./data/edits.json` from
the directory it was started in, so existing trims and the input/output folders
carry over from the web version. The JSON formats are identical, so both apps can
read the same files if you point them at them.

Set `STOCK_HOME=/some/dir` to use a throwaway state directory (`dir/data`,
`dir/.cache`) — handy for testing.

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

## Rendering

Only rotation and flips are applied: output keeps the clip's native resolution and
frame rate, H.264 High / `yuv420p` with `+faststart`, no scaling, no fps conversion,
no letterboxing. Audio is stripped unless "Keep audio" is ticked; a source codec
that MP4 players accept is stream-copied, anything else (e.g. Sony's `pcm_s16be`)
is re-encoded to AAC 192k. Renders are serial; the queue lives in the bottom drawer.

## Development

```sh
node --check server.js            # the web app still lives at the repo root
cmake --build desktop/build
```

`desktop/src/media.cpp` is the single source of truth for the ffmpeg filter and
argument builders (it is a port of `lib/media.js` — change them together). Both the
preview thumbnails, the filmstrips and the renders go through
`buildVideoFilter()`, so orientation cannot drift between them.

A headless driver for verification lives in `tests/selftest.cpp`:

```sh
cmake --build desktop/build --target stock-clipper-selftest
STOCK_HOME=/tmp/sc-test QT_QPA_PLATFORM=offscreen \
  ./desktop/build/stock-clipper-selftest
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
