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
#include "decor.h"
#include "screens.h"
extern "C" {
extern unsigned char decor_raw[]; extern unsigned int size_decor_raw;
extern unsigned char terrain1_raw[]; extern unsigned int size_terrain1_raw;
extern unsigned char screen_title[]; extern unsigned int size_screen_title;
extern unsigned char screen_worlds[]; extern unsigned int size_screen_worlds;
extern unsigned char screen_levels[]; extern unsigned int size_screen_levels;
extern unsigned char music_main[]; extern unsigned int size_music_main;
extern unsigned char music_hai[]; extern unsigned int size_music_hai;
extern unsigned char music_final[]; extern unsigned int size_music_final;
}
extern "C" {
#include "sprites.h"
#include "font.h"
#include "sounds.h"
}

enum Screen { S_TITLE, S_WORLDS, S_SELECT, S_GAME, S_OPTIONS };

// opciones (no se guardan al apagar la consola)
static bool optCursor = false, optMusic = true, optSfx = true;

// zonas de los menus, medidas sobre las imagenes originales (pantalla virtual 640x448)
static const float MENU_PX[4] = { 87, 213, 339, 464 };                       // paneles de mundos: x (ancho 102, y 64..352)
static const float MENU_TX[5] = { 148, 233, 319, 405, 490 }, MENU_TY[3] = { 65, 160, 252 };   // casillas de niveles (y + 24)
static const float OPT_ROWY[4] = { 138, 188, 238, 300 };                     // filas de opciones


static GSGLOBAL* gs;
static GSTEXTURE texPig, texBox, texWheel, texFlag, texFontS, texScreen;
static const float FONT_K = 24.f / 22.f;   // el atlas es de 22 px; esto mantiene el tamano de antes
static int screenLoaded = -1;
static GSTEXTURE texDecor[DEC_COUNT];
static GSTEXTURE texTerrain[1];                       // una por nivel con arte (por ahora solo el nivel 1)
static const unsigned char* const TERRAIN_RAW[1] = { terrain1_raw };
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
// cx = centro horizontal; (el parametro "big" ya no se usa: la tipografia grande se reemplazo por las pantallas originales)
static void textCentered(bool, float cx, float y, const char* s, float scale, int r, int g, int b) {
    scale *= FONT_K;
    float x = cx - textWidth(FONT_SMALL, s, scale) * 0.5f;
    drawTextRaw(texFontS, FONT_SMALL, x + 2.f * scale, y + 2.f * scale, s, scale, rgba(0, 0, 0, 0x60));   // sombra
    drawTextRaw(texFontS, FONT_SMALL, x, y, s, scale, rgba(r, g, b, 0x80));
}
static void textLeft(float x, float y, const char* s, float scale, int r, int g, int b) {
    scale *= FONT_K;
    drawTextRaw(texFontS, FONT_SMALL, x + 1.5f * scale, y + 1.5f * scale, s, scale, rgba(0, 0, 0, 0x60));
    drawTextRaw(texFontS, FONT_SMALL, x, y, s, scale, rgba(r, g, b, 0x80));
}

// ---------------------------------------------------------------- sonido
#ifndef NO_AUDIO
// Mezclador continuo: la musica (11025 Hz) y un efecto (22050 Hz) se suman y se envian sin cortes
// en el formato nativo de la consola: 48000 Hz, 16 bits, estereo (asi el modulo de audio no convierte nada).
enum { MUS_NONE = -1, MUS_MAIN = 0, MUS_HAI = 1, MUS_FINAL = 2, MUS_COUNT = 3 };
struct Track { short* d; int n; };
static bool audioOK = false;
static Track mus[MUS_COUNT];
static int musCur = MUS_NONE, musWant = MUS_NONE;
static float musPos = 0.f, musFade = 0.f;
static float fxPosF = 0.f;
static const SoundData* fx = NULL;
static int idleAvail = 0;                      // capacidad del buffer de audio
static int targetFill = 12288;                 // bytes que mantenemos en el buffer (~60 ms)
static const float MUS_STEP = 11025.f / 48000.f, FX_STEP = 22050.f / 48000.f;
static const float MUSIC_VOL = 0.60f, FX_VOL = 0.90f;

