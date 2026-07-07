#include "include/gfx.h"
#include "include/font8x8_basic.h"
#include <string.h>

void gfx_init(gfx_t *g, uint8_t *buf, int width, int height) {
    g->buf = buf;
    g->width = width;
    g->height = height;
    g->pages = height / 8;
}

void gfx_clear(gfx_t *g) {
    memset(g->buf, 0x00, (size_t)(g->width * g->pages));
}

void gfx_fill(gfx_t *g) {
    memset(g->buf, 0xFF, (size_t)(g->width * g->pages));
}

static inline bool in_bounds(const gfx_t *g, int x, int y) {
    return x >= 0 && x < g->width && y >= 0 && y < g->height;
}

void gfx_set_pixel(gfx_t *g, int x, int y, bool on) {
    if (!in_bounds(g, x, y)) {
        return;
    }
    uint8_t *cell = &g->buf[(y >> 3) * g->width + x];
    uint8_t mask = (uint8_t)(1u << (y & 7));
    if (on) {
        *cell |= mask;
    } else {
        *cell &= (uint8_t)~mask;
    }
}

bool gfx_get_pixel(const gfx_t *g, int x, int y) {
    if (!in_bounds(g, x, y)) {
        return false;
    }
    uint8_t cell = g->buf[(y >> 3) * g->width + x];
    return (cell & (uint8_t)(1u << (y & 7))) != 0;
}

void gfx_xor_pixel(gfx_t *g, int x, int y) {
    if (!in_bounds(g, x, y)) {
        return;
    }
    uint8_t *cell = &g->buf[(y >> 3) * g->width + x];
    *cell ^= (uint8_t)(1u << (y & 7));
}

void gfx_hline(gfx_t *g, int x0, int x1, int y, bool on) {
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    for (int x = x0; x <= x1; x++) {
        gfx_set_pixel(g, x, y, on);
    }
}

void gfx_vline(gfx_t *g, int x, int y0, int y1, bool on) {
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    for (int y = y0; y <= y1; y++) {
        gfx_set_pixel(g, x, y, on);
    }
}

void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, bool on) {
    int dx = x1 - x0 < 0 ? x0 - x1 : x1 - x0;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 - y0 < 0 ? y0 - y1 : y1 - y0;
    dy = -dy;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        gfx_set_pixel(g, x0, y0, on);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_rect(gfx_t *g, int x0, int y0, int x1, int y1, bool on) {
    gfx_hline(g, x0, x1, y0, on);
    gfx_hline(g, x0, x1, y1, on);
    gfx_vline(g, x0, y0, y1, on);
    gfx_vline(g, x1, y0, y1, on);
}

void gfx_fill_rect(gfx_t *g, int x0, int y0, int x1, int y1, bool on) {
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    for (int y = y0; y <= y1; y++) {
        gfx_hline(g, x0, x1, y, on);
    }
}

void gfx_draw_glyph(gfx_t *g, int x, int y, char c) {
    uint8_t code = (uint8_t)c;
    if (code < 0x20 || code > 0x7E) {
        code = 0x20;
    }
    const uint8_t *glyph = font8x8_basic_tr[code];
    for (int col = 0; col < 8; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 8; row++) {
            if (bits & (1u << row)) {
                gfx_xor_pixel(g, x + col, y + row);
            }
        }
    }
}

void gfx_draw_text(gfx_t *g, int x, int y, const char *s) {
    int cx = x;
    while (*s) {
        gfx_draw_glyph(g, cx, y, *s);
        cx += 8;
        s++;
    }
}

void gfx_invert_rect(gfx_t *g, int x0, int y0, int x1, int y1) {
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            gfx_xor_pixel(g, x, y);
        }
    }
}

void gfx_shift_row(gfx_t *g, int y, int dx) {
    if (y < 0 || y >= g->height || dx == 0) {
        return;
    }
    dx %= g->width;
    if (dx < 0) {
        dx += g->width;
    }
    bool tmp[256]; // width is always <= 128 for supported panels
    for (int x = 0; x < g->width; x++) {
        tmp[x] = gfx_get_pixel(g, x, y);
    }
    for (int x = 0; x < g->width; x++) {
        gfx_set_pixel(g, (x + dx) % g->width, y, tmp[x]);
    }
}

void gfx_set_row_byte(gfx_t *g, int page, int x, uint8_t byte_val) {
    if (page < 0 || page >= g->pages || x < 0 || x >= g->width) {
        return;
    }
    g->buf[page * g->width + x] = byte_val;
}

uint8_t gfx_get_row_byte(const gfx_t *g, int page, int x) {
    if (page < 0 || page >= g->pages || x < 0 || x >= g->width) {
        return 0;
    }
    return g->buf[page * g->width + x];
}
