#include "timing.h"
#include "joystick.h"
#include "kbd.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "palette.h"
#include "platform.h"

#define PIT_HZ 1193182ull
#define RATE_NUMERATOR 0x123333   /* the original's constant, close to PIT_HZ */

static int s_rate = TF_RATE_FULL;
static int64_t s_period = RATE_NUMERATOR / TF_RATE_FULL;   /* 0x841f0, PIT counts per frame */
static int64_t s_last;                                      /* 0x84210 */
static uint32_t s_counter;                                  /* 0x841e4 */

static uint64_t clock_to_ns(int64_t pit)
{
    return (uint64_t)pit * 1000000000ull / PIT_HZ;
}

/* Virtual clock (tests): waits advance it instead of sleeping, so a run
 * goes as fast as it can with the same frame sequence. */
static bool s_virtual;
static uint64_t s_vclock;

void tf_set_virtual_clock(bool on)
{
    s_vclock = platform_ticks_ns() * PIT_HZ / 1000000000ull;
    s_virtual = on;
}

uint64_t tf_clock(void)
{
    if (s_virtual) return s_vclock;
    return platform_ticks_ns() * PIT_HZ / 1000000000ull;
}

static void wait_pit(int64_t pit)
{
    if (s_virtual) {
        if ((int64_t)s_vclock < pit) s_vclock = (uint64_t)pit;
    } else {
        platform_wait_until(clock_to_ns(pit));
    }
}


void tf_set_frame_rate(int rate)
{
    if (rate <= 0) return;
    s_rate = rate;
    s_period = RATE_NUMERATOR / rate;
}

int tf_frame_rate(void) { return s_rate; }
uint32_t tf_frame_counter(void) { return s_counter; }
void tf_set_frame_counter(uint32_t n) { s_counter = n; }

void tf_wait_clock(uint64_t when)
{
    if (tf_clock() < when) wait_pit((int64_t)when);
}

static bool s_quit;
static long s_presents;              /* tf_present calls (test hooks count these) */

long tf_present_calls(void) { return s_presents; }

/* TF_KEYS="frame:code,frame:-code,...": key make (code) / break (-code)
 * events injected at the given present counts (testing menus). */
static void inject_keys(long frame)
{
    static const char *spec;
    static bool init;
    if (!init) { spec = getenv("TF_KEYS"); init = true; }
    if (!spec) return;
    for (const char *p = spec; *p; ) {
        char *end;
        long f = strtol(p, &end, 10);
        if (*end != ':') break;
        long code = strtol(end + 1, &end, 0);
        if (f == frame && code != 0) kbd_event((uint8_t)(code < 0 ? -code : code), code > 0);
        p = *end == ',' ? end + 1 : end;
        if (!*end) break;
    }
}

bool tf_quit_requested(void) { return s_quit; }

int32_t tf_present(void)
{
    /* The original's keyboard was interrupt driven; pump events once per
     * frame (shown or skipped). Tests replay recorded input only. */
    if (!s_virtual) {
        if (!platform_poll()) s_quit = true;
        joystick_poll();   /* every frame, so waits for a key see the pad too */
    }
    inject_keys(s_presents++);
    /* A palette fade in progress (0x84238) also forces every frame to be shown. */
    bool present_now = s_rate == TF_RATE_FULL || (s_counter & 1) == 0 || palette_fading();
    if (!present_now) {
        s_counter++;
        return 0;
    }

    int64_t start = (int64_t)tf_clock();
    int64_t now = start;
    if (now < s_last + s_period || palette_fading()) {
        /* The original spun until 7/8 of the period had passed and then waited
         * for VGA vertical retrace (0x2d8f0), which landed on the frame
         * boundary. There is no retrace to wait for here, so wait for the
         * whole period instead. */
        int64_t target = s_last + s_period;
        if (now < target) wait_pit(target);
        now = (int64_t)tf_clock();
    }
    /* Running late (and skipping allowed): present at once, as the original did. */
    s_last = now;
    if (palette_fading()) palette_fade_step();   /* the original stepped fades here, after retrace */
    if (!s_virtual) platform_present();   /* tests: nothing to show */
    {   /* TF_SHOT_PRESENT=n:path saves the first frame shown from the n-th
         * tf_present call on (testing; same count as TF_KEYS). */
        static long shot_at = -2;
        static char shot_path[512];
        static long presented;
        if (shot_at == -2) {
            const char *e = getenv("TF_SHOT_PRESENT");
            shot_at = -1;
            if (e && strchr(e, ':')) {
                shot_at = atol(e);
                snprintf(shot_path, sizeof shot_path, "%s", strchr(e, ':') + 1);
            }
        }
        (void)presented;
        if (shot_at >= 0 && s_presents >= shot_at) {
            platform_save_screenshot(shot_path);
            shot_at = -1;
        }
    }
    s_counter++;
    return (int32_t)(s_last - start);
}
