#ifndef DCB_ANIM_CONTROL_H
#define DCB_ANIM_CONTROL_H

#include "game.h"

typedef struct {
    /* 0x00 */ s32 unk0[5];
    /* 0x14 */ s32 unk14;
} Obj18;
typedef struct {
    /* 0x00 */ s32 id;
    /* 0x04 */ s32 flags;
    /* 0x08 */ s32 nobj;
    /* 0x0C */ Obj18 obj[1];
} Tmd18;

/* The primitive data of the OMD object a GsDOBJ4's tmd points at (its
   Tmd18's obj[0]). The file's unk14 is an offset from the Tmd18, which
   relocateOmdObjects turns into the address in place on the PS1; the word is
   the file's 32 bits, so the host keeps the offset and adds the Tmd18's
   address here. */
#ifdef PC_PORT
#define OMD_OBJ_DATA(tmd) ((u32 *)((u8 *)(tmd) - __builtin_offsetof(Tmd18, obj) + ((Obj18 *)(tmd))->unk14))
#else
#define OMD_OBJ_DATA(tmd) ((u32 *)(tmd)[5])
#endif
typedef struct {
    s32 key;
    s32p value;
} KeyValue;

void initModelScene(void);
void pauseModelAnimation(s32 slot);
void resumeModelAnimation(s32 slot);
void unloadModelAnimations(s32 slot);
void unloadEffectAnimations(void);
s32p findAnimationCacheEntry(s32 key, s32 count, KeyValue **freeEntry);
s32p loadAnimationData(s32 id, s32 anim, s32 slot, Chunk *pak);
void setModelAnimationData(Model *model, s32 *data, s32 anim);
s32 loadModelAnimation(s32 slot, s32 anim, s32 index, s32p pak);
s32 loadModelAnimationFile(s32 slot, s32 anim, s32 index);
s32 startModelAnimation(s32 slot, s32 anim, s32 loopKey, s32 rootOnly);
void applyAnimationFirstFrame(s32 slot, s32 anim);

#endif /* DCB_ANIM_CONTROL_H */
