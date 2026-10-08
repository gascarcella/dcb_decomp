/*
 * tests/port/gte_host_test.c: the host GTE macros (port/include/gte.h) against psxstack's software GTE
 * (psxstack/psyq/gte.c, libgte.c). Built and run by scripts/gte_test.sh; no disc, no game data.
 *
 * Each case drives the same GTE two ways and requires the same result: the game's macro sequence, and a reference
 * that does not go through the macro under test: a LIBGTE function the shim implements (ApplyMatrixSV, MulMatrix,
 * SetColorMatrix), the one-vertex command repeated per vertex (rtpt = 3 x rtps, ncct = 3 x nccs), the stack's
 * register entry points called directly, or the formula from the hardware's public description (psx-spx) where it
 * is exact (nclip, avsz3/4, the translation of rtv0tr, the projection within a pixel). The shim implements no
 * RotTransPers/RotTransPers3/NormalClip/AverageZ3/NormalColorCol3 at psxstack v0.3.0, hence the command references.
 * Every one of the 40 macros is used at least once. Exit 0 when every check passes.
 */
#include <stdio.h>
#include <string.h>

#include "gte.h"
#include "psxstack/psyq/libgte.h"

/* LIBGTE/LIBGS functions the shim defines without declaring them in its libgte.h (psyq/libgte.c). */
MATRIX *MulMatrix(MATRIX *m0, MATRIX *m1);
void SetColorMatrix(MATRIX *m);
void SetGeomScreen(s32 h);

/* The shim's tracing (psyq/psyq.c), which libgte.c's PSYQ_TRACE uses: off. Defined here so that the test links
 * gte.c, gte_shadow.c and libgte.c alone. */
int psyq_trace_state = 0;
int psyq_trace_decide(void) {
    return 0;
}
void psyq_trace_printf(const char *fmt, ...) {
    (void)fmt;
}

void psyq_gte_clear(void);

static int failures;
static int checks;

#define CHECK(cond, ...)                                                                                             \
    do {                                                                                                             \
        checks++;                                                                                                    \
        if (!(cond)) {                                                                                               \
            failures++;                                                                                              \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
            printf(__VA_ARGS__);                                                                                     \
            printf("\n");                                                                                            \
        }                                                                                                            \
    } while (0)

static void begin(const char *name) {
    printf("%s\n", name);
}

static u32 word_of(const void *p) {
    u32 v;

    memcpy(&v, p, 4);
    return v;
}

/* The geometry every case starts from: InitGeom's registers, the screen at (160, 120), H = 512. */
static void reset_geometry(void) {
    psyq_gte_clear();
    InitGeom();
    SetGeomOffset(160, 120);
    SetGeomScreen(512);
}

/* A rotation that mixes every axis, and a translation in front of the camera. */
static void test_matrix(MATRIX *m) {
    SVECTOR ang = { 300, -700, 1100, 0 };

    memset(m, 0, sizeof(*m));
    RotMatrixYXZ_gte(&ang, m);
    m->t[0] = 40;
    m->t[1] = -25;
    m->t[2] = 1800;
}

static SVECTOR verts[9] = {
    { 100, -50, 30, 0 }, { -120, 80, -60, 0 }, { 10, 200, 90, 0 },  { -300, -40, 250, 0 }, { 75, 75, -75, 0 },
    { 0, 0, 0, 0 },      { 400, -300, 100, 0 }, { -10, 20, -30, 0 }, { 0, 0, 0, 0 } /* the prefetch reads past */
};

