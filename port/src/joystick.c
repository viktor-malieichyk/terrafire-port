#include "joystick.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hud.h"
#include "kbd.h"
#include "platform.h"
#include "tables.h"
#include "timing.h"

/* Edge-triggered: a change sets the key's pressed or released latch. */
static void key_edge(bool *was, bool now, int code)
{
    if (code < 0) return;
    if (now && !*was) kbd_set_pressed((uint8_t)code);
    if (!now && *was) kbd_set_released((uint8_t)code);
    *was = now;
}

/* TF_PAD="from-to:x,y,fire,back;...": a simulated gamepad over ranges of
 * shown frames (testing). */
static bool test_pad(GamepadState *pad)
{
    static const char *spec;
    static bool init;
    if (!init) { spec = getenv("TF_PAD"); init = true; }
    if (!spec) return false;
    long now = tf_present_calls();
    pad->connected = true;
    for (const char *p = spec; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : NULL) {
        long a, b;
        int x, y, f, k;
        if (sscanf(p, "%ld-%ld:%d,%d,%d,%d", &a, &b, &x, &y, &f, &k) == 6 && now >= a && now <= b) {
            pad->x = x;
            pad->y = y;
            pad->fire = f;
            pad->back = k;
        }
    }
    return true;
}

void joystick_poll(void)
{
    static bool up, down, left, right, fire, back;   /* 0x56de5..0x56de9 */
    GamepadState pad = { 0 };
    bool simulated = test_pad(&pad);
    if (simulated || platform_gamepad_arrived()) g.joystick = true;
    if (!g.joystick) return;
    if (!simulated) platform_gamepad(&pad);
    if (!pad.connected) {
        g.joystick = false;
        hud_message(0xd2, "%s", g_text[0x3d]);          /* JOYSTICK DISABLED */
        return;
    }
    int k_left, k_right, k_up, k_down, k_fire;
    if (g.mission_type == 1 || g.mission_type == 2) {
        k_left = g.keys[0];
        k_right = g.keys[1];
        k_up = g.keys[2];
        k_down = -1;
        k_fire = g.keys[3];
    } else {
        k_left = 0x4b;
        k_right = 0x4d;
        k_up = 0x48;
        k_down = 0x50;
        k_fire = 0x1c;
    }
    /* Like the original, the two axes are read on alternate frames. */
    if ((tf_frame_counter() & 1) == 0) {
        key_edge(&up, pad.y < 0, k_up);
        if (!up) key_edge(&down, pad.y > 0, k_down);
    } else {
        key_edge(&left, pad.x < 0, k_left);
        if (!left) key_edge(&right, pad.x > 0, k_right);
    }
    key_edge(&fire, pad.fire, k_fire);
    key_edge(&back, pad.back, KEY_ESC);
}
