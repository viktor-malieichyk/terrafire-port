#include "kbd.h"

static uint8_t s_pressed[128];    /* 0x840d0 */
static uint8_t s_released[128];   /* 0x84150 */

void kbd_event(uint8_t scancode, bool down)
{
    uint8_t code = scancode & 0x7f;
    if (down) {
        s_pressed[0] = code;
        s_pressed[code] = code;
    } else {
        s_released[code] = code;
    }
}

uint8_t kbd_any_pressed(void) { return s_pressed[0]; }

uint8_t kbd_first_released(void)
{
    for (int i = 0; i < 128; i++)
        if (s_released[i]) return (uint8_t)i;
    return 0;
}

void kbd_clear_all(void)
{
    for (int i = 0; i < 128; i++) s_pressed[i] = s_released[i] = 0;
}

void kbd_clear_pressed(uint8_t code) { s_pressed[code & 0x7f] = 0; }
void kbd_clear_released(uint8_t code) { s_released[code & 0x7f] = 0; }
uint8_t kbd_was_pressed(uint8_t code) { return s_pressed[code & 0x7f]; }
uint8_t kbd_was_released(uint8_t code) { return s_released[code & 0x7f]; }

void kbd_set_pressed(uint8_t code)
{
    s_pressed[code & 0x7f] = 1;
    s_pressed[0] = 1;
}

void kbd_set_released(uint8_t code) { s_released[code & 0x7f] = 1; }
