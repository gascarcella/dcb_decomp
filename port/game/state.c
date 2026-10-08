/* The game-state probes (psxstack/game.h, the adapter interface): what tests/replay/probes.lua reads from the
 * emulator's RAM, read from the host's objects: the stage, the map, the checkpoint image (the player's profile in its
 * PS1 layout), the volatile ranges, and the PS1 addresses the replay scripts wait on. Also the accessor the
 * executable's OPEN_MEMCARD_CANCELLED reads through (include/dcb/overlay_calls.h). Adapted from dw2003recomp's
 * port/game/state.c (docs/THIRD_PARTY.md).
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
#include "dcb/heap.h"
#include "dcb/openseg.h"
#include "overlay_ids.h"

/* ---- The checkpoint image's layout: PlayerProfile (include/game.h, us), 0x2774 bytes on the PS1. It is not
 * pointer-free: each of the three partners keeps two pointers into the card database (baseCard, armorCard) and each
 * of the three saved decks thirty (CardSlot.card), all in the game's heap. On the host they are 8 bytes and the
 * struct is larger, so the image is written field by field in the PS1 layout, the pointers as PS1 addresses (the
 * heap's: HEAP_ARENA's address plus the offset). Every span copied whole is checked here to have no pointer (its host
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

/* the heap PLAYER_PROFILES and the card pointers point into: HEAP_ARENA, src/main/system/heap.c's HEAP_SIZE (us) */
#define GAME_HEAP_SIZE 0x148000u

/* OPENSEG's objects the scripts wait on: pointer-free, or one field after a pointer */
_Static_assert(sizeof(TextScroll) == 0x20, "TextScroll (OPEN_INTRO_TEXT) is 8 s32");
_Static_assert(PS1_OPEN_MEMCARD_STATE - PS1_OPEN_MEMCARD == 0x535, "OPEN_MEMCARD_STATE is OPEN_MEMCARD.state");
#if __SIZEOF_POINTER__ == 4
_Static_assert(offsetof(MemcardScreen, state) == 0x535, "MemcardScreen.state: not at its PS1 offset");
#endif
extern s32 OPEN_TITLE_STATE; /* src/openseg/open_bss.c (only open_title.c declares it) */

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

/* A pointer into the game's heap as its PS1 address (HEAP_ARENA's, at the same offset); NULL is 0, anything else is
 * fatal. The PS1 address equals the emulator's only while the host heap lays its blocks out as the PS1's. */
static uint32_t game_heap_ps1(const void *p, const char *what) {
    const u8 *base = (const u8 *)&HEAP_ARENA;
    if (p == NULL) {
        return 0;
    }
    if ((const u8 *)p < base || (const u8 *)p >= base + GAME_HEAP_SIZE) {
        port_fatal("state: %s (%p) is not in the game's heap", what, p);
    }
    return PS1_HEAP_ARENA + (uint32_t)((const u8 *)p - base);
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

/* game_state_random_index: libc's rand() state on the PS1 (D_801DDC10). Not here: psxstack's shim has no LIBC2
 * rand, so the host's libc rand links in its place (another sequence and RAND_MAX) and its state is not the game's to
 * read. The weak default (0) stands until the shim has Psy-Q's rand and a way to read its state. */

/* ---- The checkpoint image: the first PlayerProfile at PLAYER_PROFILES in its PS1 layout, zeros before it exists */
uint32_t game_state_image_size(void) {
    return GAME_PROFILE_SIZE;
}

static void game_image_partner(u8 *out, const Partner *p) {
    game_copy(out, p->card, sizeof(p->card));
    game_put32(out + 0x278, game_heap_ps1(p->baseCard, "Partner.baseCard"));
    game_put32(out + 0x27C, game_heap_ps1(p->armorCard, "Partner.armorCard"));
    game_copy(out + 0x280, &p->hpBonus, GAME_SPAN(Partner, hpBonus, unk295));
}

static void game_image_deck(u8 *out, const PlayerDeck *d) {
    int i;
    game_copy(out, d, GAME_SPAN(PlayerDeck, inUse, name));
    for (i = 0; i < (int)(sizeof(d->cards) / sizeof(d->cards[0])); i++) {
        u8 *o = out + 0x14 + GAME_CARD_SLOT_SIZE * i;
        game_copy(o, &d->cards[i], GAME_SPAN(CardSlot, type, id));
        game_put32(o + 4, game_heap_ps1(d->cards[i].card, "CardSlot.card"));
    }
    game_copy(out + 0x104, &d->unk104, GAME_SPAN(PlayerDeck, unk104, unk10E));
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
    game_copy(out, p, offsetof(PlayerProfile, partners));
    for (i = 0; i < 3; i++) {
        game_image_partner(out + 0x80 + GAME_PARTNER_SIZE * i, &p->partners[i]);
        game_image_deck(out + 0x2438 + GAME_DECK_SIZE * i, &p->savedDecks[i]);
    }
    game_copy(out + 0x848, p->bonusCounts, GAME_SPAN(PlayerProfile, bonusCounts, unk2435));
    game_copy(out + 0x2768, p->rewardCards, GAME_SPAN(PlayerProfile, rewardCards, unk2771));
}

int game_state_volatile_count(void) {
    return port_gamestate_volatile_count;
}

const PortRange *game_state_volatile(void) {
    return port_gamestate_volatile;
}

/* ---- PS1 addresses the replay scripts read (wait_mem). The overlay slot is the arena's, which the runtime maps.
 * Mapped here:
 * - PLAYER_PROFILES (an s32p, 8 bytes on the host): read only, as the PS1 address of the heap block it points to
 *   (the scripts compare it with 0x800C8964: the host heap must lay its blocks out as the PS1's);
 * - OPENSEG's objects, while OPENSEG is the current overlay (on the PS1 the slot holds another file's bytes
 *   otherwise): OPEN_TITLE_STATE, OPEN_INTRO_TEXT (pointer-free), OPEN_MEMCARD.state (a field after a pointer);
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
};
#define GAME_FIELD_COUNT ((int)(sizeof(game_fields) / sizeof(game_fields[0])))

/* The host bytes of PS1 address `addr` for a `size`-byte access, or NULL outside a mapped range. */
static u8 *game_state_map_addr(uint32_t addr, uint32_t size) {
    int i;
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
        *out = (int32_t)game_heap_ps1((const void *)PLAYER_PROFILES, "PLAYER_PROFILES");
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
