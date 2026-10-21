# Stock Clipper

A small local web app for turning camera clips into individual stock-footage files:
drop clips in a folder, trim each one, rotate/flip when the camera orientation is
wrong, preview the result, and render them out as H.264 MP4s with the local `ffmpeg`.

Built for the Pond5 workflow, but it just produces clean MP4s — nothing site-specific.

## Requirements

- `ffmpeg` and `ffprobe` on your `PATH` (needs libx264; a normal distro build is fine)
- Node.js 18+ (tested on 26)

## Run

```sh
npm install
npm start
```

Then open <http://localhost:4321>.

Put your clips in `input/` (or point the source folder anywhere via **Settings**),
and renders land in `renders/` by default.

## Workflow

1. **Drop clips** into the source folder. Hit **Rescan** if the app was already open.
2. **Pick a clip** in the left list. The timeline shows a filmstrip of the whole clip.
3. **Trim**: drag the amber handles, type exact in/out times, or hit **Set in** /
   **Set out** at the playhead. Playback loops the selection so you can check the result.
4. **Rotate**: 0°/90°/180°/270° buttons (plus horizontal/vertical flip). Preview,
   filmstrip and thumbnail all follow the rotation, so vertical footage shot sideways
   is shown upright. The quick `⟳` button in the list rotates a clip 90° at a time.
5. **Audio** is stripped by default (stock footage rarely needs it). Tick **Keep audio**
   in the editor for one clip, or set it as the default in **Settings → Audio**.
6. **Render clip**, or **Render all** / **Render edited** for batches. Renders run
   one at a time in the queue at the bottom; you can keep editing while they run.
7. Rendered files get a **rendered** badge. If you change the trim afterwards the
   badge turns into **re-render** so you don't upload a stale file.

## Keyboard

| Key | Action |
| --- | --- |
| `Space` | play / pause |
| `I` / `O` | set in / out point at playhead |
| `←` / `→` | nudge one frame (`Shift` = one second) |
| `[` / `]` | nudge in / out point (`Shift` = one second) |
| `R` | rotate 90° |
| `Ctrl+Enter` | render current clip |
| `Esc` | close dialogs |

## What the render does

- H.264 (`libx264`), `yuv420p`, `+faststart`, metadata stripped
- Native resolution and frame rate preserved; rotation applied to the pixels
- CRF 18 / `medium` preset by default (adjustable in Settings)
- Audio: **off by default**. When kept, the source track is stream-copied (AAC, MP3,
  AC3/E-AC3, ALAC); codecs MP4 players reject (PCM from Sony XAVC, Opus, FLAC) are
  re-encoded to AAC 192 kbps so the file still plays.
- Trim is frame-accurate: input-side seek while re-encoding, then the exact duration
- Output filename: `<original name><suffix>.mp4` (suffix defaults to `-edit`)

Re-rendering a clip overwrites its previous output; other existing files are never
clobbered (a `-2` suffix is added instead).

## Notes

- Settings, per-clip trim/rotation state and the probe cache live in `data/` and
  `.cache/`; deleting those is safe (you lose the saved edits, not the footage).
- The short-clip warning threshold in Settings is only a hint — set it to whatever
  your marketplace requires.
- Everything runs locally; the server binds to `127.0.0.1` only.
