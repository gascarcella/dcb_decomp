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
   the game gets. HEAP_ADDR_FREE(a): the free form; HEAP_ADDR_USED(a): the
   in-use form; HEAP_ADDR_PTR(a): the pointer of an in-use addr;
   HEAP_PTR_ADDR(p): the in-use addr of the pointer the game holds;
   HEAP_ARENA_START: the arena's addr. */
#ifdef PC_PORT
/* The host (docs/PORT.md "Memory and pointers"): the table holds the PS1's
   values, so the allocator makes the PS1's decisions and every block has the
   PS1 address the PS1 would give it (PLAYER_PROFILES at 0x800C8964). The
   host's bytes of the block at arena offset o are at HEAP_HOST_SCALE * o of
   its own arena, which is HEAP_HOST_SCALE times the PS1's: every block starts
   on a multiple of 4 * HEAP_HOST_SCALE bytes, the alignment of the host's
   pointers (8 on a 64-bit host, 4 at -m32: the PS1's layout there), and the
   bytes after its size up to the next block are never handed out. */
#if VERSION_US
#define HEAP_ARENA_ADDR 0x8008C848 /* config/us/symbols.txt */
#else
#error "main/system/heap: the host knows us's HEAP_ARENA address only"
#endif
#define HEAP_HOST_SCALE (sizeof(void *) / 4)
#define HEAP_PHYS(a) ((u32)(a) & 0x1FFFFFFF)

typedef union {
    u8 bytes[HEAP_SIZE * HEAP_HOST_SCALE];
    void *align;
} HeapArena;
HeapArena HEAP_ARENA;

/* a table addr (either form): its block's offset in the PS1's arena */
static u32 heapOffset(s32p addr) {
    return HEAP_PHYS(addr) - HEAP_PHYS(HEAP_ARENA_ADDR);
}

static void *heapHostPtr(s32p addr) {
    return HEAP_ARENA.bytes + HEAP_HOST_SCALE * heapOffset(addr);
}

/* a pointer the game hands back: the in-use addr of the block that starts
   there; 0 (no block's) for a pointer that is not on the arena's grid */
static s32p heapPs1Addr(const void *p) {
    u32p ofs = (u32p)p - (u32p)HEAP_ARENA.bytes;

    if (ofs >= sizeof(HEAP_ARENA) || ofs % HEAP_HOST_SCALE != 0) {
        return 0;
    }
    return (s32)(HEAP_ARENA_ADDR + (u32)(ofs / HEAP_HOST_SCALE));
}

/* The bytes a block holds when it is handed out are the PS1's: what the
   blocks before it on the same PS1 bytes left there (the game reads some it
   never writes: the profile's padding, its unset fields). The host's blocks
   are spaced wider, so a freed block's bytes (or a shrunk block's tail) are
   kept at their PS1 offsets in HEAP_PS1_BYTES, and a new block gets its
   PS1 bytes from there (the arena starts as zeros, as the PS1's .bss). */
static u8 HEAP_PS1_BYTES[HEAP_SIZE];

/* bytes [from, from + size) of the block at addr: keep them, or give them back */
static void heapKeepBytes(s32p addr, s32 from, s32 size) {
    u32 ofs = heapOffset(addr) + from;

    if (HEAP_HOST_SCALE > 1) {
        __builtin_memcpy(HEAP_PS1_BYTES + ofs, HEAP_ARENA.bytes + HEAP_HOST_SCALE * heapOffset(addr) + from, size);
    }
}

static void heapGiveBytes(s32p addr, s32 size) {
    u32 ofs = heapOffset(addr);

    if (HEAP_HOST_SCALE > 1) {
        __builtin_memcpy(HEAP_ARENA.bytes + HEAP_HOST_SCALE * ofs, HEAP_PS1_BYTES + ofs, size);
    }
}

#define HEAP_ADDR_FREE(a) ((a) & 0x3FFFFFFF)
#define HEAP_ADDR_USED(a) ((s32p)(s32)((u32)(a) | 0x80000000))
#define HEAP_ADDR_PTR(a) heapHostPtr(a)
#define HEAP_PTR_ADDR(p) heapPs1Addr(p)
#define HEAP_ARENA_START (s32p)HEAP_ARENA_ADDR
#else
#define HEAP_ADDR_FREE(a) ((a) & 0x3FFFFFFF)
#define HEAP_ADDR_USED(a) ((a) | 0x80000000)
#define HEAP_ADDR_PTR(a) ((void *)(a))
#define HEAP_PTR_ADDR(p) ((s32)(p))
#define HEAP_ARENA_START (s32p)&HEAP_ARENA
#endif

/* initialize: make the whole arena one free block; otherwise free every block
   a task owns, keeping the permanent ones */
void resetHeap(s32 initialize) {
    HeapBlock *block;
    s32 i;

    if (initialize != 0) {
        block = HEAP_BLOCKS;
        /* free blocks keep their address without the KSEG0 bit, so they're > 0 */
        block->addr = HEAP_ADDR_FREE(HEAP_ARENA_START);
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
#ifdef PC_PORT
                heapGiveBytes(ptr, size);
#endif
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
#ifdef PC_PORT
                heapKeepBytes(blockAddr, size, leftover);
#endif
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
#ifdef PC_PORT
            heapKeepBytes(blockAddr, 0, block->size);
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
   an s32 is the PS1 address of the same byte: its block's PS1 address plus its
   offset in the block. The block is the table entry whose host bytes hold it
   (in use or free; a byte in the spacing after its size is the same offset
   past its PS1 start); before resetHeap there is none, and the offset is the
   arena's. */
static const HeapBlock *heapBlockAt(u32 ofs, s32 host) {
    const HeapBlock *block;
    s32 i;
    u32 start;

    block = HEAP_BLOCKS;
    for (i = 0x3FF; i >= 0 && block->addr != 0; i--, block++) {
        start = heapOffset(block->addr);
        if (host ? ofs - HEAP_HOST_SCALE * start < HEAP_HOST_SCALE * (u32)block->size
                 : ofs - start < (u32)block->size) {
            return block;
        }
    }
    return NULL;
}

s32 game_ptr_to_s32(const void *p) {
    u32p ofs = (u32p)p - (u32p)HEAP_ARENA.bytes;
    const HeapBlock *block;
    u32 start;

    if (p == NULL || ofs >= sizeof(HEAP_ARENA)) {
        return PTR_TO_S32(p);
    }
    block = heapBlockAt((u32)ofs, 1);
    if (block == NULL) {
        return (s32)(HEAP_ARENA_ADDR + (u32)(ofs / HEAP_HOST_SCALE));
    }
    start = heapOffset(block->addr);
    return (s32)(HEAP_ARENA_ADDR + start + ((u32)ofs - HEAP_HOST_SCALE * start));
}

void *game_s32_to_ptr(s32 v) {
    u32 ofs = (u32)v - HEAP_ARENA_ADDR;
    const HeapBlock *block;
    u32 start;

    if (ofs >= HEAP_SIZE) {
        return S32_TO_PTR(void *, v);
    }
    block = heapBlockAt(ofs, 0);
    if (block == NULL) {
        return HEAP_ARENA.bytes + HEAP_HOST_SCALE * ofs;
    }
    start = heapOffset(block->addr);
    return HEAP_ARENA.bytes + HEAP_HOST_SCALE * start + (ofs - start);
}

/* whether p points into the arena's host bytes (the adapter's image: a heap
   pointer is written as its PS1 address) */
s32 game_heap_owns(const void *p) {
    return (u32p)p - (u32p)HEAP_ARENA.bytes < sizeof(HEAP_ARENA);
}
#endif
