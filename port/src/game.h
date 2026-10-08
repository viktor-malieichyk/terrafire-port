/* Game state. Each field notes the original global it replaces; fields whose
 * purpose is not known yet keep a neutral name (f_<offset or address>).
 *
 * GameState mirrors what the original kept in the 0x2973-byte block at
 * 0x5b908 that saves and demos dump. It uses the port's own layout; the
 * legacy formats are converted field by field (legacy_load_state). Bytes of
 * the block that have not been identified yet are kept verbatim in `legacy`
 * until they are, so nothing a demo or save carries is lost. */
#ifndef TF_GAME_H
#define TF_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define LEGACY_STATE_SIZE 0x2973
#define OBJ_SLOTS 652
#define SLOT_PLAYER 14        /* receives the per-difficulty shields; position tested at the exit */
#define SLOT_BONUS_TIMER 15   /* object 15: in a mission the nuke (o_0 = self-destruct countdown), in a
                                 * bonus level o_0 counts down once it is clear (0x1a23c) */

typedef struct {
    int32_t o_0, o_4, o_8, o_c;  /* per-slot values; o_c is 24.8 fixed point */
} ObjState;

typedef struct {
    int32_t difficulty;      /* +0x00  0..3 */
    int32_t mission;         /* +0x04  index into g_missions */
    int32_t mission_frames;  /* +0x08  zeroed at mission start, counted per frame */
    int32_t f_00c;           /* +0x0c  lives: session ends at 0, one indicator per life */
    int32_t score;           /* +0x10  kills add their points x (difficulty + 1) */
    uint8_t f_014;           /* +0x14 */
    int32_t f_015;           /* +0x15  = f_00c at mission start; random-table counter */
    int32_t f_019;           /* +0x19  = f_00c + 0x80 at mission start */
    uint8_t f_01d_flag;      /* +0x1d  pod rule: 0x7da pods become 0x816 */
    uint8_t f_01e_flag;      /* +0x1e  pod rule: 0x7f8 pods become 0x816 */
    char message[100];       /* +0x1f  HUD message text (hud_message) */
    int32_t message_time;    /* +0x83  frames the message stays up */
    int32_t last_shot;       /* +0x9b  mission_frames of the last main-gun shot */
    int32_t last_side_shot;  /* +0x9f  mission_frames of the last side-weapon shot */
    double tow_angle;        /* +0x87  angle of the tow line (x87 double) */
    int32_t weapon;          /* +0x8f  weapon level 0-17 (row of g_weapon_banks) */
    int32_t barrel;          /* +0x93  alternates the two barrels */
    int32_t side_ammo;       /* +0x97  side-weapon shots x2 */
    int32_t shields_max;     /* +0xa3  600/425/275/125 by difficulty */
    int32_t angle;           /* +0xa7  player heading in degrees, 0 = up, steps of 10 */
    int32_t stage;           /* +0xab  tow stage: 0 free, 1 beam attached, 2 towing; done at 2 above y 900 */
    int32_t f_0af;           /* +0xaf */
    ObjState obj[OBJ_SLOTS]; /* +0xb3 */
    uint8_t legacy[LEGACY_STATE_SIZE];   /* verbatim block, for bytes not yet identified */
} GameState;

#define SHIELDS (g_state.obj[SLOT_PLAYER].o_0)   /* 0x5ba9b */

/* Globals outside the state block. */
typedef struct {
    bool abort_session;      /* 0x5e42a */
    bool in_session;         /* 0x56ddc */
    bool session_over;       /* 0x56de4  ends the mission loop */
    bool mission_ended;      /* 0x5e428 */
    bool full_rate;          /* 0x5e332  setting: 70 Hz (else 36 Hz frameskip) */
    int32_t detail;          /* 0x5e334  setting: 0-2, parallax layers shown at >= 1 / 2 */
    bool debris;             /* 0x5e333  setting: full explosion debris (changes RNG use) */
    bool joystick;           /* 0x5e338  setting */
    bool fullscreen;         /* port setting (PORT.CFG) */
    int32_t scaling;         /* port setting (PORT.CFG): SCALE_SHARP/INTEGER/FILL */
    bool sound;              /* 0x5e27d  setting: sound effects on */
    int32_t volume;          /* 0x5e286  setting: 0-100 in steps of 20 */
    char hi_names[10][15];   /* 0x5e369  high score table (in SETTINGS.CFG) */
    int32_t hi_scores[10];   /* 0x5e3ff */
    int32_t demo_index;      /* 0x56de0  next demo, cycles 0..3 */
    int32_t load_slot;       /* 0x56dec  saved game to load at mission start, -1 = none */
    int32_t mission_type;    /* 0x5b878  current screen mode (MISSION_NORMAL/BONUS/...) */
    int32_t save_mission[6];     /* 0x5b840 */
    int32_t save_difficulty[6];  /* 0x5b858 */
    uint64_t mission_start_clock;    /* 0x5b83c */
    uint8_t f_56df0;
    /* 0x5b87c: control bits set by the input handler each frame (1/2 turn,
     * 4 thrust, 8 fire, 0x10 pause, 0x20 F1, 0x40 Esc); the request handler
     * acts on 0x10-0x40 and clears it. */
    uint32_t controls;
    uint8_t keys[4];         /* 0x5e34d..0x5e359: configured scancodes for bits 1, 2, 4, 8 */
    bool cheats;             /* 0x56e00: enables F5-F8 */
    bool save_requested;     /* 0x56df0 */
    int32_t f_580e4, f_5b838, f_5b86c, f_5b834, f_5b854;
    bool invulnerable;       /* 0x5e427: skip player collisions */
} Globals;

extern GameState g_state;
extern Globals g;

void game_init_globals(void);
void game_default_keys(void);
/* Read SETTINGS.CFG (0x1ab bytes, loaded to 0x5e27c by 0x12f48) from `dir`
 * or the current directory. Returns false (keeping defaults) if missing. */
bool game_load_settings(const char *dir);
/* 0x131d0: write the settings back (unknown bytes as they were read). */
bool game_save_settings(void);
/* Fill g_state from a legacy state block (demo or save), field by field. */
void legacy_load_state(GameState *st, const uint8_t raw[LEGACY_STATE_SIZE]);
void legacy_store_state(const GameState *st, uint8_t raw[LEGACY_STATE_SIZE]);
/* The game folder (where SETTINGS.CFG and the saves live). */
const char *game_dir(void);

#endif
