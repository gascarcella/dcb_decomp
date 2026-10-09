/* The game-state probes (psxstack/game.h, the adapter interface): what tests/replay/probes.lua reads from the
 * emulator's RAM, read from the host's objects: the stage, the map, the checkpoint image (the player's profile in its
 * PS1 layout, which is also what a save on the memory card holds: game_profile_to_ps1/_from_ps1), the volatile
 * ranges, and the PS1 addresses the replay scripts wait on. Also the accessor the executable's OPEN_MEMCARD_CANCELLED
 * reads through (include/dcb/overlay_calls.h). Adapted from dw2003recomp's port/game/state.c (docs/THIRD_PARTY.md).
 *
 * This unit includes the game's headers, which redeclare a few libc functions with the PS1's types (game.h: strlen,
 * memset, bzero, ...): it is compiled with -fno-builtin, as the game's units are (port/CMakeLists.txt), includes no
 * libc header of its own and copies bytes with its own loop. overlay_ids.h is
 * port/tools/port_inputs.py's (the overlay IDs, the PS1 addresses from config/us/). */
#include <stddef.h>
#include <stdint.h>

#include "port_runtime.h"

#include "common.h"
#include "game.h"
#include "dcb/openseg.h"
#include "dcb/dialog.h"
#include "dcb/saiseg.h"
#include "overlay_ids.h"

/* ---- The checkpoint image's layout: PlayerProfile (include/game.h, us), 0x2774 bytes on the PS1. It is not
 * pointer-free: each of the three partners keeps two pointers into the card database (baseCard, armorCard) and each
 * of the three saved decks thirty (CardSlot.card), all in the game's heap. On the host they are 8 bytes and the
 * struct is larger, so the image is written field by field in the PS1 layout, the pointers as PS1 addresses (the
 * heap's: GAME_PTR_TO_S32). Every span copied whole is checked here to have no pointer (its host
 * size is its PS1 size). */
#define GAME_PROFILE_SIZE 0x2774
#define GAME_PARTNER_SIZE 0x298
#define GAME_DECK_SIZE 0x110
#define GAME_CARD_SLOT_SIZE 8
#define GAME_SPAN(type, first, last) (offsetof(type, last) + sizeof(((type *)0)->last) - offsetof(type, first))

/* the profile: 0x00-0x80 (name .. unk58), the partners, 0x848-0x2438 (bonusCounts .. unk2435), the decks,
   0x2768-0x2774 (rewardCards .. unk2771) */
_Static_assert(offsetof(PlayerProfile, partners) == 0x80, "PlayerProfile: a field before partners grew");
_Static_assert(GAME_SPAN(PlayerProfile, bonusCounts, unk2435) == 0x2438 - 0x848, "PlayerProfile: bonusCounts..unk2435");
_Static_assert(GAME_SPAN(PlayerProfile, rewardCards, unk2771) == GAME_PROFILE_SIZE - 0x2768,
               "PlayerProfile: rewardCards..unk2771");
_Static_assert(offsetof(PlayerProfile, areaId) == 0x0E && sizeof(((PlayerProfile *)0)->areaId) == 1,
               "PlayerProfile.areaId: tests/replay/probes.lua PROFILE_AREA_ID");
_Static_assert(offsetof(PlayerProfile, profileSize) == 0x16 && sizeof(((PlayerProfile *)0)->profileSize) == 2,
               "PlayerProfile.profileSize");
_Static_assert(offsetof(PlayerProfile, profileId) == 0x10 && offsetof(PlayerProfile, playTime) == 0x24,
               "PlayerProfile: tests/replay/replay.py VOLATILE_RANGES");
