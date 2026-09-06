// input.js — The Jovian Humanitarian Conflict | MagmaCrunch Media © 2026
// Keyboard and touch state.
//
// Steering is analogue-ish: axisX/axisY return -1..1 so the same code path
// serves a held arrow key and a dragged thumb, and the ship's acceleration
// does not have to know which one it is reading.

const Input = {
    keys: {},
    justPressed: {},

    // Touch steering, in the same -1..1 space as the keys. null when no drag
    // is active, so a lifted thumb lets the ship coast rather than snapping to
    // centre.
    touchAxis: null,
    touchFiring: false,

    init() {
        document.addEventListener('keydown', (e) => {
            if (e.repeat) return;
            if (!this.keys[e.code]) {
                this.justPressed[e.code] = true;
            }
            this.keys[e.code] = true;
            if (['Space', 'KeyZ', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(e.code)) {
                e.preventDefault();
            }
        });
        document.addEventListener('keyup', (e) => {
            this.keys[e.code] = false;
        });
        // A tabbed-away window never sees the keyup, so a held key would read
        // as down forever — the ship would fly off and stay there.
        window.addEventListener('blur', () => {
            this.keys = {};
            this.justPressed = {};
            this.touchAxis = null;
            this.touchFiring = false;
        });
    },

    /**
     * Touch: drag the left two-thirds to steer, hold the right third to fire.
     *
     * Steering is relative to where the drag started rather than absolute, so
     * the ship does not jump to the thumb the instant it lands — an absolute
     * mapping makes every touch begin with an unintended dodge. The FIRE zone
     * is a separate pointer, which is why this tracks by pointerId instead of
     * assuming one contact.
     */
    initTouch(canvas) {
        const STEER_RADIUS = 46;   // px of drag for full deflection
        let steerId = null;
        let fireId = null;
        let originX = 0;
        let originY = 0;

        const zoneIsFire = (clientX) => {
            const r = canvas.getBoundingClientRect();
            return clientX - r.left > r.width * 0.66;
        };

        canvas.addEventListener('pointerdown', (e) => {
            if (e.pointerType === 'mouse') return;
            e.preventDefault();
            if (zoneIsFire(e.clientX)) {
                fireId = e.pointerId;
                this.touchFiring = true;
            } else if (steerId === null) {
                steerId = e.pointerId;
                originX = e.clientX;
                originY = e.clientY;
                this.touchAxis = { x: 0, y: 0 };
            }
        }, { passive: false });

        canvas.addEventListener('pointermove', (e) => {
            if (e.pointerId !== steerId) return;
            e.preventDefault();
            const clamp = (v) => Math.max(-1, Math.min(1, v / STEER_RADIUS));
            this.touchAxis = {
                x: clamp(e.clientX - originX),
                y: clamp(e.clientY - originY),
            };
        }, { passive: false });

        const release = (e) => {
            if (e.pointerId === steerId) { steerId = null; this.touchAxis = null; }
            if (e.pointerId === fireId)  { fireId = null;  this.touchFiring = false; }
        };
        canvas.addEventListener('pointerup', release);
        canvas.addEventListener('pointercancel', release);
    },

    isDown(code) {
        return !!this.keys[code];
    },

    wasPressed(code) {
        if (this.justPressed[code]) {
            this.justPressed[code] = false;
            return true;
        }
        return false;
    },

    clearJustPressed() {
        this.justPressed = {};
    },

    // ── Axes ──────────────────────────────────────────────────────────
    axisX() {
        if (this.touchAxis) return this.touchAxis.x;
        let v = 0;
        if (this.isDown('ArrowLeft')  || this.isDown('KeyA')) v -= 1;
        if (this.isDown('ArrowRight') || this.isDown('KeyD')) v += 1;
        return v;
    },

    axisY() {
        if (this.touchAxis) return this.touchAxis.y;
        let v = 0;
        if (this.isDown('ArrowUp')   || this.isDown('KeyW')) v -= 1;
        if (this.isDown('ArrowDown') || this.isDown('KeyS')) v += 1;
        return v;
    },

    /**
     * Held, not edge-triggered: the gun has its own cooldown, and making the
     * player mash for a fixed rate of fire only adds noise to a game whose
     * whole decision is whether to shoot at all.
     *
     * A pending press still counts, and is consumed either way. Polling the
     * held state alone drops any tap shorter than one frame, which is not
     * hypothetical — it is exactly what a low frame rate does to a quick jab at
     * the key. Consuming the flag even when the key is also down is what stops
     * a held key leaving a stale press behind to fire a phantom shot on
     * release.
     */
    firing() {
        const tappedZ = this.wasPressed('KeyZ');
        const tappedSpace = this.wasPressed('Space');
        return this.touchFiring || this.isDown('KeyZ') || this.isDown('Space')
            || tappedZ || tappedSpace;
    },

    start() {
        return this.wasPressed('Space') || this.wasPressed('Enter') || this.wasPressed('KeyZ');
    },
    // 'M' (mute) and 'P'/Escape (pause) are handled by main.js's own listener,
    // not here: they must work on the title and game-over screens too, where
    // update() is not running to poll this.
};