static void loadTrack(Track& t, const unsigned char* src, unsigned bytes) {
    t.n = (int)(bytes / 2); t.d = (short*)memalign(64, bytes); memcpy(t.d, src, bytes);   // copia alineada: la MIPS no admite lecturas de 16 bits desalineadas
}
static void audioInit() {
    SifLoadModule("rom0:LIBSD", 0, NULL);
    int ret = 0;
    int id = SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &ret);
    if (id < 0 || ret == 1) return;            // el modulo no arranco: seguimos sin sonido
    if (audsrv_init() != 0) return;
    audsrv_fmt_t fmt; fmt.freq = 48000; fmt.bits = 16; fmt.channels = 2;
    if (audsrv_set_format(&fmt) != 0) return;
    audsrv_set_volume(MAX_VOLUME);
    idleAvail = audsrv_available();
    targetFill = idleAvail * 3 / 4; if (targetFill > 23040) targetFill = 23040; targetFill &= ~3;
    loadTrack(mus[MUS_MAIN], music_main, size_music_main);
    loadTrack(mus[MUS_HAI], music_hai, size_music_hai);
    loadTrack(mus[MUS_FINAL], music_final, size_music_final);
    audioOK = true;
}
static void sndPlay(int id) { if (audioOK && optSfx) { fx = &SOUNDS[id]; fxPosF = 0.f; } }
static void musicWant(int m) { musWant = optMusic ? m : MUS_NONE; }
static void mixChunk(short* out, int n) {     // n = cuadros estereo
    for (int i = 0; i < n; i++) {
        float v = 0.f;
        if (musCur != MUS_NONE) {
            const Track& t = mus[musCur];
            int i0 = (int)musPos, i1 = i0 + 1; if (i1 >= t.n) i1 = 0;
            float fr = musPos - (float)i0;
            v += ((float)t.d[i0] * (1.f - fr) + (float)t.d[i1] * fr) * MUSIC_VOL * musFade;
            musPos += MUS_STEP; if (musPos >= (float)t.n) musPos -= (float)t.n;
            if (musFade < 1.f) { musFade += 1.f / (48000.f * 0.25f); if (musFade > 1.f) musFade = 1.f; }
        }
        if (fx) {
            int i0 = (int)fxPosF, i1 = i0 + 1; if (i1 >= fx->samples) i1 = i0;
            float fr = fxPosF - (float)i0;
            v += ((float)fx->data[i0] * (1.f - fr) + (float)fx->data[i1] * fr) * FX_VOL;
            fxPosF += FX_STEP; if (fxPosF >= (float)fx->samples) fx = NULL;
        }
        if (v > 32767.f) { v = 32767.f; }
        if (v < -32768.f) { v = -32768.f; }
        out[2 * i] = (short)v; out[2 * i + 1] = (short)v;
    }
}
static void sndPump() {
    if (!audioOK) return;
    if (musWant != musCur) { musCur = musWant; musPos = 0.f; musFade = 0.f; }   // cambio de cancion con entrada suave
    int fill = idleAvail - audsrv_available();
    int want = targetFill - fill;
    if (want < 256) return;
    if (want > 8192) want = 8192;
    static short buf[2 * 2048];
    int n = want / 4; if (n > 2048) n = 2048;
    mixChunk(buf, n);                          // siempre se envia algo (silencio si no hay nada): asi el buffer nunca se repite
    audsrv_play_audio((const char*)buf, n * 4);
}
#else
static void audioInit() {}
static void sndPlay(int) {}
static void musicWant(int) {}
static void sndPump() {}
enum { MUS_NONE = -1, MUS_MAIN = 0, MUS_HAI = 1, MUS_FINAL = 2 };
#endif

