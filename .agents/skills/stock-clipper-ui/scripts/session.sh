#!/usr/bin/env bash
# Drive the real Stock Clipper window in the running Hyprland session.
#
#   session.sh open [--no-shot]   launch on a clean workspace, wait, focus, shoot
#   session.sh shot [path]        screenshot the app window
#   session.sh focus              focus the app window
#   session.sh key <wtype args>   send keys (e.g. -k space, -M ctrl -k Return -m ctrl)
#   session.sh type <text>        type text
#   session.sh click <x> <y>      click inside the window (needs ydotool)
#   session.sh status             show the current session
#   session.sh close              close the window and restore the workspace
#
# All state lives in $STOCK_UI_STATE (default /tmp/opencode/stock-clipper-ui),
# so nothing the app saves during a session touches the user's real config.

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$script_dir/../../../.." && pwd)

state_dir=${STOCK_UI_STATE:-/tmp/opencode/stock-clipper-ui}
state_file=$state_dir/state
shots_dir=$state_dir/shots
app_log=$state_dir/app.log
app_home=$state_dir/home
app_bin=${STOCK_UI_BIN:-$repo/build/stock-clipper}

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
note() { printf '%s\n' "$*"; }

need_hyprland() {
    [[ -n ${HYPRLAND_INSTANCE_SIGNATURE:-} ]] ||
        die "not inside a Hyprland session (HYPRLAND_INSTANCE_SIGNATURE is unset)"
    command -v hyprctl >/dev/null || die "hyprctl not found"
    command -v jq >/dev/null || die "jq not found"
    hyprctl activeworkspace -j >/dev/null 2>&1 ||
        die "hyprctl cannot reach the compositor"
}

load_state() {
    [[ -f $state_file ]] || die "no UI session; run: session.sh open"
    # shellcheck disable=SC1090
    source "$state_file"
}

save_state() {
    {
        printf 'prev_ws=%q\n' "$prev_ws"
        printf 'ws=%q\n' "$ws"
        printf 'addr=%q\n' "$addr"
        printf 'pid=%q\n' "$pid"
        printf 'shot_n=%q\n' "$shot_n"
    } >"$state_file"
}

window_json() { # by pid: window_json 1234
    hyprctl clients -j | jq -c --argjson pid "$1" \
        'first(.[] | select(.pid == $pid)) // empty'
}

window_by_addr() { # window_by_addr 0x...
    hyprctl clients -j | jq -c --arg a "$1" \
        'first(.[] | select(.address == $a)) // empty'
}

active_ws_name() { hyprctl activeworkspace -j | jq -r '.name'; }

choose_workspace() {
    local used max
    used=$(hyprctl workspaces -j | jq -r '.[] | select(.id > 0) | .id')
    for i in $(seq 1 40); do
        if ! grep -qx "$i" <<<"$used"; then
            echo "$i"
            return
        fi
    done
    max=$(jq -rn '[inputs | tonumber] | max // 0' <<<"$used")
    echo $((max + 1))
}

focus_workspace() { # focus_workspace <name>
    hyprctl eval "hl.dispatch(hl.dsp.focus({ workspace = \"$1\" }))" >/dev/null
}

focus_window() { # focus_window <address>
    local deadline=$((SECONDS + 2))
    hyprctl eval "hl.dispatch(hl.dsp.focus({ window = \"address:$1\" }))" >/dev/null
    while ((SECONDS < deadline)); do
        [[ $(hyprctl activewindow -j | jq -r '.address // empty') == "$1" ]] && return 0
        sleep 0.05
    done
    return 1
}

window_geometry() { # window_geometry <json>
    jq -r '"\(.at[0]),\(.at[1]) \(.size[0])x\(.size[1])"' <<<"$1"
}

