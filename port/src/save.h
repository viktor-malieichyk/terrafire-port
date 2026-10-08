/* Saved games GAME0001.SAV-GAME0005.SAV in the original's format (see
 * re/tools/tf_state.py): 0x16bfc (write), 0x163f4 (read), 0x277c4 /
 * 0x2756c (objects), 0x24ddc (checksum). */
#ifndef TF_SAVE_H
#define TF_SAVE_H

#include <stdbool.h>

#define SAVE_SLOTS 5

/* 0x163f4: refresh slot info (stamp, mission, skill, thumbnail into bank
 * 0x936 + slot); with load, also restore the game from it. */
void save_slot_read(int slot, bool load);
bool save_slot_valid(int slot);
const char *save_slot_stamp(int slot);    /* "" if none, or an error text */
/* 0x16bfc: write the current game (and the screen as thumbnail). */
bool save_write(int slot);

#endif
