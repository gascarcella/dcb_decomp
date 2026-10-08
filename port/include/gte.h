/*
 * port/include/gte.h: the host form of include/gte.h, the game's 40 gte_* macros, for the PC port only.
 *
 * Why it exists (docs/PORT.md "The contract, for this game", the GTEMAC row): include/gte.h writes the GTE accesses
 * as MIPS inline asm over include/gte_macros.inc's mnemonics, with output operands and a prefetch whose registers
 * live across two macros. psxstack's GTEMAC translator (tools/port_gen.py) cannot take that, so this header is the
 * translation, by hand. port/CMakeLists.txt puts port/include/ first on the include path, and this file carries the
 * real header's guard, GTE_H: the units' #include "gte.h" lands here and include/gte.h is never read on the host.
 * The PS1 build never sees port/include/.
 *
 * Each macro does what its MIPS sequence does, on psxstack's software GTE (psyq/gte.c), in the same order:
 *   lwc2 $n, ofs(p)   psyq_gte_mtc2(n, word at p + ofs)
 *   swc2 $n, ofs(p)   psyq_gte_swc2_(p + ofs, n)          (the store, and the sub-pixel shadow's record of SXY0-2)
 *   mtc2 / mfc2       psyq_gte_mtc2(n, v) / v = psyq_gte_mfc2(n)
 *   ctc2 / cfc2       psyq_gte_ctc2(n, v) / v = psyq_gte_cfc2(n)
 *   a mnemonic        psyq_gte_cmd(word): the low 25 bits of gte_macros.inc's cop2op word,
 *                     fake_op << 20 | sf << 19 | mx << 17 | v << 15 | cv << 13 | lm << 10 | op
 *   nop               nothing (the software GTE has no pipeline)
 * Memory goes through byte copies (port_gte_lw_/port_gte_sw_): the host is little-endian like the PS1, and a struct
 * the game hands over may be only halfword-aligned (as psxstack's generated gtemac.h does). Every argument is
 * evaluated once. The general registers an asm sequence uses as temporaries ($8, $12-$15) are C locals, except the
 * prefetch pair's ($8-$13), which lives across two macros: see gte_prefetch_ below.
 *
 * Drift: a gte_* macro added to include/gte.h upstream and missing here makes the units that use it fail on the host
 * with an implicit declaration (psxstack's probe, port/tools/port_inventory.py, and the port build). The host test is
 * tests/port/gte_host_test.c (scripts/gte_test.sh).
 */
#ifndef GTE_H
#define GTE_H

#include "common.h"

/* psxstack's software GTE (psyq/gte.c). Declared here, as psxstack's generated gtemac.h does for the first game:
 * the stack declares them only in psyq/psyq_internal.h, whose psyq.h the game's units cannot include (game.h declares
 * the Psy-Q functions itself). */
void psyq_gte_mtc2(int reg, u32 v);
u32 psyq_gte_mfc2(int reg);
void psyq_gte_ctc2(int reg, u32 v);
u32 psyq_gte_cfc2(int reg);
void psyq_gte_cmd(u32 op);
void psyq_gte_swc2_(void *p, int reg);

/* lw and sw, as byte copies (halfword-aligned pointers are fine). */
static inline u32 port_gte_lw_(const void *p) {
    u32 v;

    __builtin_memcpy(&v, p, 4);
    return v;
}

static inline void port_gte_sw_(void *p, u32 v) {
    __builtin_memcpy(p, &v, 4);
}

/*
 * The prefetch pair's $8-$13: gte_prefetchv3c(p) reads six words into them while the GTE works, and
 * gte_ldv3_prefetched() moves them into V0-V2 later (tmd_sort.c's transformAndLightVertices). On the PS1 the six
 * registers keep the words between the two macros because the compiler is told they are clobbered and the code
 * between them uses no general registers it could allocate there; on the host they are this buffer. One per unit
 * (static): the pair is always used inside one function, and the port is single-threaded.
 */
static u32 gte_prefetch_[6] __attribute__((unused));

/* ---- the general forms ---- */

/* Load GTE data register `reg` from `ofs(p)` (lwc2) */
#define gte_lwc2(reg, ofs, p)                                                                                        \
    do {                                                                                                             \
        const u8 *gte_lwc2_p_ = (const u8 *)(p);                                                                     \
        psyq_gte_mtc2(reg, port_gte_lw_(gte_lwc2_p_ + (ofs)));                                                       \
    } while (0)

