#include "common.h"
#include "gte.h"
#include "game.h"
#include "dcb/prim3d.h"

void transformAndAddPolyFT3(s32p poly, s32p vert0, s32p vert1, s32p vert2, u8 cullBackface, s32 fixedOtz) {
    s32 depthCue;
    s32 flag;
    s32 otz;

    if (cullBackface == 0) {
        otz = RotTransPers3((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (s32 *)(poly + 8), (s32 *)(poly + 0x10),
                            (s32 *)(poly + 0x18), &depthCue, &flag);
        goto block_3;
    }
    if (RotAverageNclip3((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (s32 *)(poly + 8), (s32 *)(poly + 0x10),
                         (s32 *)(poly + 0x18), &depthCue, &otz, &flag) > 0) {
block_3:
        if ((u32) (otz - 2) < 0xFFFU) {
            if (fixedOtz == 0) {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)poly);
                return;
            }
            AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)poly);
        }
    }
}

void transformAndAddPolyFT4(s32p poly, s32p vert0, s32p vert1, s32p vert2, s32p vert3, u8 cullBackface, s32 fixedOtz) {
    s32 depthCue;
    s32 flag;
    s32 otz;

    if (cullBackface == 0) {
        otz = RotTransPers4((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (SVECTOR *)vert3, (s32 *)(poly + 8), (s32 *)(poly + 0x10),
                            (s32 *)(poly + 0x18), (s32 *)(poly + 0x20), &depthCue, &flag);
        goto block_3;
    }
    if (RotAverageNclip4((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (SVECTOR *)vert3, (s32 *)(poly + 8), (s32 *)(poly + 0x10),
                         (s32 *)(poly + 0x18), (s32 *)(poly + 0x20), &depthCue, &otz, &flag) > 0) {
block_3:
        if ((u32) (otz - 2) < 0xFFFU) {
            if (fixedOtz == 0) {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)poly);
                return;
            }
            AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)poly);
        }
    }
}

void transformAndAddPolyGT4(s32p poly, s32p vert0, s32p vert1, s32p vert2, s32p vert3, u8 cullBackface, s32 fixedOtz) {
    s32 depthCue;
    s32 flag;
    s32 otz;

    if (cullBackface == 0) {
        otz = RotTransPers4((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (SVECTOR *)vert3, (s32 *)(poly + 8), (s32 *)(poly + 0x14),
                            (s32 *)(poly + 0x20), (s32 *)(poly + 0x2C), &depthCue, &flag);
        goto block_3;
    }
    if (RotAverageNclip4((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (SVECTOR *)vert3, (s32 *)(poly + 8), (s32 *)(poly + 0x14),
                         (s32 *)(poly + 0x20), (s32 *)(poly + 0x2C), &depthCue, &otz, &flag) > 0) {
block_3:
        if ((u32) (otz - 2) < 0xFFFU) {
            if (fixedOtz == 0) {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)poly);
                return;
            }
            AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)poly);
        }
    }
}

void transformAndAddPolyF3(s32p poly, s32p tpagePrim, s32p vert0, s32p vert1, s32p vert2, u8 semiTrans, u8 cullBackface, s32 fixedOtz) {
    s32 depthCue;
    s32 flag;
    s32 otz;

    if (cullBackface == 0) {
        otz = RotTransPers3((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (s32 *)(poly + 8), (s32 *)(poly + 0xC),
                            (s32 *)(poly + 0x10), &depthCue, &flag);
        goto block_3;
    }
    if (RotAverageNclip3((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (s32 *)(poly + 8), (s32 *)(poly + 0xC), (s32 *)(poly + 0x10),
                         &depthCue, &otz, &flag) > 0) {
block_3:
        if ((u32) (otz - 2) < 0xFFFU) {
            if (fixedOtz == 0) {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)poly);
                if ((semiTrans != 0) && (tpagePrim != 0)) {
                    AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)tpagePrim);
                }
            } else {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)poly);
                if ((semiTrans != 0) && (tpagePrim != 0)) {
                    AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)tpagePrim);
                }
            }
        }
    }
}

void transformAndAddPolyF4(s32p poly, s32p tpagePrim, s32p vert0, s32p vert1, s32p vert2, s32p vert3, u8 semiTrans, u8 cullBackface, s32 fixedOtz) {
    s32 depthCue;
    s32 flag;
    s32 otz;

    if (cullBackface == 0) {
        otz = RotTransPers4((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (SVECTOR *)vert3, (s32 *)(poly + 8), (s32 *)(poly + 0xC),
                            (s32 *)(poly + 0x10), (s32 *)(poly + 0x14), &depthCue, &flag);
        goto block_3;
    }
    if (RotAverageNclip4((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (SVECTOR *)vert3, (s32 *)(poly + 8), (s32 *)(poly + 0xC),
                         (s32 *)(poly + 0x10), (s32 *)(poly + 0x14), &depthCue, &otz, &flag) > 0) {
block_3:
        if ((u32) (otz - 2) < 0xFFFU) {
            if (fixedOtz == 0) {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)poly);
                if ((semiTrans != 0) && (tpagePrim != 0)) {
                    AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)tpagePrim);
                }
            } else {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)poly);
                if ((semiTrans != 0) && (tpagePrim != 0)) {
                    AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)tpagePrim);
                }
            }
        }
    }
}

