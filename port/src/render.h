/* World renderer: off-screen buffer, camera and view, the occlusion grid and
 * object drawing. Ports of 0x2a444/0x2a4e0/0x2aa28/0x2aa38 (camera),
 * 0x2a4f4 (view size), 0x2be20 (occlusion grid), 0x2b7f8 (draw objects),
 * 0x27f00 (tile-frame objects), 0x28f30 and 0x2d691-0x2d7a0 (sprites),
 * 0x2a028/0x2a224/0x2a088 (point, fill, line) and the copy in 0x2a898.
 *
 * Drawing goes into a 352x232 buffer with a 16-pixel margin on every side
 * (tiles are drawn whole and may overhang the view); only the view
 * rectangle is copied to the screen. */
#ifndef TF_RENDER_H
#define TF_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#define BUF_W 352
#define BUF_H 232
#define BUF_MARGIN 16

typedef struct {
    int32_t cam_x, cam_y;      /* 0x841e0 / 0x841dc  world pixel at the view's top-left */
    int clip_l, clip_t;        /* 0x84218 / 0x8420c  view rectangle in screen pixels */
    int clip_r, clip_b;        /* 0x84214 / 0x84208  inclusive */
    int view_w, view_h;        /* 0x841f4 / 0x841e8 */
} View;

extern uint8_t g_buf[BUF_W * BUF_H];   /* 0x5e430 */
extern View g_view;

/* Loads the 256x256 blend table (pak entry 60) and sets a full-size view. */
bool render_init(void);

void render_camera_center(int32_t x, int32_t y);   /* 0x2a444: 24.8 world point at screen centre */
void render_camera(int32_t x, int32_t y);          /* 0x2a4e0: 24.8 top-left */
int32_t render_cam_x(void);                        /* 0x2aa28: 24.8 */
int32_t render_cam_y(void);                        /* 0x2aa38 */
void render_set_view_size(int32_t width);          /* 0x2a4f4: 24.8 width, 32..320 px */

void render_build_occlusion(void);                 /* 0x2be20 */
/* 0x2b7f8: draw active objects from `start` (-1 = first) while z <= max_z;
 * returns the first object not drawn, or -1. */
int render_objects(int start, int32_t max_z);

/* 0x2933c: draw view `view` of sprite bank `bank` at 24.8 world (x, y),
 * clipped to the 24.8 world rectangle; 0x292ac is the same unclipped. */
void render_bank(int bank, uint8_t blend, int view, int32_t x, int32_t y,
                 int32_t clip_x0, int32_t clip_y0, int32_t clip_x1, int32_t clip_y1);
#define RENDER_NO_CLIP (-32000 * 256), (-32000 * 256), (32000 * 256), (32000 * 256)
/* 0x27a7c: draw frame slot `slot` with its top left at 24.8 world (x, y). */
void render_frame_slot(int slot, int32_t x, int32_t y);
/* 0x2a310: copy the drawn view (320x200) out. */
void render_copy_view(uint8_t *out);
/* 0x2a310 with a size: the centred w x h part of the drawn view. */
void render_capture(uint8_t *out, int w, int h);
/* 0x2a388: the reverse. */
void render_load_view(const uint8_t *in);
/* Part of 0x276fc: camera at 0,0, full width view. */
void render_reset_view(void);
/* 0x28764: objects drawn after this skip the tiles under this rectangle. */
void render_mark_covered(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void render_plot(uint8_t colour, int32_t x, int32_t y);                          /* 0x2a028 */
void render_fill(uint8_t colour, int32_t x0, int32_t y0, int32_t x1, int32_t y1); /* 0x2a224 */
void render_line(uint8_t colour, int32_t x0, int32_t y0, int32_t x1, int32_t y1); /* 0x2a088 */

/* 0x2d044: is any part of the object inside the view and its own clip box? */
bool render_obj_on_screen(int slot);
/* 0x2d1a0: is all of the object inside the view and its own clip box? */
bool render_obj_fully_on_screen(int slot);

/* Copy the view to the screen and present (0x2a898); returns the time waited. */
/* 0x2436c: the beam-in/out look: each pixel of the object's sprite as a
 * streak from (offset * r / 1024) to 13/16 of that, around the object. */
void render_beam(int slot, int32_t rx, int32_t ry);
int32_t render_present(void);

#endif
