/* Frame pacing, ported from the original's limiter (0x2d4bc, 0x2a898).
 *
 * Time is measured in PIT counts (1,193,182 Hz), as the original did by
 * combining the BIOS tick count with the PIT counter (0x2d90d). Game logic
 * advances once per tf_present() call, so the logic rate is fixed at about
 * 70 steps/s; the 36 Hz setting only skips every other present (frameskip),
 * which keeps demo playback independent of that setting. */
#ifndef TF_TIMING_H
#define TF_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#define TF_RATE_FULL 70   /* 0x46 */
#define TF_RATE_HALF 36   /* 0x24: present every other frame */

void tf_set_frame_rate(int rate);       /* 0x2d4bc */
int tf_frame_rate(void);
/* Presents the frame (or skips it in half-rate mode) and waits out the rest
 * of the frame period. Returns the time waited in PIT counts, like 0x2a898. */
int32_t tf_present(void);
uint64_t tf_clock(void);
/* Tests: waits advance a virtual clock instead of sleeping. */
#include <stdbool.h>
void tf_set_virtual_clock(bool on);
/* The window was closed (seen while presenting). */
bool tf_quit_requested(void);
/* Number of tf_present calls so far (for test hooks). */
long tf_present_calls(void);                 /* PIT counts since start (0x2d90d) */
uint32_t tf_frame_counter(void);         /* 0x841e4 */
void tf_set_frame_counter(uint32_t n);   /* 0x2a4b0 */
/* Busy-wait until tf_clock() reaches `when` (the original spun on its clock). */
void tf_wait_clock(uint64_t when);

#endif
