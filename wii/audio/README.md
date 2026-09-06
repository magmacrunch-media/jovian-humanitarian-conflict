Raw PCM here is embedded into the binary by bin2s, aligned to 32 bytes for
ASND's DMA, and reachable as `<name>_pcm` / `<name>_pcm_size` from `assets.h`.

Empty, and unusually so: this game ships no audio at all. That is inherited
from the web version rather than skipped here.

  - The music is not in this repo. CONFIG.MUSIC.URL in web/js/config.js points
    at the website's jukebox, because the track is 2.7 MB and lives there in
    its own right.
  - The sound effects are not files. web/js/sfx.js synthesises all six
    procedurally in WebAudio.

So there is nothing to convert. Wiring sound means fetching the ogg out of the
website repo and converting it the way makemecookies does, or reimplementing
six effects against ASND. jov_run_resolve() already raises a JovCue with a
count per effect, so main.c is the only file either job touches.
