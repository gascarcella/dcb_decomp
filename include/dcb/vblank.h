#ifndef DCB_VBLANK_H
#define DCB_VBLANK_H

#include "game.h"

extern s32 VBLANK_COUNTER;
extern Screen SCREEN_COPY_EFFECT;
#ifdef PC_PORT
/* the PS1's label of SCREEN_COPY_EFFECT.mode (+ 0x13F): one object on the host (issue #11) */
#define SCREEN_COPY_MODE (SCREEN_COPY_EFFECT.mode)
#else
extern u8 SCREEN_COPY_MODE;
#endif
extern s32 RENDER_CALLBACKS_ENABLED;

void tickVblankCounters(void);

#endif /* DCB_VBLANK_H */