/* ---- rtpt: gte_ldv3c / gte_rtpt / gte_stsxysz3c against three rtps ---- */
static void case_rtpt(void) {
    MATRIX m;
    u32 out[6], ref[6];
    u32 flag;
    int k;

    begin("rtpt: ldv3c + rtpt + stsxysz3c == 3 x rtps; the projection within a pixel");
    test_matrix(&m);
    reset_geometry();
    gte_SetRotMatrix(&m);
    gte_SetTransMatrix(&m);
    gte_ldv3c(verts);
    gte_rtpt();
    gte_stsxysz3c(out);
    gte_stflg(&flag);
    /* IR0 (the depth cue, bit 12) saturates at this distance; no error bit (SX, SY, SZ, the division) may be set */
    CHECK(!(flag & 0x80000000u), "FLAG %08x after rtpt (no error bit expected)", flag);

    reset_geometry();
    gte_SetRotMatrix(&m);
    gte_SetTransMatrix(&m);
    for (k = 0; k < 3; k++) {
        psyq_gte_mtc2(0, word_of(&verts[k].vx));
        psyq_gte_mtc2(1, word_of(&verts[k].vz));
        psyq_gte_cmd(0x0180001); /* rtps: cop2op 0x01, 0x01 */
    }
    ref[0] = psyq_gte_mfc2(12);
    ref[1] = psyq_gte_mfc2(17);
    ref[2] = psyq_gte_mfc2(13);
    ref[3] = psyq_gte_mfc2(18);
    ref[4] = psyq_gte_mfc2(14);
    ref[5] = psyq_gte_mfc2(19);
    for (k = 0; k < 6; k++) {
        CHECK(out[k] == ref[k], "word %d: rtpt %08x, 3 x rtps %08x", k, out[k], ref[k]);
    }
    /* the screen position from the formula, in floating point: OF + H * camera / z */
    for (k = 0; k < 3; k++) {
        double c[3], sx, sy;
        int i;

        for (i = 0; i < 3; i++) {
            c[i] = m.t[i] + (m.m[i][0] * (double)verts[k].vx + m.m[i][1] * (double)verts[k].vy +
                             m.m[i][2] * (double)verts[k].vz) / 4096.0;
        }
        sx = 160 + 512 * c[0] / c[2];
        sy = 120 + 512 * c[1] / c[2];
        CHECK((s16)(out[2 * k] & 0xFFFF) - sx < 1.5 && (s16)(out[2 * k] & 0xFFFF) - sx > -1.5,
              "vertex %d: SX %d, formula %.2f", k, (s16)(out[2 * k] & 0xFFFF), sx);
        CHECK((s16)(out[2 * k] >> 16) - sy < 1.5 && (s16)(out[2 * k] >> 16) - sy > -1.5, "vertex %d: SY %d, formula %.2f",
              k, (s16)(out[2 * k] >> 16), sy);
        CHECK((s32)out[2 * k + 1] - c[2] < 1.5 && (s32)out[2 * k + 1] - c[2] > -1.5, "vertex %d: SZ %u, formula %.2f", k,
              out[2 * k + 1], c[2]);
    }
}

/* ---- the prefetch pair: transformAndLightVertices's loop against plain loads ---- */
static void case_prefetch(void) {
    MATRIX m;
    u32 out[12], ref[12];
    u32 direct[6], loaded[6];
    u32 *src;
    u32 *dst;
    s32 count;
    int i;

    begin("prefetch: prefetchv3c + ldv3_prefetched == ldv3c (the loop of transformAndLightVertices)");
    test_matrix(&m);
    reset_geometry();
    gte_SetRotMatrix(&m);
    gte_SetTransMatrix(&m);
    /* the game's loop (tmd_sort.c): six vertices, the prefetch reading one group past the end */
    src = (u32 *)verts;
    dst = out;
    count = 6;
    gte_ldv3c(src);
    src += 6;
    do {
        count -= 3;
        gte_rtpt();
        gte_prefetchv3c(src);
        gte_stsxysz3c(dst);
        dst += 6;
        gte_ldv3_prefetched();
        src += 6;
    } while (count > 0);
    /* the same without the prefetch */
    for (i = 0; i < 2; i++) {
        gte_ldv3c(&verts[3 * i]);
        gte_rtpt();
        gte_stsxysz3c(&ref[6 * i]);
    }
    for (i = 0; i < 12; i++) {
        CHECK(out[i] == ref[i], "word %d: prefetched %08x, plain %08x", i, out[i], ref[i]);
    }
    /* the pair alone: V0-V2 as ldv3c leaves them, whatever was there before */
    gte_ldv3c(&verts[3]);
    for (i = 0; i < 6; i++) {
        loaded[i] = psyq_gte_mfc2(i);
    }
    gte_prefetchv3c(&verts[3]);
    for (i = 0; i < 6; i++) {
        gte_mtc2(i, 0xDEAD0000u + i);
    }
    gte_ldv3_prefetched();
    for (i = 0; i < 6; i++) {
        gte_mfc2(i, direct[i]);
        CHECK(direct[i] == loaded[i], "register %d: %08x, ldv3c %08x", i, direct[i], loaded[i]);
    }
}

/* ---- nclip: gte_lwc2 of SXY0-2, gte_nclip, gte_stopz_reg, gte_stflg ---- */
static s32 nclip_formula(const s16 xy[6]) {
    s64 x0 = xy[0], y0 = xy[1], x1 = xy[2], y1 = xy[3], x2 = xy[4], y2 = xy[5];

    return (s32)(x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1);
}