/* Store GTE data register `reg` to `ofs(p)` (swc2) */
#define gte_swc2(reg, ofs, p)                                                                                        \
    do {                                                                                                             \
        u8 *gte_swc2_p_ = (u8 *)(p);                                                                                 \
        psyq_gte_swc2_(gte_swc2_p_ + (ofs), reg);                                                                    \
    } while (0)

/* Read GTE data register `reg` into `v` (mfc2) */
#define gte_mfc2(reg, v)                                                                                             \
    do {                                                                                                             \
        (v) = psyq_gte_mfc2(reg);                                                                                    \
    } while (0)

/* Write `v` to GTE data register `reg` (mtc2) */
#define gte_mtc2(reg, v)                                                                                             \
    do {                                                                                                             \
        psyq_gte_mtc2(reg, (u32)(v));                                                                                \
    } while (0)

/* ---- the commands (gte_macros.inc's words, low 25 bits) ---- */

#define gte_nop()                                                                                                    \
    do {                                                                                                             \
    } while (0)
#define gte_rtpt() psyq_gte_cmd(0x0280030)  /* rtpt:  cop2op 0x02, 0x30 */
#define gte_nclip() psyq_gte_cmd(0x1400006) /* nclip: cop2op 0x14, 0x06, sf = 0 */
#define gte_avsz3() psyq_gte_cmd(0x158002D) /* avsz3: cop2op 0x15, 0x2D */
#define gte_avsz4() psyq_gte_cmd(0x168002E) /* avsz4: cop2op 0x16, 0x2E */
#define gte_ncct() psyq_gte_cmd(0x118043F)  /* ncct:  cop2op 0x11, 0x3F, lm = 1 */
#define gte_nccs() psyq_gte_cmd(0x108041B)  /* nccs:  cop2op 0x10, 0x1B, lm = 1 */
/* mvmva sf, mx, v, cv, lm: cop2op 0x04, 0x12 with the fields (the asm takes literals; each is masked to its width) */
#define gte_mvmva(sf, mx, v, cv, lm)                                                                                 \
    psyq_gte_cmd(0x0400012 | ((u32)(sf) & 1) << 19 | ((u32)(mx) & 3) << 17 | ((u32)(v) & 3) << 15 |                 \
                 ((u32)(cv) & 3) << 13 | ((u32)(lm) & 1) << 10)
/* V0 times the rotation matrix, without the translation (MAC1-3 = R * V0): mvmva 1, 0, 0, 3, 0 = 0x0486012 */
#define gte_rtv0() gte_mvmva(1, 0, 0, 3, 0)
/* V0 times the rotation matrix plus TR (inline_o.h: two nops, then rtv0tr = mvmva 1, 0, 0, 0, 0 = 0x0480012) */
#define gte_rtv0tr() psyq_gte_cmd(0x0480012)

/* ---- loads ---- */

/* Load V0-V2 from the three SVECTORs at `p` */
#define gte_ldv3c(p)                                                                                                 \
    do {                                                                                                             \
        const u8 *gte_ldv3c_p_ = (const u8 *)(p);                                                                    \
        psyq_gte_mtc2(0, port_gte_lw_(gte_ldv3c_p_ + 0));                                                            \
        psyq_gte_mtc2(1, port_gte_lw_(gte_ldv3c_p_ + 4));                                                            \
        psyq_gte_mtc2(2, port_gte_lw_(gte_ldv3c_p_ + 8));                                                            \
        psyq_gte_mtc2(3, port_gte_lw_(gte_ldv3c_p_ + 12));                                                           \
        psyq_gte_mtc2(4, port_gte_lw_(gte_ldv3c_p_ + 16));                                                           \
        psyq_gte_mtc2(5, port_gte_lw_(gte_ldv3c_p_ + 20));                                                           \
    } while (0)

/* Load V0 from two registers: vx/vy packed in `xy`, vz in `z` */
#define gte_ldv0_reg(xy, z)                                                                                          \
    do {                                                                                                             \
        u32 gte_ldv0_reg_xy_ = (u32)(xy);                                                                            \
        u32 gte_ldv0_reg_z_ = (u32)(z);                                                                              \
        psyq_gte_mtc2(0, gte_ldv0_reg_xy_);                                                                          \
        psyq_gte_mtc2(1, gte_ldv0_reg_z_);                                                                           \
    } while (0)

/* Read the next three SVECTORs at `p` into $8-$13 (here gte_prefetch_) while the GTE works */
#define gte_prefetchv3c(p)                                                                                           \
    do {                                                                                                             \
        const u8 *gte_prefetch_p_ = (const u8 *)(p);                                                                 \
        int gte_prefetch_i_;                                                                                         \
        for (gte_prefetch_i_ = 0; gte_prefetch_i_ < 6; gte_prefetch_i_++) {                                          \
            gte_prefetch_[gte_prefetch_i_] = port_gte_lw_(gte_prefetch_p_ + 4 * gte_prefetch_i_);                    \
        }                                                                                                            \
    } while (0)
