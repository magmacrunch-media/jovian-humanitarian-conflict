/* =====================================================================
 * sim.h -- The Jovian Humanitarian Conflict (Wii)
 * The whole simulation: the ship, the rail, the contacts, and the IFF rules.
 *
 * A port of web/js/player.js, world.js and entities.js, plus the scoring
 * policy out of main.js. Deliberately the same shape as the browser: every
 * function here is (state, tuning, dt) and the translation unit contains no
 * GRRLIB, no libogc, no audio and no drawing whatsoever.
 *
 * Keep it that way. That separation is what lets tests/test_simulation.c link
 * the real shipped rules on the machine you are sitting at, exactly as the web
 * suite loads the real modules into a vm context -- and the suite is the only
 * thing standing between this port and the browser version quietly becoming
 * two different games. A GRRLIB call in sim.c costs the entire suite.
 *
 * ---------------------------------------------------------------------
 * Three places this port deviates, and why.
 *
 * 1. Scoring lives here, not in main.c. The browser puts it in main.js and
 *    keeps entities.js free of it, which is right there because main.js is
 *    also reachable from the test vm. main.c is not reachable from `make
 *    test` -- it includes magnolia -- so pricing an event in main.c would put
 *    friendly-fire attribution, the second most costly thing in this game to
 *    get wrong, outside the suite. jov_run_resolve() is the split instead: it
 *    prices events and emits CUES, and main.c turns cues into sound and
 *    shake. No policy in main.c, no audio here.
 *
 * 2. Events carry a COPY of the contact, not a pointer to it. The browser
 *    holds a live object reference and the contact survives because it is
 *    garbage collected. Here the array is compacted in the same frame the
 *    event is raised, so a pointer would dangle by the time main.c read it.
 *
 * 3. Arrays are fixed. See the capacity notes in config.h for what overflows
 *    and what that costs.
 * ===================================================================== */
#ifndef SIM_H
#define SIM_H

#include "config.h"
#include "projection.h"

/* -- Determinism ------------------------------------------------------
 * mulberry32, the same generator the web suite uses. It is all 32-bit integer
 * arithmetic, so this is bit-identical to the JavaScript for a given seed --
 * which is what makes a failing test reproducible rather than a story about
 * something that happened once.
 *
 * Note that bit-identical RNG does NOT make the two simulations bit-identical:
 * the game runs on float here and double there, and libm's sinf/cosf/powf are
 * not V8's. Identical seeds give identical SPAWNS; they do not give identical
 * trajectories. There is no oracle test here yet for that reason -- see
 * AGENTS.md.
 */
typedef struct { unsigned int a; } JovRng;

void  jov_rng_seed(JovRng *r, unsigned int seed);
float jov_rng_next(JovRng *r);      /* [0, 1) */

/* -- Difficulty -------------------------------------------------------
 * The single source of "how hard is it now". Every escalating quantity is
 * derived from one number, so tuning the curve never means hunting for a
 * second place that also reads the clock. `t` is 0 at the start of a run and
 * 1 once DIFFICULTY_DISTANCE is behind you.
 */
float jov_lerp(float a, float b, float t);
float jov_diff_at(float distance);
float jov_diff_rail_speed(float t);
float jov_diff_spawn_interval(float t);
float jov_diff_aid_share(float t);
float jov_diff_aggro(float t);
int   jov_diff_wave_size(float t, JovRng *rng);

/* Frames a contact spends between spawning and entering firing range. This is
 * the identification budget, and the reason RAIL_SPEED_MAX is capped where it
 * is. */
float jov_diff_identify_frames(float t);

/* -- The ship ---------------------------------------------------------
 * Never leaves the z = 0 plane, so its x/y are world units measured from the
 * rail's centre line and need no projection -- the one thing in the game
 * positioned without going through proj_point().
 */
typedef struct {
    float x, y;
    float vx, vy;
    float bank;             /* -1..1, lags vx so the roll reads as inertia */
    float shoot_cooldown;
    float invincible;
    int   lives;
    float thrust_phase;
} JovPlayer;

void jov_player_reset(JovPlayer *p);

/* One frame of flight. `wants_fire` is passed in rather than read from input
 * so the headless tests can fly the ship across the whole input space without
 * a Wiimote. Returns 1 on the frames a shot is actually fired. */
int  jov_player_update(JovPlayer *p, float axis_x, float axis_y,
                       int wants_fire, float dt);

/* Where a shot leaves the ship. The nose, not the centre. */
void jov_player_muzzle(const JovPlayer *p, float *out_x, float *out_y);
int  jov_player_can_be_hit(const JovPlayer *p);

/* Returns 1 if the hit actually landed -- i.e. was not an i-frame. */
int  jov_player_take_hit(JovPlayer *p);

