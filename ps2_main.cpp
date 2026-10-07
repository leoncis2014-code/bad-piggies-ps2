// Capa de PlayStation 2: dibuja con gsKit y lee el mando con libpad. El juego en si vive en game.cpp.
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <libpad.h>
#include <gsKit.h>
#include <dmaKit.h>
#include "game.h"
#include "level1_data.h"
extern "C" {
#include "sprites.h"
}

static GSGLOBAL* gs;
static GSTEXTURE texPig, texBox, texWheel;
static char padBuf[256] __attribute__((aligned(64)));
static float kx = 1.f, ky = 1.f;   // virtual 640x448 -> pantalla real

static u64 rgba(int r, int g, int b, int a) { return GS_SETREG_RGBAQ(r, g, b, a, 0x00); }

static void loadTex(GSTEXTURE& t, const uint32_t* pix, int w, int h) {
    memset(&t, 0, sizeof(t));
    t.Width = w; t.Height = h; t.PSM = GS_PSM_CT32; t.Filter = GS_FILTER_LINEAR;
    t.Mem = (u32*)memalign(128, w * h * 4);
    memcpy(t.Mem, pix, w * h * 4);
    t.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(w, h, t.PSM), GSKIT_ALLOC_USERBUFFER);
    gsKit_texture_upload(gs, &t);
}

static void rect(float x1, float y1, float x2, float y2, u64 col) {
    gsKit_prim_sprite(gs, x1 * kx, y1 * ky, x2 * kx, y2 * ky, 2, col);
}
static void frameRect(float x1, float y1, float x2, float y2, float th, u64 col) {
    rect(x1, y1, x2, y1 + th, col); rect(x1, y2 - th, x2, y2, col);
    rect(x1, y1, x1 + th, y2, col); rect(x2 - th, y1, x2, y2, col);
}
static void tri(float x1, float y1, float x2, float y2, float x3, float y3, u64 col) {
    gsKit_prim_triangle(gs, x1 * kx, y1 * ky, x2 * kx, y2 * ky, x3 * kx, y3 * ky, 1, col);
}

// Sprite centrado en (cx,cy) con tamano w x h (pantalla virtual), girado ang radianes
static void sprite(GSTEXTURE& t, float cx, float cy, float w, float h, float ang) {
    float c = cosf(ang), s = sinf(ang), hw = w * 0.5f, hh = h * 0.5f;
    float lx[4] = { -hw, hw, -hw, hw }, ly[4] = { -hh, -hh, hh, hh };
    float X[4], Y[4];
    for (int i = 0; i < 4; i++) { X[i] = (cx + lx[i] * c - ly[i] * s) * kx; Y[i] = (cy + lx[i] * s + ly[i] * c) * ky; }
    float tw = (float)t.Width, th = (float)t.Height;
    u64 col = rgba(0x80, 0x80, 0x80, 0x80);
    gsKit_prim_triangle_texture(gs, &t, X[0], Y[0], 0, 0, X[1], Y[1], tw, 0, X[2], Y[2], 0, th, 1, col);
    gsKit_prim_triangle_texture(gs, &t, X[1], Y[1], tw, 0, X[3], Y[3], tw, th, X[2], Y[2], 0, th, 1, col);
}
static GSTEXTURE& texOf(int type) { return type == P_PIG ? texPig : (type == P_BOX ? texBox : texWheel); }
static void spriteOf(int type, float cx, float cy, float scale, float ang) {
    GSTEXTURE& t = texOf(type);
    sprite(t, cx, cy, t.Width * scale, t.Height * scale, ang);
}

static void worldToScreen(const Game& g, float wx, float wy, float& sx, float& sy) {
    float cx, cy; g.camera(cx, cy);
    sx = (wx - cx) * ZOOM + SCREEN_W * 0.5f; sy = (wy - cy) * ZOOM + SCREEN_H * 0.5f;
}

static void drawTerrain(const Game& g) {
    u64 fill = rgba(0x5A, 0x78, 0x6E, 0x80), grass = rgba(0x4C, 0xA8, 0x32, 0x80);
    float X[64], Y[64];
    for (int i = 0; i < L1_NPTS && i < 64; i++) worldToScreen(g, L1_PTS[i][0], L1_PTS[i][1], X[i], Y[i]);
    for (int i = 0; i < L1_NTRIS; i++) {
        int a = L1_TRIS[i][0], b = L1_TRIS[i][1], c = L1_TRIS[i][2];
        tri(X[a], Y[a], X[b], Y[b], X[c], Y[c], fill);
    }
    for (int i = 0; i < L1_NPTS && i < 64; i++) {
        if (!L1_TOP[i]) continue;
        int j = (i + 1) % L1_NPTS;
        for (int k = 0; k < 4; k++)   // linea gruesa de hierba
            gsKit_prim_line(gs, X[i] * kx, (Y[i] + k) * ky, X[j] * kx, (Y[j] + k) * ky, 2, grass);
    }
}

