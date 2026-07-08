#include "sdkconfig.h" // must come before the #if below - nothing else here pulls it in
#include "include/face.h"
#include <math.h>

#if CONFIG_MAXHEADRON_FACE_HAL_EYE

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

#elif CONFIG_MAXHEADRON_FACE_STARBURST

#define TAU 6.28318530f

// Sparse scattered pixels behind the starburst - reads as signal
// noise/static rather than a deliberate pattern. A tiny xorshift PRNG
// seeded from `seed` (the caller's free-running frame counter) keeps this
// self-contained (no esp_random dependency) while still looking different
// every frame.
static void draw_static(gfx_t *g, uint32_t seed) {
    uint32_t x = seed * 2654435761u + 1u; // avoid a zero/degenerate seed
    const int dot_count = (g->width * g->height) / 40;
    for (int i = 0; i < dot_count; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        int px = (int)(x % (uint32_t)g->width);
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        int py = (int)(x % (uint32_t)g->height);
        gfx_set_pixel(g, px, py, true);
    }
}

static inline int rnd(float v) {
    return (int)(v + (v >= 0 ? 0.5f : -0.5f));
}

// Eight rays alternating long/short, rotating around the iris - the closest
// thing to a "face" here: not features, just something that orients and
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

// A burst of particles flying outward from (cx, cy) - each one a short
// radial streak (tail trailing behind the leading point) rather than a
// bare dot, so it reads as motion rather than scattered noise. `t` is the
// burst's progress from 0 (just fired) to 1 (fully spent, particles at
// max_r); negative skips drawing entirely.
static void draw_explosion(gfx_t *g, int cx, int cy, int max_r, float t, uint32_t seed) {
    if (t < 0.0f) {
        return;
    }
    const int n = 14;
    float dist = t * (float)max_r;
    float tail = dist - (float)max_r * 0.2f;
    if (tail < 0.0f) {
        tail = 0.0f;
    }
    uint32_t x = seed * 2654435761u + 1u;
    for (int i = 0; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        float a = (float)(x % 100000u) * (TAU / 100000.0f);
        int x0 = cx + rnd(cosf(a) * tail);
        int y0 = cy + rnd(sinf(a) * tail);
        int x1 = cx + rnd(cosf(a) * dist);
        int y1 = cy + rnd(sinf(a) * dist);
        gfx_line(g, x0, y0, x1, y1, true);
    }
}

void face_draw(gfx_t *g, const face_pose_t *pose) {
    gfx_clear(g);
    draw_static(g, (uint32_t)pose->grid_scroll);

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

    int boom_r = (int)sqrtf((float)(base_cx * base_cx + base_cy * base_cy)) + 2;
    draw_explosion(g, cx, cy, boom_r, pose->explode_t, pose->explode_seed);
}

#else // Max Headroom bust (default)

// Base geometry is tuned for a 128x64 panel; vertical sizes scale down
// automatically for a 128x32 panel via `s`.
static void draw_grid(gfx_t *g, int scroll) {
    int phase = ((scroll % 8) + 8) % 8;
    for (int y = phase; y < g->height; y += 8) {
        gfx_hline(g, 0, g->width - 1, y, true);
    }
    for (int x = 8; x < g->width; x += 16) {
        gfx_vline(g, x, 0, g->height - 1, true);
    }
}

static void fill_ellipse_off(gfx_t *g, int cx, int cy, int rx, int ry) {
    for (int y = cy - ry; y <= cy + ry; y++) {
        float dy = (float)(y - cy) / (float)ry;
        float inside = 1.0f - dy * dy;
        if (inside < 0) {
            continue;
        }
        int hw = (int)(rx * sqrtf(inside));
        gfx_fill_rect(g, cx - hw, y, cx + hw, y, false);
        gfx_set_pixel(g, cx - hw, y, true);
        gfx_set_pixel(g, cx + hw, y, true);
    }
}

static void draw_hair(gfx_t *g, int cx, int top_y, int rx) {
    static const int8_t dx_pts[] = {-16, -11, -6, -1, 4, 9, 14, 17};
    static const int8_t dy_pts[] = {2, -7, -2, -9, -3, -8, -1, 3};
    const int n = sizeof(dx_pts);
    for (int i = 0; i + 1 < n; i++) {
        gfx_line(g, cx + dx_pts[i], top_y + dy_pts[i],
                  cx + dx_pts[i + 1], top_y + dy_pts[i + 1], true);
    }
    (void)rx;
}

