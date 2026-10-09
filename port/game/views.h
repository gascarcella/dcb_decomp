/* Views of the game's objects by PS1 offset, for the replay scripts' wait_mem (port/game/state.c's game_state_read):
 * an object whose host layout is not the PS1's (a pointer before the field moves it on the host) is mapped field by
 * field, each at its PS1 offset. state.c holds the executable's and the duel's; each overlay's are a table in
 * views_<overlay>.c (its headers clash with other overlays'), listed in state.c's game_overlay_views. */
#ifndef DCB_PORT_VIEWS_H
#define DCB_PORT_VIEWS_H

#include <stddef.h>
#include <stdint.h>

/* A field of an object whose host layout is not the PS1's: its PS1 offset and size, its host offset. */
typedef struct GameViewField {
    uint32_t ofs;
    uint32_t size;
    size_t host;
} GameViewField;
#define GAME_VIEW_FIELD(ofs, type, field) { ofs, sizeof(((type *)0)->field), offsetof(type, field) }
#define GAME_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* An object, or an array of `count` objects (PS1 stride `ps1_stride`, the host's its element size), mapped by field
   while its overlay is current. */
typedef struct GameView {
    uint32_t addr;       /* the PS1 address of the first object */
    uint32_t ps1_stride; /* the PS1 size of one (arrays; 0 for one object) */
    int count;
    void *host;
    size_t host_stride;
    int32_t overlay; /* the file ID that must be current */
    const GameViewField *fields;
    int nfields;
} GameView;
#define GAME_VIEW(sym, obj, n, ps1_size, overlay, fields) \
    { PS1_##sym, ps1_size, n, obj, sizeof(*(obj)), overlay, fields, GAME_COUNT(fields) }

/* The overlays' tables (views_<overlay>.c) */
extern const GameView game_subseg_views[];
extern const int game_subseg_view_count;

#endif /* DCB_PORT_VIEWS_H */