/* Hand them to V0-V2 (mtc2 $8-$13 to $0-$5) */
#define gte_ldv3_prefetched()                                                                                        \
    do {                                                                                                             \
        int gte_prefetched_i_;                                                                                       \
        for (gte_prefetched_i_ = 0; gte_prefetched_i_ < 6; gte_prefetched_i_++) {                                    \
            psyq_gte_mtc2(gte_prefetched_i_, gte_prefetch_[gte_prefetched_i_]);                                      \
        }                                                                                                            \
    } while (0)

/* Set the colour and code (RGBC) the lighting commands start from */
#define gte_ldrgbc(v)                                                                                                \
    do {                                                                                                             \
        psyq_gte_mtc2(6, (u32)(v));                                                                                  \
    } while (0)

/*
 * A textured packet's UV words wait in V0-V2 until emitTextured* stores them: VXY1 = UV0 + CLUT, VXY2 = UV1 + TPAGE,
 * VZ1 = UV2 and VZ2 = UV3 (quads). `i` is a word index into `p`. On the PS1 `t` is the scratch register the sum is
 * built in (an input operand the asm overwrites; the C never reads it otherwise): here the sum is a local, and `t`
 * is only evaluated.
 */
#define gte_lduv0(t, p, i, clut)                                                                                     \
    do {                                                                                                             \
        const u8 *gte_lduv0_p_ = (const u8 *)(p);                                                                    \
        (void)(t);                                                                                                   \
        psyq_gte_mtc2(2, port_gte_lw_(gte_lduv0_p_ + (i) * 4) + (u32)(clut));                                        \
    } while (0)
#define gte_lduv1(t, p, i, tpage)                                                                                    \
    do {                                                                                                             \
        const u8 *gte_lduv1_p_ = (const u8 *)(p);                                                                    \
        (void)(t);                                                                                                   \
        psyq_gte_mtc2(4, port_gte_lw_(gte_lduv1_p_ + (i) * 4) + (u32)(tpage));                                       \
    } while (0)
/* gte_lduv0 from p[i] and gte_lduv1 from p[i + 1] (both sums are made after both operands are read, as the asm's
 * operands are registers loaded before it runs) */
#define gte_lduv01(t, p, i, clut, tpage)                                                                             \
    do {                                                                                                             \
        const u8 *gte_lduv01_p_ = (const u8 *)(p);                                                                   \
        u32 gte_lduv01_clut_ = (u32)(clut);                                                                          \
        u32 gte_lduv01_tpage_ = (u32)(tpage);                                                                        \
        (void)(t);                                                                                                   \
        psyq_gte_mtc2(2, port_gte_lw_(gte_lduv01_p_ + (i) * 4) + gte_lduv01_clut_);                                  \
        psyq_gte_mtc2(4, port_gte_lw_(gte_lduv01_p_ + (i) * 4 + 4) + gte_lduv01_tpage_);                             \
    } while (0)
#define gte_lduv2(p, i)                                                                                              \
    do {                                                                                                             \
        const u8 *gte_lduv2_p_ = (const u8 *)(p);                                                                    \
        psyq_gte_mtc2(3, port_gte_lw_(gte_lduv2_p_ + (i) * 4));                                                      \
    } while (0)
#define gte_lduv3(p, i)                                                                                              \
    do {                                                                                                             \
        const u8 *gte_lduv3_p_ = (const u8 *)(p);                                                                    \
        psyq_gte_mtc2(5, port_gte_lw_(gte_lduv3_p_ + (i) * 4));                                                      \
    } while (0)

/* Load V0 from the SVECTOR at `p` (the inline_c.h form) */
#define gte_ldv0c(p)                                                                                                 \
    do {                                                                                                             \
        const u8 *gte_ldv0c_p_ = (const u8 *)(p);                                                                    \
        psyq_gte_mtc2(0, port_gte_lw_(gte_ldv0c_p_ + 0));                                                            \
        psyq_gte_mtc2(1, port_gte_lw_(gte_ldv0c_p_ + 4));                                                            \
    } while (0)

