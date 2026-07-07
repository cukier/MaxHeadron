#include "include/face.h"
#include <math.h>

#define TAU 6.28318530f

static inline int rnd(float v) {
    return (int)(v + (v >= 0 ? 0.5f : -0.5f));
}

static void draw_ring(gfx_t *g, int cx, int cy, int r, int steps) {
    for (int k = 0; k < steps; k++) {
        float a = (float)k * (TAU / steps);
        gfx_set_pixel(g, cx + rnd(cosf(a) * r), cy + rnd(sinf(a) * r), true);
    }
}

// The recessed panel HAL's lens sits in - a plain rectangular bezel with
// a little inset, nothing more.
static void draw_panel(gfx_t *g) {
    gfx_rect(g, 2, 1, g->width - 3, g->height - 2, true);
    gfx_rect(g, 5, 4, g->width - 6, g->height - 5, true);
}

void face_draw(gfx_t *g, const face_pose_t *pose) {
    gfx_clear(g);
    draw_panel(g);

    float s = (float)g->height / 64.0f;
    int cx = g->width / 2 + pose->dx;
    int cy = g->height / 2 + pose->dy;

    int r1 = (int)(4 * s) + pose->pulse;
    int r2 = (int)(10 * s) + pose->pulse;
    int r3 = (int)(16 * s) + pose->pulse;
    int r4 = (int)(22 * s) + pose->pulse;
    if (r1 < 2) {
        r1 = 2;
    }

    int max_r = (g->width / 2 < g->height / 2 ? g->width / 2 : g->height / 2) - 6;
    if (r4 > max_r) {
        r4 = max_r;
    }
    if (r3 > r4 - 2) {
        r3 = r4 - 2;
    }
    if (r2 > r3 - 2) {
        r2 = r3 - 2;
    }
    if (r1 > r2 - 2) {
        r1 = r2 - 2;
    }

    draw_ring(g, cx, cy, r4, 40);
    draw_ring(g, cx, cy, r3, 32);
    draw_ring(g, cx, cy, r2, 24);
    gfx_fill_rect(g, cx - r1, cy - r1, cx + r1, cy + r1, true);

    // A single glassy highlight, offset up and to the left - the one
    // asymmetry on an otherwise perfectly calm, centered eye.
    int gx = cx - (int)(r2 * 0.55f);
    int gy = cy - (int)(r2 * 0.55f);
    gfx_fill_rect(g, gx - 1, gy - 1, gx + 1, gy + 1, true);
    if (pose->spark) {
        gfx_fill_rect(g, gx - 2, gy - 2, gx + 2, gy + 2, true);
    }
}
