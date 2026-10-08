#ifndef DCB_PRIM3D_H
#define DCB_PRIM3D_H

#include "game.h"

void transformAndAddPolyFT3(s32p poly, s32p vert0, s32p vert1, s32p vert2, u8 cullBackface, s32 fixedOtz);
void transformAndAddPolyFT4(s32p poly, s32p vert0, s32p vert1, s32p vert2, s32p vert3, u8 cullBackface, s32 fixedOtz);
void transformAndAddPolyGT4(s32p poly, s32p vert0, s32p vert1, s32p vert2, s32p vert3, u8 cullBackface, s32 fixedOtz);
void transformAndAddPolyF3(s32p poly, s32p tpagePrim, s32p vert0, s32p vert1, s32p vert2, u8 semiTrans, u8 cullBackface, s32 fixedOtz);
void transformAndAddPolyF4(s32p poly, s32p tpagePrim, s32p vert0, s32p vert1, s32p vert2, s32p vert3, u8 semiTrans, u8 cullBackface, s32 fixedOtz);
void transformAndAddPolyGT3(s32p poly, s32p vert0, s32p vert1, s32p vert2, u8 cullBackface, s32 fixedOtz);
void transformAndAddPolyG3(s32p poly, s32p tpagePrim, s32p vert0, s32p vert1, s32p vert2, u8 semiTrans, u8 cullBackface, s32 fixedOtz);
void transformAndAddPolyG4(s32p poly, s32p tpagePrim, s32p vert0, s32p vert1, s32p vert2, s32p vert3, u8 semiTrans, u8 cullBackface, s32 fixedOtz);
void transformAndAddLineF2(s32p line, s32p tpagePrim, s32p vert0, s32p vert1, u8 semiTrans, s32 fixedOtz);
void transformAndAddLineG2(s32p line, s32p tpagePrim, s32p vert0, s32p vert1, u8 semiTrans, s32 fixedOtz);

#endif /* DCB_PRIM3D_H */
