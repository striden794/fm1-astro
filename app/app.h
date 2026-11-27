/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Tomorrow's Man (TIDE) */
/* TIDE (from AMB-1) app: the project (everything that is saved), the app's entry points and its half of the contract with
 * fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "amb.h"

#define APP_NAME "STARDRIFT"             /* the start-up screen's name, and the crash, safe-mode and screen-reader ones (SAMSARA: the original) */
#define APP_TAGLINE "GENERATIVE SPACE AMBIENCE"   /* under the name on the start-up screen; capitals: the small face has no
                                          * lower case in this build (the original's was a dedication: "FOR BRETT") */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: TIDE's project in its own sector pair, 0xD8000 / 0xD9000 in the data area (Tomorrow's Man, TESTING #5), so AMB-1 and
 * its descendants, which all keep theirs at 0xFC000, and TIDE never overwrite each other's pieces; the panel
 * calibration at 0xDC000, where AMB-1 keeps it (it carries over) */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xD8000u, 0xDC000u}
#define PROJ_MAGIC 0x314D4153u           /* "SAM1" (AMB-1's is "ENO1": a record of its is not read). SAMSARA is the
                                          * name Tomorrow's Man gave TIDE on 2026-11-10; "TIDE" in these files is the same work */
#define PROJ_FORMAT 1u                   /* TIDE 1: AMB-1's format 4 and the main page's Mood and Space; another
                                          * format gives the recipes */
#define NPIECES EN_NPIECES

typedef struct {
    int16_t par[P_NPARAMS];
    uint16_t pool;
} slot_t;

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* the piece playing, as it is now (amb.h P_*) */
    uint16_t pool;                       /* its scale, relative to the root (bit 0 = the root) */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t piece;                       /* which piece */
    uint8_t rsv0[2];
    slot_t slot[NPIECES];                /* every piece as it was left (Tune and MIDI out are global) */
    uint8_t rsv[16];                     /* room to grow */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */
const char *piece_name(int i);
void project_piece(int i);               /* the playing piece kept, piece i taken up as it was left */
void project_piece_reset(void);          /* the playing piece back to its recipe */

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved project (or the defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
#define APP_OVERLOAD                     /* fm1-base calls app_overload when a block took over 90 % of its time */
void app_overload(void);                 /* the guard sheds a step (amb.h en_load) */
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's part (app/host.c): AMB-1's checks, its commands, the screen reader's dump */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);              /* the project forgotten, as RAM is at a restart */
