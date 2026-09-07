Raw PCM here is embedded into the binary by bin2s, aligned to 32 bytes for
ASND's DMA, and reachable as `<name>_pcm` / `<name>_pcm_size` from `assets.h`.

`music.pcm` is generated, not committed -- see `.gitignore`. Make it with:

    tools/convert-audio.sh

That reads the track from the website repo's jukebox, because the track is not
in this repo and is not going to be: it is 2.7MB and lives on the jukebox in its
own right, which is the same trade `web/js/config.js` makes by pointing at it
there. Without it the game builds and runs with no music and nothing else
missing.

The sound EFFECTS are not here and never will be. `source/sfx.c` synthesises all
six at startup out of oscillators and noise bursts, the way `web/js/sfx.js` does
in WebAudio -- about 144KB of RAM against the track's 6.5MB, and tunable by
editing numbers rather than re-exporting files.
