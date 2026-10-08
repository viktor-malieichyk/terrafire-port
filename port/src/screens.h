/* Between missions: briefing 0x1a500, mission complete 0x12d48, game over
 * 0x16b48 (+ 0x22304), high score entry 0x22058, game complete 0x25dac. */
#ifndef TF_SCREENS_H
#define TF_SCREENS_H

#include <stdbool.h>

void briefing_show(void);              /* 0x1a500 */
void mission_complete_screen(void);    /* 0x12d48 / 0x12d20 / 0x12cf4 */
void complete_frame(bool present);     /* screen mode 3 */
void game_over(void);                  /* 0x16b48 */
void high_score_entry(void);           /* 0x22058 */
void game_complete(void);              /* 0x25dac */
void splash_screen(void);              /* 0x1c4e0: at startup */
void title_screen(void);               /* 0x1405c: at startup and after a demo */
void cb_title_stop(int slot);          /* 0x24790 */
void cb_star(int slot);                /* 0x24b6c */

#endif