static void drawUI(const Game& g) {
    if (g.mode() == M_BUILD) {
        u64 cellc = rgba(0xFF, 0xFF, 0xFF, 0x24), cellb = rgba(0xFF, 0xFF, 0xFF, 0x60);
        for (int r = 0; r < 2; r++) for (int c = 0; c < 3; c++) {
            float sx, sy; g.cellScreen(c, r, sx, sy);
            rect(sx - 31, sy - 31, sx + 31, sy + 31, cellc);
            frameRect(sx - 31, sy - 31, sx + 31, sy + 31, 1.5f, cellb);
        }
        // bandeja inferior: cerdito, caja, rueda con puntos = cantidad que queda
        for (int i = 0; i < P_COUNT; i++) {
            float sx, sy; g.traySlot(i, sx, sy);
            rect(sx - 30, sy - 30, sx + 30, sy + 30, rgba(0x10, 0x20, 0x30, 0x50));
            spriteOf(i, sx, sy - 4, 0.8f, 0.f);
            for (int k = 0; k < g.remaining(i); k++) rect(sx - 24 + k * 10, sy + 20, sx - 18 + k * 10, sy + 26, rgba(0xFF, 0xFF, 0xFF, 0x80));
            if (g.selected() == i) frameRect(sx - 30, sy - 30, sx + 30, sy + 30, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
        }
        // cursor
        float cx, cy; g.cursor(cx, cy);
        tri(cx, cy, cx + 14, cy + 6, cx + 6, cy + 14, rgba(0xFF, 0xFF, 0xFF, 0x80));
        tri(cx + 1, cy + 1, cx + 11, cy + 6, cx + 6, cy + 11, rgba(0x20, 0x20, 0x20, 0x80));
    } else if (g.mode() == M_WIN) {
        rect(0, 0, SCREEN_W, 34, rgba(0x30, 0xC0, 0x30, 0x70));
        for (int i = 0; i < 5; i++) spriteOf(P_PIG, 120 + i * 100, 17, 0.55f, 0.f);
    } else if (g.mode() == M_LOSE) {
        rect(0, 0, SCREEN_W, 34, rgba(0xD0, 0x30, 0x30, 0x70));
    }
}

int main(int, char**) {
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, padBuf);

    gs = gsKit_init_global();
    gs->PrimAlphaEnable = GS_SETTING_ON;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->DoubleBuffering = GS_SETTING_ON;
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    kx = gs->Width / SCREEN_W; ky = gs->Height / SCREEN_H;
    const float dt = (gs->Mode == GS_MODE_PAL) ? (1.f / 50.f) : (1.f / 60.f);

    loadTex(texPig, SPR_PIG_PIX, SPR_PIG_W, SPR_PIG_H);
    loadTex(texBox, SPR_BOX_PIX, SPR_BOX_W, SPR_BOX_H);
    loadTex(texWheel, SPR_WHEEL_PIX, SPR_WHEEL_W, SPR_WHEEL_H);

    Game game;
    unsigned prev = 0;
    for (;;) {
        // --- entrada ---
        GameInput in;
        int st = padGetState(0, 0);
        if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) {
            struct padButtonStatus b;
            if (padRead(0, 0, &b) != 0) {
                unsigned btn = 0xffff ^ b.btns;
                float ax = ((int)b.ljoy_h - 128) / 128.f, ay = ((int)b.ljoy_v - 128) / 128.f;
                if (fabsf(ax) < 0.2f) { ax = 0; }
                if (fabsf(ay) < 0.2f) { ay = 0; }
                if (btn & PAD_LEFT) { ax = -1; }
                if (btn & PAD_RIGHT) { ax = 1; }
                if (btn & PAD_UP) { ay = -1; }
                if (btn & PAD_DOWN) { ay = 1; }
                in.stickX = ax; in.stickY = ay;
                unsigned edge = btn & ~prev;
                in.pressX = edge & PAD_CROSS; in.pressSquare = edge & PAD_SQUARE;
                in.pressTriangle = edge & PAD_TRIANGLE; in.pressCircle = edge & PAD_CIRCLE;
                prev = btn;
            }
        }
        game.update(dt, in);

        // --- dibujo ---
        gsKit_clear(gs, rgba(0xB4, 0xDC, 0xFF, 0x80));   // alfa 0x80 = opaco; con 0x00 la pantalla no se borra y quedan estelas
        drawTerrain(game);
        DrawItem items[16]; int n = game.drawList(items, 16);
        for (int i = 0; i < n; i++) {
            float sx, sy; worldToScreen(game, items[i].x, items[i].y, sx, sy);
            spriteOf(items[i].type, sx, sy, items[i].type == P_PIG ? 0.8f : 1.f, items[i].angle);
        }
        drawUI(game);
        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
