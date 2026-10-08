/* Status bar and HUD messages: 0x1d428 (message), 0x1d104 (draw). */
#ifndef TF_HUD_H
#define TF_HUD_H

/* Message texts (English column of the message table at 0x4276c, g_text). */
#define MSG_PAUSED        "PAUSED"
#define MSG_WEAPON_UPGRADE "WEAPON UPGRADE"
#define MSG_EXTRA_LIFE    "EXTRA LIFE"
#define MSG_SHIELDS_RESTORED "SHIELDS RESTORED"
#define MSG_SHIELDS_CRITICAL "SHIELDS CRITICAL"
#define MSG_SELF_DESTRUCT "SELF-DESTRUCT INITIATED"
#define MSG_EVACUATE      "EVACUATE NOW"
#define MSG_DETONATION    "DETONATION IMMINENT"
#define MSG_AUTO_DESTRUCT "AUTO-DESTRUCT COMPLETE"
#define MSG_CRUSHER       "CRUSHER ACTIVATED"
#define MSG_FIRE_WALLS    "FIRE-WALLS DOWN"
#define MSG_GUNS_OFF      "GUNS OFF"
#define MSG_WATER_PUMPS   "WATER PUMPS ON"
#define MSG_ROCK_FOUNTAIN "ROCK FOUNTAIN DOWN"
#define MSG_GATE_OPEN     "GATE OPEN"
#define MSG_GATE_CLOSING  "GATE CLOSING"

enum { ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTRE };
/* 0x293e0: printf-style text from sprite banks font + ch - 0x20 at 24.8
 * world (x, y); see hud.c. */
#include <stdbool.h>
#include <stdint.h>
/* 0x29cf8: width of a proportional string (24.8). */
int32_t text_width(int font, int32_t spacing, int32_t space, const char *s);
void text_draw(bool prop, int align, int32_t x, int32_t y, int font, int32_t spacing,
               int32_t advance, const char *fmt, ...);

/* 0x1d428: show a printf-style message for `frames` HUD draws. */
void hud_message(int frames, const char *fmt, ...);
/* 0x1d104: draw the status bar; counts the message time down. */
void hud_draw(void);

#endif
