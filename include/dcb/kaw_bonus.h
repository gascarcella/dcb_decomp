#ifndef DCB_KAW_BONUS_H
#define DCB_KAW_BONUS_H

#include "game.h"

void KAW_trackSpecialties(s32 player);
s32 KAW_drawBonuses(s32 x, s32 y, s32 count, s32 z, s32 exp);
void KAW_countEarnedBonuses(void);
s32 KAW_checkHandBonuses(s32 player);
void KAW_showBonusBanner(s32 player, s32 id);

#endif /* DCB_KAW_BONUS_H */