/* a partner: card[2] (0x278, pointer-free), baseCard, armorCard, then 0x280-0x298 (hpBonus .. unk295) */
_Static_assert(sizeof(DigimonCardData) == 0x13C && offsetof(Partner, baseCard) == 0x278, "Partner.card[2]");
_Static_assert(GAME_SPAN(Partner, hpBonus, unk295) == GAME_PARTNER_SIZE - 0x280, "Partner: hpBonus..unk295");
/* a saved deck: 0x00-0x14 (inUse, name), cards[30] (type, index, id, card), then 0x104-0x110 (unk104 .. unk10E) */
_Static_assert(GAME_SPAN(PlayerDeck, inUse, name) == 0x14 && GAME_SPAN(CardSlot, type, id) == 4,
               "PlayerDeck: inUse..name; CardSlot: type..id");
_Static_assert(GAME_SPAN(PlayerDeck, unk104, unk10E) == GAME_DECK_SIZE - 0x104, "PlayerDeck: unk104..unk10E");
#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(PlayerProfile) == GAME_PROFILE_SIZE, "PlayerProfile: the PS1 size at -m32");
#endif

/* OPENSEG's objects the scripts wait on: pointer-free, or one field after a pointer */
_Static_assert(sizeof(TextScroll) == 0x20, "TextScroll (OPEN_INTRO_TEXT) is 8 s32");
_Static_assert(PS1_OPEN_MEMCARD_STATE - PS1_OPEN_MEMCARD == 0x535, "OPEN_MEMCARD_STATE is OPEN_MEMCARD.state");
#if __SIZEOF_POINTER__ == 4
_Static_assert(offsetof(MemcardScreen, state) == 0x535, "MemcardScreen.state: not at its PS1 offset");
#endif
extern s32 OPEN_TITLE_STATE; /* src/openseg/open_bss.c (only open_title.c declares it) */
extern UiWindow KAW_TUTORIAL_WINDOW; /* src/kawseg/kaw_bss.c (only kaw_tutorial.c declares it) */
extern s32 KAW_RESULT_SCREEN_STATE;  /* src/kawseg/kaw_bss.c (dcb/overlay_calls.h declares it too) */

/* The duel's words first_duel_play.json waits on, by their PS1 offsets: in the duel state (Duel, the heap block
   DUEL_STATE points to), in a window (UiWindow: KAW_TUTORIAL_WINDOW, the tutorial's message box) and in a dialog
   (Dialog: DUEL_DIALOG, the duel's Yes/No box). Each holds a pointer before these fields on the host, so they are
   mapped one by one (game_views below); at -m32 every field is at its PS1 offset. */
#if __SIZEOF_POINTER__ == 4
_Static_assert(offsetof(Duel, state) == 0x810 && offsetof(Duel, turnPlayer) == 0x817 && offsetof(Duel, step) == 0x818 &&
                   offsetof(Duel, cursorPlayer) == 0x81B && offsetof(Duel, cursorSlot) == 0x81C &&
                   offsetof(Duel, tutorialBusy) == 0x820 && offsetof(Duel, awaitingInput) == 0x822 &&
                   offsetof(Duel, quit) == 0x824 && sizeof(Duel) == 0x86C,
               "Duel: a mapped field is not at its PS1 offset");
_Static_assert(offsetof(UiWindow, cur.w) == 0x18 && offsetof(UiWindow, animDone) == 0x41,
               "UiWindow: a mapped field is not at its PS1 offset");
_Static_assert(offsetof(Dialog, choice) == 0xA5 && offsetof(Dialog, closed) == 0xB4 && sizeof(Dialog) == 0xB8,
               "Dialog: a mapped field is not at its PS1 offset");
#endif

/* tests/port/vram.py's alignment waits (docs/PORT.md "Testing"): the scrolling background's state after its tim
   pointer (mode .. texWindowH, pointer-free: scrollPos at +0x70), and SAISEG's area state from imageHidden to
   nextBlink (bytes: mode, typing, waitingForCross, nextBlink) */
#define GAME_SCROLL_TAIL 0x6C
#define GAME_SAI_AREA_BYTES 0x109
_Static_assert(GAME_SPAN(ScrollBackground, mode, texWindowH) == 0x80 - GAME_SCROLL_TAIL,
               "ScrollBackground: mode..texWindowH");
