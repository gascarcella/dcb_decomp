/* tests/port/sound_replay.c: the port's LIBSND, LIBSPU and SPU core replayed on the emulator's timeline
 * (tests/port/sound.py). Adapted from dw2003recomp's tests/port/sound_replay.c (docs/THIRD_PARTY.md).
 *
 * The game calls LIBSND at frames that depend on CPU time the port does not reproduce (the CD, the loaders, the movie),
 * so a port run's trace and the emulator's interleave the game's calls with the sequencer's ticks differently. This
 * program takes the game out: sound.py turns an emulator trace (tests/sound/spu_trace.py: the game's calls and
 * LIBSND's ticks as comments) into a replay script, and this program makes exactly those calls, and runs LIBSND's tick
 * (SsSeqCalledTbyT) exactly where the emulator ran it, and writes the SPU trace they make. sound.py then requires it
 * to equal the emulator's trace, store for store and tick for tick.
 *
 * Script (one item per line; `#` comments):
 *   data <n> <path>                         a file (a PAK chunk: a VAB header or body, a SEQ) as buffer n
 *   vsync <index> <vsync>                   the trace's vsync count becomes `vsync` at its event `index` (the ticks of
 *                                           the stores)
 *   tick <index> <vsync> [locked] [cycles=N]  LIBSND's tick in `vsync`, which came after the trace's first `index` events;
 *                                           `locked`: the emulator's tick came while a key call held LIBSND's lock (the
 *                                           vblank interrupted SsUtKeyOnV), so it flushed nothing: run with the lock set;
 *                                           `cycles`: the emulator's CPU cycle count at the tick
 *   call <vsync> <Function> <args...>       a call; a pointer argument is `<n>+<offset>` into buffer n, a struct
 *                                           `x<hex bytes>` (SpuVoiceAttr, SpuCommonAttr in the PS1's layout)
 *   cdinit <vsync>                          LIBCD's CdInit's five SPU stores
 * A tick runs when the script reaches it, or, when the emulator ran it in the middle of a call (its stores before and
 * after it), when that call's store count reaches its index. Before each tick the SPU core renders the samples since
 * the previous tick, so that the envelopes LIBSND reads at the flush are where the emulator's were: with the ticks'
 * cycle counts, the samples the emulator's SPU made between them (one per 768 CPU cycles: floor(cycles / 768) of the
 * two ticks apart); without, SAMPLES_PER_VSYNC a vsync, spread exactly. The cycle count matters: the flush runs after
 * the game's vblank handler, whose CPU time varies, and a voice whose release ends within a few samples of a flush is
 * free or not by them (LIBSND's allocator reads the envelopes).
 *
 * Usage: sound_replay SCRIPT OUT.trace SAMPLES_PER_VSYNC_X100 [pal|ntsc [OUT.wav]]
 * OUT.wav: the samples rendered (44,100 Hz, stereo, 16 bits), from the first tick on: what the port's SPU makes of the
 * emulator's register writes (tests/port/sound.py `wav`). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsnd_internal.h"
#include "psxstack/psyq/libsnd.h"
#include "psxstack/psyq/libspu.h"
#include "sha1.h"

/* ---- The shim pieces libsnd*.c and libspu.c use ---- */
int psyq_trace_state = 0;

int psyq_trace_decide(void) {
    psyq_trace_state = 0;
    return 0;
}

void psyq_trace_printf(const char *fmt, ...) {
    (void)fmt;
}

void port_unimplemented(const char *fn) {
    fprintf(stderr, "sound_replay: unimplemented: %s\n", fn);
    exit(3);
}

static int video_mode = 0; /* MODE_NTSC (this game); "pal" as the 4th argument: 1 */

int SetVideoMode(int mode) {
    int prev = video_mode;
    video_mode = mode;
    return prev;
}

/* ---- The script ---- */
#define MAX_DATA 256
#define MAX_ARGS 8

typedef struct Item {
    char kind;          /* 'v' vsync, 't' tick, 'c' call, 'i' cdinit */
    long tick, index;
    char name[32];
    int nargs;
    long arg[MAX_ARGS];
    int ptr_buf[MAX_ARGS]; /* -1, or the buffer a pointer argument points into */
    u8 *bytes;             /* a struct argument's bytes (its first) */
    int nbytes;
    int locked;            /* a tick: LIBSND was locked when it came */
    long long cycles;      /* a tick: the emulator's CPU cycle count (0: not in the trace) */
} Item;