void transformAndAddPolyGT3(s32p poly, s32p vert0, s32p vert1, s32p vert2, u8 cullBackface, s32 fixedOtz) {
    s32 depthCue;
    s32 flag;
    s32 otz;

    if (cullBackface == 0) {
        otz = RotTransPers3((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (s32 *)(poly + 8), (s32 *)(poly + 0x14),
                            (s32 *)(poly + 0x20), &depthCue, &flag);
        goto block_3;
    }
    if (RotAverageNclip3((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (s32 *)(poly + 8), (s32 *)(poly + 0x14),
                         (s32 *)(poly + 0x20), &depthCue, &otz, &flag) > 0) {
block_3:
        if ((u32) (otz - 2) < 0xFFFU) {
            if (fixedOtz == 0) {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)poly);
                return;
            }
            AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)poly);
        }
    }
}

void transformAndAddPolyG3(s32p poly, s32p tpagePrim, s32p vert0, s32p vert1, s32p vert2, u8 semiTrans, u8 cullBackface, s32 fixedOtz) {
    s32 depthCue;
    s32 flag;
    s32 otz;

    if (cullBackface == 0) {
        otz = RotTransPers3((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (s32 *)(poly + 8), (s32 *)(poly + 0x10),
                            (s32 *)(poly + 0x18), &depthCue, &flag);
        goto block_3;
    }
    if (RotAverageNclip3((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (s32 *)(poly + 8), (s32 *)(poly + 0x10),
                         (s32 *)(poly + 0x18), &depthCue, &otz, &flag) > 0) {
block_3:
        if ((u32) (otz - 2) < 0xFFFU) {
            if (fixedOtz == 0) {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)poly);
                if ((semiTrans != 0) && (tpagePrim != 0)) {
                    AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)tpagePrim);
                }
            } else {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)poly);
                if ((semiTrans != 0) && (tpagePrim != 0)) {
                    AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)tpagePrim);
                }
            }
        }
    }
}

void transformAndAddPolyG4(s32p poly, s32p tpagePrim, s32p vert0, s32p vert1, s32p vert2, s32p vert3, u8 semiTrans, u8 cullBackface, s32 fixedOtz) {
    s32 depthCue;
    s32 flag;
    s32 otz;

    if (cullBackface == 0) {
        otz = RotTransPers4((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (SVECTOR *)vert3, (s32 *)(poly + 8), (s32 *)(poly + 0x10),
                            (s32 *)(poly + 0x18), (s32 *)(poly + 0x20), &depthCue, &flag);
        goto block_3;
    }
    if (RotAverageNclip4((SVECTOR *)vert0, (SVECTOR *)vert1, (SVECTOR *)vert2, (SVECTOR *)vert3, (s32 *)(poly + 8), (s32 *)(poly + 0x10),
                         (s32 *)(poly + 0x18), (s32 *)(poly + 0x20), &depthCue, &otz, &flag) > 0) {
block_3:
        if ((u32) (otz - 2) < 0xFFFU) {
            if (fixedOtz == 0) {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)poly);
                if ((semiTrans != 0) && (tpagePrim != 0)) {
                    AddPrim(&CURRENT_FRAME_BUFFER->ot[otz], (void *)tpagePrim);
                }
            } else {
                AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)poly);
                if ((semiTrans != 0) && (tpagePrim != 0)) {
                    AddPrim(&CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)tpagePrim);
                }
            }
        }
    }
}

void transformAndAddLineF2(s32p line, s32p tpagePrim, s32p vert0, s32p vert1, u8 semiTrans, s32 fixedOtz) {
    s32 depthCue;
    s32 flag;
    s32 otz;

    RotTransPers((SVECTOR *)vert0, (s32 *)(line + 8), &depthCue, &flag);
    otz = RotTransPers((SVECTOR *)vert1, (s32 *)(line + 0xC), &depthCue, &flag);
    if ((u32) (otz - 2) < 0xFFFU) {
        if (fixedOtz == 0) {
            AddPrim((s32 *) &CURRENT_FRAME_BUFFER->ot[otz], (void *)line);
            if ((semiTrans != 0) && (tpagePrim != 0)) {
                AddPrim((s32 *) &CURRENT_FRAME_BUFFER->ot[otz], (void *)tpagePrim);
            }
        } else {
            AddPrim((s32 *) &CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)line);
            if ((semiTrans != 0) && (tpagePrim != 0)) {
                AddPrim((s32 *) &CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)tpagePrim);
            }
        }
    }
}

void transformAndAddLineG2(s32p line, s32p tpagePrim, s32p vert0, s32p vert1, u8 semiTrans, s32 fixedOtz) {
    s32 depthCue;
    s32 flag;
    s32 otz;

    RotTransPers((SVECTOR *)vert0, (s32 *)(line + 8), &depthCue, &flag);
    otz = RotTransPers((SVECTOR *)vert1, (s32 *)(line + 0x10), &depthCue, &flag);
    if ((u32) (otz - 2) < 0xFFFU) {
        if (fixedOtz == 0) {
            AddPrim((s32 *) &CURRENT_FRAME_BUFFER->ot[otz], (void *)line);
            if ((semiTrans != 0) && (tpagePrim != 0)) {
                AddPrim((s32 *) &CURRENT_FRAME_BUFFER->ot[otz], (void *)tpagePrim);
            }
        } else {
            AddPrim((s32 *) &CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)line);
            if ((semiTrans != 0) && (tpagePrim != 0)) {
                AddPrim((s32 *) &CURRENT_FRAME_BUFFER->ot[fixedOtz], (void *)tpagePrim);
            }
        }
    }
}
