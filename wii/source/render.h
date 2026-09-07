/* =====================================================================
 * render.h -- The Jovian Humanitarian Conflict (Wii)
 * Draws state, decides nothing.
 *
 * PLACEHOLDER. What this draws is a legible diagnostic view of the real
 * simulation -- the rail, the contacts, the transponder, the locks and the HUD
 * -- and not the game. The look it has to grow into is web/js/world.js,
 * entities.js and player.js. See ../AGENTS.md for what is finished and what is
 * not.
 *
 * Every function here reads a const JovSim and writes pixels. Nothing here may
 * change simulation state: the day the renderer nudges a contact is the day
 * `make test` stops being a statement about the game.
 *
 * ---------------------------------------------------------------------
 * Two coordinate spaces, and which is which.
 *
 * The PLAYFIELD is drawn in the game's own 480x270 design frame -- the
 * browser's canvas, kept so the tuning transfers -- letterboxed into the
 * TV-safe area by rd_map_*(). The HUD is drawn in magnolia's 640x480 design
 * space through ui_draw_*(), because that is what the engine's overscan
 * handling and font metrics are written against.
 *
 * Mixing them is the mistake to watch for: a HUD coordinate passed to
 * rd_map_x() lands in roughly the right place on NTSC and in the wrong place
 * on PAL, which is the kind of thing nobody sees until somebody else runs it.
 * ===================================================================== */
#ifndef RENDER_H
#define RENDER_H

#include <magnolia.h>

#include "sim.h"

/* Recompute the playfield's letterbox from the running video mode and the
 * engine's safe area. Call once after magnolia_init(), and again only if the
 * overscan setting changes. */
void rd_init(void);

/* Design-frame (480x270) -> screen pixels. Public so main.c can place anything
 * that has to sit in the world rather than on the HUD. */
float rd_map_x(float design_x);
float rd_map_y(float design_y);
float rd_map_s(float design_units);   /* a length, not a position */

/* Stars are here rather than in the rail because they respond only to camera
 * drift, nothing can collide with them, and their placement is random -- all
 * three of which are reasons to keep them out of the tested translation unit. */
void rd_reset_stars(unsigned int seed);

/* Confine drawing to the playfield's letterbox, and release it again.
 *
 * The browser gets this free: a canvas clips at its own edges, so world.js can
 * draw a deck band 1800 world units wide and see only the part that lands on
 * the canvas. GX does not clip, so the same band ran out across the letterbox
 * and into the TV's overscan -- long horizontal lines to the left and right of
 * the picture, which read as a rendering fault rather than as a deck.
 *
 * Paired explicitly rather than being set inside rd_draw_rail(), because the
 * title card draws the rail and then draws TEXT over it, and text belongs to
 * the HUD's coordinate space and its own full safe area. A clip left on would
 * cut the title in half.
 */
void rd_playfield_begin(void);
void rd_playfield_end(void);

/* The world behind the rail: void, stars, the gas giant, the cloud deck. */
void rd_draw_rail(const JovRail *rail);

/* Contacts far to near, their transponders, the lock links and countdowns,
 * shots, particles and popups. */
void rd_draw_contacts(const JovSim *sim);

/* The ship, and the reticle out at firing depth. */
void rd_draw_player(const JovSim *sim);

/* Score, combo, lives, strikes, and the contact strip -- the second
 * identification channel, and the one that survives a cluttered frame. */
void rd_draw_hud(const JovSim *sim, const JovRun *run);

/* The cards. Each is lettering over whatever main.c has already drawn behind
 * it, and all of them belong to the HUD's coordinate space -- the full safe
 * area, outside the playfield clip. The title's BACKDROP is the ordinary rail,
 * which is why the title is text-only here.
 *
 * These follow magnolia's score-attack shell: title, ready, play, pause, game
 * over, initials on a qualifying score, then the table. The shell owns the
 * transitions and the initials editor; this owns what they look like. */
void rd_draw_title_text(void);

/* The briefing. This game's whole premise is one rule, and a player who has not
 * been told it will shoot a convoy in the first ten seconds and not know why
 * the run ended -- so the two contact types are drawn side by side, which is
 * the same thing the scripted opening wave does. */
void rd_draw_ready(float frame);

void rd_draw_paused(void);

/* The initials editor and the table. `gs` is magnolia's state machine, which
 * holds the cursor and the letter under it. */
void rd_draw_initials(const GameStateMachine *gs, const JovRun *run);
void rd_draw_scores(const GameStateMachine *gs);
void rd_draw_results(const JovRun *run);

/* Screen shake and the friendly-fire flash, both driven by JovCue. Decays on
 * its own clock so main.c only has to feed it. */
void rd_add_shake(float amount);
void rd_add_flash(float alpha, unsigned int color);
void rd_decay(float dt);

#endif