/* -- The rail ---------------------------------------------------------
 * Camera drift and the receding cloud deck. Holds no geometry that anything
 * collides with: it is entirely depth cueing. That is not decoration -- at
 * this scale, the difference between "ships are growing" and "I am flying at
 * them" is carried almost wholly by the deck bands streaming past.
 *
 * The stars are NOT here. They respond only to camera drift, never to the
 * rail, and nothing can collide with them, so they live in render.c where
 * their random placement costs the suite nothing.
 */
#define RAIL_BANDS 30

typedef struct {
    float distance;
    float cam_x, cam_y;
    float bands[RAIL_BANDS];
} JovRail;

void jov_rail_reset(JovRail *r);
void jov_rail_update(JovRail *r, const JovPlayer *p, float rail_speed, float dt);

/* The title screen's backdrop: the same rail, drifting gently on its own. */
void jov_rail_drift(JovRail *r, float frame);

/* -- Contacts ---------------------------------------------------------
 *
 * Why identification is built the way it is: the premise only works if
 * refusing to shoot is a decision rather than a gamble. At spawn depth a
 * convoy hull is under 6px wide in the design frame, so silhouette cannot
 * carry the read, and colour alone excludes anyone who cannot separate amber
 * from magenta. So a contact announces itself on four channels that come
 * legible in this order:
 *
 *   1. Transponder blink   -- every depth, including the spawn frame. Drawn at
 *                             a CONSTANT size, so it does not shrink away.
 *                             Convoys squawk a double-blink; hostiles are
 *                             dark. This is the channel the fairness test is
 *                             written against, and it is motion, not colour.
 *   2. HUD contact strip   -- drawn from the depth ordering below, so a
 *                             cluttered frame never hides the answer.
 *   3. Silhouette          -- from Z_SHAPE_READABLE inward.
 *   4. Colour              -- last, and never alone.
 *
 * Do not let a change collapse these into one. The reason this port is even
 * possible is that channel 1 owes nothing to resolution or hue -- which
 * matters more on a TV across a room than it ever did on a monitor.
 *
 * Everything stores WORLD coordinates and converts at draw and collision time.
 */
typedef enum { JOV_HOSTILE = 0, JOV_AID } JovKind;

typedef struct {
    int     id;
    JovKind kind;
    float   x, y, z;
    float   phase;          /* drift phase, seeded per contact */
    float   drift_seed;
    int     aggro;
    float   doom_timer;     /* set when a hostile locks this convoy */
    int     locked_by;
    int     dead;
    int     pinged;
    float   age;            /* frames alive -- what the telegraph is measured in */
} JovContact;

typedef struct { float x, y, z; } JovShot;

typedef struct {
    float x, y, z;
    float vx, vy;
    float life, max;
    unsigned int color;
} JovParticle;

typedef struct {
    float x, y, z;
    char  text[POPUP_TEXT_MAX];
    unsigned int color;
    float life, max;
} JovPopup;

/* -- Events -----------------------------------------------------------
 * What happened, never what it is worth. The simulation reports; jov_run_*
 * prices. That split is what lets the whole thing be flown headlessly without
 * dragging a scoreboard along.
 */
typedef enum {
    JOV_EV_HOSTILE_KILLED = 0,
    JOV_EV_AID_ESCORTED,
    JOV_EV_AID_LOST,
    JOV_EV_FRIENDLY_FIRE,
    JOV_EV_PLAYER_HIT,
    JOV_EV_AID_SIGHTED,
    JOV_EV_AID_LOCKED
} JovEventType;

typedef struct {
    JovEventType type;
    /* A copy, not a reference -- see the header comment. */
    int     contact_id;
    JovKind contact_kind;
    float   x, y, z;
} JovEvent;

/* -- The simulation ---------------------------------------------------- */
typedef struct {
    JovPlayer player;
    JovRail   rail;

    JovContact  contacts[MAX_CONTACTS];   int n_contacts;
    JovShot     shots[MAX_SHOTS];         int n_shots;
    JovParticle particles[MAX_PARTICLES]; int n_particles;
    JovPopup    popups[MAX_POPUPS];       int n_popups;

    JovEvent events[MAX_EVENTS];
    int      n_events;
    /* Set when an event was raised with the queue full. Nothing here drops a
       rule -- the contact still died, the score is still owed -- but a dropped
       event is an unpaid one, so the suite asserts this stays zero rather than
       leaving it to be noticed as a scoreboard that is quietly light. */
    int      events_dropped;

    float spawn_timer;
    int   next_id;
    int   waves_spawned;
    /* Waves refused because the contact array was full. Zero in every run the
       suite flies; a non-zero value means MAX_CONTACTS is undersized. */
    int   waves_dropped;

    float frame;        /* fractional: advanced by dt, not by 1 */
    JovRng rng;
} JovSim;

void jov_sim_reset(JovSim *s, unsigned int seed);

/* One frame. Runs the ship, the rail and the contacts in the browser's order
 * -- difficulty is read from the distance BEFORE the rail advances, which is
 * a one-frame lag the web version has and the port keeps rather than quietly
 * improving on. */
