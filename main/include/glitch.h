#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "gfx.h"

typedef enum {
    GLITCH_NONE = 0,
    GLITCH_TEAR,
    GLITCH_NOISE,
    GLITCH_INVERT,
    GLITCH_SHOVE,
} glitch_kind_t;

typedef struct {
    uint32_t next_burst_at;
    uint32_t burst_until;
    glitch_kind_t kind;
    int freeze_frames;
    int shove_dx, shove_dy, shove_shear;

    bool ticker_active;
    uint32_t next_ticker_at;
    uint32_t ticker_until;
    int ticker_phrase;

    int scanline_y;
    uint32_t next_scanline_move;
} glitch_state_t;

void glitch_init(glitch_state_t *gs, uint32_t now_ms);

// Advances the glitch state machine. Call exactly once per frame, before
// drawing, using a free-running millisecond clock.
void glitch_update(glitch_state_t *gs, uint32_t now_ms);

// True while a "freeze" is in effect: the caller should keep showing the
// previously rendered frame instead of drawing (and pushing) a new one.
bool glitch_is_frozen(const glitch_state_t *gs);

// Adds the current glitch "shove" (if any) on top of the base pose offsets.
void glitch_pose_shove(const glitch_state_t *gs, int *dx, int *dy, int *shear);

// Post-processing pixel effects (tearing, noise, invert flash, ambient
// scanline roll). Call after face_draw() has rendered the frame.
void glitch_apply_post(const glitch_state_t *gs, gfx_t *g);

// Draws the stuttering text ticker, if one is currently active.
void glitch_apply_ticker(const glitch_state_t *gs, gfx_t *g);
