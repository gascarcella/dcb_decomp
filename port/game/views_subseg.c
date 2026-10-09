/* SUBSEG's objects the replay scripts wait on (tests/replay/scripts/deck_edit.json: the deck editor and the partner
 * equipment screen), by PS1 offset (views.h). Each holds a pointer before these fields on the host (a menu its window
 * and cursor, a window its label, the editor its buffers, the deck edit its counts, the deck menu its decks): the
 * editor's hidden is -1 between two screens and 0 once one runs, editing and listShown its sub-states; the deck
 * edit's mode (1 the slots, 2 the card picker) and slot; the deck menu's count and slot; the menus' rows and active
 * flags; the windows' animDone. */
#include "common.h"
#include "game.h"
#include "dcb/menu.h"
#include "dcb/subseg.h"
#include "dcb/sub_sort.h"
#include "overlay_ids.h"
#include "views.h"

#define GAME_UIWINDOW_SIZE 0x44
#if __SIZEOF_POINTER__ == 4
_Static_assert(offsetof(Menu, row) == 0x14 && offsetof(Menu, nrows) == 0x20 && offsetof(Menu, active) == 0x26 &&
                   sizeof(Menu) == 0x2C,
               "Menu: a mapped field is not at its PS1 offset");
_Static_assert(offsetof(UiWindow, cur.w) == 0x18 && offsetof(UiWindow, animDone) == 0x41 &&
                   sizeof(UiWindow) == GAME_UIWINDOW_SIZE,
               "UiWindow: a mapped field is not at its PS1 offset, or not the PS1 size (SUB_WINDOWS' stride)");
_Static_assert(offsetof(EditorState, hidden) == 0x14 && offsetof(EditorState, editing) == 0x15 &&
                   offsetof(EditorState, running) == 0x16 && offsetof(EditorState, listShown) == 0x17,
               "EditorState: a mapped field is not at its PS1 offset");
_Static_assert(offsetof(DeckEditState, cardId) == 0x108 && offsetof(DeckEditState, statsPage) == 0x10A &&
                   offsetof(DeckEditState, slot) == 0x10C && offsetof(DeckEditState, mode) == 0x10E,
               "DeckEditState: a mapped field is not at its PS1 offset");
_Static_assert(offsetof(DeckMenuState, count) == 0xC && offsetof(DeckMenuState, current) == 0xD &&
                   offsetof(DeckMenuState, slot) == 0xE,
               "DeckMenuState: a mapped field is not at its PS1 offset");
#endif

extern Menu SUB_ABILITY_MENU, SUB_EQUIPMENT_MENU; /* src/subseg/partner/sub_partner.c's */
extern UiWindow SUB_PARTNER_WINDOW, SUB_ARMOR_WINDOW, SUB_ABILITY_WINDOW, SUB_EQUIPMENT_WINDOW;
extern UiWindow SUB_DECK_SORT_WINDOW; /* src/subseg/sub_bss.c */

static const GameViewField game_menu_fields[] = {
    GAME_VIEW_FIELD(0x14, Menu, row), GAME_VIEW_FIELD(0x20, Menu, nrows), GAME_VIEW_FIELD(0x26, Menu, active),
};
static const GameViewField game_window_fields[] = {
    GAME_VIEW_FIELD(0x18, UiWindow, cur.w), GAME_VIEW_FIELD(0x41, UiWindow, animDone),
};
static const GameViewField game_editor_fields[] = {
    GAME_VIEW_FIELD(0x14, EditorState, hidden),  GAME_VIEW_FIELD(0x15, EditorState, editing),
    GAME_VIEW_FIELD(0x16, EditorState, running), GAME_VIEW_FIELD(0x17, EditorState, listShown),
};
static const GameViewField game_deck_edit_fields[] = {
    GAME_VIEW_FIELD(0x108, DeckEditState, cardId), GAME_VIEW_FIELD(0x10A, DeckEditState, statsPage),
    GAME_VIEW_FIELD(0x10C, DeckEditState, slot),   GAME_VIEW_FIELD(0x10E, DeckEditState, mode),
};
static const GameViewField game_deck_menu_fields[] = {
    GAME_VIEW_FIELD(0xC, DeckMenuState, count), GAME_VIEW_FIELD(0xD, DeckMenuState, current),
    GAME_VIEW_FIELD(0xE, DeckMenuState, slot),
};

const GameView game_subseg_views[] = {
    GAME_VIEW(SUB_EDITOR, &SUB_EDITOR, 1, 0, GAME_OVERLAY_SUBSEG, game_editor_fields),
    GAME_VIEW(SUB_DECK_EDIT, &SUB_DECK_EDIT, 1, 0, GAME_OVERLAY_SUBSEG, game_deck_edit_fields),
    GAME_VIEW(SUB_DECK_MENU, &SUB_DECK_MENU, 1, 0, GAME_OVERLAY_SUBSEG, game_deck_menu_fields),
    GAME_VIEW(SUB_CARD_LIST_MENU, &SUB_CARD_LIST_MENU, 1, 0, GAME_OVERLAY_SUBSEG, game_menu_fields),
    GAME_VIEW(SUB_CARD_SORT_MENU, &SUB_CARD_SORT_MENU, 1, 0, GAME_OVERLAY_SUBSEG, game_menu_fields),
    GAME_VIEW(SUB_DECK_SORT_MENU, &SUB_DECK_SORT_MENU, 1, 0, GAME_OVERLAY_SUBSEG, game_menu_fields),
    GAME_VIEW(SUB_EQUIPMENT_MENU, &SUB_EQUIPMENT_MENU, 1, 0, GAME_OVERLAY_SUBSEG, game_menu_fields),
    GAME_VIEW(SUB_ABILITY_MENU, &SUB_ABILITY_MENU, 1, 0, GAME_OVERLAY_SUBSEG, game_menu_fields),
    GAME_VIEW(SUB_WINDOWS, SUB_WINDOWS, GAME_COUNT(SUB_WINDOWS), GAME_UIWINDOW_SIZE, GAME_OVERLAY_SUBSEG,
              game_window_fields),
    GAME_VIEW(SUB_PARTNER_WINDOW, &SUB_PARTNER_WINDOW, 1, 0, GAME_OVERLAY_SUBSEG, game_window_fields),
    GAME_VIEW(SUB_ARMOR_WINDOW, &SUB_ARMOR_WINDOW, 1, 0, GAME_OVERLAY_SUBSEG, game_window_fields),
    GAME_VIEW(SUB_ABILITY_WINDOW, &SUB_ABILITY_WINDOW, 1, 0, GAME_OVERLAY_SUBSEG, game_window_fields),
    GAME_VIEW(SUB_EQUIPMENT_WINDOW, &SUB_EQUIPMENT_WINDOW, 1, 0, GAME_OVERLAY_SUBSEG, game_window_fields),
    GAME_VIEW(SUB_DECK_SORT_WINDOW, &SUB_DECK_SORT_WINDOW, 1, 0, GAME_OVERLAY_SUBSEG, game_window_fields),
};
const int game_subseg_view_count = GAME_COUNT(game_subseg_views);
