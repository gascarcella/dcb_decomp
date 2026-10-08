#include "common.h"
#include "gte.h"
#include "game.h"
#include "dcb/heap.h"
#include "dcb/main.h"
#include "dcb/task.h"

/* the bytes the heap hands out, from HEAP_ARENA on */
#if VERSION_JP
#define HEAP_SIZE 0x146F00
#elif VERSION_US || VERSION_EU
#define HEAP_SIZE 0x148000
#endif

/* A block's addr: free blocks keep their address without the KSEG0 bit, so
   they're > 0; a block in use has bit 31 set, which makes it the KSEG0 address
   the game gets. On the host addr is pointer-wide (s32p, heap.h) and its top
   bit is the in-use flag: a free block holds the pointer, a used one the
   pointer with the flag. HEAP_ADDR_FREE(a): the free form; HEAP_ADDR_USED(a):
   the in-use form; HEAP_ADDR_PTR(a): the pointer of an in-use addr;
   HEAP_PTR_ADDR(p): the in-use addr of the pointer the game holds. */
#ifdef PC_PORT
#define HEAP_ADDR_FLAG ((s32p)((u32p)1 << (sizeof(s32p) * 8 - 1)))
#define HEAP_ADDR_FREE(a) ((a) & ~HEAP_ADDR_FLAG)
#define HEAP_ADDR_USED(a) ((a) | HEAP_ADDR_FLAG)
#define HEAP_ADDR_PTR(a) ((void *)((a) & ~HEAP_ADDR_FLAG))
#define HEAP_PTR_ADDR(p) ((s32p)(p) | HEAP_ADDR_FLAG)
#else
#define HEAP_ADDR_FREE(a) ((a) & 0x3FFFFFFF)
#define HEAP_ADDR_USED(a) ((a) | 0x80000000)
#define HEAP_ADDR_PTR(a) ((void *)(a))
#define HEAP_PTR_ADDR(p) ((s32)(p))
#endif

/* initialize: make the whole arena one free block; otherwise free every block
   a task owns, keeping the permanent ones */
void resetHeap(s32 initialize) {
    HeapBlock *block;
    s32 i;

    if (initialize != 0) {
        block = HEAP_BLOCKS;
        /* free blocks keep their address without the KSEG0 bit, so they're > 0 */
        block->addr = HEAP_ADDR_FREE((s32p)&HEAP_ARENA);
        block->size = HEAP_SIZE;
        block->tag = -1;
        i = 0x3FF;
        do {
            block++;
            block->addr = 0;
            block->size = 0;
            i--;
            block->tag = 0;
        } while (i > 0);
        return;
    }
    block = HEAP_BLOCKS;
    for (i = 0x3FF; i >= 0 && block->addr != 0; i--, block++) {
        /* freeing merges entries: look at this one again */
        while (block->addr < 0 && block->tag >= 0) {
            if (freeHeapBlock(HEAP_ADDR_PTR(block->addr)) != 0) {
                break;
            }
        }
    }
}

s32 getLargestFreeHeapBlock(void) {
    HeapBlock *block;
    s32 i;
    s32 largest;

    largest = 0;
    block = HEAP_BLOCKS;
    for (i = 0x3FF; i >= 0 && block->addr != 0; i--, block++) {
        if (block->addr > 0 && largest < block->size) {
            largest = block->size;
        }
    }
    return largest;
}

/* first fit; what is left of the free block goes in a new entry after it */
void *allocHeapBlock(s32 size, s32 ownerTag) {
    HeapBlock *block;
    HeapBlock *shiftBlock;
    s32 i;
    s32p blockAddr;
    s32 freeSize;
    s32p ptr;

    size = (size + 3) & ~3;
    if (size == 0) {
        return 0;
    }
    disableInterrupts();
    block = HEAP_BLOCKS;
    for (i = 0x3FF; i >= 0 && (blockAddr = block->addr) != 0; i--, block++) {
        if (blockAddr >= 0) {
            freeSize = block->size;
            if (freeSize >= size) {
                ptr = HEAP_ADDR_USED(blockAddr);
                block->addr = ptr;
                block->size = size;
                freeSize -= size;
                block->tag = ownerTag;
                if (freeSize != 0) {
                    blockAddr += size;
                    /* shift the rest of the table down one entry */
                    shiftBlock = &HEAP_BLOCKS[0x3FF];
                    for (i--; i > 0; i--) {
                        *shiftBlock = shiftBlock[-1];
                        shiftBlock--;
                    }
                    shiftBlock->addr = blockAddr;
                    shiftBlock->size = freeSize;
                    shiftBlock->tag = -1;
                }
                restoreInterrupts();
                return HEAP_ADDR_PTR(ptr);
            }
        }
    }
    restoreInterrupts();
    return 0;
}

void *allocPermanentHeapBlock(s32 size) {
    return allocHeapBlock(size, -2);
}

void *allocTaskHeapBlock(s32 size) {
    return allocHeapBlock(size, getCurrentTaskId());
}

