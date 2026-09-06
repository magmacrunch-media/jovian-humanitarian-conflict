// renderer.js — The Jovian Humanitarian Conflict | MagmaCrunch Media © 2026
// Canvas presentation and screen effects. Nothing here knows what a ship is.

const Renderer = {
    /**
     * Size the canvas element so one game pixel covers a whole number of
     * screen pixels wherever there is room for it.
     *
     * The canvas is 480x270 stretched to fill the viewport, so on anything
     * that is not an exact multiple some game pixels land two screen pixels
     * wide and their neighbours one — a shimmer that is mild on a platformer
     * and severe here, because the whole frame is streaming toward the camera
     * at once. Snapping to an integer scale fixes it, but only above 2x:
     * rounding 1.6x down to 1x would shrink the game to a stamp.
     */
    fit(canvas) {
        const fit = Math.min(
            window.innerWidth / CONFIG.CANVAS_W,
            window.innerHeight / CONFIG.CANVAS_H
        );
        const scale = fit >= 2 ? Math.floor(fit) : fit;
        canvas.style.width = Math.round(CONFIG.CANVAS_W * scale) + 'px';
        canvas.style.height = Math.round(CONFIG.CANVAS_H * scale) + 'px';
    },

    bindResize(canvas) {
        this.fit(canvas);
        window.addEventListener('resize', () => this.fit(canvas));
    },

    shake: 0,
    flashAmount: 0,
    flashColor: '255,255,255',

    addShake(amount) {
        this.shake = Math.min(12, this.shake + amount);
    },

    /** A coloured full-frame flash. Used for hits and for friendly fire. */
    addFlash(amount, rgb) {
        this.flashAmount = Math.min(1, this.flashAmount + amount);
        this.flashColor = rgb || '255,255,255';
    },

    decay(dt) {
        if (this.shake > 0) this.shake = Math.max(0, this.shake - 0.55 * dt);
        if (this.flashAmount > 0) this.flashAmount = Math.max(0, this.flashAmount - 0.045 * dt);
    },

    /**
     * Offset to translate the frame by. Alternating sign per frame rather than
     * a random walk, so the shake reads as a rattle and cannot wander off
     * centre and stay there.
     */
    shakeOffset(frame) {
        if (this.shake <= 0) return { x: 0, y: 0 };
        const s = this.shake;
        return {
            x: Math.round((frame % 2 ? s : -s) * 0.6),
            y: Math.round((frame % 4 < 2 ? s : -s) * 0.35),
        };
    },

    /**
     * Soft radial glow. Filling an arc at a flat low alpha does not read as
     * light — it reads as a grey disc with a hard rim.
     */
    glow(ctx, x, y, radius, rgb, alpha) {
        if (radius <= 0) return;
        const g = ctx.createRadialGradient(x, y, 0, x, y, radius);
        g.addColorStop(0, 'rgba(' + rgb + ',' + alpha + ')');
        g.addColorStop(0.5, 'rgba(' + rgb + ',' + (alpha * 0.4).toFixed(3) + ')');
        g.addColorStop(1, 'rgba(' + rgb + ',0)');
        ctx.fillStyle = g;
        ctx.fillRect(x - radius, y - radius, radius * 2, radius * 2);
    },

    vignette(ctx) {
        const W = CONFIG.CANVAS_W;
        const H = CONFIG.CANVAS_H;
        const g = ctx.createRadialGradient(W / 2, H / 2, H * 0.32, W / 2, H / 2, H * 0.82);
        g.addColorStop(0, 'rgba(0,0,0,0)');
        g.addColorStop(1, 'rgba(0,0,0,0.55)');
        ctx.fillStyle = g;
        ctx.fillRect(0, 0, W, H);
    },

    flash(ctx) {
        if (this.flashAmount <= 0) return;
        ctx.fillStyle = 'rgba(' + this.flashColor + ',' + (this.flashAmount * 0.6).toFixed(3) + ')';
        ctx.fillRect(0, 0, CONFIG.CANVAS_W, CONFIG.CANVAS_H);
    },

    reset() {
        this.shake = 0;
        this.flashAmount = 0;
    },
};
