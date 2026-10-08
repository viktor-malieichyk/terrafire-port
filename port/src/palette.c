#include "palette.h"

#include "pak.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

Palette g_pal_active = {0}, g_pal_black = {0}, g_pal_white = {0};

static uint8_t s_dac[768];          /* 0x83dc8: colours on screen when the fade began */
static const Palette *s_target;     /* 0x84228 */
static int s_steps;                 /* 0x8422c */
static int s_step;                  /* 0x84238: 0 = idle, else next step number */

void palette_set_dac(int first, int last, const uint8_t *rgb6)
{
    static uint8_t shown[768];
    for (int i = first * 3; i <= last * 3 + 2; i++) {
        uint8_t c = rgb6[i - first * 3] & 0x3f;
        shown[i] = (uint8_t)(c << 2 | c >> 4);
    }
    platform_set_palette(shown);
}

bool palette_read(int entry, Palette *out)
{
    size_t size;
    uint8_t *e = pak_read(entry, &size);
    if (!e) return false;
    bool ok = size >= 2;
    if (ok) {
        int first = e[0], last = e[1];
        ok = first <= last && size == 2 + (size_t)(last - first + 1) * 3;
        if (ok) {
            out->first = first;
            out->last = last;
            memcpy(out->rgb + first * 3, e + 2, (size_t)(last - first + 1) * 3);
        }
    }
    free(e);
    return ok;
}

void palette_fade_start(const Palette *target, int steps)
{
    s_target = target;
    s_steps = steps;
    s_step = 1;
}

bool palette_fading(void) { return s_step != 0; }

void palette_fade_step(void)
{
    if (!s_step || !s_target) return;
    const Palette *t = s_target;
    if (s_step == s_steps) {
        /* Last step: copy the target exactly and finish. */
        memcpy(s_dac + t->first * 3, t->rgb + t->first * 3, (size_t)(t->last - t->first + 1) * 3);
        palette_set_dac(t->first, t->last, s_dac + t->first * 3);
        s_step = 0;
        return;
    }
    /* 32 and 64 steps use an arithmetic shift (rounds toward -inf); any other
     * count divides (rounds toward zero). Kept separate to match exactly. */
    int shift = s_steps == 32 ? 5 : s_steps == 64 ? 6 : -1;
    uint8_t out[768];
    for (int i = t->first * 3; i <= t->last * 3 + 2; i++) {
        int delta = (int)t->rgb[i] - (int)s_dac[i];
        int scaled = shift < 0 ? delta * s_step / s_steps : (delta * s_step) >> shift;
        out[i] = (uint8_t)(s_dac[i] + (int8_t)scaled);
    }
    palette_set_dac(t->first, t->last, out + t->first * 3);
    s_step++;
}