static void case_nclip(void) {
    static const s16 tri[3][6] = {
        { 10, 10, 200, 20, 50, 150 },     /* one winding */
        { 10, 10, 50, 150, 200, 20 },     /* the other */
        { -1000, -1000, 1023, -900, 0, 1023 },
    };
    int t;

    begin("nclip: lwc2 SXY0-2 + nclip + stopz_reg == the formula; FLAG 0");
    for (t = 0; t < 3; t++) {
        u32 w[3];
        s32 opz;
        u32 flag;
        int i;

        for (i = 0; i < 3; i++) {
            w[i] = (u16)tri[t][2 * i] | (u32)(u16)tri[t][2 * i + 1] << 16;
        }
        reset_geometry();
        gte_lwc2(12, 0, &w[0]);
        gte_lwc2(13, 0, &w[1]);
        gte_lwc2(14, 0, &w[2]);
        gte_nclip();
        gte_stopz_reg(opz);
        gte_stflg(&flag);
        CHECK(opz == nclip_formula(tri[t]), "triangle %d: nclip %d, formula %d", t, opz, nclip_formula(tri[t]));
        CHECK(flag == 0, "triangle %d: FLAG %08x", t, flag);
    }
}

/* ---- avsz3 / avsz4: lwc2 of SZ0-3, OTZ by gte_mfc2(7), FLAG on saturation ---- */
static void case_avsz(void) {
    u32 sz[4] = { 400, 1000, 2000, 3001 };
    u32 otz, mac0, flag;
    s64 sum;

    begin("avsz3/avsz4: OTZ == (ZSF * sum) >> 12; saturation sets FLAG bits 18 and 31 (stflg == cfc2 31)");
    reset_geometry();
    gte_lwc2(16, 0, &sz[0]);
    gte_lwc2(17, 0, &sz[1]);
    gte_lwc2(18, 0, &sz[2]);
    gte_lwc2(19, 0, &sz[3]);
    gte_avsz3();
    gte_mfc2(7, otz);
    gte_mfc2(24, mac0);
    sum = 0x155 * (s64)(sz[1] + sz[2] + sz[3]);
    CHECK(otz == (u32)(sum >> 12), "avsz3: OTZ %u, formula %lld", otz, (long long)(sum >> 12));
    CHECK(mac0 == (u32)sum, "avsz3: MAC0 %u, formula %lld", mac0, (long long)sum);
    gte_avsz4();
    gte_mfc2(7, otz);
    sum = 0x100 * (s64)(sz[0] + sz[1] + sz[2] + sz[3]);
    CHECK(otz == (u32)(sum >> 12), "avsz4: OTZ %u, formula %lld", otz, (long long)(sum >> 12));
    gte_stflg(&flag);
    CHECK(flag == 0, "avsz4: FLAG %08x", flag);
    /* ZSF3 and SZ large: OTZ saturates to 0xFFFF */
    psyq_gte_ctc2(29, 0x7000);
    gte_mtc2(17, 30000);
    gte_mtc2(18, 30000);
    gte_mtc2(19, 30000);
    gte_avsz3();
    gte_mfc2(7, otz);
    gte_stflg(&flag);
    CHECK(otz == 0xFFFF, "avsz3 saturated: OTZ %u", otz);
    CHECK((flag & 0x80040000u) == 0x80040000u, "avsz3 saturated: FLAG %08x (bits 18 and 31 expected)", flag);
    CHECK(flag == psyq_gte_cfc2(31), "stflg %08x, cfc2 31 %08x", flag, psyq_gte_cfc2(31));
}

