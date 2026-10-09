/* EVOSEG's objects the replay scripts wait on (tests/replay/scripts/fusion.json: the Fusion Shop, a card fusion with
 * its cutscene, a partner fusion), by PS1 offset (views.h). EVO_FUSION holds the card archive's pointer before its
 * words (the script's scriptState and textTyping, the screen's step, the type choice's fusionType and swapState, the
 * card list's pickSlot and previewOpen, the result's resultKind, cutscene and resultStep), mapped as one pointer-free
 * span; EVO_DIALOG is a Dialog on the host (issue #23), mapped as state.c maps DUEL_DIALOG; EVO_CURSOR_CARD (the card
 * under the list's cursor), EVO_CUTSCENE_STEP and EVO_BANNER_FADE (the fused card's banner, 1 while it fades in) are
 * pointer-free words. */
#include "common.h"
#include "game.h"
#include "dcb/dialog.h"
#include "dcb/evoseg.h"
#include "overlay_ids.h"
#include "views.h"

#define GAME_EVO_FUSION_WORDS 0xA4 /* EvoFusion.swapTimer .. busy[2] */
#define GAME_SPAN(type, first, last) (offsetof(type, last) + sizeof(((type *)0)->last) - offsetof(type, first))
_Static_assert(GAME_SPAN(EvoFusion, swapTimer, busy) == 0xCF - GAME_EVO_FUSION_WORDS, "EvoFusion: swapTimer..busy");
#if __SIZEOF_POINTER__ == 4
_Static_assert(offsetof(EvoFusion, swapTimer) == GAME_EVO_FUSION_WORDS && sizeof(EvoFusion) == 0xD0,
               "EvoFusion: swapTimer not at its PS1 offset");
_Static_assert(offsetof(Dialog, win.cur.w) == 0x18 && offsetof(Dialog, win.animDone) == 0x41 &&
                   offsetof(Dialog, choice) == 0xA5 && offsetof(Dialog, closed) == 0xB4,
               "Dialog: a mapped field is not at its PS1 offset");
#endif

extern s16 EVO_CURSOR_CARD; /* src/evoseg/evo_bss.c */
extern s8 EVO_BANNER_FADE;  /* src/evoseg/evo_bss.c */

static const GameViewField game_fusion_fields[] = {
    { GAME_EVO_FUSION_WORDS, 0xCF - GAME_EVO_FUSION_WORDS, offsetof(EvoFusion, swapTimer) },
};
static const GameViewField game_dialog_fields[] = {
    GAME_VIEW_FIELD(0x18, Dialog, win.cur.w), GAME_VIEW_FIELD(0x41, Dialog, win.animDone),
    GAME_VIEW_FIELD(0xA5, Dialog, choice),    GAME_VIEW_FIELD(0xB4, Dialog, closed),
};
static const GameViewField game_s16_fields[] = { { 0, 2, 0 } };
static const GameViewField game_s8_fields[] = { { 0, 1, 0 } };

const GameView game_evoseg_views[] = {
    GAME_VIEW(EVO_FUSION, &EVO_FUSION, 1, 0, GAME_OVERLAY_EVOSEG, game_fusion_fields),
    GAME_VIEW(EVO_DIALOG, &EVO_DIALOG, 1, 0, GAME_OVERLAY_EVOSEG, game_dialog_fields),
    GAME_VIEW(EVO_CURSOR_CARD, &EVO_CURSOR_CARD, 1, 0, GAME_OVERLAY_EVOSEG, game_s16_fields),
    GAME_VIEW(EVO_CUTSCENE_STEP, &EVO_CUTSCENE_STEP, 1, 0, GAME_OVERLAY_EVOSEG, game_s8_fields),
    GAME_VIEW(EVO_BANNER_FADE, &EVO_BANNER_FADE, 1, 0, GAME_OVERLAY_EVOSEG, game_s8_fields),
};
const int game_evoseg_view_count = GAME_COUNT(game_evoseg_views);
