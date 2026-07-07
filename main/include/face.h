#pragma once
#include "gfx.h"

// HAL 9000's lens: a single, unblinking, concentric-ringed eye set in a
// recessed panel. No mouth, no rotation, almost no idle motion - the
// calm stillness is the point.
typedef struct {
    int dx, dy;   // rare glitch shove only - HAL does not fidget
    int pulse;     // very small lens dilation (breathing)
    bool spark;    // brief brightening of the lens glint
} face_pose_t;

// Clears the buffer and draws the panel and lens for the given pose.
void face_draw(gfx_t *g, const face_pose_t *pose);