static void draw_shoulders(gfx_t *g, int cx, int dy, float s) {
    int top = (int)(42 * s) + dy;
    int bottom = g->height - 1;
    int half_top = (int)(7 * s);
    int half_bottom = (int)(46 * s);
    if (half_bottom > 62) {
        half_bottom = 62;
    }
    // Erase the grid under the shoulders, then trace the outline.
    for (int y = top; y <= bottom; y++) {
        if (bottom == top) {
            break;
        }
        float t = (float)(y - top) / (float)(bottom - top);
        int hw = half_top + (int)((half_bottom - half_top) * t);
        gfx_fill_rect(g, cx - hw, y, cx + hw, y, false);
    }
    gfx_line(g, cx - half_top, top, cx - half_bottom, bottom, true);
    gfx_line(g, cx + half_top, top, cx + half_bottom, bottom, true);
    gfx_hline(g, cx - half_top, cx + half_top, top, true);

    // Collar V and tie.
    gfx_line(g, cx - half_top, top, cx, top + (int)(6 * s), true);
    gfx_line(g, cx + half_top, top, cx, top + (int)(6 * s), true);
    gfx_vline(g, cx, top + (int)(6 * s), bottom, true);
}

void face_draw(gfx_t *g, const face_pose_t *pose) {
    gfx_clear(g);
    draw_grid(g, pose->grid_scroll);

    float s = (float)g->height / 64.0f;
    int cx = g->width / 2;

    int head_cy = (int)(24 * s) + pose->dy;
    int rx = (int)(16 * s);
    if (rx < 8) {
        rx = 8;
    }
    int ry = (int)(14 * s);
    if (ry < 6) {
        ry = 6;
    }
    int headx = cx + pose->dx + pose->shear;

    draw_shoulders(g, cx + pose->dx, pose->dy, s);

    // Neck, drawn before the head so the head silhouette sits cleanly on top.
    int neck_top = head_cy + ry - (int)(2 * s);
    int neck_bottom = (int)(42 * s) + pose->dy;
    int neck_half_w = (int)(6 * s);
    if (neck_bottom > neck_top) {
        gfx_fill_rect(g, headx - neck_half_w, neck_top, headx + neck_half_w, neck_bottom, false);
        gfx_vline(g, headx - neck_half_w, neck_top, neck_bottom, true);
        gfx_vline(g, headx + neck_half_w, neck_top, neck_bottom, true);
    }

    fill_ellipse_off(g, headx, head_cy, rx, ry);
    draw_hair(g, headx, head_cy - ry, rx);

    // Visor / sunglasses - the single most Max Headroom feature on this face.
    int visor_h = (int)(6 * s);
    if (visor_h < 3) {
        visor_h = 3;
    }
    int visor_y0 = head_cy - (int)(4 * s);
    int visor_y1 = visor_y0 + visor_h;
    int visor_w = rx - 2;
    gfx_fill_rect(g, headx - visor_w, visor_y0, headx + visor_w, visor_y1, true);
    int lens_w = visor_w / 2;
    gfx_fill_rect(g, headx - visor_w + 2, visor_y0 + 1, headx - lens_w + 1, visor_y1 - 1, false);
    gfx_fill_rect(g, headx + lens_w - 1, visor_y0 + 1, headx + visor_w - 2, visor_y1 - 1, false);
    if (pose->lens_glint) {
        gfx_line(g, headx - visor_w, visor_y0, headx - lens_w + 1, visor_y1, true);
        gfx_line(g, headx + lens_w - 1, visor_y0, headx + visor_w, visor_y1, true);
    }

    // Nose.
    gfx_vline(g, headx, visor_y1 + 1, visor_y1 + (int)(3 * s) + 1, true);

    // Mouth - height driven by the talking animation.
    int mouth_y = visor_y1 + (int)(6 * s);
    int mouth_half_w = (int)(5 * s);
    if (mouth_half_w < 4) {
        mouth_half_w = 4;
    }
    int mh = pose->mouth_open;
    if (mh < 0) {
        mh = 0;
    }
    if (mh > 4) {
        mh = 4;
    }
    gfx_fill_rect(g, headx - mouth_half_w, mouth_y - mh, headx + mouth_half_w, mouth_y + mh, false);
    gfx_hline(g, headx - mouth_half_w, headx + mouth_half_w, mouth_y - mh, true);
    gfx_hline(g, headx - mouth_half_w, headx + mouth_half_w, mouth_y + mh, true);
}

#endif // MAXHEADRON_FACE choice