_Static_assert(offsetof(ScrollBackground, scrollPos) - offsetof(ScrollBackground, mode) == 0x70 - GAME_SCROLL_TAIL,
               "ScrollBackground.scrollPos");
_Static_assert(GAME_SPAN(AreaState, imageHidden, nextBlink) == 0x122 - GAME_SAI_AREA_BYTES,
               "AreaState: imageHidden..nextBlink");
_Static_assert(offsetof(AreaState, nextBlink) - offsetof(AreaState, imageHidden) == 0x121 - GAME_SAI_AREA_BYTES,
               "AreaState.nextBlink");
#if __SIZEOF_POINTER__ == 4
_Static_assert(offsetof(ScrollBackground, mode) == GAME_SCROLL_TAIL, "ScrollBackground.mode: not at its PS1 offset");
_Static_assert(offsetof(AreaState, imageHidden) == GAME_SAI_AREA_BYTES, "AreaState.imageHidden: not at its PS1 offset");
#endif

static void game_copy(u8 *dst, const void *src, size_t n) {
    const u8 *s = src;
    while (n--) {
        *dst++ = *s++;
    }
}

static void game_put32(u8 *out, uint32_t v) {
    out[0] = (u8)v;
    out[1] = (u8)(v >> 8);
    out[2] = (u8)(v >> 16);
    out[3] = (u8)(v >> 24);
}

/* A pointer as the PS1 address the game would keep (include/port.h GAME_PTR_TO_S32, src/main/system/heap.c: its
 * heap block's PS1 address plus the offset in the block; NULL is 0; anything else goes to psxstack's PTR_TO_S32,
 * fatal outside the arena). The host's block table holds the PS1's addresses, so it equals the emulator's as long as
 * the game makes the same allocations with the same sizes. */
static uint32_t game_heap_ps1(const void *p) {
    return (uint32_t)GAME_PTR_TO_S32(p);
}

/* The two profiles' block (PLAYER_PROFILES: player 0's, then player 1's) holds pointers into itself: a saved deck's
 * slot holding a partner's card points to that partner (card_db.c's deck check: CardSlot.card = &partners[slot]). Its
 * host layout is not the PS1's past partners[0], so such a pointer is mapped by field: a byte of partners[k].card[]
 * (pointer-free) at its PS1 offset. Returns 1 and the PS1 address when p is in the block. */
#define GAME_PROFILES 2
static int game_profile_ptr_ps1(const void *p, uint32_t *out) {
    const u8 *base = (const u8 *)PLAYER_PROFILES;
    size_t off, in, r;
    if (base == NULL || (const u8 *)p < base || (const u8 *)p >= base + GAME_PROFILES * sizeof(PlayerProfile)) {
        return 0;
    }
    off = (size_t)((const u8 *)p - base);
    in = off % sizeof(PlayerProfile);
    r = (in - offsetof(PlayerProfile, partners)) % sizeof(Partner);
    if (in < offsetof(PlayerProfile, partners) || in >= offsetof(PlayerProfile, bonusCounts) ||
        r >= offsetof(Partner, baseCard)) {
        port_fatal("state: a pointer into player %d's profile at host offset 0x%X is not in a partner's cards",
                   (int)(off / sizeof(PlayerProfile)), (unsigned)in);
    }
    *out = game_heap_ps1(base) + (uint32_t)(off / sizeof(PlayerProfile)) * GAME_PROFILE_SIZE + 0x80 +
           (uint32_t)((in - offsetof(PlayerProfile, partners)) / sizeof(Partner)) * GAME_PARTNER_SIZE + (uint32_t)r;
    return 1;
}

