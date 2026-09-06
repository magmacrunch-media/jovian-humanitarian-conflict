Drop PNGs here. They are embedded into the binary by bin2s at build time and
reachable as `<name>_png` / `<name>_png_size` from `assets.h`.

Empty so far: source/render.c draws everything with rectangles, lines and
circles. The look this has to reach is web/js/entities.js, world.js and
player.js, all of which draw procedurally in the browser rather than shipping
sheets -- so these will have to be authored or baked out, not copied across.

Whatever lands here, the transponder stays a constant-size mark drawn over the
hull rather than part of a sprite. It is the one channel that must not scale
with depth; see ../AGENTS.md.
