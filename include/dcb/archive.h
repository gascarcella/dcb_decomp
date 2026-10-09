#ifndef DCB_ARCHIVE_H
#define DCB_ARCHIVE_H

#include "game.h"

extern s32 BITSTREAM_BITS_LEFT;
extern u32 BITSTREAM_BYTE;
extern u8 *BITSTREAM_SRC;
extern s32 HUFFMAN_NEXT_NODE;
#ifdef PC_PORT
/* The Huffman tree's child tables, 0x220 nodes each (0x880 bytes, config/us/symbols.txt): arrays on the host, so the
   sanitizer knows their size. HUFFMAN_NODE(table, i) is node i of a table ((&table)[i] on the PS1). */
extern s32 HUFFMAN_LEFT[0x220];
extern s32 HUFFMAN_RIGHT[0x220];
#define HUFFMAN_NODE(table, i) (table)[i]
#else
extern s32 HUFFMAN_LEFT;
extern s32 HUFFMAN_RIGHT;
#define HUFFMAN_NODE(table, i) (&table)[i]
#endif
extern s32 HUFFMAN_SYMBOLS_DECODED;
extern u8 *DECOMPRESS_DST;
extern u8 LZ_WINDOW[0x1000];

void *findPakChunk(Chunk *cursor, s32 id, s32 sub);
void truncatePakAtChunk(Chunk *cursor, s32 id, s32 sub);
void truncatePakTextures(Chunk *pak);

#endif /* DCB_ARCHIVE_H */
