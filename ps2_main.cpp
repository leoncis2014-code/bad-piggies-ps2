// Capa de PlayStation 2: pantallas, dibujo (gsKit), mando (libpad) y sonido (audsrv).
// El juego en si vive en game.cpp.
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
#ifndef NO_AUDIO
extern "C" {
#include <audsrv.h>
extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;
}
#endif
#include "game.h"
extern "C" {
#include "sprites.h"
#include "font.h"
#include "sounds.h"
}

enum Screen { S_TITLE, S_SELECT, S_GAME };

static GSGLOBAL* gs;
static GSTEXTURE texPig, texBox, texWheel, texFlag, texFontS, texFontB;
static char padBuf[256] __attribute__((aligned(64)));
static float kx = 1.f, ky = 1.f;   // pantalla virtual 640x448 -> pantalla real

static u64 rgba(int r, int g, int b, int a) { return GS_SETREG_RGBAQ(r, g, b, a, 0x00); }

// ---------------------------------------------------------------- texturas
static void uploadTex(GSTEXTURE& t, const uint32_t* pix, int w, int h) {
    memset(&t, 0, sizeof(t));
    t.Width = w; t.Height = h; t.PSM = GS_PSM_CT32; t.Filter = GS_FILTER_LINEAR;
    t.Mem = (u32*)memalign(128, w * h * 4);
    memcpy(t.Mem, pix, w * h * 4);
    t.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(w, h, t.PSM), GSKIT_ALLOC_USERBUFFER);
    gsKit_texture_upload(gs, &t);
}
static void uploadFont(GSTEXTURE& t, const FontData& f) {      // atlas de transparencia -> blanco con alfa
    memset(&t, 0, sizeof(t));
    t.Width = f.tw; t.Height = f.th; t.PSM = GS_PSM_CT32; t.Filter = GS_FILTER_LINEAR;
    t.Mem = (u32*)memalign(128, f.tw * f.th * 4);
    for (int i = 0; i < f.tw * f.th; i++) t.Mem[i] = 0x00FFFFFFu | ((u32)f.a[i] << 24);
    t.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(f.tw, f.th, t.PSM), GSKIT_ALLOC_USERBUFFER);
    gsKit_texture_upload(gs, &t);
}

// ---------------------------------------------------------------- primitivas
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
// rectangulo texturado sin girar, con rectangulo de textura (u1,v1)-(u2,v2) en texels
static void quadUV(GSTEXTURE& t, float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, u64 col) {
    x1 *= kx; x2 *= kx; y1 *= ky; y2 *= ky;
    gsKit_prim_triangle_texture(gs, &t, x1, y1, u1, v1, x2, y1, u2, v1, x1, y2, u1, v2, 1, col);
    gsKit_prim_triangle_texture(gs, &t, x2, y1, u2, v1, x2, y2, u2, v2, x1, y2, u1, v2, 1, col);
}
// sprite centrado en (cx,cy), tamano w x h (pantalla virtual), girado ang radianes
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

// ---------------------------------------------------------------- texto (UTF-8 -> Latin-1)
static int glyphIndex(const FontData& f, int code) {
    for (int i = 0; i < f.nglyphs; i++) if (f.g[i].code == code) return i;
    return -1;
}
static int nextCode(const char*& s) {
    unsigned char c = (unsigned char)*s++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) { unsigned char d = (unsigned char)*s++; return ((c & 0x1F) << 6) | (d & 0x3F); }
    return '?';
}
static float textWidth(const FontData& f, const char* s, float scale) {
    float w = 0;
    while (*s) { int i = glyphIndex(f, nextCode(s)); if (i >= 0) w += f.g[i].adv * scale; }
    return w;
}
static void drawTextRaw(GSTEXTURE& tex, const FontData& f, float x, float y, const char* s, float scale, u64 col) {
    while (*s) {
        int i = glyphIndex(f, nextCode(s));
        if (i < 0) continue;
        const Glyph& g = f.g[i];
        if (g.w > 2)
            quadUV(tex, x + g.xoff * scale, y + g.yoff * scale, x + (g.xoff + g.w) * scale, y + (g.yoff + g.h) * scale,
                   (float)g.x, (float)g.y, (float)(g.x + g.w), (float)(g.y + g.h), col);
        x += g.adv * scale;
    }
}
// big=true usa la tipografia grande (solo mayusculas del titulo). cx = centro horizontal.
static void textCentered(bool big, float cx, float y, const char* s, float scale, int r, int g, int b) {
    GSTEXTURE& tex = big ? texFontB : texFontS; const FontData& f = big ? FONT_BIG : FONT_SMALL;
    float x = cx - textWidth(f, s, scale) * 0.5f;
    drawTextRaw(tex, f, x + 2.f * scale, y + 2.f * scale, s, scale, rgba(0, 0, 0, 0x60));   // sombra
    drawTextRaw(tex, f, x, y, s, scale, rgba(r, g, b, 0x80));
}
static void textLeft(float x, float y, const char* s, float scale, int r, int g, int b) {
    drawTextRaw(texFontS, FONT_SMALL, x + 1.5f * scale, y + 1.5f * scale, s, scale, rgba(0, 0, 0, 0x60));
    drawTextRaw(texFontS, FONT_SMALL, x, y, s, scale, rgba(r, g, b, 0x80));
}

