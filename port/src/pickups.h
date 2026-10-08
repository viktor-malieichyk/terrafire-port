/* Pods and the pickups they release: 0x20cb4 (pod hit) and 0x20f00. */
#ifndef TF_PICKUPS_H
#define TF_PICKUPS_H

#include <stdint.h>

void pod_hit(int slot);                                     /* 0x20cb4 */
void pickup_collect(int slot);                              /* 0x211d8 */
void cb_pickup_expire(int slot);                            /* 0x20eb0 */
void cb_pickup_burst(int slot);                             /* 0x20e50: the 0x834 pickup */
/* contents: a pod contents code (0x7da..0x834). dir: launch angle on bonus levels. */
void pickup_spawn(int contents, int32_t x, int32_t y, int dir);   /* 0x20f00 */

#endif
