/* Keyboard state exactly as the original's int 9 handler (0x2d609) kept it:
 * two 128-entry latch tables indexed by XT scancode. A make code stores the
 * scancode in pressed[code] and pressed[0] ("any key"); a break code stores it
 * in released[code]. Nothing is cleared automatically - game code clears
 * latches itself, which is what gives "pressed"/"held" their meaning. */
#ifndef TF_KBD_H
#define TF_KBD_H

#include <stdbool.h>
#include <stdint.h>

/* Called by the platform layer for every key make/break (XT scancode). */
void kbd_event(uint8_t scancode, bool down);

uint8_t kbd_any_pressed(void);              /* 0x279b4 */
uint8_t kbd_first_released(void);           /* 0x279bc: lowest released code, or 0 */
void kbd_clear_all(void);                   /* 0x279d4 */
void kbd_clear_pressed(uint8_t code);       /* 0x279f0 */
void kbd_clear_released(uint8_t code);      /* 0x279fc */
uint8_t kbd_was_pressed(uint8_t code);      /* 0x27a08 */
uint8_t kbd_was_released(uint8_t code);     /* 0x27a10 */
void kbd_set_pressed(uint8_t code);         /* 0x27a18 */
void kbd_set_released(uint8_t code);        /* 0x27a2c */

#endif