/* A pointer field of the profile as the image holds it: a pointer into the heap (or NULL) as its PS1 address (into
 * the profiles' block by field, above); any other value is not a pointer the game made but the bytes the heap block
 * held before (resetPlayerData leaves the partners' and the decks' card pointers as it finds them, and the boot's
 * checkpoints come before a profile exists): its low 32 bits, the word the PS1 would read there if its bytes were the
 * same. */
static uint32_t game_image_ptr(const void *p) {
    uint32_t v;
    if (game_profile_ptr_ps1(p, &v)) {
        return v;
    }
    if (p == NULL || game_heap_owns(p)) {
        return game_heap_ps1(p);
    }
    return (uint32_t)(uintptr_t)p;
}

/* ---- OPENSEG's OPEN_MEMCARD.cancelled, which the executable reads by its own name (game_flow.c) */
u8 *game_open_memcard_cancelled(void) {
    return &OPEN_MEMCARD.cancelled;
}

/* ---- The stage: the slot's first word, each overlay's own id (SUGSEG 4, KAWSEG 5, SAISEG 6, SUBSEG 7, OPENSEG 8,
 * EVOSEG 9, ENDSEG 10), 0 before the first load. The host reads the current overlay's own object, the one its C
 * defines first in its rodata (D_801DDF38, renamed per overlay under PC_PORT): the stage says which overlay's code
 * the host runs, whatever bytes the disc read put in the slot. ENDSEG's and EVOSEG's are strings ("\n", "\t"), whose
 * word is their bytes and the zero padding after them. */
extern const char END_D_801DDF38[], EVO_D_801DDF38[];
extern const s32 KAW_D_801DDF38, OPEN_D_801DDF38, SAI_D_801DDF38, SUB_D_801DDF38, SUG_D_801DDF38;

static int32_t game_string_word(const char *s) {
    uint32_t w = 0;
    int i;
    for (i = 0; i < 4 && s[i] != '\0'; i++) {
        w |= (uint32_t)(u8)s[i] << (8 * i);
    }
    return (int32_t)w;
}

int32_t game_state_stage(void) {
    const PortOverlay *o = port_overlay_current(GAME_OVERLAY_TIER);
    if (o == NULL) {
        return 0;
    }
    switch (o->file) {
    case GAME_OVERLAY_ENDSEG:
        return game_string_word(END_D_801DDF38);
    case GAME_OVERLAY_EVOSEG:
        return game_string_word(EVO_D_801DDF38);
    case GAME_OVERLAY_KAWSEG:
        return KAW_D_801DDF38;
    case GAME_OVERLAY_OPENSEG:
        return OPEN_D_801DDF38;
    case GAME_OVERLAY_SAISEG:
        return SAI_D_801DDF38;
    case GAME_OVERLAY_SUBSEG:
        return SUB_D_801DDF38;
    case GAME_OVERLAY_SUGSEG:
        return SUG_D_801DDF38;
    }
    port_fatal("state: the current overlay %s (file %d) has no stage word", o->name, (int)o->file);
}

/* ---- The map: the player's profile's areaId (the SAISEG area), 0 before initPlayerData */
int32_t game_state_map(void) {
    const PlayerProfile *p = (const PlayerProfile *)PLAYER_PROFILES;
    return p != NULL ? p->areaId : 0;
}

/* ---- The random index: LIBC2's rand() state, what the PS1 keeps in D_801DDC10 (0 at power-on, then each draw's).
 * The shim's rand is the PS1's generator (psxstack v0.3.3), so the value is the same kind as the emulator's; the
 * record keeps it but the test does not compare it: the idle loop in main() calls rand() as often as it spins between
 * vsyncs on the PS1, a count the host does not reproduce (docs/PORT.md "Testing"). */
int32_t game_state_random_index(void) {
    return (int32_t)port_rand_seed();
}

