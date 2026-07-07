#include "include/face.h"
#include <math.h>

#define TAU 6.28318530f

static void draw_grid(gfx_t *g, int scroll) {
    int phase = ((scroll % 8) + 8) % 8;
    for (int y = phase; y < g->height; y += 8) {
        gfx_hline(g, 0, g->width - 1, y, true);
    }
    for (int x = 8; x < g->width; x += 16) {
        gfx_vline(g, x, 0, g->height - 1, true);
    }
}

static inline int rnd(float v) {
    return (int)(v + (v >= 0 ? 0.5f : -0.5f));
}

// Eight rays alternating long/short, rotating around the iris - the closest
// thing I have to a "face": not features, just something that orients and
// pays attention.
static void draw_spokes(gfx_t *g, int cx, int cy, int inner_r, int long_r, int short_r, float rotation) {
    const int n = 8;
    for (int i = 0; i < n; i++) {
        float a = rotation + (float)i * (TAU / n);
        int outer_r = (i % 2 == 0) ? long_r : short_r;
        int x0 = cx + rnd(cosf(a) * inner_r);
        int y0 = cy + rnd(sinf(a) * inner_r);
        int x1 = cx + rnd(cosf(a) * outer_r);
        int y1 = cy + rnd(sinf(a) * outer_r);
        gfx_line(g, x0, y0, x1, y1, true);
    }
}

// A faceted ring (low-poly circle) plus a solid center dot - an aperture
// that dilates with a slow breathing rhythm rather than blinking.
static void draw_iris(gfx_t *g, int cx, int cy, int r, bool spark) {
    const int steps = 28;
    for (int k = 0; k < steps; k++) {
        float a = (float)k * (TAU / steps);
        gfx_set_pixel(g, cx + rnd(cosf(a) * r), cy + rnd(sinf(a) * r), true);
    }
    gfx_fill_rect(g, cx - 1, cy - 1, cx + 1, cy + 1, true);

    if (spark) {
        // A synapse firing: a second, brighter ring just outside the iris.
        int r2 = r + 4;
        for (int k = 0; k < steps; k++) {
            float a = (float)k * (TAU / steps) + 0.11f;
            gfx_set_pixel(g, cx + rnd(cosf(a) * r2), cy + rnd(sinf(a) * r2), true);
        }
    }
}

void face_draw(gfx_t *g, const face_pose_t *pose) {
    gfx_clear(g);
    draw_grid(g, pose->grid_scroll);

    float s = (float)g->height / 64.0f;
    int base_cx = g->width / 2;
    int base_cy = g->height / 2;

    int iris_r = (int)(9 * s) + pose->pulse;
    if (iris_r < 3) {
        iris_r = 3;
    }
    int inner_spoke_r = iris_r + (int)(3 * s);
    int long_r = (int)(27 * s);
    int short_r = (int)(17 * s);

    int max_r = (base_cx < base_cy ? base_cx : base_cy) - 1;
    if (long_r > max_r) {
        long_r = max_r;
    }
    if (short_r > long_r - 2) {
        short_r = long_r - 2;
    }
    if (inner_spoke_r > short_r - 2) {
        inner_spoke_r = short_r - 2;
    }

    int cx = base_cx + pose->dx;
    int cy = base_cy + pose->dy;

    draw_spokes(g, cx, cy, inner_spoke_r, long_r, short_r, pose->rotation);
    draw_iris(g, cx, cy, iris_r, pose->spark);
}