/* ---- mvmva: gte_rtv0 / gte_mvmva / gte_stmac123 against ApplyMatrixSV; gte_rtv0tr / gte_stlvnl ---- */
static void case_mvmva(void) {
    MATRIX m;
    SVECTOR out;
    VECTOR lv;
    s32 x, y, z;
    u32 flag;
    int k;

    begin("mvmva: rtv0 == ApplyMatrixSV; rtv0tr == ApplyMatrixSV + TR; mvmva(1,0,0,3,0) == rtv0");
    test_matrix(&m);
    for (k = 0; k < 8; k++) {
        reset_geometry();
        ApplyMatrixSV(&m, &verts[k], &out);
        gte_SetRotMatrix_c(&m);
        gte_ldv0c(&verts[k]);
        gte_rtv0();
        gte_stmac123(x, y, z);
        CHECK(x == out.vx && y == out.vy && z == out.vz, "vertex %d: rtv0 (%d %d %d), ApplyMatrixSV (%d %d %d)", k, x,
              y, z, out.vx, out.vy, out.vz);
        gte_ldv0_reg(word_of(&verts[k].vx), word_of(&verts[k].vz));
        gte_mvmva(1, 0, 0, 3, 0);
        gte_stmac123(x, y, z);
        CHECK(x == out.vx && y == out.vy && z == out.vz, "vertex %d: mvmva (%d %d %d)", k, x, y, z);
        gte_SetRotMatrix(&m);
        gte_SetTransMatrix_c(&m);
        gte_ldv0(&verts[k]);
        gte_rtv0tr();
        gte_stlvnl(&lv);
        gte_stflg(&flag);
        CHECK(lv.vx == m.t[0] + out.vx && lv.vy == m.t[1] + out.vy && lv.vz == m.t[2] + out.vz,
              "vertex %d: rtv0tr (%d %d %d), expected (%d %d %d)", k, lv.vx, lv.vy, lv.vz, m.t[0] + out.vx,
              m.t[1] + out.vy, m.t[2] + out.vz);
        CHECK(flag == 0, "vertex %d: FLAG %08x", k, flag);
    }
}

/* ---- the matrix setters against the shim's (MulMatrix loads RT; SetColorMatrix LCM) and the struct's words ---- */
static void case_matrices(void) {
    MATRIX m, a, ident;
    u32 ref[5];
    int i;

    begin("matrices: SetRotMatrix(_c) == MulMatrix's RT, SetColorMatrix == the shim's, SetLightMatrix, "
          "SetTransMatrix(_c) == the struct's words");
    test_matrix(&m);
    m.m[2][2] = -1234; /* a negative last element: the word with the pad halfword */
    memset(&ident, 0, sizeof(ident));
    ident.m[0][0] = ident.m[1][1] = ident.m[2][2] = 4096;
    reset_geometry();
    a = m;
    MulMatrix(&a, &ident); /* RT = m's rotation, as LIBGTE loads it */
    for (i = 0; i < 5; i++) {
        ref[i] = psyq_gte_cfc2(i);
    }
    psyq_gte_clear();
    gte_SetRotMatrix(&m);
    for (i = 0; i < 5; i++) {
        CHECK(psyq_gte_cfc2(i) == ref[i], "SetRotMatrix RT word %d: %08x, MulMatrix's %08x", i, psyq_gte_cfc2(i),
              ref[i]);
    }
    psyq_gte_clear();
    gte_SetRotMatrix_c(&m);
    for (i = 0; i < 5; i++) {
        CHECK(psyq_gte_cfc2(i) == ref[i], "SetRotMatrix_c RT word %d: %08x, MulMatrix's %08x", i, psyq_gte_cfc2(i),
              ref[i]);
    }
    psyq_gte_clear();
    SetColorMatrix(&m);
    for (i = 0; i < 5; i++) {
        ref[i] = psyq_gte_cfc2(16 + i);
    }
    psyq_gte_clear();
    gte_SetColorMatrix(&m);
    for (i = 0; i < 5; i++) {
        CHECK(psyq_gte_cfc2(16 + i) == ref[i], "SetColorMatrix LCM word %d: %08x, the shim's %08x", i,
              psyq_gte_cfc2(16 + i), ref[i]);
    }
    psyq_gte_clear();
    gte_SetLightMatrix(&m);
    for (i = 0; i < 5; i++) {
        CHECK(psyq_gte_cfc2(8 + i) == ref[i], "SetLightMatrix LLM word %d: %08x, expected %08x", i, psyq_gte_cfc2(8 + i),
              ref[i]);
    }
    psyq_gte_clear();
    gte_SetTransMatrix(&m);
    for (i = 0; i < 3; i++) {
        CHECK(psyq_gte_cfc2(5 + i) == (u32)m.t[i], "SetTransMatrix TR %d: %08x, t %08x", i, psyq_gte_cfc2(5 + i),
              (u32)m.t[i]);
    }
    psyq_gte_clear();
    gte_SetTransMatrix_c(&m);
    for (i = 0; i < 3; i++) {
        CHECK(psyq_gte_cfc2(5 + i) == (u32)m.t[i], "SetTransMatrix_c TR %d: %08x, t %08x", i, psyq_gte_cfc2(5 + i),
              (u32)m.t[i]);
    }
}

