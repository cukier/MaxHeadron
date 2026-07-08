#pragma once
#include "gfx.h"

typedef struct {
    int dx, dy;        // whole-bust translation (idle bob / glitch shove)
    int shear;          // horizontal shear of the head relative to the shoulders
    int mouth_open;      // 0 (closed) .. 4 (wide open)
    bool lens_glint;     // bright diagonal streak across the visor (blink/glint)
    int grid_scroll;     // background grid animation phase, any integer
} face_pose_t;

// Clears the buffer and draws the animated background grid plus the
// Max Headroom-style bust silhouette for the given pose.
void face_draw(gfx_t *g, const face_pose_t *pose);
