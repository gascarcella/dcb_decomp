#ifndef DCB_DECOMPRESS_H
#define DCB_DECOMPRESS_H

#include "game.h"
#include "dcb/archive.h"

s32 readBitstreamBit(void);
u32 readBitstreamBits(s32 bitCount);
s32 readHuffmanTree(void);
s32p decompressForTask(s32p src);
s32p decompressArchiveEntry(s32p archive, s32 index);
void decompressLzHuffman(u32 outputSize);
s32p decompressToHeap(s32p src, s32 heapTag);

#endif /* DCB_DECOMPRESS_H */