/* ---- The profile in its PS1 layout: what the checkpoint image and a save on the memory card hold (the game writes
 * the profile to the card as it is in memory: open_save.c's OPEN_prepareSaveData copies it into the card buffer, and
 * OPEN_applyLoadedSave copies the buffer back). The host's PlayerProfile is 0x2A78 bytes with its pointers 8 bytes
 * wide; the card holds the PS1's 0x2774, so a save moves between the port and the emulator (docs/PORT.md "Saves"). */
static void game_image_partner(u8 *out, const Partner *p) {
    game_copy(out, p->card, sizeof(p->card));
    game_put32(out + 0x278, game_image_ptr(p->baseCard));
    game_put32(out + 0x27C, game_image_ptr(p->armorCard));
    game_copy(out + 0x280, (const u8 *)p + offsetof(Partner, hpBonus), GAME_SPAN(Partner, hpBonus, unk295));
}

static void game_image_deck(u8 *out, const PlayerDeck *d) {
    int i;
    game_copy(out, d, GAME_SPAN(PlayerDeck, inUse, name));
    for (i = 0; i < (int)(sizeof(d->cards) / sizeof(d->cards[0])); i++) {
        u8 *o = out + 0x14 + GAME_CARD_SLOT_SIZE * i;
        game_copy(o, &d->cards[i], GAME_SPAN(CardSlot, type, id));
        game_put32(o + 4, game_image_ptr(d->cards[i].card));
    }
    game_copy(out + 0x104, (const u8 *)d + offsetof(PlayerDeck, unk104), GAME_SPAN(PlayerDeck, unk104, unk10E));
}

void game_profile_to_ps1(u8 *out, const void *profile) {
    const PlayerProfile *p = profile;
    int i;
    game_copy(out, p, offsetof(PlayerProfile, partners));
    if (p->profileSize == (s16)sizeof(PlayerProfile)) {
        /* resetPlayerData stores sizeof(PlayerProfile): the PS1's is the image's */
        out[offsetof(PlayerProfile, profileSize)] = (u8)GAME_PROFILE_SIZE;
        out[offsetof(PlayerProfile, profileSize) + 1] = (u8)(GAME_PROFILE_SIZE >> 8);
    }
    for (i = 0; i < 3; i++) {
        game_image_partner(out + 0x80 + GAME_PARTNER_SIZE * i, &p->partners[i]);
        game_image_deck(out + 0x2438 + GAME_DECK_SIZE * i, &p->savedDecks[i]);
    }
    game_copy(out + 0x848, (const u8 *)p + offsetof(PlayerProfile, bonusCounts), GAME_SPAN(PlayerProfile, bonusCounts, unk2435));
    game_copy(out + 0x2768, (const u8 *)p + offsetof(PlayerProfile, rewardCards), GAME_SPAN(PlayerProfile, rewardCards, unk2771));
}

static uint32_t game_get32(const u8 *in) {
    return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}

/* A pointer field from its PS1 word (game_image_ptr's inverse): a heap address becomes the host pointer to the same
 * byte (the host's block table holds the PS1's addresses, so a card pointer saved by the emulator points into the
 * same card database here; one into the profiles' block, a partner's cards, by field), 0 NULL; any other word, and
 * an address in the profiles' block outside the partners' cards, is not a pointer the game made (the stale bytes of
 * the partners and decks the game has not set up, as the PS1's heap held them) and is kept as it is, so it is saved
 * back the same. */
static void *game_ps1_ptr(uint32_t v) {
    uint32_t base, off, in, r;
    if (v == 0) {
        return NULL;
    }
    if (!game_heap_holds((s32)v)) {
        return (void *)(uintptr_t)v;
    }
    base = game_heap_ps1((const void *)PLAYER_PROFILES);
    off = v - base;
    if (PLAYER_PROFILES != 0 && off < GAME_PROFILES * GAME_PROFILE_SIZE) {
        in = off % GAME_PROFILE_SIZE;
        r = (in - 0x80) % GAME_PARTNER_SIZE;
        if (in < 0x80 || in >= 0x848 || r >= 0x278) {
            return (void *)(uintptr_t)v;
        }
        return (u8 *)&((PlayerProfile *)PLAYER_PROFILES)[off / GAME_PROFILE_SIZE].partners[(in - 0x80) /
                                                                                         GAME_PARTNER_SIZE] + r;
    }
    return GAME_S32_TO_PTR(void *, (s32)v);
}

