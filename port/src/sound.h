/* Sound effects: the DiamondWare-based player 0x2e4a8 (play), 0x2e6d8 (stop),
 * 0x2e5f8 (playing?), 0x2e744 (stop all) over the table at 0x43c10, loaded
 * by 0x23300. Sound numbers index g_sound_table. */
#ifndef TF_SOUND_H
#define TF_SOUND_H

#include <stdbool.h>

void sound_init(bool enabled);   /* 0x23300: load every sample */
void sound_play(int n);          /* 0x2e4a8 */
void sound_stop(int n);          /* 0x2e6d8 */
bool sound_playing(int n);       /* 0x2e5f8 */
void sound_pause(void);
void sound_set_volume(int percent);   /* 0x2e7a4: 0-100 */          /* 0x2e744: stop everything */

#endif
