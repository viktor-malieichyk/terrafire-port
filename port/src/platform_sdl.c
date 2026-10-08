/* SDL3 implementation of platform.h. */
#include "platform.h"

#include "kbd.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

uint8_t g_screen[SCREEN_W * SCREEN_H] = {0};
bool g_key_down[128] = {0};

static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static uint32_t s_palette[256];          /* XRGB8888 */
static uint32_t s_pixels[SCREEN_W * SCREEN_H];

static SDL_AudioDeviceID s_audio;
static SDL_AudioStream *s_channels[SOUND_CHANNELS];
static struct {
    const int8_t *samples;
    int count;
    bool loop;                           /* read on the audio thread: stream lock held */
} s_voice[SOUND_CHANNELS];
static SDL_AudioSpec s_device_spec;

static SDL_Gamepad *s_pad;
static bool s_pad_arrived;

static uint8_t s_keyq[32];
static unsigned s_keyq_head, s_keyq_tail;

/* SDL scancode -> IBM XT set-1 scancode. Extended keys (arrows, Home, ...)
 * use their base codes, as DOS int 9 handlers that ignore the E0 prefix see. */
static uint8_t xt_scancode(SDL_Scancode sc)
{
    static const uint8_t letters[26] = {
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c,
    };
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return letters[sc - SDL_SCANCODE_A];
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return (uint8_t)(0x02 + (sc - SDL_SCANCODE_1));
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return (uint8_t)(0x3b + (sc - SDL_SCANCODE_F1));
    switch (sc) {
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_MINUS: return 0x0c;
    case SDL_SCANCODE_EQUALS: return 0x0d;
    case SDL_SCANCODE_BACKSPACE: return 0x0e;
    case SDL_SCANCODE_TAB: return 0x0f;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1a;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1b;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x1c;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x1d;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_LSHIFT: return 0x2a;
    case SDL_SCANCODE_BACKSLASH: return 0x2b;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x38;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_CAPSLOCK: return 0x3a;
    case SDL_SCANCODE_HOME: case SDL_SCANCODE_KP_7: return 0x47;
    case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_KP_9: return 0x49;
    case SDL_SCANCODE_KP_MINUS: return 0x4a;
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4: return 0x4b;
    case SDL_SCANCODE_KP_5: return 0x4c;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: return 0x4d;
    case SDL_SCANCODE_KP_PLUS: return 0x4e;
    case SDL_SCANCODE_END: case SDL_SCANCODE_KP_1: return 0x4f;
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_KP_3: return 0x51;
    case SDL_SCANCODE_INSERT: case SDL_SCANCODE_KP_0: return 0x52;
    case SDL_SCANCODE_DELETE: case SDL_SCANCODE_KP_PERIOD: return 0x53;
    case SDL_SCANCODE_F11: return 0x57;
    case SDL_SCANCODE_F12: return 0x58;
    default: return 0;
    }
}

bool platform_init(const char *title, int scale)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    /* 320x200 was shown on 4:3 monitors, so present it at 320x240. */
    if (!SDL_CreateWindowAndRenderer(title, SCREEN_W * scale, 240 * scale,
                                     SDL_WINDOW_RESIZABLE, &s_window, &s_renderer)) {
        SDL_Log("window creation failed: %s", SDL_GetError());
        return false;
    }
    /* No vsync: game logic advances once per present, so presents must be
     * paced by the original's 70 Hz timer (timing.c), not the monitor. */
    SDL_SetRenderVSync(s_renderer, 0);
    SDL_SetRenderLogicalPresentation(s_renderer, SCREEN_W, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_XRGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    if (!s_texture) {
        SDL_Log("texture creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(s_texture, SDL_SCALEMODE_NEAREST);

    /* Sound is optional: the original also ran with it disabled. */
    s_audio = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (s_audio && SDL_GetAudioDeviceFormat(s_audio, &s_device_spec, NULL)) {
        const SDL_AudioSpec src = { SDL_AUDIO_S8, 1, 11025 };
        for (int i = 0; i < SOUND_CHANNELS; i++) {
            s_channels[i] = SDL_CreateAudioStream(&src, &s_device_spec);
            if (s_channels[i]) SDL_BindAudioStream(s_audio, s_channels[i]);
        }
    } else {
        SDL_Log("sound disabled: %s", SDL_GetError());
    }
    return true;
}

void platform_shutdown(void)
{
    for (int i = 0; i < SOUND_CHANNELS; i++)
        if (s_channels[i]) SDL_DestroyAudioStream(s_channels[i]);
    if (s_audio) SDL_CloseAudioDevice(s_audio);
    SDL_DestroyTexture(s_texture);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
}

void platform_set_palette(const uint8_t rgb[768])
{
    for (int i = 0; i < 256; i++)
        s_palette[i] = 0xff000000u | (uint32_t)rgb[i * 3] << 16 | (uint32_t)rgb[i * 3 + 1] << 8 | rgb[i * 3 + 2];
}

void platform_present(void)
{
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        s_pixels[i] = s_palette[g_screen[i]];
    SDL_UpdateTexture(s_texture, NULL, s_pixels, SCREEN_W * (int)sizeof(uint32_t));
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);
    const SDL_FRect dst = { 0, 0, SCREEN_W, 240 };
    SDL_RenderTexture(s_renderer, s_texture, NULL, &dst);
    SDL_RenderPresent(s_renderer);
}

bool platform_save_screenshot(const char *path)
{
    SDL_Surface *surf = SDL_CreateSurface(SCREEN_W, SCREEN_H, SDL_PIXELFORMAT_XRGB8888);
    if (!surf) return false;
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)surf->pixels + y * surf->pitch);
        for (int x = 0; x < SCREEN_W; x++) row[x] = s_palette[g_screen[y * SCREEN_W + x]];
    }
    bool ok = SDL_SaveBMP(surf, path);
    SDL_DestroySurface(surf);
    return ok;
}