/* Load V0 from the SVECTOR at `r` (the inline_o.h form) */
#define gte_ldv0(r)                                                                                                  \
    do {                                                                                                             \
        const u8 *gte_ldv0_p_ = (const u8 *)(r);                                                                     \
        psyq_gte_mtc2(0, port_gte_lw_(gte_ldv0_p_ + 0));                                                             \
        psyq_gte_mtc2(1, port_gte_lw_(gte_ldv0_p_ + 4));                                                             \
    } while (0)

/* ---- stores and reads ---- */

/* Store SXY0, SZ1, SXY1, SZ2, SXY2, SZ3 (the last rtpt's three vertices) as six words at `p` */
#define gte_stsxysz3c(p)                                                                                             \
    do {                                                                                                             \
        u8 *gte_stsxysz3c_p_ = (u8 *)(p);                                                                            \
        psyq_gte_swc2_(gte_stsxysz3c_p_ + 0, 12);                                                                    \
        psyq_gte_swc2_(gte_stsxysz3c_p_ + 4, 17);                                                                    \
        psyq_gte_swc2_(gte_stsxysz3c_p_ + 8, 13);                                                                    \
        psyq_gte_swc2_(gte_stsxysz3c_p_ + 12, 18);                                                                   \
        psyq_gte_swc2_(gte_stsxysz3c_p_ + 16, 14);                                                                   \
        psyq_gte_swc2_(gte_stsxysz3c_p_ + 20, 19);                                                                   \
    } while (0)

/* Store RGB0-RGB2 (the last ncct's three colours) as three words at `p` */
#define gte_strgb3c(p)                                                                                               \
    do {                                                                                                             \
        u8 *gte_strgb3c_p_ = (u8 *)(p);                                                                              \
        psyq_gte_swc2_(gte_strgb3c_p_ + 0, 20);                                                                      \
        psyq_gte_swc2_(gte_strgb3c_p_ + 4, 21);                                                                      \
        psyq_gte_swc2_(gte_strgb3c_p_ + 8, 22);                                                                      \
    } while (0)

/* Store RGB2 (the last lit colour) as a word at `p` */
#define gte_strgb(p)                                                                                                 \
    do {                                                                                                             \
        u8 *gte_strgb_p_ = (u8 *)(p);                                                                                \
        psyq_gte_swc2_(gte_strgb_p_, 22);                                                                            \
    } while (0)

/* Read MAC0 (the last nclip's result) into `v` */
#define gte_stopz_reg(v)                                                                                             \
    do {                                                                                                             \
        (v) = psyq_gte_mfc2(24);                                                                                     \
    } while (0)

/* Read MAC1-MAC3 (the last mvmva's results) into `x`, `y`, `z` */
#define gte_stmac123(x, y, z)                                                                                        \
    do {                                                                                                             \
        (x) = psyq_gte_mfc2(25);                                                                                     \
        (y) = psyq_gte_mfc2(26);                                                                                     \
        (z) = psyq_gte_mfc2(27);                                                                                     \
    } while (0)

/* Start a primitive packet at `p`: p[2] = SXY0 (swc2), p[1] = RGB0 | `code` (sw). RGB0 is read first, as the asm's
 * mfc2 $8, $20 comes before the SXY0 store. */
#define gte_stsxy0_rgbcode(p, code)                                                                                  \
    do {                                                                                                             \
        u8 *gte_rgbcode_p_ = (u8 *)(p);                                                                              \
        u32 gte_rgbcode_code_ = (u32)(code);                                                                         \
        u32 gte_rgbcode_r8_ = psyq_gte_mfc2(20);                                                                     \
        psyq_gte_swc2_(gte_rgbcode_p_ + 8, 12);                                                                      \
        port_gte_sw_(gte_rgbcode_p_ + 4, gte_rgbcode_r8_ | gte_rgbcode_code_);                                       \
    } while (0)

/* Store MAC1-MAC3 as three words at `r` (a VECTOR) */
#define gte_stlvnl(r)                                                                                                \
    do {                                                                                                             \
        u8 *gte_stlvnl_p_ = (u8 *)(r);                                                                               \
        psyq_gte_swc2_(gte_stlvnl_p_ + 0, 25);                                                                       \
        psyq_gte_swc2_(gte_stlvnl_p_ + 4, 26);                                                                       \
        psyq_gte_swc2_(gte_stlvnl_p_ + 8, 27);                                                                       \
    } while (0)

/* Store FLAG (control 31) as a word at `r` */
#define gte_stflg(r)                                                                                                 \
    do {                                                                                                             \
        u8 *gte_stflg_p_ = (u8 *)(r);                                                                                \
        port_gte_sw_(gte_stflg_p_, psyq_gte_cfc2(31));                                                               \
    } while (0)

