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
#   make-library.sh <dir>            a few real films in the usual layouts
#   make-library.sh <dir> <count>    count empty fake films beside them
#
# The real ones are spoken lines from espeak-ng in a black video, the way
# lapse's make-media.sh builds them, with a subtitle 4.2 s late, so lapse
# finds them solid. The fake ones are empty files and only there to be
# found and paired.
set -e

LIB=$1
COUNT=${2:-0}
LINE="The quick brown fox jumps over the lazy dog near the river bank."
LEAD=2
SHIFT=4200
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

build_video() {
    espeak-ng -w "$WORK/spoken.wav" -s 150 "$LINE"
    trim="silenceremove=start_periods=1:start_threshold=-40dB:detection=peak"
    ffmpeg -v error -y -i "$WORK/spoken.wav" -ar 16000 -ac 1 \
        -af "${trim},areverse,${trim},areverse" "$WORK/line.wav"
    spoken=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$WORK/line.wav")
    CUE=$(python3 -c "print(int(float('$spoken') * 1000))")
    SLOTS=$(python3 -c "
import random
random.seed(5)
print(' '.join(str(random.randint(6, 11)) for _ in range(20)))
")
    inputs="" filters="" chain="" i=0
    for slot in $SLOTS; do
        inputs="$inputs -i $WORK/line.wav"
        filters="${filters}[${i}:a]aresample=16000,apad=whole_dur=${slot}[a${i}];"
        chain="${chain}[a${i}]"
        i=$((i + 1))
    done
    # shellcheck disable=SC2086
    ffmpeg -v error -y $inputs -filter_complex \
        "${filters}${chain}concat=n=20:v=0:a=1,adelay=${LEAD}000|${LEAD}000,apad=pad_dur=2[out]" \
        -map "[out]" -ar 16000 -ac 1 "$WORK/audio.wav"
    ffmpeg -v error -y -f lavfi -i color=c=black:s=160x120:r=24 -i "$WORK/audio.wav" \
        -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac -shortest "$WORK/video.mkv"
}

# path, shift in ms
speech_srt() {
    python3 -c "
import sys
path, shift = sys.argv[1], int(sys.argv[2])
slots = [int(x) * 1000 for x in '$SLOTS'.split()]
def ts(ms):
    h = ms // 3600000; ms %= 3600000
    m = ms // 60000; ms %= 60000
    s = ms // 1000; ms %= 1000
    return '%02d:%02d:%02d,%03d' % (h, m, s, ms)
out = []
at = $LEAD * 1000
for i, slot in enumerate(slots):
    start = at + shift
    out.append('%d\n%s --> %s\nline %d\n' % (i + 1, ts(start), ts(start + $CUE), i + 1))
    at += slot
open(path, 'w').write('\n'.join(out))
" "$1" "$2"
}

film() {
    mkdir -p "$(dirname "$LIB/$1")"
    cp "$WORK/video.mkv" "$LIB/$1"
}

build_video

film "Films/Heat (1995)/Heat.1995.1080p.BluRay.x264-GRP.mkv"
speech_srt "$LIB/Films/Heat (1995)/Heat.1995.1080p.BluRay.x264-GRP.en.srt" $SHIFT
speech_srt "$LIB/Films/Heat (1995)/Heat.1995.1080p.BluRay.x264-GRP.da.srt" -1500

film "Films/Alien (1979)/movie.mkv"
speech_srt "$LIB/Films/Alien (1979)/English.srt" $SHIFT

film "Films/Scene.Film.2010.720p.WEB-DL-GRP/Scene.Film.2010.720p.WEB-DL-GRP.mkv"
mkdir -p "$LIB/Films/Scene.Film.2010.720p.WEB-DL-GRP/Subs"
speech_srt "$LIB/Films/Scene.Film.2010.720p.WEB-DL-GRP/Subs/2_English.srt" $SHIFT

film "Films/Blade Runner (1982)/Blade.Runner.1982.1080p.mkv"
film "Films/Blade Runner (1982)/Blade.Runner.1982.2160p.mkv"
speech_srt "$LIB/Films/Blade Runner (1982)/Blade.Runner.1982.en.srt" $SHIFT

film "Shows/Show/Season 1/Show.S01E01.mkv"
film "Shows/Show/Season 1/Show.S01E02.mkv"
speech_srt "$LIB/Shows/Show/Season 1/Show.S01E01.en.srt" $SHIFT
speech_srt "$LIB/Shows/Show/Season 1/Show.S01E02.en.srt" $SHIFT

film "Films/The Matrix (1999)/The.Matrix.1999.mkv"
mkdir -p "$LIB/Downloads"
speech_srt "$LIB/Downloads/The.Matrix.1999.en.srt" $SHIFT
speech_srt "$LIB/Downloads/Something.Else.Entirely.srt" $SHIFT

if [ "$COUNT" -gt 0 ]; then
    python3 -c "
import os, random, sys
lib, count = sys.argv[1], int(sys.argv[2])
random.seed(3)
words = 'night river cold light house iron storm glass winter long last city road silent red black dark wild blue king'.split()
for i in range(count):
    tag = ''.join(chr(97 + i // 26 ** k % 26) for k in range(3))
    title = '.'.join(random.sample(words, random.randint(1, 3))).title() + '.' + tag.title()
    year = 1950 + i % 70
    name = '%s.%d.1080p.BluRay.x264-FAKE' % (title, year)
    d = os.path.join(lib, 'Fake', name[:1], name)
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, name + '.mkv'), 'w').close()
    for lang in random.sample(['en', 'da', 'sv', 'de'], random.randint(0, 3)):
        open(os.path.join(d, name + '.' + lang + '.srt'), 'w').close()
" "$LIB" "$COUNT"
fi