take_shot() { # take_shot [path] -> prints path
    local path=${1:-} json geom scale
    json=$(window_json "$pid")
    [[ -n $json ]] || die "app window is gone (pid $pid); check $app_log"

    [[ $(jq -r '(.workspace.id|tostring)' <<<"$json") == "$ws" ]] ||
        die "app is on workspace $(jq -r '.workspace.id' <<<"$json"), not $ws; run: session.sh focus"

    focus_window "$addr" || die "app window is not focused; is it still open? (session.sh status)"
    sleep 0.3

    if [[ -z $path ]]; then
        mkdir -p "$shots_dir"
        shot_n=$((shot_n + 1))
        path=$shots_dir/shot-$shot_n.png
    else
        mkdir -p "$(dirname "$path")"
    fi

    geom=$(window_geometry "$json")
    # grim's -s overrides the output scale: 1 = logical pixels (the size the UI
    # is laid out at). Set STOCK_UI_SHOT_SCALE to the monitor scale for a
    # full-resolution capture.
    scale=${STOCK_UI_SHOT_SCALE:-1}
    grim -g "$geom" -s "$scale" "$path"

    save_state
    note "$path"
}

cmd_open() {
    need_hyprland
    [[ -x $app_bin ]] || die "missing $app_bin; build it first: cmake --build build"
    command -v grim >/dev/null || die "grim not found"

    if [[ -f $state_file ]]; then
        # shellcheck disable=SC1090
        source "$state_file"
        if [[ -n ${pid:-} ]] && kill -0 "$pid" 2>/dev/null; then
            die "a UI session is already open on workspace $ws (pid $pid); run: session.sh close"
        fi
        rm -f "$state_file"
    fi

    prev_ws=$(active_ws_name)
    ws=$(choose_workspace)
    shot_n=0

    focus_workspace "$ws"
    sleep 0.3
    [[ $(active_ws_name) == "$ws" ]] || die "could not switch to workspace $ws"

    mkdir -p "$state_dir"
    rm -rf "$app_home"
    : >"$app_log"

    (
        cd "$repo" || exit 1
        STOCK_HOME="$app_home" setsid "$app_bin" >>"$app_log" 2>&1 &
        echo $! >"$state_dir/app.pid"
    )
    pid=$(cat "$state_dir/app.pid")

    local json deadline=$((SECONDS + 15))
    while :; do
        json=$(window_json "$pid")
        [[ -n $json ]] && break
        if ! kill -0 "$pid" 2>/dev/null; then
            tail -n 20 "$app_log" >&2 || true
            die "app exited during startup"
        fi
        ((SECONDS < deadline)) || {
            tail -n 20 "$app_log" >&2 || true
            die "no app window after 15s"
        }
        sleep 0.2
    done

    addr=$(jq -r '.address' <<<"$json")

    # The window should already be on the workspace we switched to; move it if
    # a window rule or activation token sent it elsewhere.
    if [[ $(jq -r '.workspace.id' <<<"$json") != "$ws" ]]; then
        focus_window "$addr"
        hyprctl eval "hl.dispatch(hl.dsp.window.move({ workspace = \"$ws\" }))" >/dev/null
        sleep 0.3
        json=$(window_json "$pid")
        addr=$(jq -r '.address' <<<"$json")
    fi

    focus_window "$addr" || die "window appeared but could not be focused"
    save_state

    note "workspace: $ws"
    note "window:    $addr (pid $pid)"
    note "geometry:  $(window_geometry "$json")"
    if [[ ${1:-} != --no-shot ]]; then
        note "shot:      $(take_shot "")"
    fi
    if ! command -v ydotool >/dev/null; then
        note "note:      no ydotool, so pointer clicks are unavailable; keyboard works via 'key'/'type'"
    fi
}

cmd_shot() {
    need_hyprland
    load_state
    [[ -n ${pid:-} ]] || die "no UI session; run: session.sh open"
    take_shot "${1:-}"
}

cmd_focus() {
    need_hyprland
    load_state
    focus_window "$addr" || die "app window is gone; run: session.sh close"
    # Focusing a window on another workspace also switches to it.
    note "workspace: $(active_ws_name)"
}