static void game_unpack_partner(Partner *p, const u8 *in) {
    game_copy((u8 *)p->card, in, sizeof(p->card));
    p->baseCard = game_ps1_ptr(game_get32(in + 0x278));
    p->armorCard = game_ps1_ptr(game_get32(in + 0x27C));
    game_copy((u8 *)p + offsetof(Partner, hpBonus), in + 0x280, GAME_SPAN(Partner, hpBonus, unk295));
}

static void game_unpack_deck(PlayerDeck *d, const u8 *in) {
    int i;
    game_copy((u8 *)d, in, GAME_SPAN(PlayerDeck, inUse, name));
    for (i = 0; i < (int)(sizeof(d->cards) / sizeof(d->cards[0])); i++) {
        const u8 *o = in + 0x14 + GAME_CARD_SLOT_SIZE * i;
        game_copy((u8 *)&d->cards[i] + offsetof(CardSlot, type), o, GAME_SPAN(CardSlot, type, id));
        d->cards[i].card = game_ps1_ptr(game_get32(o + 4));
    }
    game_copy((u8 *)d + offsetof(PlayerDeck, unk104), in + 0x104, GAME_SPAN(PlayerDeck, unk104, unk10E));
}

void game_profile_from_ps1(void *profile, const u8 *in) {
    PlayerProfile *p = profile;
    int i;
    game_copy((u8 *)p, in, offsetof(PlayerProfile, partners));
    if (p->profileSize == GAME_PROFILE_SIZE) {
        p->profileSize = (s16)sizeof(PlayerProfile); /* game_profile_to_ps1's inverse */
    }
    for (i = 0; i < 3; i++) {
        game_unpack_partner(&p->partners[i], in + 0x80 + GAME_PARTNER_SIZE * i);
        game_unpack_deck(&p->savedDecks[i], in + 0x2438 + GAME_DECK_SIZE * i);
    }
    game_copy((u8 *)p + offsetof(PlayerProfile, bonusCounts), in + 0x848, GAME_SPAN(PlayerProfile, bonusCounts, unk2435));
    game_copy((u8 *)p + offsetof(PlayerProfile, rewardCards), in + 0x2768, GAME_SPAN(PlayerProfile, rewardCards, unk2771));
}

/* ---- The checkpoint image: the first PlayerProfile at PLAYER_PROFILES in its PS1 layout, zeros before it exists */
uint32_t game_state_image_size(void) {
    return GAME_PROFILE_SIZE;
}

void game_state_image(uint8_t *out) {
    const PlayerProfile *p = (const PlayerProfile *)PLAYER_PROFILES;
    int i;
    if (p == NULL) {
        for (i = 0; i < GAME_PROFILE_SIZE; i++) {
            out[i] = 0;
        }
        return;
    }
    game_profile_to_ps1(out, p);
}

int game_state_volatile_count(void) {
    return port_gamestate_volatile_count;
}

const PortRange *game_state_volatile(void) {
    return port_gamestate_volatile;
}