/* gives the end of the block back: to the next block if that one is free,
   else as a new free entry after it */
void *shrinkHeapBlock(void *ptr, s32 size) {
    HeapBlock *block;
    s32 i;
    s32p blockAddr;
    s32 leftover;

    size = (size + 3) & ~3;
    disableInterrupts();
    block = HEAP_BLOCKS;
    for (i = 0x3FF; i >= 0 && (blockAddr = block->addr) != 0; i--, block++) {
        if (blockAddr == HEAP_PTR_ADDR(ptr)) {
            leftover = block->size - size;
            if (leftover < 0) {
                restoreInterrupts();
                return 0;
            }
            if (leftover != 0 && i != 0) {
                block->size = size;
                block++;
#if VERSION_JP || VERSION_EU
                blockAddr = HEAP_PTR_ADDR(ptr) + size;
#elif VERSION_US
                blockAddr += size;
#endif
                if (block->addr > 0) {
                    leftover += block->size;
                } else {
                    block = &HEAP_BLOCKS[0x3FF];
                    for (i--; i > 0; i--) {
                        *block = block[-1];
                        block--;
                    }
                }
                block->addr = HEAP_ADDR_FREE(blockAddr);
                block->size = leftover;
                block->tag = -1;
            }
            restoreInterrupts();
            return ptr;
        }
    }
    restoreInterrupts();
    return 0;
}

void releaseHeapBlock(void *ptr) {
    freeHeapBlock(ptr);
}

/* merges the block with a free neighbour on either side, then moves the rest
   of the table up over the entries that merged */
s32p freeHeapBlock(void *ptr) {
    HeapBlock *block;
    HeapBlock *nextBlock;
    s32 i;
    s32p blockAddr;
    s32 size;

    if (ptr == 0) {
        return 0;
    }
    disableInterrupts();
    block = HEAP_BLOCKS;
    for (i = 0x3FF; i >= 0 && (blockAddr = block->addr) != 0; i--, block++) {
#if VERSION_EU
        /* compared through xor: eu masks ptr, not the equal blockAddr */
        if ((blockAddr ^ HEAP_PTR_ADDR(ptr)) == 0) {
#elif VERSION_JP || VERSION_US
        if (blockAddr == HEAP_PTR_ADDR(ptr)) {
#else
#error "main/system/heap: version not checked"
#endif
            blockAddr = HEAP_ADDR_FREE(HEAP_PTR_ADDR(ptr));
            size = block->size;
            nextBlock = block;
            if (block != HEAP_BLOCKS && block[-1].addr > 0) {
                block--;
                blockAddr = block->addr;
                size += block->size;
                i++;
            }
            if (i > 0 && nextBlock[1].addr > 0) {
                if (nextBlock != block) {
                    i--;
                }
                nextBlock++;
                size += nextBlock->size;
            }
            block->addr = blockAddr;
            block->size = size;
            block->tag = -1;
            if (block != nextBlock) {
                for (i--; i > 0; i--) {
                    block++;
                    nextBlock++;
                    *block = *nextBlock;
                }
                while (block < nextBlock) {
                    block++;
                    block->addr = 0;
                    block->tag = 0;
                }
            }
            restoreInterrupts();
            return 0;
        }
    }
    restoreInterrupts();
    return 0;
}

s32 freeHeapBlocksByTag(s32 tag) {
    HeapBlock *block;
    s32 blockAddr;
    s32 blocksLeft;

    block = HEAP_BLOCKS;
    for (blocksLeft = 0x3FF; blocksLeft >= 0 && block->addr != 0; blocksLeft--, block++) {
        /* freeing it merges the next block into this one: look at it again */
        while (block->addr < 0 && block->tag == tag) {
            if (freeHeapBlock(HEAP_ADDR_PTR(block->addr)) != 0) {
                break;
            }
        }
    }
    return 0;
}

#ifdef PC_PORT
/* GAME_PTR_TO_S32 and GAME_S32_TO_PTR (include/port.h): a heap pointer kept in
   an s32 is the PS1 address of the same byte of HEAP_ARENA */
#if VERSION_US
#define HEAP_ARENA_ADDR 0x8008C848 /* config/us/symbols.txt */
#else
#error "main/system/heap: the host knows us's HEAP_ARENA address only"
#endif

s32 game_ptr_to_s32(const void *p) {
    u32p ofs = (u32p)p - (u32p)&HEAP_ARENA;

    if (p != NULL && ofs < HEAP_SIZE) {
        return (s32)(HEAP_ARENA_ADDR + ofs);
    }
    return PTR_TO_S32(p);
}

void *game_s32_to_ptr(s32 v) {
    u32 ofs = (u32)v - HEAP_ARENA_ADDR;

    if (ofs < HEAP_SIZE) {
        return (u8 *)&HEAP_ARENA + ofs;
    }
    return S32_TO_PTR(void *, v);
}
#endif
