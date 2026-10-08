/* Menus: the engine 0x1afd4 / 0x1ad74 / 0x1a930 over the original's menu
 * tables, the main menu 0x57368 and its submenus, the in-game menu 0x580cc. */
#ifndef TF_MENU_H
#define TF_MENU_H

#include <stdbool.h>

void main_menu(void);                     /* 0x1006c's loop: until EXIT or Esc */
void ingame_menu(void);                   /* Esc during a mission (0x15bbc) */
void menu_frame(bool present, bool flash);   /* 0x1a930: screen mode 0 frame */
void cb_menu_backdrop(int slot);          /* 0x1a900 */
void save_point_menu(void);
/* 0x182e8: show `pages` pages of message lines from `first` (0x5b = the
 * instructions, 199 = credits). */
void pages_show(int first, int pages);               /* 0x16f60: at a save point */
void high_scores_setup(void);             /* 0x2219c */
void high_scores_frame(bool present);     /* 0x21f44: screen mode 4 frame */

#endif