/* ---- PS1 addresses the replay scripts read (wait_mem). The overlay slot is the arena's, which the runtime maps.
 * Mapped here:
 * - the duel's words (game_views): the duel state's fields, while DUEL_STATE points into the heap (its PS1 address
 *   is the block's, the same as the emulator's: first_duel_play.json names it), the tutorial window's (KAWSEG
 *   current) and the duel dialog's;
 * - PAD_INPUT_ENABLED and KAWSEG's KAW_RESULT_SCREEN_STATE (pointer-free words, whose names the generated tables do
 *   not list: a .bss stand-in, an overlay's);
 * - PLAYER_PROFILES (an s32p, 8 bytes on the host): read only, as the PS1 address of the heap block it points to
 *   (the scripts compare it with 0x800C8964: the host's block table holds the PS1's addresses);
 * - OPENSEG's objects, while OPENSEG is the current overlay (on the PS1 the slot holds another file's bytes
 *   otherwise): OPEN_TITLE_STATE, OPEN_INTRO_TEXT (pointer-free), OPEN_MEMCARD.state (a field after a pointer);
 * - tests/port/vram.py's alignment waits: SCROLL_BACKGROUND's fields after its tim pointer (scrollPos), and SAISEG's
 *   SAI_AREA bytes imageHidden..nextBlink while SAISEG is current;
 * - the executable's sized data symbols whose host layout is the PS1's (port_exe_data, generated). */
typedef struct GameField {
    uint32_t addr;   /* the PS1 address */
    uint32_t size;   /* the PS1 size: the host object's */
    void *host;      /* the host object */
    int32_t overlay; /* 0: the executable's; else the file ID that must be current */
} GameField;

static const GameField game_fields[] = {
    { PS1_OPEN_TITLE_STATE, sizeof(OPEN_TITLE_STATE), &OPEN_TITLE_STATE, GAME_OVERLAY_OPENSEG },
    { PS1_OPEN_INTRO_TEXT, sizeof(OPEN_INTRO_TEXT), &OPEN_INTRO_TEXT, GAME_OVERLAY_OPENSEG },
    { PS1_OPEN_MEMCARD_STATE, sizeof(OPEN_MEMCARD.state), &OPEN_MEMCARD.state, GAME_OVERLAY_OPENSEG },
    { PS1_SCROLL_BACKGROUND + GAME_SCROLL_TAIL, 0x80 - GAME_SCROLL_TAIL, &SCROLL_BACKGROUND.mode, 0 },
    { PS1_SAI_AREA + GAME_SAI_AREA_BYTES, 0x122 - GAME_SAI_AREA_BYTES, &SAI_AREA.imageHidden, GAME_OVERLAY_SAISEG },
    { PS1_PAD_INPUT_ENABLED, sizeof(PAD_INPUT_ENABLED), &PAD_INPUT_ENABLED, 0 },
    { PS1_KAW_RESULT_SCREEN_STATE, sizeof(KAW_RESULT_SCREEN_STATE), &KAW_RESULT_SCREEN_STATE, GAME_OVERLAY_KAWSEG },
};
#define GAME_FIELD_COUNT ((int)(sizeof(game_fields) / sizeof(game_fields[0])))

/* A field of an object whose host layout is not the PS1's: its PS1 offset and size, its host offset. */
typedef struct GameViewField {
    uint32_t ofs;
    uint32_t size;
    size_t host;
} GameViewField;
#define GAME_VIEW_FIELD(ofs, type, field) { ofs, sizeof(((type *)0)->field), offsetof(type, field) }

static const GameViewField game_duel_fields[] = {
    GAME_VIEW_FIELD(0x810, Duel, state),        GAME_VIEW_FIELD(0x817, Duel, turnPlayer),
    GAME_VIEW_FIELD(0x818, Duel, step),         GAME_VIEW_FIELD(0x81B, Duel, cursorPlayer),
    GAME_VIEW_FIELD(0x81C, Duel, cursorSlot),   GAME_VIEW_FIELD(0x820, Duel, tutorialBusy),
    GAME_VIEW_FIELD(0x822, Duel, awaitingInput), GAME_VIEW_FIELD(0x824, Duel, quit),
};
static const GameViewField game_window_fields[] = {
    GAME_VIEW_FIELD(0x18, UiWindow, cur.w), GAME_VIEW_FIELD(0x41, UiWindow, animDone),
};
static const GameViewField game_dialog_fields[] = {
    GAME_VIEW_FIELD(0x18, Dialog, win.cur.w), GAME_VIEW_FIELD(0x41, Dialog, win.animDone),
    GAME_VIEW_FIELD(0xA5, Dialog, choice),    GAME_VIEW_FIELD(0xB4, Dialog, closed),
};
#define GAME_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* The host bytes of the field at `ofs` (`size` bytes) of an object at PS1 `base` and host `host`, or NULL. */
static u8 *game_view_field(uint32_t addr, uint32_t size, uint32_t base, void *host, const GameViewField *fields,
                           int count) {
    int i;
    for (i = 0; i < count; i++) {
        if (addr >= base + fields[i].ofs && addr - base - fields[i].ofs + size <= fields[i].size) {
            return (u8 *)host + fields[i].host + (addr - base - fields[i].ofs);
        }
    }
    return NULL;
}

