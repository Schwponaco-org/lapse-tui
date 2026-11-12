#!/usr/bin/env bash
# lapse-tui - a terminal interface for the LAPSE subtitle sync engine
# Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#
#   drive-tui.sh <lapse-tui> <lapse> [folder for the screens]
#
# Runs lapse-tui in tmux at 80x24 against a library from make-library.sh
# with 3000 fake films beside the real ones, presses keys the way a person
# would, and reads the screen and the files on disk back after each step.
set -e

TUI=$(realpath "$1")
LAPSE=$(realpath "$2")
SHOTS=${3:-}
HERE=$(dirname "$(realpath "$0")")
WORK=$(mktemp -d)
LIB=$WORK/lib
RUN=0
T="tmux -L lapse-tui-drive-$$-$RUN -f /dev/null"

cleanup() {
    for n in $(seq 0 "$RUN"); do tmux -L "lapse-tui-drive-$$-$n" kill-server 2> /dev/null || true; done
    rm -rf "$WORK"
}
trap cleanup EXIT

export HOME=$WORK/home
unset XDG_CONFIG_HOME XDG_CACHE_HOME XDG_STATE_HOME NO_COLOR
mkdir -p "$HOME"
[ -n "$SHOTS" ] && mkdir -p "$SHOTS"

"$HERE/make-library.sh" "$LIB" 3000 > /dev/null
ALIEN="$LIB/Films/Alien (1979)/English.srt"

# a server of its own each time, one still on its way out cannot get in the way
start() {
    $T kill-server 2> /dev/null || true
    RUN=$((RUN + 1))
    T="tmux -L lapse-tui-drive-$$-$RUN -f /dev/null"
    $T new-session -d -x 80 -y 24 "cd '$WORK' && $1"
}

screen() {
    $T capture-pane -p "$@"
}

fail() {
    echo "FAIL: $1"
    screen 2> /dev/null || true
    exit 1
}

wait_for() {
    for _ in $(seq 100); do
        screen 2> /dev/null | grep -qF -- "$1" && return 0
        sleep 0.1
    done
    fail "never saw: $1"
}

# an Escape with the next key right behind it reads as Alt and that key
keys() {
    for k in "$@"; do
        $T send-keys "$k"
        if [ "$k" = Escape ]; then sleep 0.5; else sleep 0.1; fi
    done
}

type_text() {
    $T send-keys -l -- "$1"
    sleep 0.3
}

shot() {
    if [ -n "$SHOTS" ]; then screen > "$SHOTS/$1.txt"; fi
}

first_cue() {
    sed -n 2p "$1" | cut -c1-12
}

# the start of the first line in ms. lapse with Silero and with libfvad
# hear the synthetic speech a few tens of ms apart, so this has some give
first_ms() {
    first_cue "$1" | awk -F'[:,]' '{ print (($1 * 60 + $2) * 60 + $3) * 1000 + $4 }'
}

echo "the library, found and paired"
start "'$TUI' --engine '$LAPSE' '$LIB'"
wait_for "3008 videos"
wait_for "Alien (1979)"
shot library

echo "find as you type, over the whole index"
keys /
type_text "alien"
wait_for "movie.mkv"
keys Enter
wait_for "Enter syncs its subtitles"

echo "sync one, solid, with a backup"
keys j Enter
wait_for "solid -4"
[ -f "$ALIEN.bak" ] || fail "no backup beside English.srt"
moved=$(first_ms "$ALIEN")
[ "$moved" -gt 1800 ] && [ "$moved" -lt 2200 ] || fail "English.srt was not moved: $(first_cue "$ALIEN")"
keys Tab
wait_for "auto/shifted, 1 part, written"
shot queue
keys Tab

echo "undo puts the original back"
keys u
wait_for "undone"
[ ! -f "$ALIEN.bak" ] || fail "the backup is still there"
[ "$(first_cue "$ALIEN")" = "00:00:06,200" ] || fail "English.srt is not the original"

echo "a dry run writes nothing"
keys d
wait_for "DRY RUN"
keys Enter
wait_for "dry solid -4"
[ ! -f "$ALIEN.bak" ] || fail "a dry run left a backup"
keys d

echo "a question, and saying yes to it"
keys f f
wait_for "Blade.Runner.1982.en.srt"
wait_for "70%?"
shot questions
keys /
type_text "matrix 1999 en srt"
keys Enter
wait_for "c yes  x no"
keys c
wait_for "paired by hand"

echo "pairing by hand, then a shift"
keys /
type_text "something else"
keys Enter
wait_for "No film found for it"
keys m C-u
type_text "heat"
wait_for "Pick the film"
shot pair
keys Enter
wait_for "Paired Something.Else.Entirely.srt with Heat"
keys s
if "$LAPSE" 2>&1 | grep -q -- --shift; then
    type_text "-500"
    keys Enter
    wait_for "shifted -500 ms"
else
    wait_for "This lapse has no --shift"
fi

echo "options and help"
keys o
wait_for "Options for the next syncs"
shot options
keys Escape '?'
wait_for "say yes or no to a pair"
shot help
keys q

echo "resizing"
$T resize-window -x 132 -y 40
sleep 0.5
[ "$(screen | head -1 | wc -m)" -gt 120 ] || fail "did not grow with the terminal"
$T resize-window -x 80 -y 24
sleep 0.5
[ "$(screen | head -1 | wc -m)" -le 81 ] || fail "did not shrink with the terminal"

echo "the mouse picks the queue tab"
$T send-keys -l $'\e[<0;30;1M'
wait_for "x cancel"
$T send-keys -l $'\e[<0;10;1M'
wait_for "enter sync"

echo "quitting"
keys q
for _ in $(seq 50); do $T has-session 2> /dev/null || break; sleep 0.1; done
! $T has-session 2> /dev/null || fail "still running after q"

echo "the next start comes from the index and the settings"
mkdir -p "$HOME/.config/lapse-tui"
echo "root = $LIB" > "$HOME/.config/lapse-tui/config"
start "'$TUI' --engine '$LAPSE'"
wait_for "3008 videos"
wait_for "dry solid -4"

echo "NO_COLOR"
start "NO_COLOR=1 '$TUI' --engine '$LAPSE'"
wait_for "3008 videos"
if screen -e | grep -q $'\e\[3[0-7]m'; then fail "colours with NO_COLOR set"; fi

echo "the queue finishes when the terminal goes away"
cat > "$WORK/slow-lapse" << EOF
#!/bin/sh
case "\$1" in --version|--formats|--undo) exec '$LAPSE' "\$@";; esac
sleep 1
exec '$LAPSE' "\$@"
EOF
chmod +x "$WORK/slow-lapse"
start "'$TUI' --engine '$WORK/slow-lapse'"
wait_for "3008 videos"
keys /
type_text "show s01"
keys Enter
wait_for "Show.S01E01"
keys Space Space Enter
wait_for "queued to sync"
$T kill-server
for _ in $(seq 100); do pgrep -f "slow-lapse" > /dev/null || break; sleep 0.1; done
sleep 0.5
grep -q "Show.S01E02.en.srt.solid" <(tr '\t' '.' < "$HOME/.local/state/lapse-tui/state") ||
    fail "the jobs did not finish after the hangup"

echo "all good"