static void ring(float cx, float cy, float r, float th, u64 col) {
    const int N = 28;
    for (int i = 0; i < N; i++) {
        float a0 = i * 6.2831853f / N, a1 = (i + 1) * 6.2831853f / N;
        float x0 = cx + cosf(a0) * r, y0 = cy + sinf(a0) * r, x1 = cx + cosf(a1) * r, y1 = cy + sinf(a1) * r;
        float u0 = cx + cosf(a0) * (r - th), v0 = cy + sinf(a0) * (r - th), u1 = cx + cosf(a1) * (r - th), v1 = cy + sinf(a1) * (r - th);
        tri(x0, y0, x1, y1, u1, v1, col); tri(x0, y0, u1, v1, u0, v0, col);
    }
}
// que elemento de un menu hay bajo el cursor (-1 = ninguno)
static int hitTest(Screen sc, float x, float y) {
    if (sc == S_TITLE) {
        float dx = x - 335.f, dy = y - 234.f;
        if (dx * dx + dy * dy < 56.f * 56.f) return 0;
        if (x >= 400 && x <= 560 && y >= 271 && y <= 311) return 1;
    } else if (sc == S_WORLDS) {
        for (int i = 0; i < 4; i++) if (x >= MENU_PX[i] && x <= MENU_PX[i] + 102 && y >= 64 && y <= 352) return i;
    } else if (sc == S_SELECT) {
        for (int i = 0; i < 15; i++) { float cx = MENU_TX[i % 5], cy = 24 + MENU_TY[i / 5]; if (fabsf(x - cx) <= 36 && y >= cy - 36 && y <= cy + 52) return i; }
    } else if (sc == S_OPTIONS) {
        for (int r = 0; r < 4; r++) if (x >= 110 && x <= 530 && y >= OPT_ROWY[r] - 8 && y <= OPT_ROWY[r] + 36) return r;
    }
    return -1;
}

// ---------------------------------------------------------------- pantallas originales
static void screenInit() {
    memset(&texScreen, 0, sizeof(texScreen));
    texScreen.Width = SCR_W; texScreen.Height = SCR_H; texScreen.PSM = GS_PSM_CT16; texScreen.Filter = GS_FILTER_LINEAR;
    texScreen.Mem = (u32*)memalign(128, SCR_W * SCR_H * 2);
    texScreen.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(SCR_W, SCR_H, GS_PSM_CT16), GSKIT_ALLOC_USERBUFFER);
}
static void screenLoad(int idx) {            // sube la imagen pedida a la memoria de video (se hace al cambiar de pantalla)
    if (idx == screenLoaded) return;
    const unsigned char* src = idx == SCR_TITLE ? screen_title : (idx == SCR_WORLDS ? screen_worlds : screen_levels);
    memcpy(texScreen.Mem, src, SCR_W * SCR_H * 2);
    FlushCache(0);
    gsKit_texture_upload(gs, &texScreen);
    screenLoaded = idx;
}
static void screenDraw(int idx) {            // la imagen es 640x400: va centrada, con franjas del color del borde arriba y abajo
    const int* c = SCR_BARS[idx];
    rect(0, 0, SCREEN_W, 24, rgba(c[0], c[1], c[2], 0x80));
    rect(0, 424, SCREEN_W, SCREEN_H, rgba(c[3], c[4], c[5], 0x80));
    gs->PrimAlphaEnable = GS_SETTING_OFF;      // imagen opaca: sin mezcla
    quadUV(texScreen, 0, 24, 640, 424, 0, 0, (float)SCR_W, (float)SCR_H, rgba(0x80, 0x80, 0x80, 0x80));
    gs->PrimAlphaEnable = GS_SETTING_ON;
}