static u8 *data_buf[MAX_DATA];
static Item *items;
static long n_items;
static long *tick_item; /* indexes of the tick items, in order */
static long n_ticks, next_tick;
static long *vsync_item; /* and of the vsync items */
static long n_vsyncs, next_vsync;
static long events, cur_tick, last_render_tick = -1;
static int in_tick;
static FILE *out, *wav;
static long wav_frames;

static void put32(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

/* The RIFF header for `frames` stereo 16-bit frames at 44,100 Hz. */
static void wav_header(FILE *f, long frames) {
    u8 h[44];
    memcpy(h, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\0\0\0\0\0\0\0\0\x04\0\x10\0data\0\0\0\0", 44);
    put32(h + 4, (u32)(36 + frames * 4));
    put32(h + 24, 44100);
    put32(h + 28, 44100 * 4);
    put32(h + 40, (u32)(frames * 4));
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
}

static void fatal(const char *msg, const char *detail) {
    fprintf(stderr, "sound_replay: %s%s\n", msg, detail);
    exit(2);
}

static u8 *load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    long size;
    u8 *buf;

    if (f == NULL) {
        fatal("cannot read ", path);
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = calloc(1, (size_t)size + 0x10000); /* room for reads past the end (the DMA's last block) */
    if (buf == NULL || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fatal("cannot load ", path);
    }
    fclose(f);
    return buf;
}

static int hexval(char c) {
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

static void read_script(const char *path) {
    FILE *f = fopen(path, "r");
    char line[1024];
    long cap = 0;

    if (f == NULL) {
        fatal("cannot read ", path);
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *tok, *save = NULL;
        Item it;

        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        memset(&it, 0, sizeof(it));
        tok = strtok_r(line, " \n", &save);
        if (tok == NULL) {
            continue;
        }
        if (strcmp(tok, "data") == 0) {
            int n = atoi(strtok_r(NULL, " \n", &save));
            char *p = strtok_r(NULL, " \n", &save);
            if (n < 0 || n >= MAX_DATA || p == NULL) {
                fatal("bad data line", "");
            }
            data_buf[n] = load_file(p);
            continue;
        }
        if (strcmp(tok, "tick") == 0 || strcmp(tok, "vsync") == 0) {
            it.kind = tok[0];
            it.index = atol(strtok_r(NULL, " \n", &save));
            it.tick = atol(strtok_r(NULL, " \n", &save));
            while ((tok = strtok_r(NULL, " \n", &save)) != NULL) {
                if (strcmp(tok, "locked") == 0) {
                    it.locked = 1;
                } else if (strncmp(tok, "cycles=", 7) == 0) {
                    it.cycles = strtoll(tok + 7, NULL, 10);
                }
            }
        } else if (strcmp(tok, "cdinit") == 0) {
            it.kind = 'i';
            it.tick = atol(strtok_r(NULL, " \n", &save));
        } else if (strcmp(tok, "call") == 0) {
            it.kind = 'c';
            it.tick = atol(strtok_r(NULL, " \n", &save));
            snprintf(it.name, sizeof(it.name), "%s", strtok_r(NULL, " \n", &save));
            while ((tok = strtok_r(NULL, " \n", &save)) != NULL && it.nargs < MAX_ARGS) {
                char *plus = strchr(tok, '+');
                it.ptr_buf[it.nargs] = -1;
                if (tok[0] == 'x') {
                    int n = (int)strlen(tok + 1) / 2;
                    it.bytes = calloc(1, (size_t)n + 64);
                    for (int k = 0; k < n; k++) {
                        it.bytes[k] = (u8)(hexval(tok[1 + 2 * k]) << 4 | hexval(tok[2 + 2 * k]));
                    }
                    it.nbytes = n;
                } else if (plus != NULL) {
                    it.ptr_buf[it.nargs] = atoi(tok);
                    it.arg[it.nargs] = strtol(plus + 1, NULL, 0);
                } else {
                    it.arg[it.nargs] = strtol(tok, NULL, 0);
                }
                it.nargs++;
            }
        } else {
            fatal("bad line: ", tok);
        }
        if (n_items == cap) {
            cap = cap ? cap * 2 : 4096;
            items = realloc(items, (size_t)cap * sizeof(Item));
        }
        items[n_items++] = it;
    }
    fclose(f);
    tick_item = malloc((size_t)(n_items + 1) * sizeof(long));
    vsync_item = malloc((size_t)(n_items + 1) * sizeof(long));
    for (long i = 0; i < n_items; i++) {
        if (items[i].kind == 't') {
            tick_item[n_ticks++] = i;
        } else if (items[i].kind == 'v') {
            vsync_item[n_vsyncs++] = i;
        }
    }
}

static long rate_x100 = 73500; /* samples per vsync, x 100 */
static long rate_acc;

/* The SPU's samples of the vsyncs up to `tick` (each vsync's share spread exactly). */
static void render_until(long tick) {
    static int16_t frame[1000 * 2];

    if (last_render_tick < 0) {
        last_render_tick = tick - 1;
    }
    while (last_render_tick < tick) {
        long samples;
        rate_acc += rate_x100;
        samples = rate_acc / 100;
        rate_acc %= 100;
        spu_render(frame, (int)samples);
        if (wav != NULL) {
            fwrite(frame, 4, (size_t)samples, wav);
            wav_frames += samples;
        }
        last_render_tick++;
    }
}

static long long last_cycles = -1;

/* The samples since the previous tick by the CPU cycles between them (768 a sample), the first tick a vsync's. */
static void render_cycles(long long cycles) {
    static int16_t frame[4096 * 2];
    long long n = last_cycles < 0 ? rate_x100 / 100 : cycles / 768 - last_cycles / 768;

    last_cycles = cycles;
    while (n > 0) {
        int chunk = n > 4096 ? 4096 : (int)n;
        spu_render(frame, chunk);
        if (wav != NULL) {
            fwrite(frame, 4, (size_t)chunk, wav);
            wav_frames += chunk;
        }
        n -= chunk;
    }
}

static void run_tick(void) {
    const Item *t = &items[tick_item[next_tick++]];

    in_tick = 1;
    cur_tick = t->tick;
    if (t->cycles > 0) {
        render_cycles(t->cycles);
    } else {
        render_until(t->tick);
    }
    fprintf(out, "# %ld tick%s\n", cur_tick, t->locked ? " (locked)" : "");
    if (t->locked) {
        int lock = snd.lock;
        snd.lock = 1;
        SsSeqCalledTbyT();
        snd.lock = lock;
    } else {
        SsSeqCalledTbyT();
    }
    in_tick = 0;
}

/* ---- The trace (psxstack's runtime/spu_trace.c format) ---- */
static const char *const voice_regs[8] = { "vol.l", "vol.r", "pitch", "addr", "adsr.lo", "adsr.hi", "adsr.vol", "loop" };
static const char *const control_regs[32] = {
    "mvol.l", "mvol.r", "rvol.l", "rvol.r", "kon.lo", "kon.hi", "koff.lo", "koff.hi",
    "pmon.lo", "pmon.hi", "non.lo", "non.hi", "eon.lo", "eon.hi", "endx.lo", "endx.hi",
    "unk_da0", "rev.base", "irq.addr", "xfer.addr", "xfer.fifo", "spucnt", "xfer.ctrl", "spustat",
    "cdvol.l", "cdvol.r", "extvol.l", "extvol.r", "curvol.l", "curvol.r", "unk_dbc", "unk_dbe",
};
static const char *const reverb_regs[32] = {
    "dAPF1", "dAPF2", "vIIR", "vCOMB1", "vCOMB2", "vCOMB3", "vCOMB4", "vWALL",
    "vAPF1", "vAPF2", "mLSAME", "mRSAME", "mLCOMB1", "mRCOMB1", "mLCOMB2", "mRCOMB2",
    "dLSAME", "dRSAME", "mLDIFF", "mRDIFF", "mLCOMB3", "mRCOMB3", "mLCOMB4", "mRCOMB4",
    "dLDIFF", "dRDIFF", "mLAPF1", "mRAPF1", "mLAPF2", "mRAPF2", "vLIN", "vRIN",
};

static void hook(uint32_t offset, uint16_t value, const uint16_t *dma, uint32_t halfwords) {
    /* a tick the emulator ran inside a call: before this store */
    if (!in_tick && next_tick < n_ticks && items[tick_item[next_tick]].index == events) {
        run_tick();
    }
    /* the vsync count of this store */
    while (next_vsync < n_vsyncs && items[vsync_item[next_vsync]].index <= events) {
        cur_tick = items[vsync_item[next_vsync++]].tick;
    }
    events++;
    if (dma == NULL) {
        char name[24];
        offset &= 0x1FE;
        if (offset < 0x180) {
            snprintf(name, sizeof(name), "v%02u.%s", (unsigned)(offset >> 4), voice_regs[(offset & 0xF) >> 1]);
        } else if (offset < 0x1C0) {
            snprintf(name, sizeof(name), "%s", control_regs[(offset - 0x180) >> 1]);
        } else {
            snprintf(name, sizeof(name), "rev.%s", reverb_regs[(offset - 0x1C0) >> 1]);
        }
        fprintf(out, "%ld %03x %s %04x\n", cur_tick, (unsigned)(0xC00 + offset), name, value);
    } else {
        PortSha1 c;
        uint8_t digest[20], le[2];
        char hex[41];
        port_sha1_init(&c);
        for (uint32_t i = 0; i < halfwords; i++) {
            le[0] = (uint8_t)(dma[i] & 0xFF);
            le[1] = (uint8_t)(dma[i] >> 8);
            port_sha1_update(&c, le, 2);
        }
        port_sha1_final(&c, digest);
        port_sha1_hex(digest, hex);
        fprintf(out, "%ld dma4 spu=%05x len=%u sha1=%s\n", cur_tick, (unsigned)offset, (unsigned)(halfwords * 2), hex);
    }
}

/* ---- The calls ---- */
static u8 *ptr_arg(const Item *it, int i) {
    if (it->ptr_buf[i] < 0 || data_buf[it->ptr_buf[i]] == NULL) {
        fatal("a pointer argument without its data: ", it->name);
    }
    return data_buf[it->ptr_buf[i]] + it->arg[i];
}

/* A struct argument, copied into `dst` (the shim's structs have the PS1's layout: fixed-size fields only). */
static void struct_arg(const Item *it, void *dst, size_t size) {
    if (it->bytes == NULL) {
        fatal("a struct argument without its bytes: ", it->name);
    }
    memset(dst, 0, size);
    memcpy(dst, it->bytes, (size_t)it->nbytes < size ? (size_t)it->nbytes : size);
}

static void call(const Item *it) {
    const long *a = it->arg;
    const char *n = it->name;

    fprintf(out, "# %ld call %s\n", cur_tick, n);
    if (strcmp(n, "reset") == 0) { /* the console's reset: the shim's LIBSND, then the SPU (psxstack/runtime/reset.c) */
        psyq_snd_reset();
        spu_reset();
    } else if (strcmp(n, "SsInit") == 0) {
        SsInit();
    } else if (strcmp(n, "SsSetTableSize") == 0) {
        static u8 table[0x1000];
        SsSetTableSize(table, (s16)a[1], (s16)a[2]);
    } else if (strcmp(n, "SsSetTickMode") == 0) {
        SsSetTickMode((s32)a[0]);
    } else if (strcmp(n, "SsStart") == 0) {
        SsStart();
    } else if (strcmp(n, "SsStart2") == 0) {
        SsStart2();
    } else if (strcmp(n, "SsSetMVol") == 0) {
        SsSetMVol((s16)a[0], (s16)a[1]);
    } else if (strcmp(n, "SsSetStereo") == 0) {
        SsSetStereo();
    } else if (strcmp(n, "SsSetMono") == 0) {
        SsSetMono();
    } else if (strcmp(n, "SsSetSerialAttr") == 0) {
        SsSetSerialAttr((char)a[0], (char)a[1], (char)a[2]);
    } else if (strcmp(n, "SsSetSerialVol") == 0) {
        SsSetSerialVol((char)a[0], (s16)a[1], (s16)a[2]);
    } else if (strcmp(n, "SsUtSetReverbType") == 0) {
        SsUtSetReverbType((s16)a[0]);
    } else if (strcmp(n, "SsUtSetReverbDepth") == 0) {
        SsUtSetReverbDepth((s16)a[0], (s16)a[1]);
    } else if (strcmp(n, "SsUtReverbOn") == 0) {
        SsUtReverbOn();
    } else if (strcmp(n, "SsUtReverbOff") == 0) {
        SsUtReverbOff();
    } else if (strcmp(n, "SpuClearReverbWorkArea") == 0) {
        SpuClearReverbWorkArea((s32)a[0]);
    } else if (strcmp(n, "SsVabOpenHeadSticky") == 0) {
        SsVabOpenHeadSticky(ptr_arg(it, 0), (s16)a[1], (u32)a[2]);
    } else if (strcmp(n, "SsVabTransBody") == 0) {
        SsVabTransBody(ptr_arg(it, 0), (s16)a[1]);
    } else if (strcmp(n, "SsVabTransCompleted") == 0) {
        SsVabTransCompleted((s16)a[0]);
    } else if (strcmp(n, "SsVabClose") == 0) {
        SsVabClose((s16)a[0]);
    } else if (strcmp(n, "SsSeqOpen") == 0) {
        SsSeqOpen((u32 *)ptr_arg(it, 0), (s16)a[1]);
    } else if (strcmp(n, "SsSeqClose") == 0) {
        SsSeqClose((s16)a[0]);
    } else if (strcmp(n, "SsSeqPlay") == 0) {
        SsSeqPlay((s16)a[0], (char)a[1], (s16)a[2]);
    } else if (strcmp(n, "SsSeqStop") == 0) {
        SsSeqStop((s16)a[0]);
    } else if (strcmp(n, "SsSeqSetVol") == 0) {
        SsSeqSetVol((s16)a[0], (s16)a[1], (s16)a[2]);
    } else if (strcmp(n, "SsSeqGetVol") == 0) {
        s16 l, r;
        SsSeqGetVol((s16)a[0], (s16)a[1], &l, &r);
    } else if (strcmp(n, "SsUtAllKeyOff") == 0) {
        SsUtAllKeyOff((s16)a[0]);
    } else if (strcmp(n, "SsUtKeyOnV") == 0) {
        SsUtKeyOnV((s16)a[0], (s16)a[1], (s16)a[2], (s16)a[3], (s16)a[4], (s16)a[5], (s16)a[6], (s16)a[7]);
    } else if (strcmp(n, "SsUtKeyOffV") == 0) {
        SsUtKeyOffV((s16)a[0]);
    } else if (strcmp(n, "SpuSetVoiceAttr") == 0) {
        SpuVoiceAttr attr;
        struct_arg(it, &attr, sizeof(attr));
        SpuSetVoiceAttr(&attr);
    } else if (strcmp(n, "SpuSetCommonAttr") == 0) {
        SpuCommonAttr attr;
        struct_arg(it, &attr, sizeof(attr));
        SpuSetCommonAttr(&attr);
    } else {
        fatal("unknown call ", n);
    }
}

int main(int argc, char **argv) {
    long i;

    if (argc < 4 || argc > 6 || (argc >= 5 && strcmp(argv[4], "ntsc") != 0 && strcmp(argv[4], "pal") != 0)) {
        fprintf(stderr, "usage: sound_replay SCRIPT OUT.trace SAMPLES_PER_VSYNC_X100 [pal|ntsc [OUT.wav]]\n");
        return 2;
    }
    if (argc >= 5) {
        video_mode = strcmp(argv[4], "pal") == 0 ? 1 : 0;
    }
    if (argc == 6) {
        wav = fopen(argv[5], "wb");
        if (wav == NULL) {
            fatal("cannot write ", argv[5]);
        }
        wav_header(wav, 0);
    }
    rate_x100 = atol(argv[3]);
    if (rate_x100 < 50000 || rate_x100 > 99900) {
        fatal("bad samples per vsync: ", argv[3]);
    }
    _Static_assert(sizeof(SpuVoiceAttr) == 0x40 && sizeof(SpuCommonAttr) == 0x28, "the PS1's struct layouts");
    read_script(argv[1]);
    out = fopen(argv[2], "w");
    if (out == NULL) {
        fatal("cannot write ", argv[2]);
    }
    fprintf(out, "# dcb spu trace v1\n# LIBSND replayed on the emulator's timeline (tests/port/sound_replay.c)\n");
    spu_init();
    spu_set_write_hook(hook);
    for (i = 0; i < n_items; i++) {
        const Item *it = &items[i];
        if (it->kind == 'v') {
            continue;
        }
        if (it->kind == 't') {
            if (next_tick < n_ticks && tick_item[next_tick] == i) {
                run_tick();
            }
            continue;
        }
        if (cur_tick < it->tick) {
            cur_tick = it->tick;
        }
        if (it->kind == 'i') {
            /* LIBCD's CdInit (its CD_initvol): main volume, CD volume, the CD input on */
            spu_write16(0x180, 0x3FFF);
            spu_write16(0x182, 0x3FFF);
            spu_write16(0x1B0, 0x3FFF);
            spu_write16(0x1B2, 0x3FFF);
            spu_write16(0x1AA, 0xC001);
        } else {
            call(it);
        }
    }
    fprintf(out, "# events %ld\n", events);
    fclose(out);
    if (wav != NULL) {
        wav_header(wav, wav_frames);
        fclose(wav);
    }
    return 0;
}
