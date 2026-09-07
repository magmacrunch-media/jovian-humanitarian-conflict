#!/bin/sh
# Converts the game's track into the raw PCM magnolia plays.
#
#   tools/convert-audio.sh [path-to-jukebox-songs-dir]
#
# ---------------------------------------------------------------------------
# THE TRACK IS NOT IN THIS REPO, and that is deliberate.
#
# CONFIG.MUSIC.URL in web/js/config.js points at ../../music/jukebox/songs/ --
# a *website* path. The track is 2.7MB and is on the jukebox in its own right,
# so a second copy under this game would be pure duplication, and the web
# version accepts the consequence: opened out of a bare checkout it runs silent.
# Roderick Tron makes the same trade.
#
# The consequence here is larger, because a console cannot fetch anything: this
# script needs the website repo checked out beside this one. Without it there
# is no music.pcm, the Makefile emits an empty asset blob, and the game builds
# and runs perfectly well with no music at all. That is the intended degraded
# state, not a failure.
#
# ---------------------------------------------------------------------------
# The sound EFFECTS are not here.
#
# Only one file is converted below. The six effects are synthesised at startup
# by source/sfx.c, the way web/js/sfx.js synthesises them in WebAudio -- no
# assets, about 144KB of RAM, and tunable by editing numbers. See sfx.h.
set -e

DEFAULT_SRC="$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)/web/website/music/jukebox/songs"
SRC=${1:-$DEFAULT_SRC}
OUT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)/audio

# Clips are held decoded in main RAM, so the format is a memory decision before
# it is a fidelity one:
#
#   48kHz stereo  ~192 KB/s     24kHz mono  ~48 KB/s
#
# 24kHz mono matches makemecookies. Mono costs almost nothing on a TV speaker,
# and 12kHz of bandwidth is dull for music but is what the budget allows.
MUSIC_RATE=${MUSIC_RATE:-24000}
MUSIC_CHANNELS=${MUSIC_CHANNELS:-1}

# The ogg is the original encode; the mp3 beside it is a second-generation
# transcode of it, so this always reads the ogg.
TRACK="$SRC/Jimmi - JIMMI - 02 The Jovian Humanitarian Conflict.ogg"

if [ ! -f "$TRACK" ]; then
    echo "error: no track at" >&2
    echo "  $TRACK" >&2
    echo "" >&2
    echo "The track lives in the website repo, not this one -- see the note at" >&2
    echo "the top of this script. Either check out magmacrunch.com beside this" >&2
    echo "repo, or pass its music/jukebox/songs directory as the first argument." >&2
    echo "" >&2
    echo "The game builds and runs without it, silently." >&2
    exit 1
fi

command -v ffmpeg >/dev/null 2>&1 || { echo "error: ffmpeg not found" >&2; exit 1; }

mkdir -p "$OUT"

echo "music: whole track, ${MUSIC_RATE}Hz, ${MUSIC_CHANNELS}ch"
ffmpeg -v error -y -i "$TRACK" \
    -f s16le -acodec pcm_s16le -ar "$MUSIC_RATE" -ac "$MUSIC_CHANNELS" \
    "$OUT/music.pcm"

size=$(wc -c < "$OUT/music.pcm")
ms=$(( size * 1000 / (MUSIC_RATE * MUSIC_CHANNELS * 2) ))

echo ""
printf "  music.pcm  %s KB  %s.%ss\n" "$((size / 1024))" "$((ms / 1000))" \
    "$(printf '%03d' $((ms % 1000)) | cut -c1)"
echo ""
echo "This is linked into the .dol and resident for the whole session. The Wii"
echo "has 24MB; source/config.h states the budget this sits inside."
echo ""
echo "The WHOLE track goes in, uncut. Unlike george-boole -- which had to fold a"
echo "3m50s piece into a 60s loop with a crossfaded seam -- nothing here needs a"
echo "seam, because a run is 12 to 50 seconds and almost never reaches the end."
echo "magnolia loops the voice forever, so a very long run hears the track's own"
echo "start-to-end join, which is untreated. Trimming is the lever if this ever"
echo "has to get smaller; it costs a seam somebody has to judge by ear."
echo ""
echo "config.h must agree:"
echo "    #define MUSIC_RATE     ${MUSIC_RATE}"
echo "    #define MUSIC_CHANNELS ${MUSIC_CHANNELS}"
echo "Nothing in this game is timed off the music -- unlike makemecookies, where"
echo "the track IS the round -- so a re-encode at another rate needs those two"
echo "lines changed and nothing else."
