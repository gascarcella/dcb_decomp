/* ENDSEG's objects the replay scripts wait on (tests/replay/scripts/records.json: the player records screen), by PS1
 * offset (views.h). END_SECTION_OFFSETS is pointer-free, its host layout the PS1's: END_runPlayerRecords sets it
 * once its files are loaded and the records counted, just before the screen's window opens ([1] is always 0x5E74);
 * the file's own zeroes until then. The screen's other state (its scroll, its window) is the function's locals.
 *
 * ENDSEG's code reads no object through a second layout (#36's audit): its one cast of a game object is the deck
 * file's pointer stored in SessionData's first field (npcDeckFile, a pointer on both layouts); the deck file, the
 * card art and the TIM list it indexes are disc files (byte offsets into pointer-free data); the profile and the
 * card database are read by field. */
#include "common.h"
#include "game.h"
#include "overlay_ids.h"
#include "views.h"

extern s32 END_SECTION_OFFSETS[12]; /* src/endseg/endseg.c's */

static const GameViewField game_section_offsets_fields[] = {
    { 0, sizeof(END_SECTION_OFFSETS), 0 },
};

const GameView game_endseg_views[] = {
    GAME_VIEW(END_SECTION_OFFSETS, END_SECTION_OFFSETS, 1, 0, GAME_OVERLAY_ENDSEG, game_section_offsets_fields),
};
const int game_endseg_view_count = GAME_COUNT(game_endseg_views);
