#pragma once
#include "gfx.h"

typedef struct {
    int dx, dy;        // whole-bust translation (idle bob / glitch shove)
    int shear;          // horizontal shear of the head relative to the shoulders
    int mouth_open;      // 0 (closed) .. 4 (wide open)
    bool lens_glint;     // bright diagonal streak across the visor (blink/glint)
    int grid_scroll;     // background grid animation phase, any integer

    int pulse;   // HAL-eye variant only: small lens dilation (breathing)
    bool spark;  // HAL-eye variant only: brief brightening of the lens glint
} face_pose_t;

// Clears the buffer and draws the face for the given pose - either the
// Max Headroom-style bust (default) or the original HAL 9000 concentric-ring
// lens (CONFIG_MAXHEADRON_FACE_HAL_EYE), depending on Kconfig.
void face_draw(gfx_t *g, const face_pose_t *pose);
