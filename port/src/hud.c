#include "hud.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "render.h"
#include "res.h"
#include "unported.h"

void hud_message(int frames, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_state.message, sizeof g_state.message, fmt, ap);
    va_end(ap);
    g_state.message_time = frames;
}

#define FONT 0x34e

/* 0x31442 (sprintf), for the message table's formats. */
static void text_vformat(char *out, size_t n, const char *fmt, va_list ap)
{
    vsnprintf(out, n, fmt, ap);
}

static int32_t glyph_w(int font, unsigned char c)
{
    int b = c + font - 0x20;
    return b >= 0 && b < RES_BANKS ? g_banks[b].view[0].w : 0;
}

/* 0x29cf8: width of a proportional string (24.8). */
int32_t text_width(int font, int32_t spacing, int32_t space, const char *s)
{
    int32_t w = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == 9) w = ((w >> 12) + 1) * 0x1000;
        else if (c == ' ') w += space;
        else w += glyph_w(font, c) + spacing;
    }
    return w;
}


/* 0x293e0: draw text at 24.8 world (x, y), one sprite bank per character
 * (font + ch - 0x20). Proportional text steps by glyph width + spacing;
 * fixed text right-aligns each glyph in an `advance`-wide cell. Spaces step
 * by `advance`, tabs to the next 16 px. Centred text is also centred
 * vertically on the height of the font's '!' glyph. */
void text_draw(bool prop, int align, int32_t x, int32_t y, int font, int32_t spacing,
                      int32_t advance, const char *fmt, ...)
{
    char s[256];
    va_list ap;
    va_start(ap, fmt);
    text_vformat(s, sizeof s, fmt, ap);
    va_end(ap);
    int32_t len = (int32_t)strlen(s);
    if (align == ALIGN_CENTRE) {
        x -= prop ? text_width(font, spacing, advance, s) >> 1 : (len * advance) >> 1;
        if (font + 1 < RES_BANKS) y -= g_banks[font + 1].view[0].h >> 1;
    } else if (align == ALIGN_RIGHT) {
        x -= prop ? text_width(font, spacing, advance, s) + 1 : advance * len + 1;
    }
    int32_t pos = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == 9) { pos = ((pos >> 12) + 1) * 0x1000; continue; }
        if (c == ' ') { pos += advance; continue; }
        int b = c + font - 0x20;
        if (prop) {
            render_bank(b, 0, 0, x + pos, y, RENDER_NO_CLIP);
            pos += glyph_w(font, c) + spacing;
        } else {
            render_bank(b, 0, 0, x + pos + advance - glyph_w(font, c), y, RENDER_NO_CLIP);
            pos += advance;
        }
    }
}

/* Status panel (bank 0x67f) and the shield bar (bank 0x67e, clipped to
 * 1-40 px by shield level) in the top-left corner, the score top right,
 * the current message and, while towing, the self-destruct countdown. */
void hud_draw(void)
{
    int32_t cx = render_cam_x(), cy = render_cam_y();
    render_bank(0x67f, 0, 0, cx + 0x300, cy + 0x300, RENDER_NO_CLIP);
    if (SHIELDS != 0 && g_state.shields_max > 0) {
        int32_t w = SHIELDS * 0x28 / g_state.shields_max;
        if (w < 1) w = 1;
        render_bank(0x67e, 0, 0, cx + 0x300, cy + 0x300, cx + 0x300, cy + 0x300,
                    w * 0x100 + 0x200 + cx, cy + 0x700);
    }
    text_draw(false, ALIGN_RIGHT, cx + 0x13e00, cy + 0x100, FONT, -0x100, 0x500, "%d", g_state.score);
    if (g_state.message_time > 0) {
        g_state.message_time--;
        text_draw(true, ALIGN_CENTRE, cx + 0xa000, cy + 0x600, FONT, -0x100, 0x300, "%s", g_state.message);
    }
    /* Self-destruct countdown while towing; flashes in the last 10 steps. */
    int32_t n = g_state.obj[15].o_0;
    if (g_state.stage == 2 && (n > 10 || (g_state.mission_frames >> 4 & 1))) {
        if (n > 1) text_draw(false, ALIGN_LEFT, cx, cy + 0xc00, FONT, -0x100, 0x500, "%3d %s", n, "SECS");
        else text_draw(false, ALIGN_LEFT, cx, cy + 0xc00, FONT, -0x100, 0x500, "  1 %s", "SEC");
    }
}
