---
name: stock-clipper-ui
description: Open the real Stock Clipper window on a clean, empty Hyprland workspace, sized to fill it, take screenshots of the actual UI, send keyboard input, and restore the user's workspace afterwards. Use when asked to run, open, see, check, verify, screenshot, or interact with the app's real interface or layout — not for headless render tests.
---

# Stock Clipper UI session

Drives the actual `stock-clipper` binary in the running Hyprland session. It
temporarily switches to a fresh workspace, launches the app isolated from the
user's saved settings, waits for the window, and tiles it to the full
workspace. Everything is undone by `close`.

Use this instead of guessing `hyprctl` invocations. On Hyprland 0.56 (Omarchy)
dispatchers are Lua (`hyprctl eval 'hl.dispatch(...)'`); plain
`hyprctl dispatch workspace 2` is a syntax error.

## Requirements

- A live Hyprland session (`HYPRLAND_INSTANCE_SIGNATURE` is set), `hyprctl`, `jq`.
- `grim` (screenshots) and `wtype` (keyboard), both present on Omarchy.
- A built binary: `cmake --build build` → `build/stock-clipper`.
- Optional: `ydotool` for pointer clicks. It is **not installed here**, so
  clicks are unavailable unless the user installs it.

## Commands

Run from the repo root (the script finds the repo itself):

```sh
S=.agents/skills/stock-clipper-ui/scripts/session.sh

$S open          # new workspace + launch + focus + first screenshot
$S shot          # another screenshot (default path) 
$S shot /tmp/x.png
$S focus         # bring the app window/workspace back
$S status        # workspace, window address, geometry
$S key -k space  # keyboard via wtype
$S type "C001"   # type text (goes to the focused widget)
$S click 100 200 # window-relative click; needs ydotool
$S close         # close app, restore the previous workspace
```

`open` prints:

```text
workspace: 5
window:    0x55949613b250 (pid 489170)
geometry:  12,38 1386x890
shot:      /tmp/opencode/stock-clipper-ui/shots/shot-1.png
```

`shot` prints the PNG path. Read that path with the image tool; do not
re-screenshot blindly. By default shots are logical pixels (`grim -s 1`, i.e.
the size the QML is laid out at). Set `STOCK_UI_SHOT_SCALE=1.6` for a
full-resolution capture.

State lives in `/tmp/opencode/stock-clipper-ui/`:
`state` (workspace, pid, address), `shots/`, `app.log`, `home/` (the scratch
`STOCK_HOME`). Override the directory with `STOCK_UI_STATE`, the binary with
`STOCK_UI_BIN`.

## Keyboard

The app is mostly keyboard-driven once a clip is selected, but selecting a clip
and pressing buttons needs the pointer (see below). Useful `wtype` sequences:

```sh
$S key -k space                        # play / pause
$S key -k i                            # set in at playhead
$S key -k o                            # set out at playhead
$S key -k Left                         # nudge (Shift for big steps)
$S key -M ctrl -k Return -m ctrl       # render current clip
$S key -k Escape                       # close a dialog
```

Key names are XKB keysyms (`space`, `Left`, `i`, `Return`, `Escape`, …).

**Pointer limitation:** no `ydotool`/`dotool`/`wlrctl` is installed, so the
script cannot click. `click` exists and will tell you so. If a task needs a
click (selecting a clip, Browse/Settings buttons), either
ask the user to click on the open workspace, or use the offscreen harness
below for the specific check. Do not install system packages for this.

## Important behaviour

- `open` refuses if a session is already running; use `status`, then `close`.
- Settings/edits are isolated: the app runs with `STOCK_HOME` under the state
  dir, seeded from this checkout's `data/` on first launch. Changing settings
  in the UI never touches the user's real config.
- Renders still write to the real output folder (`./renders` by default). Do
  not press Render unless the task needs it, and delete test outputs
  afterwards.
- `close` only restores the previous workspace if the agent workspace is still
  active. If the user has moved elsewhere, it leaves them alone.
- Never leave the app open across turns. It steals the user's screen:
  announce what you are doing, take the shots you need, `close`.
- Hyprland deletes the empty workspace when `close` switches away, so no
  workspace cleanup is needed.

## Troubleshooting

- `not inside a Hyprland session` — the agent shell is not in the graphical
  session; fall back to the offscreen harness.
- `no app window after 15s` — the last lines of `app.log` are printed. Usual
  causes: binary not built, ffmpeg missing (the app still opens), or a QML
  error at load.
- Window opens on the wrong workspace — `open` moves it by PID. If it still
  looks wrong, `$S status`, then check `hyprctl clients -j`.
- `app is on workspace X, not Y; run: session.sh focus` — the user switched
  away; `focus` brings it back before shooting.

## When not to use this

For quick, non-intrusive layout checks, the repo's headless harness avoids
touching the desktop:

```sh
STOCK_HOME=/tmp/sc-test QT_QPA_PLATFORM=offscreen SC_SHOT=/tmp/shot.png \
  SC_OPEN=settings SC_CLIP=C0015 ./build/stock-clipper
```

That path uses `SC_SHOT`, `SC_OPEN`, `SC_CLIP` and `SC_SHOT_DELAY` (see
`src/main.cpp`), renders the same QML offscreen, and cannot show real video
playback or the native folder picker.
