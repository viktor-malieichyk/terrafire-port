/* Object system: the 652-entry object table and its z-sorted active list.
 * Ports of 0x2bacc (find free), 0x2bbb0 (spawn), 0x2bc60 (insert),
 * 0x2cac0 (remove), 0x2c118 (kill), 0x2c204 (callbacks), 0x2c7d0/0x2c800
 * (animations), 0x2cd18 (animation step), 0x2bdb8 (move with children) and
 * 0x2cbbc (per-frame update). */
#ifndef TF_OBJECTS_H
#define TF_OBJECTS_H

#include <stdbool.h>
#include <stdint.h>

#define OBJ_COUNT 652

typedef struct {               /* original 0x60-byte record */
    int16_t id;                /* +0x00  own slot index */
    uint8_t alive;             /* +0x02 */
    int16_t next, prev;        /* +0x03/+0x07  active list (pointers in the original) */
    int16_t bank;              /* +0x0b  sprite bank drawn when slot == -1 */
    int16_t orient;            /* +0x0d  0-7, selects the bank's view */
    uint8_t f_0f;              /* +0x0f */
    int16_t slot;              /* +0x10  frame slot drawn instead of a bank, -1 = none */
    int32_t x, y, z;           /* +0x12  24.8; the list is sorted by z (draw order) */
    int32_t vx, vy;            /* +0x1e */
    int32_t ax, ay;            /* +0x26  added to the velocity each frame */
    int32_t box[4];            /* +0x2e  stored >> 8; spawned as -32000..32000 */
    int16_t anim;              /* +0x3e  index into g_anims, -1 = none */
    int16_t anim_frame;        /* +0x42 */
    uint8_t frozen;            /* +0x44  skip animation and movement */
    int16_t parent;            /* +0x45 */
    int16_t child;             /* +0x47  moves with this object unless no_drag is set */
    uint8_t f_49;              /* +0x49 */
    int16_t f_4a;              /* +0x4a */
    int16_t no_drag;           /* +0x4c  -1 = children follow this object */
    int16_t cb_timer[3];       /* +0x4e  frames until callback fires, -1 = none */
    int16_t cb[3];             /* +0x54  index into g_callbacks */
} Object;

extern Object g_objects[OBJ_COUNT];

void objects_init(void);                      /* part of 0x27164 */
void objects_clear_active(void);
/* 0x15b94: take every live object out of the list (kept as they are) so a
 * menu can run, and put them back in afterwards (as 0x15bbc does). */
void objects_park(bool parked[OBJ_COUNT]);
void obj_link(int slot);                      /* 0x2bc60: mark alive, insert by z */
void objects_unpark(bool parked[OBJ_COUNT]);              /* part of 0x276fc */

int obj_find_free(int lo, int hi, bool must); /* 0x2bacc */
void obj_spawn(int slot, int bank, int32_t x, int32_t y, int32_t z);  /* 0x2bbb0 */
void obj_kill(int slot);                      /* 0x2c118 */
void obj_unfreeze(int slot);                  /* 0x2aaa0 */
void obj_freeze(int slot);                    /* 0x2c1c4: also hides it */
void obj_clear_callbacks(int slot);           /* 0x2ab04: cancel all three timers */
void obj_clear_anim(int slot);                /* 0x2c8fc */
/* 0x2c278: attach `child` to `parent` at attach points (-1/-1 = just follow);
 * copy_orient also gives the child the parent's orientation. */
void obj_attach(bool copy_orient, int child, int child_point, int parent, int parent_point);
int obj_sprite_bank(int slot);                /* 0x2c478: bank, or -1 if it draws a frame slot */
void obj_set_pos(int slot, int32_t x, int32_t y);   /* 0x2bd1c: children follow */
void obj_set_slot(int slot, int frame_slot);  /* 0x2c378: draw a frame slot (-1 = keep) */
void obj_set_orient(int slot, int orient);    /* 0x2c398: also sets the attached child */
/* 0x2bdb8: move an object and (unless it opts out) its attached children. */
void obj_move(int slot, int32_t dx, int32_t dy);
/* 0x2c42c: clip box, 24.8 world coordinates (stored in pixels). */
void obj_set_box(int slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
bool obj_alive(int slot);                     /* 0x2bb98 */
int32_t obj_x(int slot);                      /* 0x2d36c */
int32_t obj_y(int slot);                      /* 0x2d3f4 */
/* 0x2d39c / 0x2d424: this frame's motion - velocity plus acceleration plus
 * the current animation frame's step (through the orientation). */
int32_t obj_motion_x(int slot);
int32_t obj_motion_y(int slot);
void obj_set_velocity(int slot, int32_t vx, int32_t vy);   /* 0x2c098 */
void obj_add_velocity(int slot, int32_t dvx, int32_t dvy); /* 0x2c0bc */

/* 0x2c204: run callback `cb` (a CB_ index) on `slot` after `frames` updates;
 * 0 runs it now, -1 cancels timer k. */
void obj_set_callback(int slot, int k, int cb, int frames);
void obj_set_anim(int slot, int anim, int frame);           /* 0x2c7d0 */
/* 0x2c800: play `anim` from the start and run `cb` on timer k when it ends. */
void obj_play_anim_then(int slot, int anim, int k, int cb);
void obj_anim_step(int slot);                 /* 0x2cd18 */

void objects_update(void);                    /* 0x2cbbc */

/* First (lowest z) active object, or -1 (0x841ec). */
int objects_first(void);

/* Iteration state of objects_update(), visible to callbacks (0x59e24/28). */
int objects_current(void);

/* Testing aid: called at the start of every objects_update() (NULL = off). */
void objects_set_step_hook(void (*fn)(void));

/* Testing aid: route every callback to `fn` instead of g_callbacks (NULL = off). */
void objects_set_callback_override(void (*fn)(int slot));

#endif