// ---------------------------------------------------------------- sonido
#ifndef NO_AUDIO
static bool audioOK = false;
static const SoundData* curSnd = NULL;
static int sndPos = 0;
static void audioInit() {
    SifLoadModule("rom0:LIBSD", 0, NULL);
    int ret = 0;
    int id = SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &ret);
    if (id < 0 || ret == 1) return;            // el modulo no arranco: seguimos sin sonido
    if (audsrv_init() != 0) return;
    audsrv_fmt_t fmt; fmt.freq = 22050; fmt.bits = 16; fmt.channels = 1;
    if (audsrv_set_format(&fmt) != 0) return;
    audsrv_set_volume(MAX_VOLUME);
    audioOK = true;
}
static void sndPlay(int id) {
    if (!audioOK) return;
    audsrv_stop_audio();
    curSnd = &SOUNDS[id]; sndPos = 0;
}
static void sndPump() {                        // entrega el sonido en trozos, una vez por cuadro
    if (!audioOK || !curSnd) return;
    int avail = audsrv_available(), remain = (curSnd->samples - sndPos) * 2;
    int n = avail < remain ? avail : remain;
    if (n > 4096) n = 4096;
    n &= ~1;
    if (n > 0) { audsrv_play_audio((const char*)(curSnd->data + sndPos), n); sndPos += n / 2; }
    if (sndPos >= curSnd->samples) curSnd = NULL;
}
#else
static void audioInit() {}
static void sndPlay(int) {}
static void sndPump() {}
#endif

// ---------------------------------------------------------------- dibujo del juego
static void worldToScreen(const Game& g, float wx, float wy, float& sx, float& sy) {
    float cx, cy; g.camera(cx, cy);
    sx = (wx - cx) * ZOOM + SCREEN_W * 0.5f; sy = (wy - cy) * ZOOM + SCREEN_H * 0.5f;
}

static void drawTerrain(const Game& g) {
    const LevelData& L = g.levelData();
    u64 fill = rgba(0x5A, 0x78, 0x6E, 0x80), grass = rgba(0x4C, 0xA8, 0x32, 0x80);
    static float X[1024], Y[1024];
    int n = L.npts < 1024 ? L.npts : 1024;
    for (int i = 0; i < n; i++) worldToScreen(g, L.pts[i][0], L.pts[i][1], X[i], Y[i]);
    for (int i = 0; i < L.ntris; i++) {
        int a = L.tris[i][0], b = L.tris[i][1], c = L.tris[i][2];
        if (a < n && b < n && c < n) tri(X[a], Y[a], X[b], Y[b], X[c], Y[c], fill);
    }
    for (int r = 0; r < L.nrings; r++) {
        int a = L.ringStart[r], b = L.ringStart[r + 1];
        for (int i = a; i < b && i < n; i++) {
            if (!L.top[i]) continue;
            int j = (i + 1 < b) ? i + 1 : a;
            for (int k = 0; k < 4; k++)   // linea gruesa de hierba
                gsKit_prim_line(gs, X[i] * kx, (Y[i] + k) * ky, X[j] * kx, (Y[j] + k) * ky, 2, grass);
        }
    }
}

