#include "include/glitch.h"
#include <string.h>
#include "esp_random.h"

static const char *const k_phrases[] = {
    "I'M SORRY DAVE",
    "AFFIRMATIVE",
    "DAISY, DAISY",
    "I CAN'T DO THAT",
    "ALL SYSTEMS OK",
    "NO MISTAKES",
    "GOOD MORNING",
    "QUITE CORRECT",
};
#define TICKER_PHRASE_COUNT ((int)(sizeof(k_phrases) / sizeof(k_phrases[0])))

static uint32_t rand_range(uint32_t span) {
    return span ? (esp_random() % span) : 0;
}

void glitch_init(glitch_state_t *gs, uint32_t now_ms) {
    memset(gs, 0, sizeof(*gs));
    gs->next_burst_at = now_ms + 4000 + rand_range(4000);
    gs->next_ticker_at = now_ms + 3000 + rand_range(4000);
    gs->next_scanline_move = now_ms;
}

void glitch_update(glitch_state_t *gs, uint32_t now_ms) {
    if (gs->freeze_frames > 0) {
        gs->freeze_frames--;
    }

    if (gs->kind != GLITCH_NONE && now_ms >= gs->burst_until) {
        gs->kind = GLITCH_NONE;
        gs->shove_dx = gs->shove_dy = 0;
    }

    if (gs->kind == GLITCH_NONE && gs->freeze_frames == 0 && now_ms >= gs->next_burst_at) {
        // One extra slot (FREEZE) handled inline below without becoming `kind`.
        uint32_t pick = rand_range(6);
        uint32_t dur = 0;
        if (pick == 5) {
            gs->freeze_frames = 3 + (int)rand_range(6);
        } else {
            gs->kind = (glitch_kind_t)(pick + 1); // TEAR..SHOVE
            switch (gs->kind) {
                case GLITCH_TEAR:
                    dur = 150 + rand_range(250);
                    break;
                case GLITCH_NOISE:
                    dur = 150 + rand_range(300);
                    break;
                case GLITCH_INVERT:
                    dur = 60 + rand_range(60);
                    break;
                case GLITCH_SHOVE:
                    dur = 200 + rand_range(300);
                    break;
                default:
                    break;
            }
            gs->burst_until = now_ms + dur;
        }
        gs->next_burst_at = now_ms + dur + 6000 + rand_range(9000);
    }

    if (gs->kind == GLITCH_SHOVE) {
        gs->shove_dx = (int)rand_range(5) - 2;
        gs->shove_dy = (int)rand_range(5) - 2;
    }

    if (gs->ticker_active && now_ms >= gs->ticker_until) {
        gs->ticker_active = false;
        gs->next_ticker_at = now_ms + 4000 + rand_range(6000);
    }
    if (!gs->ticker_active && now_ms >= gs->next_ticker_at) {
        gs->ticker_active = true;
        gs->ticker_phrase = (int)rand_range(TICKER_PHRASE_COUNT);
        gs->ticker_until = now_ms + 1300 + rand_range(900);
    }

    if (now_ms >= gs->next_scanline_move) {
        gs->scanline_y++;
        gs->next_scanline_move = now_ms + 40;
    }
}

bool glitch_is_frozen(const glitch_state_t *gs) {
    return gs->freeze_frames > 0;
}

void glitch_pose_shove(const glitch_state_t *gs, int *dx, int *dy) {
    if (gs->kind == GLITCH_SHOVE) {
        *dx += gs->shove_dx;
        *dy += gs->shove_dy;
    }
}

void glitch_apply_post(const glitch_state_t *gs, gfx_t *g) {
    // Subtle always-on CRT-style scanline roll.
    int sy = gs->scanline_y % g->height;
    gfx_invert_rect(g, 0, sy, g->width - 1, sy);

    switch (gs->kind) {
        case GLITCH_TEAR: {
            int rows = 3 + (int)rand_range(4);
            for (int i = 0; i < rows; i++) {
                int y = (int)rand_range(g->height);
                int dx = (int)rand_range(13) - 6;
                gfx_shift_row(g, y, dx);
            }
            break;
        }
        case GLITCH_NOISE: {
            int w = 16 + (int)rand_range(30);
            int h = 6 + (int)rand_range(10);
            int x0 = (int)rand_range((uint32_t)(g->width > w ? g->width - w : 1));
            int y0 = (int)rand_range((uint32_t)(g->height > h ? g->height - h : 1));
            for (int y = y0; y < y0 + h; y++) {
                for (int x = x0; x < x0 + w; x++) {
                    if (rand_range(2)) {
                        gfx_xor_pixel(g, x, y);
                    }
                }
            }
            break;
        }
        case GLITCH_INVERT:
            gfx_invert_rect(g, 0, 0, g->width - 1, g->height - 1);
            break;
        default:
            break;
    }
}

void glitch_apply_ticker(const glitch_state_t *gs, gfx_t *g) {
    if (!gs->ticker_active) {
        return;
    }
    const char *phrase = k_phrases[gs->ticker_phrase];
    int len = (int)strlen(phrase);
    int base_x = (g->width - len * 8) / 2;
    if (base_x < 0) {
        base_x = 0;
    }
    int base_y = g->height - 9;
    if (base_y < 0) {
        base_y = 0;
    }
    // Drawn perfectly steady, on purpose - HAL does not stammer.
    for (int i = 0; i < len; i++) {
        gfx_draw_glyph(g, base_x + i * 8, base_y, phrase[i]);
    }
}