void jov_sim_update(JovSim *s, float axis_x, float axis_y, int wants_fire,
                    float dt);

/* Half a frame each. Public as testing seams: the web suite calls
 * entities.updateContacts() and entities.updateShots() directly to isolate a
 * rule from everything else moving, and a port that could not do the same
 * would be a port of the rules without a port of the checks. Game code should
 * call jov_sim_update(). */
void jov_sim_update_contacts(JovSim *s, float rail_speed, float dt);
void jov_sim_update_shots(JovSim *s, float dt);

/* Release a wave. Public because the spawner has properties worth asserting
 * on their own -- the separation guarantee, and the scripted opening. */
void jov_sim_spawn_wave(JovSim *s, float t);

/* n lateral spawn positions, every pair at least SPAWN_MIN_SEPARATION apart.
 *
 * Rejection sampling was the obvious way and it does not work: three contacts
 * 62 units apart across a 300-unit rail fails often enough that a bounded
 * retry loop regularly gave up and stacked a convoy on a hostile -- precisely
 * the frame where telling them apart has to be possible. This cannot fail:
 * reserve the gaps first, draw n uniform values from the slack that remains,
 * sort them, and hand the reserved gap back as you go. */
void jov_spread_x(int n, JovRng *rng, float *out);

void jov_sim_fire(JovSim *s);

/* The box a shot must pass through to hit this contact. One place, so
 * collision and anything that wants to draw or assert it cannot disagree. */
void jov_hit_box(const JovContact *c, float *half_w, float *half_h);

/* Is this contact's transponder lit on the given frame? A double-tap rather
 * than a single pulse, because one blink at 2Hz is easy to mistake for a
 * rendering artefact among moving sprites, and two is not. Hostiles never
 * light -- the absence is the signal. */
int jov_beacon_lit(const JovContact *c, float frame);

/* Fills `out` with contact indices ordered far to near, so nearer contacts
 * draw over what is behind them and the HUD strip reads in depth order.
 * Returns the count. `out` must hold MAX_CONTACTS. */
int jov_sim_order_by_depth(const JovSim *s, int *out);

/* Live contact with this id, or NULL. */
const JovContact *jov_sim_find(const JovSim *s, int id);

void jov_sim_add_popup(JovSim *s, float x, float y, float z,
                       const char *text, unsigned int color);

/* -- The bot -----------------------------------------------------------
 *
 * Steers at the nearest hostile inside firing range and shoots it, and holds
 * fire whenever a convoy is in the line. Writes an axis and a trigger; reads
 * nothing but the simulation. Stateless, so the caller owns how often it is
 * asked -- which is the bot's reaction time, and the only thing separating a
 * demo that looks played from one that looks solved.
 *
 * It lives here, in the engine-free half, for two reasons. It is what the
 * ATTRACT MODE flies, so it is shipped code and not a test fixture. And being
 * here it can be flown by `make test`, which turns "the bot never shoots a
 * convoy" into an assertion -- and that is a claim about the GAME, not about
 * the bot: if something that can read the transponder straight off the struct
 * still cannot avoid convoys, the rules are not fair and no human will manage
 * it either.
 *
 * What it cannot tell you is whether a person could. It never mistakes a
 * convoy for a hostile because it never has to look.
 */
void jov_bot(const JovSim *s, float *ax, float *ay, int *fire);

/* -- Scoring policy ----------------------------------------------------
 * What a thing is worth, and nothing about how it sounds.
 *
 * The asymmetry is the game: a kill is worth 100 and a rescue 500, but
 * shooting the thing you were sent to protect costs 1000 and one of three
 * strikes. Restraint is a losing condition, not a scoring preference.
 */
typedef struct {
    int   score;
    int   kills, escorted, lost, strikes;
    int   combo;
    float combo_timer;
} JovRun;

/* Cues for main.c: sound and screen shake, raised by jov_run_resolve() and
 * consumed by the frame that asked for them. Counts rather than flags, so two
 * kills in a frame are not silently one. */
typedef struct {
    int shoot, explode, escort, lost, friendly_fire, player_hit, ping, lock;
    float shake;
    float flash;
    unsigned int flash_color;
} JovCue;

void jov_run_reset(JovRun *r);

/* Drains the simulation's events, prices them, applies the player's hit, adds
 * the popups, and fills `cue`. Returns nothing: everything it decided is in
 * `run` and `cue`. */
void jov_run_resolve(JovRun *run, JovSim *sim, JovCue *cue);

/* Combo lapse. Separate from resolve because it is a clock, not an event. */
void jov_run_tick(JovRun *run, float dt);

/* Three strikes ends the run whatever the score says, and so does running out
 * of ships. */
int jov_run_over(const JovRun *run, const JovPlayer *p);

/* A rank from how the convoys fared, not from the score. */
const char *jov_run_rank(const JovRun *run);

#endif