/* ---- the matrices (a MATRIX: m[3][3] s16 and a pad halfword, 5 words; t[3] s32 at 20) ---- */

/* The rotation RT (control 0-4) from the MATRIX at `r` (the inline_c.h and inline_o.h forms load the same words) */
#define gte_SetRotMatrix_c(r)                                                                                        \
    do {                                                                                                             \
        const u8 *gte_rot_c_p_ = (const u8 *)(r);                                                                    \
        psyq_gte_ctc2(0, port_gte_lw_(gte_rot_c_p_ + 0));                                                            \
        psyq_gte_ctc2(1, port_gte_lw_(gte_rot_c_p_ + 4));                                                            \
        psyq_gte_ctc2(2, port_gte_lw_(gte_rot_c_p_ + 8));                                                            \
        psyq_gte_ctc2(3, port_gte_lw_(gte_rot_c_p_ + 12));                                                           \
        psyq_gte_ctc2(4, port_gte_lw_(gte_rot_c_p_ + 16));                                                           \
    } while (0)
#define gte_SetRotMatrix(r)                                                                                          \
    do {                                                                                                             \
        const u8 *gte_rot_p_ = (const u8 *)(r);                                                                      \
        psyq_gte_ctc2(0, port_gte_lw_(gte_rot_p_ + 0));                                                              \
        psyq_gte_ctc2(1, port_gte_lw_(gte_rot_p_ + 4));                                                              \
        psyq_gte_ctc2(2, port_gte_lw_(gte_rot_p_ + 8));                                                              \
        psyq_gte_ctc2(3, port_gte_lw_(gte_rot_p_ + 12));                                                             \
        psyq_gte_ctc2(4, port_gte_lw_(gte_rot_p_ + 16));                                                             \
    } while (0)

/* The light matrix LLM (control 8-12) from the MATRIX at `r` */
#define gte_SetLightMatrix(r)                                                                                        \
    do {                                                                                                             \
        const u8 *gte_llm_p_ = (const u8 *)(r);                                                                      \
        psyq_gte_ctc2(8, port_gte_lw_(gte_llm_p_ + 0));                                                              \
        psyq_gte_ctc2(9, port_gte_lw_(gte_llm_p_ + 4));                                                              \
        psyq_gte_ctc2(10, port_gte_lw_(gte_llm_p_ + 8));                                                             \
        psyq_gte_ctc2(11, port_gte_lw_(gte_llm_p_ + 12));                                                            \
        psyq_gte_ctc2(12, port_gte_lw_(gte_llm_p_ + 16));                                                            \
    } while (0)

/* The light colour matrix LCM (control 16-20) from the MATRIX at `r` */
#define gte_SetColorMatrix(r)                                                                                        \
    do {                                                                                                             \
        const u8 *gte_lcm_p_ = (const u8 *)(r);                                                                      \
        psyq_gte_ctc2(16, port_gte_lw_(gte_lcm_p_ + 0));                                                             \
        psyq_gte_ctc2(17, port_gte_lw_(gte_lcm_p_ + 4));                                                             \
        psyq_gte_ctc2(18, port_gte_lw_(gte_lcm_p_ + 8));                                                             \
        psyq_gte_ctc2(19, port_gte_lw_(gte_lcm_p_ + 12));                                                            \
        psyq_gte_ctc2(20, port_gte_lw_(gte_lcm_p_ + 16));                                                            \
    } while (0)

/* The translation TR (control 5-7) from the MATRIX at `r`'s t[3] (the inline_c.h and inline_o.h forms) */
#define gte_SetTransMatrix_c(r)                                                                                      \
    do {                                                                                                             \
        const u8 *gte_tr_c_p_ = (const u8 *)(r);                                                                     \
        psyq_gte_ctc2(5, port_gte_lw_(gte_tr_c_p_ + 20));                                                            \
        psyq_gte_ctc2(6, port_gte_lw_(gte_tr_c_p_ + 24));                                                            \
        psyq_gte_ctc2(7, port_gte_lw_(gte_tr_c_p_ + 28));                                                            \
    } while (0)
#define gte_SetTransMatrix(r)                                                                                        \
    do {                                                                                                             \
        const u8 *gte_tr_p_ = (const u8 *)(r);                                                                       \
        psyq_gte_ctc2(5, port_gte_lw_(gte_tr_p_ + 20));                                                              \
        psyq_gte_ctc2(6, port_gte_lw_(gte_tr_p_ + 24));                                                              \
        psyq_gte_ctc2(7, port_gte_lw_(gte_tr_p_ + 28));                                                              \
    } while (0)

#endif /* GTE_H */