cmd_key() {
    need_hyprland
    load_state
    command -v wtype >/dev/null || die "wtype not found"
    [[ $# -gt 0 ]] || die "usage: session.sh key <wtype args...>"
    focus_window "$addr" || die "app window is not focused; refusing to type into another window"
    sleep 0.15
    wtype "$@"
}

cmd_type() {
    need_hyprland
    load_state
    command -v wtype >/dev/null || die "wtype not found"
    [[ $# -gt 0 ]] || die "usage: session.sh type <text>"
    focus_window "$addr" || die "app window is not focused; refusing to type into another window"
    sleep 0.15
    wtype -- "$@"
}

cmd_click() {
    need_hyprland
    load_state
    command -v ydotool >/dev/null ||
        die "ydotool is not installed, so pointer clicks cannot be injected; use 'key'/'type' or ask the user to click"
    [[ $# -eq 2 ]] || die "usage: session.sh click <window-x> <window-y>"
    [[ $1 =~ ^-?[0-9]+$ && $2 =~ ^-?[0-9]+$ ]] || die "click takes integer window coordinates"
    local json gx gy
    focus_window "$addr" || die "app window is not focused"
    json=$(window_json "$pid")
    [[ -n $json ]] || die "app window is gone"
    gx=$(jq -r ".at[0] + $1" <<<"$json")
    gy=$(jq -r ".at[1] + $2" <<<"$json")
    hyprctl eval "hl.dispatch(hl.dsp.cursor.move({ x = $gx, y = $gy }))" >/dev/null
    sleep 0.1
    ydotool click 0xC0
}

cmd_status() {
    need_hyprland
    load_state
    note "workspace: $ws (active: $(active_ws_name))"
    note "prev workspace: $prev_ws"
    local json
    json=$(window_json "$pid")
    if [[ -z $json ]]; then
        note "window: gone (pid $pid)"
        return 1
    fi
    note "window: $addr (pid $pid)"
    note "geometry: $(window_geometry "$json")"
    note "shots: $shots_dir"
}

cmd_close() {
    need_hyprland
    if [[ ! -f $state_file ]]; then
        note "no UI session"
        return 0
    fi
    load_state

    if [[ -n ${pid:-} ]] && kill -0 "$pid" 2>/dev/null; then
        local json deadline
        json=$(window_json "$pid")
        if [[ -n $json ]]; then
            addr=$(jq -r '.address' <<<"$json")
            focus_window "$addr"
            hyprctl eval 'hl.dispatch(hl.dsp.window.close())' >/dev/null
            deadline=$((SECONDS + 3))
            while kill -0 "$pid" 2>/dev/null && ((SECONDS < deadline)); do
                sleep 0.1
            done
        fi
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            deadline=$((SECONDS + 2))
            while kill -0 "$pid" 2>/dev/null && ((SECONDS < deadline)); do
                sleep 0.1
            done
            kill -9 "$pid" 2>/dev/null || true
        fi
    fi

    # Only yank the user back if they have not already moved elsewhere.
    if [[ $(active_ws_name) == "$ws" ]]; then
        if [[ $prev_ws == special:* ]]; then
            hyprctl eval "hl.dispatch(hl.dsp.workspace.toggle_special(\"${prev_ws#special:}\"))" >/dev/null
        elif [[ -n $prev_ws && $prev_ws != "$ws" ]]; then
            focus_workspace "$prev_ws"
        fi
    fi

    rm -f "$state_file"
    note "closed; active workspace: $(active_ws_name)"
}

case "${1:-}" in
open) shift; cmd_open "$@" ;;
shot) shift; cmd_shot "$@" ;;
focus) cmd_focus ;;
key) shift; cmd_key "$@" ;;
type) shift; cmd_type "$@" ;;
click) shift; cmd_click "$@" ;;
status) cmd_status ;;
close) cmd_close ;;
*)
    sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