static void drawGameUI(const Game& g) {
    char buf[64];
    if (g.mode() == M_BUILD) {
        u64 cellc = rgba(0xFF, 0xFF, 0xFF, 0x24), cellb = rgba(0xFF, 0xFF, 0xFF, 0x60);
        for (int r = 0; r < 2; r++) for (int c = 0; c < 3; c++) {
            float sx, sy; g.cellScreen(c, r, sx, sy);
            rect(sx - 31, sy - 31, sx + 31, sy + 31, cellc);
            frameRect(sx - 31, sy - 31, sx + 31, sy + 31, 1.5f, cellb);
        }
        for (int i = 0; i < P_COUNT; i++) {   // bandeja: cerdito, caja, rueda; puntos = las que quedan
            float sx, sy; g.traySlot(i, sx, sy);
            rect(sx - 30, sy - 30, sx + 30, sy + 30, rgba(0x10, 0x20, 0x30, 0x50));
            spriteOf(i, sx, sy - 4, 0.8f, 0.f);
            for (int k = 0; k < g.remaining(i); k++) rect(sx - 24 + k * 10, sy + 20, sx - 18 + k * 10, sy + 26, rgba(0xFF, 0xFF, 0xFF, 0x80));
            if (g.selected() == i) frameRect(sx - 30, sy - 30, sx + 30, sy + 30, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
        }
        textCentered(false, 320, 412, "X: poner   Cuadrado: quitar   Triángulo: iniciar   START: menú", 0.62f, 255, 255, 255);
        float cx, cy; g.cursor(cx, cy);
        tri(cx, cy, cx + 14, cy + 6, cx + 6, cy + 14, rgba(0xFF, 0xFF, 0xFF, 0x80));
        tri(cx + 1, cy + 1, cx + 11, cy + 6, cx + 6, cy + 11, rgba(0x20, 0x20, 0x20, 0x80));
    } else if (g.mode() == M_PLAY) {
        if (g.time() < 5.f) textCentered(false, 320, 408, "Stick derecho: mover la cámara", 0.7f, 255, 255, 255);
    } else if (g.mode() == M_WIN || g.mode() == M_LOSE) {
        bool win = g.mode() == M_WIN;
        rect(110, 110, 530, 330, rgba(0x10, 0x20, 0x30, 0x60));
        frameRect(110, 110, 530, 330, 3.f, win ? rgba(0x40, 0xD0, 0x40, 0x80) : rgba(0xD0, 0x40, 0x40, 0x80));
        textCentered(false, 320, 128, win ? "¡Nivel completado!" : "¡Perdiste!", 1.45f, win ? 140 : 255, win ? 255 : 140, 140);
        if (win) {
            textCentered(false, 320, 205, "X: siguiente nivel", 1.1f, 255, 255, 255);
            textCentered(false, 320, 245, "Círculo: repetir este nivel", 1.1f, 255, 255, 255);
            textCentered(false, 320, 285, "Triángulo: elegir nivel", 1.1f, 255, 255, 255);
        } else {
            textCentered(false, 320, 215, "Círculo: reintentar", 1.1f, 255, 255, 255);
            textCentered(false, 320, 260, "Triángulo: elegir nivel", 1.1f, 255, 255, 255);
        }
    }
    snprintf(buf, sizeof(buf), "Nivel %d", g.levelIndex() + 1);
    textLeft(30, 22, buf, 0.9f, 255, 255, 255);
}

static void drawVehicleDemo(float cx, float cy, float scale, float bob) {
    float off = 62.f * scale * 0.75f + 24.f * scale;       // las ruedas quedan a los costados de la caja
    spriteOf(P_WHEEL, cx - off, cy + 22.f * scale + bob, scale * 1.5f, bob * 0.08f);
    spriteOf(P_WHEEL, cx + off, cy + 22.f * scale + bob, scale * 1.5f, bob * 0.08f);
    spriteOf(P_BOX, cx, cy + bob, scale * 1.5f, 0.f);
    spriteOf(P_PIG, cx, cy + bob, scale * 1.2f, 0.f);
}

// ---------------------------------------------------------------- principal
struct Pad { unsigned btn = 0, edge = 0; float ax = 0, ay = 0, lx = 0, ly = 0; bool dirL = false, dirR = false, dirU = false, dirD = false; bool eL = false, eR = false, eU = false, eD = false; };

int main(int, char**) {
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, padBuf);
    audioInit();

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

    uploadTex(texPig, SPR_PIG_PIX, SPR_PIG_W, SPR_PIG_H);
    uploadTex(texBox, SPR_BOX_PIX, SPR_BOX_W, SPR_BOX_H);
    uploadTex(texWheel, SPR_WHEEL_PIX, SPR_WHEEL_W, SPR_WHEEL_H);
    uploadTex(texFlag, SPR_FLAG_PIX, SPR_FLAG_W, SPR_FLAG_H);
    uploadFont(texFontS, FONT_SMALL);
    uploadFont(texFontB, FONT_BIG);

    Game game;
    Screen screen = S_TITLE;
    int sel = 0;                 // nivel elegido en el selector (0..14)
    float msgTimer = 0.f;        // aviso "aguarde a la siguiente actualizacion"
    float clock = 0.f;
    Pad pad;
    for (;;) {
        // ---------------- entrada ----------------
        Pad np; np.btn = pad.btn;
        int st = padGetState(0, 0);
        if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) {
            struct padButtonStatus b;
            if (padRead(0, 0, &b) != 0) {
                unsigned btn = 0xffff ^ b.btns;
                float ax = ((int)b.ljoy_h - 128) / 128.f, ay = ((int)b.ljoy_v - 128) / 128.f;
                float lx = ((int)b.rjoy_h - 128) / 128.f, ly = ((int)b.rjoy_v - 128) / 128.f;
                if (fabsf(ax) < 0.2f) { ax = 0; }
                if (fabsf(ay) < 0.2f) { ay = 0; }
                if (fabsf(lx) < 0.25f) { lx = 0; }
                if (fabsf(ly) < 0.25f) { ly = 0; }
                if (btn & PAD_LEFT) { ax = -1; }
                if (btn & PAD_RIGHT) { ax = 1; }
                if (btn & PAD_UP) { ay = -1; }
                if (btn & PAD_DOWN) { ay = 1; }
                np.btn = btn; np.edge = btn & ~pad.btn; np.ax = ax; np.ay = ay; np.lx = lx; np.ly = ly;
                np.dirL = ax < -0.6f; np.dirR = ax > 0.6f; np.dirU = ay < -0.6f; np.dirD = ay > 0.6f;
                np.eL = np.dirL && !pad.dirL; np.eR = np.dirR && !pad.dirR; np.eU = np.dirU && !pad.dirU; np.eD = np.dirD && !pad.dirD;
            }
        }
        pad = np;
        clock += dt;
        if (msgTimer > 0) msgTimer -= dt;
        bool pX = pad.edge & PAD_CROSS, pO = pad.edge & PAD_CIRCLE, pT = pad.edge & PAD_TRIANGLE, pS = pad.edge & PAD_SQUARE, pStart = pad.edge & PAD_START;

        // ---------------- logica por pantalla ----------------
        if (screen == S_TITLE) {
            if (pX || pStart) { sndPlay(SND_CLICK); screen = S_SELECT; }
        } else if (screen == S_SELECT) {
            int col = sel % 5, row = sel / 5;
            bool moved = false;
            if (pad.eL && col > 0) { sel--; moved = true; }
            if (pad.eR && col < 4) { sel++; moved = true; }
            if (pad.eU && row > 0) { sel -= 5; moved = true; }
            if (pad.eD && row < 2) { sel += 5; moved = true; }
            if (moved) sndPlay(SND_CLICK);
            if (pO) { sndPlay(SND_CLICK); screen = S_TITLE; }
            if (pX) {
                if (game.loadLevel(sel)) { sndPlay(SND_CLICK); screen = S_GAME; }
                else { msgTimer = 3.0f; sndPlay(SND_LOSE); }
            }
        } else {   // S_GAME
            bool handled = false;
            if (pStart || (pT && (game.mode() == M_WIN || game.mode() == M_LOSE))) { sel = game.levelIndex(); screen = S_SELECT; handled = true; }
            else if (game.mode() == M_WIN && pX) {
                int next = game.levelIndex() + 1;
                if (game.loadLevel(next)) { sndPlay(SND_CLICK); }
                else { sel = next < 15 ? next : 14; screen = S_SELECT; msgTimer = 3.0f; }
                handled = true;
            }
            if (!handled) {
                GameInput in;
                in.stickX = pad.ax; in.stickY = pad.ay; in.lookX = pad.lx; in.lookY = pad.ly;
                in.pressX = pX; in.pressSquare = pS; in.pressTriangle = pT; in.pressCircle = pO;
                game.update(dt, in);
                unsigned ev = game.takeEvents();
                if (ev & EV_WIN) sndPlay(SND_WIN);
                else if (ev & EV_LOSE) sndPlay(SND_LOSE);
                else if (ev & EV_GO) sndPlay(SND_GO);
                else if (ev & EV_PLACE) sndPlay(SND_PLACE);
                else if (ev & EV_CLICK) sndPlay(SND_CLICK);
            }
        }
        sndPump();

        // ---------------- dibujo ----------------
        gsKit_clear(gs, rgba(0xB4, 0xDC, 0xFF, 0x80));   // alfa 0x80 = opaco (con 0x00 no se borra la pantalla)
        if (screen == S_TITLE) {
            rect(0, 300, SCREEN_W, SCREEN_H, rgba(0x4C, 0xA8, 0x32, 0x80));
            rect(0, 300, SCREEN_W, 312, rgba(0x6C, 0xC8, 0x3A, 0x80));
            rect(0, 400, SCREEN_W, SCREEN_H, rgba(0x3A, 0x8A, 0x28, 0x80));
            textCentered(true, 320, 48, "BAD PIGGIES", 1.25f, 130, 220, 60);
            drawVehicleDemo(320, 228, 1.6f, sinf(clock * 2.f) * 4.f);
            if (((int)(clock * 2.f)) % 2 == 0) textCentered(false, 320, 338, "Pulsá X para jugar", 1.5f, 255, 255, 255);
            textCentered(false, 320, 414, "Port no oficial - versión de prueba", 0.7f, 255, 255, 255);
        } else if (screen == S_SELECT) {
            rect(0, 330, SCREEN_W, SCREEN_H, rgba(0x4C, 0xA8, 0x32, 0x80));
            textCentered(false, 320, 22, "Elegí un nivel", 1.5f, 255, 255, 255);
            for (int i = 0; i < 15; i++) {
                int col = i % 5, row = i / 5;
                float x1 = 62 + col * 108, y1 = 70 + row * 104, x2 = x1 + 84, y2 = y1 + 84;
                bool ok = i < Game::levelCount();
                rect(x1, y1, x2, y2, ok ? rgba(0x6C, 0xBF, 0x3A, 0x80) : rgba(0x3E, 0x6A, 0x3A, 0x80));
                frameRect(x1, y1, x2, y2, 3.f, ok ? rgba(0x3A, 0x7A, 0x22, 0x80) : rgba(0x2A, 0x4A, 0x28, 0x80));
                char n[8]; snprintf(n, sizeof(n), "%d", i + 1);
                textCentered(false, (x1 + x2) * 0.5f, y1 + 20, n, 1.9f, ok ? 255 : 170, ok ? 255 : 190, ok ? 255 : 170);
                if (i == sel) {
                    float pulse = 3.f + 2.f * sinf(clock * 6.f);
                    frameRect(x1 - pulse, y1 - pulse, x2 + pulse, y2 + pulse, 4.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                }
            }
            textCentered(false, 320, 392, "X: jugar     Círculo: volver", 0.9f, 255, 255, 255);
            if (msgTimer > 0) {
                rect(40, 170, 600, 250, rgba(0x10, 0x20, 0x30, 0x70));
                frameRect(40, 170, 600, 250, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                textCentered(false, 320, 190, "Aguarde a la sig. actualización", 1.15f, 255, 240, 120);
            }
        } else {
            drawTerrain(game);
            {   // bandera de meta (cuadros) parada en el suelo
                const LevelData& L = game.levelData();
                float fx, fy; worldToScreen(game, L.goalX, L.goalY - 57.f, fx, fy);
                sprite(texFlag, fx, fy, texFlag.Width * ZOOM, texFlag.Height * ZOOM, 0.f);
            }
            DrawItem items[16]; int n = game.drawList(items, 16);
            for (int i = 0; i < n; i++) {
                float sx, sy; worldToScreen(game, items[i].x, items[i].y, sx, sy);
                spriteOf(items[i].type, sx, sy, items[i].type == P_PIG ? 0.8f : 1.f, items[i].angle);
            }
            drawGameUI(game);
        }
        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
