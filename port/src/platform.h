/* Platform layer: everything the original did by talking to DOS hardware
 * (VGA mode 13h, the int 9 keyboard handler, the PIT timer and the
 * DiamondWare sound driver) goes through this interface. Game code never
 * includes SDL directly. */
#ifndef TF_PLATFORM_H
#define TF_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 200
#define SOUND_CHANNELS 34   /* one per entry of the game's sound table */

/* Indexed 8-bit framebuffer, SCREEN_W * SCREEN_H, like VGA memory at 0xa0000. */
extern uint8_t g_screen[SCREEN_W * SCREEN_H];

bool platform_init(const char *title, int scale);
void platform_shutdown(void);

/* Palette as 256 RGB triples with 8-bit components. */
void platform_set_palette(const uint8_t rgb[768]);
/* Convert g_screen through the palette and show it. */
void platform_present(void);
/* Port display options: desktop fullscreen, and how the picture is scaled
 * to the window (see present_rect). */
enum { SCALE_SHARP, SCALE_INTEGER, SCALE_FILL, SCALE_MODES };
void platform_set_fullscreen(bool on);
void platform_set_scaling(int mode);

void platform_set_title(const char *title);
/* Write the current g_screen (through the palette) to a BMP file. */
bool platform_save_screenshot(const char *path);

/* Pumps events; returns false once the user has asked to quit. */
bool platform_poll(void);

/* Keyboard state indexed by IBM XT (set 1) scancode, which is what the
 * original's int 9 handler stored, so game code can keep its key constants. */
extern bool g_key_down[128];
/* Next key press since the last call, as an XT scancode, or 0 if none. */
int platform_next_key(void);

/* The first connected gamepad (SDL's standard layout): direction from the
 * D-pad or left stick (-1, 0, 1; y up = -1), fire = A or X, back = B or Start. */
typedef struct {
    bool connected;
    int x, y;
    bool fire, back;
} GamepadState;
void platform_gamepad(GamepadState *out);
/* A gamepad was plugged in since the last call. */
bool platform_gamepad_arrived(void);

enum {
    KEY_ESC = 0x01, KEY_1 = 0x02, KEY_0 = 0x0b, KEY_ENTER = 0x1c, KEY_LCTRL = 0x1d,
    KEY_P = 0x19, KEY_S = 0x1f, KEY_LSHIFT = 0x2a, KEY_SPACE = 0x39, KEY_LALT = 0x38,
    KEY_UP = 0x48, KEY_LEFT = 0x4b, KEY_RIGHT = 0x4d, KEY_DOWN = 0x50,
    KEY_PGUP = 0x49, KEY_PGDN = 0x51,
};

/* Nanoseconds since platform_init. */
uint64_t platform_ticks_ns(void);
/* Sleep until roughly the given time (platform_ticks_ns units). */
void platform_wait_until(uint64_t when_ns);

/* Play signed 8-bit mono PCM (the DWD sample format) on a channel,
 * replacing whatever that channel was playing. gain 0..1; a looping sound
 * repeats until stopped. The samples must stay valid while playing. */
void platform_play_sound(int channel, const int8_t *samples, size_t count, int rate_hz,
                         float gain, bool loop);
void platform_stop_sound(int channel);
bool platform_sound_playing(int channel);

#endif