void platform_set_title(const char *title)
{
    SDL_SetWindowTitle(s_window, title);
}

bool platform_poll(void)
{
    SDL_Event e;
    bool running = true;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            running = false;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!s_pad) {
                s_pad = SDL_OpenGamepad(e.gdevice.which);
                if (s_pad) s_pad_arrived = true;
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (s_pad && SDL_GetGamepadID(s_pad) == e.gdevice.which) {
                SDL_CloseGamepad(s_pad);
                s_pad = NULL;
            }
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            uint8_t xt = xt_scancode(e.key.scancode);
            if (!xt) break;
            bool down = e.type == SDL_EVENT_KEY_DOWN;
            g_key_down[xt] = down;
            /* Forward auto-repeat too: DOS typematic repeat resent make codes,
             * re-setting the pressed latch even after the game cleared it. */
            kbd_event(xt, down);
            if (down && !e.key.repeat && s_keyq_head - s_keyq_tail < sizeof s_keyq)
                s_keyq[s_keyq_head++ % sizeof s_keyq] = xt;
            break;
        }
        default:
            break;
        }
    }
    return running;
}

int platform_next_key(void)
{
    if (s_keyq_tail == s_keyq_head) return 0;
    return s_keyq[s_keyq_tail++ % sizeof s_keyq];
}

uint64_t platform_ticks_ns(void)
{
    return SDL_GetTicksNS();
}

void platform_wait_until(uint64_t when_ns)
{
    uint64_t now = SDL_GetTicksNS();
    if (when_ns > now) SDL_DelayNS(when_ns - now);
}

/* Audio thread: keep a looping voice topped up. */
static void SDLCALL loop_feed(void *user, SDL_AudioStream *st, int additional, int total)
{
    (void)total;
    int ch = (int)(intptr_t)user;
    if (!s_voice[ch].loop || !s_voice[ch].count) return;
    for (int queued = 0; queued < additional; queued += s_voice[ch].count)
        SDL_PutAudioStreamData(st, s_voice[ch].samples, s_voice[ch].count);
}

void platform_play_sound(int channel, const int8_t *samples, size_t count, int rate_hz,
                         float gain, bool loop)
{
    if (channel < 0 || channel >= SOUND_CHANNELS || !s_channels[channel] || !count) return;
    SDL_AudioStream *st = s_channels[channel];
    const SDL_AudioSpec src = { SDL_AUDIO_S8, 1, rate_hz };
    SDL_LockAudioStream(st);
    SDL_ClearAudioStream(st);
    SDL_SetAudioStreamFormat(st, &src, NULL);
    SDL_SetAudioStreamGain(st, gain);
    s_voice[channel].samples = samples;
    s_voice[channel].count = (int)count;
    s_voice[channel].loop = loop;
    SDL_SetAudioStreamGetCallback(st, loop_feed, (void *)(intptr_t)channel);
    SDL_PutAudioStreamData(st, samples, (int)count);
    /* Without a flush the resampler keeps the last few samples back waiting
     * for more input, so a one-shot sound would never finish playing. */
    if (!loop) SDL_FlushAudioStream(st);
    SDL_UnlockAudioStream(st);
}

void platform_stop_sound(int channel)
{
    if (channel < 0 || channel >= SOUND_CHANNELS || !s_channels[channel]) return;
    SDL_LockAudioStream(s_channels[channel]);
    s_voice[channel].loop = false;
    SDL_ClearAudioStream(s_channels[channel]);
    SDL_UnlockAudioStream(s_channels[channel]);
}

bool platform_sound_playing(int channel)
{
    if (channel < 0 || channel >= SOUND_CHANNELS || !s_channels[channel]) return false;
    return s_voice[channel].loop || SDL_GetAudioStreamQueued(s_channels[channel]) > 0;
}

bool platform_gamepad_arrived(void)
{
    bool a = s_pad_arrived;
    s_pad_arrived = false;
    return a;
}

void platform_gamepad(GamepadState *out)
{
    memset(out, 0, sizeof *out);
    if (!s_pad) return;
    out->connected = true;
    const int dead = 16000;   /* about half way */
    int ax = SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_LEFTX);
    int ay = SDL_GetGamepadAxis(s_pad, SDL_GAMEPAD_AXIS_LEFTY);
    out->x = ax < -dead ? -1 : ax > dead ? 1 : 0;
    out->y = ay < -dead ? -1 : ay > dead ? 1 : 0;
    if (SDL_GetGamepadButton(s_pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) out->x = -1;
    if (SDL_GetGamepadButton(s_pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) out->x = 1;
    if (SDL_GetGamepadButton(s_pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) out->y = -1;
    if (SDL_GetGamepadButton(s_pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) out->y = 1;
    out->fire = SDL_GetGamepadButton(s_pad, SDL_GAMEPAD_BUTTON_SOUTH) ||
                SDL_GetGamepadButton(s_pad, SDL_GAMEPAD_BUTTON_WEST);
    out->back = SDL_GetGamepadButton(s_pad, SDL_GAMEPAD_BUTTON_EAST) ||
                SDL_GetGamepadButton(s_pad, SDL_GAMEPAD_BUTTON_START);
}