/* ---- ncct: gte_ldrgbc / gte_ldv3c / gte_ncct / gte_strgb3c against 3 x nccs / gte_strgb and the formula ---- */
static s32 clamp(s32 v, s32 lo, s32 hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* NCCS (sf 1, lm 1) as psx-spx gives it: IR = LLM * V >> 12; IR = (BK * 0x1000 + LCM * IR) >> 12; MAC = (RGB * IR)
 * << 4 >> 12; the colour = MAC / 16, CODE from RGBC. */
static u32 ncc_formula(const MATRIX *llm, const MATRIX *lcm, const s32 bk[3], u32 rgbc, const SVECTOR *v) {
    s32 ir[3], ir2[3], c[3];
    const s32 vec[3] = { v->vx, v->vy, v->vz };
    int i, j;

    for (i = 0; i < 3; i++) {
        s64 s = 0;

        for (j = 0; j < 3; j++) {
            s += (s64)llm->m[i][j] * vec[j];
        }
        ir[i] = clamp((s32)(s >> 12), 0, 0x7FFF);
    }
    for (i = 0; i < 3; i++) {
        s64 s = (s64)bk[i] * 0x1000;

        for (j = 0; j < 3; j++) {
            s += (s64)lcm->m[i][j] * ir[j];
        }
        ir2[i] = clamp((s32)(s >> 12), 0, 0x7FFF);
    }
    for (i = 0; i < 3; i++) {
        s64 mac = (((s64)((rgbc >> (8 * i)) & 0xFF) * ir2[i]) << 4) >> 12;

        c[i] = clamp((s32)(mac / 16), 0, 0xFF);
    }
    return (u32)c[0] | (u32)c[1] << 8 | (u32)c[2] << 16 | (rgbc & 0xFF000000u);
}

static void case_ncct(void) {
    static SVECTOR normals[3] = { { 0, 0, 4096, 0 }, { 2896, 0, 2896, 0 }, { -1000, 3000, 2000, 0 } };
    MATRIX llm, lcm;
    s32 bk[3];
    u32 rgbc = 0x34806040u; /* code 0x34, colour (0x40, 0x60, 0x80) */
    u32 out[3], ref[3];
    int k;

    begin("ncct: ldrgbc + ldv3c + ncct + strgb3c == 3 x (ldv0c + nccs + strgb) == the formula");
    memset(&llm, 0, sizeof(llm));
    memset(&lcm, 0, sizeof(lcm));
    llm.m[0][0] = 2000, llm.m[0][1] = -500, llm.m[0][2] = 3000; /* the light directions, rows */
    llm.m[1][0] = 1500, llm.m[1][1] = 2500, llm.m[1][2] = 800;
    llm.m[2][0] = -300, llm.m[2][1] = 700, llm.m[2][2] = 3500;
    lcm.m[0][0] = 3000, lcm.m[0][1] = 500, lcm.m[0][2] = 200; /* the light colours, columns */
    lcm.m[1][0] = 400, lcm.m[1][1] = 2800, lcm.m[1][2] = 600;
    lcm.m[2][0] = 100, lcm.m[2][1] = 900, lcm.m[2][2] = 2600;
    reset_geometry();
    gte_SetLightMatrix(&llm);
    gte_SetColorMatrix(&lcm);
    SetBackColor(30, 20, 10);
    for (k = 0; k < 3; k++) {
        bk[k] = (s32)psyq_gte_cfc2(13 + k);
    }
    gte_ldrgbc(rgbc);
    gte_ldv3c(normals);
    gte_nop();
    gte_nop();
    gte_ncct();
    gte_strgb3c(out);
    for (k = 0; k < 3; k++) {
        gte_ldv0c(&normals[k]);
        gte_nccs();
        gte_strgb(&ref[k]);
        CHECK(out[k] == ref[k], "normal %d: ncct %08x, nccs %08x", k, out[k], ref[k]);
        CHECK(out[k] == ncc_formula(&llm, &lcm, bk, rgbc, &normals[k]), "normal %d: ncct %08x, formula %08x", k, out[k],
              ncc_formula(&llm, &lcm, bk, rgbc, &normals[k]));
    }
}

/* ---- the packet macros: lduv0/1/01/2/3, stsxy0_rgbcode, swc2 (emitTexturedQuad's stores) ---- */
static void case_packet(void) {
    u32 uv[8] = { 0x11111111u, 0x22220102u, 0x33330304u, 0x44440506u, 0x55550708u, 0x66660910u, 0x0000A0B0u,
                  0x0000C0D0u };
    u32 packet[13];
    u32 scratch = 0;
    u32 clut = 0x7FC00000u, tpage = 0x00150000u, code = 0x2C000000u;
    u32 sxy = 0x00400030u, rgb = 0x00102030u;

    begin("packet: lduv0/1/01/2/3 + stsxy0_rgbcode + swc2 lay out a textured quad");
    reset_geometry();
    memset(packet, 0xEE, sizeof(packet));
    gte_mtc2(12, sxy);
    gte_mtc2(20, rgb);
    gte_lduv01(scratch, uv, 2, clut, tpage);
    gte_lduv2(uv, 6);
    gte_lduv3(uv, 7);
    gte_stsxy0_rgbcode(packet, code);
    gte_swc2(2, 12, packet);
    gte_swc2(4, 20, packet);
    gte_swc2(3, 28, packet);
    gte_swc2(5, 36, packet);
    CHECK(packet[1] == (rgb | code), "p[1] %08x, RGB0 | code %08x", packet[1], rgb | code);
    CHECK(packet[2] == sxy, "p[2] %08x, SXY0 %08x", packet[2], sxy);
    CHECK(packet[3] == uv[2] + clut, "p[3] %08x, uv[2] + clut %08x", packet[3], uv[2] + clut);
    CHECK(packet[5] == uv[3] + tpage, "p[5] %08x, uv[3] + tpage %08x", packet[5], uv[3] + tpage);
    /* VZ1 and VZ2 are 16-bit registers: a read (swc2) gives them sign-extended, on the PS1 as here */
    CHECK(packet[7] == (u32)(s16)uv[6], "p[7] %08x, uv[6] sign-extended %08x", packet[7], (u32)(s16)uv[6]);
    CHECK(packet[9] == (u32)(s16)uv[7], "p[9] %08x, uv[7] sign-extended %08x", packet[9], (u32)(s16)uv[7]);
    CHECK(packet[0] == 0xEEEEEEEEu && packet[4] == 0xEEEEEEEEu, "a word the macros do not write changed");
    gte_lduv0(scratch, uv, 4, clut);
    gte_lduv1(scratch, uv, 5, tpage);
    gte_swc2(2, 12, packet);
    gte_swc2(4, 20, packet);
    CHECK(packet[3] == uv[4] + clut, "lduv0: p[3] %08x, expected %08x", packet[3], uv[4] + clut);
    CHECK(packet[5] == uv[5] + tpage, "lduv1: p[5] %08x, expected %08x", packet[5], uv[5] + tpage);
    CHECK(scratch == 0, "the scratch operand changed: %08x", scratch);
}

/* ---- every pointer argument evaluated once ---- */
static int evaluations;

static void *once(void *p) {
    evaluations++;
    return p;
}

static void case_once(void) {
    MATRIX m;
    u32 buf[8] = { 0 };
    u32 t = 0;
    int n;

    begin("arguments: each evaluated once");
    test_matrix(&m);
    evaluations = 0, n = 0;
#define COUNT(stmt) \
    do {          \
        stmt;     \
        n++;      \
    } while (0)
    COUNT(gte_ldv3c(once(verts)));
    COUNT(gte_prefetchv3c(once(verts)));
    COUNT(gte_stsxysz3c(once(buf)));
    COUNT(gte_strgb3c(once(buf)));
    COUNT(gte_lduv01(t, once(buf), 1, 0, 0));
    COUNT(gte_stsxy0_rgbcode(once(buf), 0));
    COUNT(gte_SetRotMatrix(once(&m)));
    COUNT(gte_SetTransMatrix(once(&m)));
    COUNT(gte_SetLightMatrix(once(&m)));
    COUNT(gte_SetColorMatrix(once(&m)));
    COUNT(gte_stlvnl(once(buf)));
    COUNT(gte_stflg(once(buf)));
    COUNT(gte_ldv0(once(verts)));
    COUNT(gte_lwc2(0, 4, once(buf)));
    COUNT(gte_swc2(0, 4, once(buf)));
#undef COUNT
    CHECK(evaluations == n, "%d evaluations for %d macros", evaluations, n);
}

int main(void) {
    case_rtpt();
    case_prefetch();
    case_nclip();
    case_avsz();
    case_mvmva();
    case_matrices();
    case_ncct();
    case_packet();
    case_once();
    printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