/* The duel's words (above): NULL when `addr` is none of them or its object is not there. */
static u8 *game_views(uint32_t addr, uint32_t size) {
    const PortOverlay *o = port_overlay_current(GAME_OVERLAY_TIER);
    u8 *p;
    if ((p = game_view_field(addr, size, PS1_DUEL_DIALOG, &DUEL_DIALOG, game_dialog_fields,
                             GAME_COUNT(game_dialog_fields))) != NULL) {
        return p;
    }
    if (o != NULL && o->file == GAME_OVERLAY_KAWSEG &&
        (p = game_view_field(addr, size, PS1_KAW_TUTORIAL_WINDOW, &KAW_TUTORIAL_WINDOW, game_window_fields,
                             GAME_COUNT(game_window_fields))) != NULL) {
        return p;
    }
    if (DUEL_STATE != NULL && game_heap_owns(DUEL_STATE)) {
        return game_view_field(addr, size, game_heap_ps1(DUEL_STATE), DUEL_STATE, game_duel_fields,
                               GAME_COUNT(game_duel_fields));
    }
    return NULL;
}

/* The host bytes of PS1 address `addr` for a `size`-byte access, or NULL outside a mapped range. */
static u8 *game_state_map_addr(uint32_t addr, uint32_t size) {
    u8 *view;
    int i;
    if ((view = game_views(addr, size)) != NULL) {
        return view;
    }
    for (i = 0; i < GAME_FIELD_COUNT; i++) {
        const GameField *f = &game_fields[i];
        if (addr >= f->addr && addr - f->addr + size <= f->size) {
            const PortOverlay *o = port_overlay_current(GAME_OVERLAY_TIER);
            if (f->overlay != 0 && (o == NULL || o->file != f->overlay)) {
                return NULL;
            }
            return (u8 *)f->host + (addr - f->addr);
        }
    }
    for (i = 0; i < port_exe_data_count; i++) {
        const PortExeData *d = &port_exe_data[i];
        if (addr >= d->addr && addr - d->addr + size <= d->identical) {
            return (u8 *)(uintptr_t)d->host + (addr - d->addr);
        }
    }
    return NULL;
}

void *game_state_host(uint32_t addr, int size) {
    if (size != 1 && size != 2 && size != 4) {
        return NULL;
    }
    return game_state_map_addr(addr, (uint32_t)size);
}

int game_state_read(uint32_t addr, int size, int is_signed, int32_t *out) {
    const u8 *p;
    uint32_t v = 0;
    int i;
    if (size != 1 && size != 2 && size != 4) {
        return 0;
    }
    if (addr == PS1_PLAYER_PROFILES && size == 4) {
        *out = (int32_t)game_heap_ps1((const void *)PLAYER_PROFILES);
        return 1;
    }
    p = game_state_map_addr(addr, (uint32_t)size);
    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < size; i++) {
        v |= (uint32_t)p[i] << (8 * i);
    }
    if (is_signed && size < 4 && (v & (1u << (8 * size - 1))) != 0) {
        v |= ~0u << (8 * size);
    }
    *out = (int32_t)v;
    return 1;
}