// ---------------------------------------------------------------- dibujo del juego
static void worldToScreen(const Game& g, float wx, float wy, float& sx, float& sy) {
    float cx, cy; g.camera(cx, cy);
    sx = (wx - cx) * ZOOM + SCREEN_W * 0.5f; sy = (wy - cy) * ZOOM + SCREEN_H * 0.5f;
}

static void drawDecor(const Game& g, int layer) {
    const LevelData& L = g.levelData();
    for (int i = 0; i < L.ndecor; i++) {
        const DecorItem& d = L.decor[i];
        if (d.layer != layer) continue;
        float sx, sy; worldToScreen(g, d.x, d.y, sx, sy);
        GSTEXTURE& t = texDecor[d.type];
        float w = t.Width * d.scale, h = t.Height * d.scale, hh = h * 0.5f;
        if (sx < -w - 40 || sx > SCREEN_W + w + 40) continue;     // fuera de pantalla
        sprite(t, sx + hh * sinf(d.angle), sy - hh * cosf(d.angle), w, h, d.angle);   // la base queda apoyada en (x,y)
    }
}

static void drawTerrain(const Game& g) {
    const LevelData& L = g.levelData();
    bool textured = L.texW > 0;
    GSTEXTURE* tt = textured ? &texTerrain[g.levelIndex()] : NULL;
    u64 white = rgba(0x80, 0x80, 0x80, 0x80), flat = rgba(0x3A, 0x66, 0x66, 0x80);
    static float X[1024], Y[1024], U[1024], V[1024];
    int n = L.npts < 1024 ? L.npts : 1024;
    for (int i = 0; i < n; i++) {
        worldToScreen(g, L.pts[i][0], L.pts[i][1], X[i], Y[i]);
        if (textured) {
            U[i] = (L.pts[i][0] - L.texOX) * L.texScale; V[i] = (L.pts[i][1] - L.texOY) * L.texScale;
            if (U[i] < 0.5f) { U[i] = 0.5f; }
            if (U[i] > L.texW - 0.5f) { U[i] = L.texW - 0.5f; }
            if (V[i] < 0.5f) { V[i] = 0.5f; }
            if (V[i] > L.texH - 0.5f) { V[i] = L.texH - 0.5f; }
        }
    }
    for (int i = 0; i < L.ntris; i++) {
        int a = L.tris[i][0], b = L.tris[i][1], c = L.tris[i][2];
        if (a >= n || b >= n || c >= n) continue;
        float mnx = fminf(X[a], fminf(X[b], X[c])), mxx = fmaxf(X[a], fmaxf(X[b], X[c]));
        if (mxx < -60 || mnx > SCREEN_W + 60) continue;                         // fuera de pantalla
        if (textured)
            gsKit_prim_triangle_texture(gs, tt, X[a] * kx, Y[a] * ky, U[a], V[a], X[b] * kx, Y[b] * ky, U[b], V[b], X[c] * kx, Y[c] * ky, U[c], V[c], 1, white);
        else
            tri(X[a], Y[a], X[b], Y[b], X[c], Y[c], flat);
    }
    // franja de hierba sobre el borde superior: banda clara arriba, oscura abajo
    u64 lightG = rgba(0x8C, 0xD2, 0x3E, 0x80), darkG = rgba(0x4A, 0x96, 0x22, 0x80);
    for (int r = 0; r < L.nrings; r++) {
        int a = L.ringStart[r], b = L.ringStart[r + 1];
        for (int i = a; i < b && i < n; i++) {
            if (!L.top[i]) continue;
            int j = (i + 1 < b) ? i + 1 : a;
            if ((X[i] < -60 && X[j] < -60) || (X[i] > SCREEN_W + 60 && X[j] > SCREEN_W + 60)) continue;
            float dx = X[j] - X[i], dy = Y[j] - Y[i], ln = sqrtf(dx * dx + dy * dy);
            if (ln < 0.01f) continue;
            float nx = dy / ln, ny = -dx / ln;                          // normal hacia el aire
            const float O = 5.f, M = -1.f, I = -9.f;
            float ox1 = X[i] + nx * O, oy1 = Y[i] + ny * O, ox2 = X[j] + nx * O, oy2 = Y[j] + ny * O;
            float mx1 = X[i] + nx * M, my1 = Y[i] + ny * M, mx2 = X[j] + nx * M, my2 = Y[j] + ny * M;
            float ix1 = X[i] + nx * I, iy1 = Y[i] + ny * I, ix2 = X[j] + nx * I, iy2 = Y[j] + ny * I;
            tri(ox1, oy1, ox2, oy2, mx2, my2, lightG); tri(ox1, oy1, mx2, my2, mx1, my1, lightG);
            tri(mx1, my1, mx2, my2, ix2, iy2, darkG);  tri(mx1, my1, ix2, iy2, ix1, iy1, darkG);
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
            if (g.remaining(i) > 6) textCentered(false, sx, sy + 10, "inf", 0.7f, 255, 255, 255);   // piezas ilimitadas (sandbox)
            else for (int k = 0; k < g.remaining(i); k++) rect(sx - 24 + k * 10, sy + 20, sx - 18 + k * 10, sy + 26, rgba(0xFF, 0xFF, 0xFF, 0x80));
            if (g.selected() == i) frameRect(sx - 30, sy - 30, sx + 30, sy + 30, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
        }
        textCentered(false, 320, 412, "X: poner   Cuadrado: quitar   Triángulo: iniciar   START: menú", 0.62f, 255, 255, 255);
        float cx, cy; g.cursor(cx, cy);
        tri(cx, cy, cx + 14, cy + 6, cx + 6, cy + 14, rgba(0xFF, 0xFF, 0xFF, 0x80));
        tri(cx + 1, cy + 1, cx + 11, cy + 6, cx + 6, cy + 11, rgba(0x20, 0x20, 0x20, 0x80));
    } else if (g.mode() == M_PLAY) {
        if (g.isSandbox()) textCentered(false, 320, 408, "Círculo: volver a construir     Stick derecho: cámara", 0.7f, 255, 255, 255);
        else if (g.time() < 5.f) textCentered(false, 320, 408, "Stick derecho: mover la cámara", 0.7f, 255, 255, 255);
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
    if (g.isSandbox()) snprintf(buf, sizeof(buf), "Sandbox");
    else snprintf(buf, sizeof(buf), "Nivel %d", g.levelIndex() + 1);
    textLeft(30, 22, buf, 0.9f, 255, 255, 255);
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
    for (int i = 0; i < DEC_COUNT; i++) uploadTex(texDecor[i], (const uint32_t*)(decor_raw + DECOR_TEX[i].offset), DECOR_TEX[i].w, DECOR_TEX[i].h);
    Game game;
    for (int i = 0; i < Game::levelCount() && i < 1; i++) {
        game.loadLevel(i);
        const LevelData& LD = game.levelData();
        uploadTex(texTerrain[i], (const uint32_t*)TERRAIN_RAW[i], LD.texW, LD.texH);
    }
    game.loadLevel(0);
    uploadFont(texFontS, FONT_SMALL);
    screenInit();

    Screen screen = S_TITLE;
    int sel = 0;                 // nivel elegido en el selector (0..14)
    int wsel = 0;                // mundo elegido (0..3)
    int titleSel = 0;            // 0 = jugar, 1 = opciones
    int optSel = 0;              // fila elegida en opciones
    float mcx = 320.f, mcy = 224.f;   // cursor de los menus (opcion)
    float msgTimer = 0.f;        // aviso "aguarde a la siguiente actualizacion"
    bool keepMsg = false;        // conservar el aviso al cambiar de pantalla
    Screen prevScreen = S_TITLE;
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
        bool useCur = optCursor && screen != S_GAME;       // cursor libre en los menus (opcion)
        int hover = -1;
        if (useCur) {
            mcx += pad.ax * 320.f * dt; mcy += pad.ay * 320.f * dt;
            if (mcx < 0) { mcx = 0; }
            if (mcx > SCREEN_W) { mcx = SCREEN_W; }
            if (mcy < 0) { mcy = 0; }
            if (mcy > SCREEN_H) { mcy = SCREEN_H; }
            hover = hitTest(screen, mcx, mcy);
            if (hover >= 0) {
                if (screen == S_TITLE) titleSel = hover; else if (screen == S_WORLDS) wsel = hover;
                else if (screen == S_SELECT) sel = hover; else if (screen == S_OPTIONS) optSel = hover;
            }
        }
        bool nav = !useCur;                                 // sin cursor: se navega con las direcciones
        bool act = pX && (!useCur || hover >= 0);           // con cursor: X actua sobre lo que esta debajo
        if (screen == S_TITLE) {
            if (nav) { if (pad.eR || pad.eD) titleSel = 1; if (pad.eL || pad.eU) titleSel = 0; }
            if (pStart) { sndPlay(SND_CLICK); screen = S_WORLDS; }
            else if (act) { sndPlay(SND_CLICK); if (titleSel == 0) screen = S_WORLDS; else { screen = S_OPTIONS; optSel = 0; } }
        } else if (screen == S_OPTIONS) {
            if (nav) {
                if (pad.eU && optSel > 0) { optSel--; sndPlay(SND_CLICK); }
                if (pad.eD && optSel < 3) { optSel++; sndPlay(SND_CLICK); }
            }
            if (pO) { sndPlay(SND_CLICK); screen = S_TITLE; }
            else if (act) {
                if (optSel == 0) optCursor = !optCursor;
                else if (optSel == 1) optMusic = !optMusic;
                else if (optSel == 2) optSfx = !optSfx;
                else screen = S_TITLE;
                sndPlay(SND_CLICK);
            }
        } else if (screen == S_WORLDS) {
            if (nav) {
                if (pad.eL && wsel > 0) { wsel--; sndPlay(SND_CLICK); }
                if (pad.eR && wsel < 3) { wsel++; sndPlay(SND_CLICK); }
            }
            if (pO) { sndPlay(SND_CLICK); screen = S_TITLE; }
            else if (act) {
                if (wsel == 0) { sndPlay(SND_CLICK); screen = S_SELECT; }
                else if (wsel == 3) { if (game.loadLevel(Game::sandboxIndex())) { sndPlay(SND_CLICK); screen = S_GAME; } }
                else { msgTimer = 3.0f; sndPlay(SND_LOSE); }
            }
        } else if (screen == S_SELECT) {
            if (nav) {
                int col = sel % 5, row = sel / 5;
                bool moved = false;
                if (pad.eL && col > 0) { sel--; moved = true; }
                if (pad.eR && col < 4) { sel++; moved = true; }
                if (pad.eU && row > 0) { sel -= 5; moved = true; }
                if (pad.eD && row < 2) { sel += 5; moved = true; }
                if (moved) sndPlay(SND_CLICK);
            }
            if (pO) { sndPlay(SND_CLICK); screen = S_WORLDS; }
            else if (act) {
                if (game.loadLevel(sel)) { sndPlay(SND_CLICK); screen = S_GAME; }
                else { msgTimer = 3.0f; sndPlay(SND_LOSE); }
            }
        } else {   // S_GAME
            bool handled = false;
            if (pStart || (pT && (game.mode() == M_WIN || game.mode() == M_LOSE))) { if (game.isSandbox()) screen = S_WORLDS; else { sel = game.levelIndex(); screen = S_SELECT; } handled = true; }
            else if (game.mode() == M_WIN && pX) {
                int next = game.levelIndex() + 1;
                if (game.loadLevel(next)) { sndPlay(SND_CLICK); }
                else { sel = next < 15 ? next : 14; screen = S_SELECT; msgTimer = 3.0f; keepMsg = true; }
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
        if (screen != prevScreen) { if (!keepMsg) msgTimer = 0.f; keepMsg = false; prevScreen = screen; }
        // musica segun la pantalla (como el juego original): menus = main, nivel = hai, victoria = final
        if (screen != S_GAME) musicWant(MUS_MAIN);
        else musicWant(game.mode() == M_WIN ? MUS_FINAL : MUS_HAI);
        sndPump();

        // ---------------- dibujo ----------------
        if (screen == S_TITLE || screen == S_OPTIONS) screenLoad(SCR_TITLE);
        else if (screen == S_WORLDS) screenLoad(SCR_WORLDS);
        else if (screen == S_SELECT) screenLoad(SCR_LEVELS);
        gsKit_clear(gs, rgba(0xB4, 0xDC, 0xFF, 0x80));   // alfa 0x80 = opaco (con 0x00 no se borra la pantalla)
        if (screen == S_TITLE) {
            screenDraw(SCR_TITLE);                                           // pantalla de inicio original
            rect(400, 271, 560, 311, rgba(0x6C, 0xBF, 0x3A, 0x80));          // boton "Opciones" (el original no lo tiene)
            frameRect(400, 271, 560, 311, 3.f, rgba(0x3A, 0x7A, 0x22, 0x80));
            textCentered(false, 480, 275, "Opciones", 1.0f, 255, 255, 255);
            {
                float pulse = 2.f + 2.f * sinf(clock * 6.f);
                if (titleSel == 0) ring(335, 234, 58.f + pulse, 4.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                else frameRect(400 - pulse, 271 - pulse, 560 + pulse, 311 + pulse, 4.f, rgba(0xFF, 0xE0, 0x20, 0x80));
            }
            if (((int)(clock * 2.f)) % 2 == 0) textCentered(false, 320, 330, titleSel == 0 ? "Pulsá X para jugar" : "Pulsá X para abrir las opciones", 1.2f, 255, 255, 255);
        } else if (screen == S_WORLDS) {
            screenDraw(SCR_WORLDS);                                          // selector de mundos original
            for (int i = 1; i < 3; i++) rect(MENU_PX[i], 24 + 40, MENU_PX[i] + 102, 24 + 328, rgba(0x10, 0x20, 0x30, 0x48));   // velo: bloqueados
            {   // candado sobre "When Pigs Fly" (el original lo muestra abierto)
                float cx = MENU_PX[1] + 51, cy = 24 + 190;
                frameRect(cx - 14, cy - 34, cx + 14, cy - 4, 5.f, rgba(0xC0, 0xC4, 0xC8, 0x80));
                rect(cx - 24, cy - 8, cx + 24, cy + 30, rgba(0xF0, 0xC8, 0x20, 0x80));
                rect(cx - 4, cy + 6, cx + 4, cy + 20, rgba(0x60, 0x48, 0x10, 0x80));
            }
            {
                float pulse = 2.f + 2.f * sinf(clock * 6.f);
                frameRect(MENU_PX[wsel] - pulse, 24 + 40 - pulse, MENU_PX[wsel] + 102 + pulse, 24 + 328 + pulse, 4.f, rgba(0xFF, 0xE0, 0x20, 0x80));
            }
            {   // Sandbox abierto: se tapa el candado y el cartel "LOCKED" del original
                float cx = MENU_PX[3] + 51, cy = 24 + 190;
                rect(cx - 40, cy - 40, cx + 40, cy + 40, rgba(0x58, 0xB0, 0x2A, 0x80));
                frameRect(cx - 40, cy - 40, cx + 40, cy + 40, 3.f, rgba(0xE8, 0xF0, 0xD0, 0x80));
                tri(cx - 12, cy - 22, cx - 12, cy + 22, cx + 24, cy, rgba(0xFF, 0xFF, 0xFF, 0x80));
                rect(MENU_PX[3] + 18, 24 + 281, MENU_PX[3] + 84, 24 + 313, rgba(0x9A, 0x70, 0x38, 0x80));
                textCentered(false, MENU_PX[3] + 51, 24 + 285, "ABIERTO", 0.55f, 255, 255, 255);
            }
            textCentered(false, 320, 396, "X: elegir     Círculo: volver", 0.8f, 255, 255, 255);
            if (msgTimer > 0) {
                rect(40, 170, 600, 250, rgba(0x10, 0x20, 0x30, 0x70));
                frameRect(40, 170, 600, 250, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                textCentered(false, 320, 190, "Aguarde a la sig. actualización", 1.15f, 255, 240, 120);
            }
        } else if (screen == S_SELECT) {
            screenDraw(SCR_LEVELS);                                          // selector de niveles original
            for (int i = 0; i < 15; i++) {
                float cx = MENU_TX[i % 5], cy = 24 + MENU_TY[i / 5];
                if (i >= Game::levelCount()) rect(cx - 35, cy - 35, cx + 35, cy + 52, rgba(0x10, 0x20, 0x30, 0x50));   // velo: no existe todavia
                if (i == sel) {
                    float pulse = 2.f + 2.f * sinf(clock * 6.f);
                    frameRect(cx - 38 - pulse, cy - 38 - pulse, cx + 38 + pulse, cy + 38 + pulse, 4.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                }
            }
            textCentered(false, 320, 396, "X: jugar     Círculo: volver", 0.8f, 255, 255, 255);
            if (msgTimer > 0) {
                rect(40, 170, 600, 250, rgba(0x10, 0x20, 0x30, 0x70));
                frameRect(40, 170, 600, 250, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                textCentered(false, 320, 190, "Aguarde a la sig. actualización", 1.15f, 255, 240, 120);
            }
        } else if (screen == S_OPTIONS) {
            screenDraw(SCR_TITLE);
            rect(0, 24, SCREEN_W, 424, rgba(0x10, 0x20, 0x30, 0x58));        // fondo oscurecido
            rect(100, 64, 540, 372, rgba(0x10, 0x20, 0x30, 0x70));
            frameRect(100, 64, 540, 372, 3.f, rgba(0xD8, 0xDC, 0xE0, 0x80));
            textCentered(false, 320, 76, "Opciones", 1.5f, 255, 240, 120);
            static const char* LABEL[4] = { "Cursor en los menús", "Música", "Efectos de sonido", "Volver" };
            bool val[3] = { optCursor, optMusic, optSfx };
            for (int r = 0; r < 4; r++) {
                float y = OPT_ROWY[r];
                if (r == optSel) {
                    float pulse = 1.5f + 1.5f * sinf(clock * 6.f);
                    frameRect(118 - pulse, y - 8 - pulse, 522 + pulse, y + 34 + pulse, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                }
                textLeft(135, y, LABEL[r], 1.05f, 255, 255, 255);
                if (r < 3) textLeft(462, y, val[r] ? "Sí" : "No", 1.1f, val[r] ? 140 : 255, val[r] ? 255 : 140, 140);
            }
            textCentered(false, 320, 340, "Las opciones no se guardan al apagar la consola", 0.6f, 220, 220, 220);
            textCentered(false, 320, 396, "X: cambiar     Círculo: volver", 0.8f, 255, 255, 255);
        } else {
            drawDecor(game, 0);
            drawTerrain(game);
            drawDecor(game, 1);
            if (!game.isSandbox()) {   // bandera de meta (cuadros) parada en el suelo
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
        if (optCursor && screen != S_GAME) {   // cursor de los menus
            tri(mcx, mcy, mcx + 14, mcy + 6, mcx + 6, mcy + 14, rgba(0xFF, 0xFF, 0xFF, 0x80));
            tri(mcx + 1, mcy + 1, mcx + 11, mcy + 6, mcx + 6, mcy + 11, rgba(0x20, 0x20, 0x20, 0x80));
        }
        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
