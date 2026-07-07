#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Monochrome framebuffer matching SSD1306 GDDRAM layout: one byte per
// 8-pixel-tall column strip ("page"), bit 0 = top pixel of the page.
// buf[page * width + x], page = y / 8.
typedef struct {
    uint8_t *buf;
    int width;
    int height;
    int pages; // height / 8
} gfx_t;

void gfx_init(gfx_t *g, uint8_t *buf, int width, int height);
void gfx_clear(gfx_t *g);
void gfx_fill(gfx_t *g);

void gfx_set_pixel(gfx_t *g, int x, int y, bool on);
bool gfx_get_pixel(const gfx_t *g, int x, int y);
void gfx_xor_pixel(gfx_t *g, int x, int y);

void gfx_hline(gfx_t *g, int x0, int x1, int y, bool on);
void gfx_vline(gfx_t *g, int x, int y0, int y1, bool on);
void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, bool on);
void gfx_rect(gfx_t *g, int x0, int y0, int x1, int y1, bool on);
void gfx_fill_rect(gfx_t *g, int x0, int y0, int x1, int y1, bool on);

// Draws one 8x8 glyph from the built-in font (printable ASCII 0x20-0x7E).
// XORed onto the buffer so it composites over any background.
void gfx_draw_glyph(gfx_t *g, int x, int y, char c);
void gfx_draw_text(gfx_t *g, int x, int y, const char *s);

// Invert every pixel in a rectangular region (clipped to the buffer).
void gfx_invert_rect(gfx_t *g, int x0, int y0, int x1, int y1);

// Shift a single pixel row left/right by `dx` pixels, wrapping around.
void gfx_shift_row(gfx_t *g, int y, int dx);

// Overwrite one row's worth of a page-byte directly (used by glitch noise).
void gfx_set_row_byte(gfx_t *g, int page, int x, uint8_t byte_val);
uint8_t gfx_get_row_byte(const gfx_t *g, int page, int x);
