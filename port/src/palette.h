/* Palettes and fades, ported from 0x28e3c (load), 0x28e24 (start fade) and
 * 0x28be0 (fade step, run by the present routine every shown frame).
 * Colours are kept as 6-bit VGA DAC values like the original. */
#ifndef TF_PALETTE_H
#define TF_PALETTE_H

#include <stdbool.h>
#include <stdint.h>

/* Matches the original's 0x308-byte palette buffers. */
typedef struct {
    uint8_t rgb[768];   /* 6-bit components */
    int first, last;    /* index range this palette defines */
} Palette;

extern Palette g_pal_active;   /* 0x5b280: palette screens load into */
extern Palette g_pal_black;    /* 0x5ae78: pak entry 52, fade-to-black target */
extern Palette g_pal_white;    /* 0x5ab70: pak entry 58, fade-to-white target */

/* Pak palette entry: u8 first, u8 last, (last-first+1) x RGB. */
bool palette_read(int entry, Palette *out);
/* Start fading the DAC toward `target` over `steps` shown frames. */
void palette_fade_start(const Palette *target, int steps);
bool palette_fading(void);       /* 0x84238 != 0 */
void palette_fade_step(void);    /* called by tf_present() while fading */
/* Set DAC entries directly (6-bit RGB), as 0x2d65e did. */
void palette_set_dac(int first, int last, const uint8_t *rgb6);

#endif
