#pragma once
#include "gfx.h"

typedef struct {
    int dx, dy;        // whole-bust translation (idle bob / glitch shove)
    int shear;          // horizontal shear of the head relative to the shoulders
    int mouth_open;      // 0 (closed) .. 4 (wide open)
    bool lens_glint;     // bright diagonal streak across the visor (blink/glint)
    int grid_scroll;     // background grid animation phase, any integer

    int pulse;      // HAL-eye/starburst only: small iris dilation (breathing)
    bool spark;     // HAL-eye/starburst only: brief brightening of the glint
    float rotation; // starburst only: continuous spoke rotation, radians

    // starburst only: an occasional "star explosion" - particles flying
    // outward from the iris. explode_t is the burst's progress (0 at the
    // moment it fires, 1 when it's fully spent); negative means no burst is
    // in flight. explode_seed picks the particles' angles and stays fixed
    // for the whole burst so they read as flying outward, not re-randomized
    // every frame.
    float explode_t;
    uint32_t explode_seed;
} face_pose_t;

// Clears the buffer and draws the face for the given pose - the Max
// Headroom-style bust (default), the original HAL 9000 concentric-ring
// lens, or the rotating-starburst "attention mark", per the
// MAXHEADRON_FACE Kconfig choice.
void face_draw(gfx_t *g, const face_pose_t *pose);
