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
#include <libmc.h>
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
extern unsigned char terrain2_raw[]; extern unsigned int size_terrain2_raw;
extern unsigned char icon_sys[]; extern unsigned int size_icon_sys;
extern unsigned char icon_ico[]; extern unsigned int size_icon_ico;
extern unsigned char screen_title[]; extern unsigned int size_screen_title;
extern unsigned char screen_worlds[]; extern unsigned int size_screen_worlds;
extern unsigned char screen_levels[]; extern unsigned int size_screen_levels;
extern unsigned char screen_levels2[]; extern unsigned int size_screen_levels2;
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
static bool optTouch = true;          // modo de control: true = tactil (cursor y botones azules), false = fisico (botones del mando)

// zonas de los menus, medidas sobre las imagenes originales (pantalla virtual 640x448)
static const float MENU_PX[4] = { 87, 213, 339, 464 };                       // paneles de mundos: x (ancho 102, y 64..352)
static const float MENU_TX[5] = { 148, 233, 319, 405, 490 }, MENU_TY[3] = { 65, 160, 252 };   // casillas de niveles (y + 24)
static const float OPT_ROWY[5] = { 122, 158, 194, 230, 272 };                     // filas de opciones

// ---------------------------------------------------------------- guardado en la memory card (ranura 1)
struct SaveData { char magic[4]; int version; unsigned char stars[16]; unsigned char opt[4]; float best[16]; };
static SaveData sv;
static bool mcOK = false;
static bool saveExists = false;       // ya hay una partida escrita en la memory card
static bool g_newRecord = false;      // el ultimo nivel ganado batio el mejor tiempo
static float g_bestShown = 0.f;
enum { FIO_RD = 0x0001, FIO_WR = 0x0002, FIO_CREAT = 0x0200, FIO_TRUNC = 0x0400 };
static int mcResult() { int cmd = 0, res = 0; mcSync(0, &cmd, &res); return res; }
static void saveClear() { memset(&sv, 0, sizeof(sv)); sv.magic[0] = 'B'; sv.magic[1] = 'P'; sv.magic[2] = 'G'; sv.magic[3] = '1'; sv.version = 1; sv.opt[1] = 1; sv.opt[2] = 1; sv.opt[3] = 0; }
static void saveInit() {
    saveClear();
    if (mcInit(MC_TYPE_MC) < 0) return;
    int type = 0, freeKb = 0, fmt = 0;
    mcGetInfo(0, 0, &type, &freeKb, &fmt);
    int r = mcResult();
    mcOK = (r >= -1);                           // 0 o -1: hay tarjeta; menos de -1: no hay o no esta formateada
}
static bool saveLoad() {
    if (!mcOK) return false;
    mcOpen(0, 0, "BPIGGIES/progreso.dat", FIO_RD);
    int fd = mcResult();
    if (fd < 0) return false;
    SaveData tmp; memset(&tmp, 0, sizeof(tmp));
    mcRead(fd, &tmp, sizeof(tmp)); int n = mcResult();
    mcClose(fd); mcResult();
    if (n != (int)sizeof(tmp) || tmp.magic[0] != 'B' || tmp.magic[1] != 'P' || tmp.magic[2] != 'G' || tmp.magic[3] != '1') return false;
    sv = tmp;
    saveExists = true;
    return true;
}
static bool writeFile(const char* path, const void* data, int size) {
    mcOpen(0, 0, path, FIO_WR | FIO_CREAT | FIO_TRUNC);
    int fd = mcResult();
    if (fd < 0) return false;
    mcWrite(fd, data, size); mcResult();
    mcClose(fd); mcResult();
    return true;
}
static void saveIconIfMissing() {           // el icono (un cerdito) que muestra el explorador de la memory card
    mcOpen(0, 0, "BPIGGIES/icon.sys", FIO_RD);
    int fd = mcResult();
    if (fd >= 0) { mcClose(fd); mcResult(); return; }
    if (writeFile("BPIGGIES/icon.sys", icon_sys, (int)size_icon_sys)) writeFile("BPIGGIES/bpiggies.ico", icon_ico, (int)size_icon_ico);
}
static bool saveWrite() {
    if (!mcOK) return false;
    mcMkDir(0, 0, "BPIGGIES"); mcResult();                       // si ya existe da error y no importa
    saveIconIfMissing();
    bool ok = writeFile("BPIGGIES/progreso.dat", &sv, (int)sizeof(sv));
    if (ok) saveExists = true;
    return ok;
}
static void saveOptions() { sv.opt[0] = optCursor; sv.opt[1] = optMusic; sv.opt[2] = optSfx; sv.opt[3] = optTouch ? 0 : 1; }   // solo en memoria: se escribe con el boton del disquete
static void recordWin(const Game& g) {      // estrellas y mejor tiempo del nivel que se acaba de ganar
    int li = g.levelIndex(); if (li < 0 || li >= 16 || g.isSandbox()) return;
    int st = g.resultStars(); float ft = g.finalTime();
    if (st > sv.stars[li]) sv.stars[li] = (unsigned char)st;
    g_newRecord = (sv.best[li] <= 0.f || ft < sv.best[li]);
    if (g_newRecord) sv.best[li] = ft;
    g_bestShown = sv.best[li];                // se guarda en memoria; a la tarjeta solo con el boton del disquete
}




static GSGLOBAL* gs;
static GSTEXTURE texPig, texBox, texWheel, texFlag, texFontS, texScreen;
static const float FONT_K = 24.f / 22.f;   // el atlas es de 22 px; esto mantiene el tamano de antes
static int slotOwner = -1;   // que hay en la textura compartida de 512 KB: 0..2 = pantallas, 10+n = terreno del nivel n
static GSTEXTURE texDecor[DEC_COUNT];
static GSTEXTURE texTerr;                             // terreno del nivel actual (comparte la memoria de video con las imagenes de los menus)
static const unsigned char* const TERRAIN_RAW[3] = { terrain1_raw, terrain2_raw, NULL };   // por indice de nivel (el sandbox no usa textura)
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
static void sprite(GSTEXTURE& t, float cx, float cy, float w, float h, float ang, u64 tint = 0) {
    float c = cosf(ang), s = sinf(ang), hw = w * 0.5f, hh = h * 0.5f;
    float lx[4] = { -hw, hw, -hw, hw }, ly[4] = { -hh, -hh, hh, hh };
    float X[4], Y[4];
    for (int i = 0; i < 4; i++) { X[i] = (cx + lx[i] * c - ly[i] * s) * kx; Y[i] = (cy + lx[i] * s + ly[i] * c) * ky; }
    float tw = (float)t.Width, th = (float)t.Height;
    u64 col = tint ? tint : rgba(0x80, 0x80, 0x80, 0x80);
    gsKit_prim_triangle_texture(gs, &t, X[0], Y[0], 0, 0, X[1], Y[1], tw, 0, X[2], Y[2], 0, th, 1, col);
    gsKit_prim_triangle_texture(gs, &t, X[1], Y[1], tw, 0, X[3], Y[3], tw, th, X[2], Y[2], 0, th, 1, col);
}
static GSTEXTURE& texOf(int type) {
    switch (type) { case P_PIG: return texPig; case P_BOX: return texBox; case P_WHEEL: return texWheel; case P_FAN: return texDecor[DEC_FAN]; default: return texDecor[DEC_BALLOON]; }
}
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
static unsigned musIdx = 0, musFrac = 0;      // posiciones en punto fijo (24 bits de fraccion): sin la deriva de los decimales
static float musFade = 0.f;
static unsigned fxIdx = 0, fxFrac = 0;
static const SoundData* fx = NULL;
static int idleAvail = 0;                      // capacidad del buffer de audio
static int targetFill = 12288;                 // bytes que mantenemos en el buffer (~60 ms)
static const unsigned MUS_STEP_FP = 3853517u, FX_STEP_FP = 7707034u;   // 11025/48000 y 22050/48000, por 2^24
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
static void sndPlay(int id) { if (audioOK && optSfx) { fx = &SOUNDS[id]; fxIdx = 0; fxFrac = 0; } }
static void musicWant(int m) { musWant = optMusic ? m : MUS_NONE; }
static void mixChunk(short* out, int n) {     // n = cuadros estereo
    for (int i = 0; i < n; i++) {
        float v = 0.f;
        if (musCur != MUS_NONE) {
            const Track& t = mus[musCur];
            unsigned i0 = musIdx, i1 = i0 + 1; if (i1 >= (unsigned)t.n) i1 = 0;
            float fr = (float)(musFrac >> 8) * (1.f / 65536.f);
            v += ((float)t.d[i0] * (1.f - fr) + (float)t.d[i1] * fr) * MUSIC_VOL * musFade;
            musFrac += MUS_STEP_FP; musIdx += musFrac >> 24; musFrac &= 0xFFFFFFu;
            if (musIdx >= (unsigned)t.n) musIdx -= (unsigned)t.n;
            if (musFade < 1.f) { musFade += 1.f / (48000.f * 0.25f); if (musFade > 1.f) musFade = 1.f; }
        }
        if (fx) {
            unsigned i0 = fxIdx, i1 = i0 + 1; if (i1 >= (unsigned)fx->samples) i1 = i0;
            float fr = (float)(fxFrac >> 8) * (1.f / 65536.f);
            v += ((float)fx->data[i0] * (1.f - fr) + (float)fx->data[i1] * fr) * FX_VOL;
            fxFrac += FX_STEP_FP; fxIdx += fxFrac >> 24; fxFrac &= 0xFFFFFFu;
            if (fxIdx >= (unsigned)fx->samples) fx = NULL;
        }
        if (v > 32767.f) { v = 32767.f; }
        if (v < -32768.f) { v = -32768.f; }
        out[2 * i] = (short)v; out[2 * i + 1] = (short)v;
    }
}
static void sndPump() {
    if (!audioOK) return;
    if (musWant != musCur) { musCur = musWant; musIdx = 0; musFrac = 0; musFade = 0.f; }   // cambio de cancion con entrada suave
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
        float fx = x - 40.f, fy = y - 330.f;
        if (fx * fx + fy * fy < 30.f * 30.f) return 2;
        float dx = x - 335.f, dy = y - 234.f;
        if (dx * dx + dy * dy < 56.f * 56.f) return 0;
        if (x >= 400 && x <= 560 && y >= 271 && y <= 311) return 1;
    } else if (sc == S_WORLDS) {
        if (x >= 14 && x <= 96 && y >= 372 && y <= 414) return 10;                                  // flecha de volver (la del dibujo original)
        for (int i = 0; i < 4; i++) if (x >= MENU_PX[i] && x <= MENU_PX[i] + 102 && y >= 64 && y <= 352) return i;
    } else if (sc == S_SELECT) {
        if (x >= 14 && x <= 96 && y >= 372 && y <= 414) return 10;
        for (int i = 0; i < 15; i++) { float cx = MENU_TX[i % 5], cy = 24 + MENU_TY[i / 5]; if (fabsf(x - cx) <= 36 && y >= cy - 36 && y <= cy + 52) return i; }
    } else if (sc == S_OPTIONS) {
        if (x >= 14 && x <= 96 && y >= 372 && y <= 414) return 10;                                  // flecha de volver
        for (int r = 0; r < 5; r++) if (x >= 110 && x <= 530 && y >= OPT_ROWY[r] - 6 && y <= OPT_ROWY[r] + 30) return r;
    }
    return -1;
}

static float g_clock = 0.f;
static float gcx = 320.f, gcy = 300.f;      // cursor durante la partida
static void disc(float cx, float cy, float r, u64 col) {
    const int N = 24;
    for (int i = 0; i < N; i++) {
        float a0 = i * 6.2831853f / N, a1 = (i + 1) * 6.2831853f / N;
        tri(cx, cy, cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, col);
    }
}
static void drawFloppy(float cx, float cy, bool hot) {      // icono de disquete
    rect(cx - 22, cy - 22, cx + 22, cy + 22, rgba(0x20, 0x38, 0x80, 0x80));
    rect(cx - 22, cy - 22, cx + 22, cy - 20, hot ? rgba(0xFF, 0xE0, 0x20, 0x80) : rgba(0x50, 0x70, 0xC0, 0x80));
    rect(cx - 13, cy - 22, cx + 12, cy - 7, rgba(0xC8, 0xCC, 0xD4, 0x80));       // persiana metalica
    rect(cx + 3, cy - 20, cx + 8, cy - 9, rgba(0x30, 0x34, 0x40, 0x80));
    rect(cx - 15, cy + 1, cx + 15, cy + 22, rgba(0xF2, 0xF2, 0xF2, 0x80));       // etiqueta
    rect(cx - 11, cy + 7, cx + 11, cy + 9, rgba(0xB0, 0xB0, 0xB8, 0x80));
    rect(cx - 11, cy + 13, cx + 11, cy + 15, rgba(0xB0, 0xB0, 0xB8, 0x80));
    if (hot) frameRect(cx - 27, cy - 27, cx + 27, cy + 27, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
}
// botones azules de la partida (como en el juego original): uno por tipo de pieza activable
static int playButtons(const Game& g, float xs[2], int ty[2]) {    // ty: 0 = ventilador, 1 = globo
    int n = 0;
    if (g.fanCount() > 0) ty[n++] = 0;
    if (g.balloonsLeft() > 0) ty[n++] = 1;
    for (int i = 0; i < n; i++) xs[i] = 320.f + ((float)i - (float)(n - 1) * 0.5f) * 80.f;
    return n;
}
static const float PLAY_BTN_Y = 366.f;
static int playButtonAt(const Game& g, float x, float y) {
    float xs[2]; int ty[2]; int n = playButtons(g, xs, ty);
    for (int i = 0; i < n; i++) { float dx = x - xs[i], dy = y - PLAY_BTN_Y; if (dx * dx + dy * dy < 32.f * 32.f) return ty[i]; }
    return -1;
}
static void drawPlayButtons(const Game& g) {
    float xs[2]; int ty[2]; int n = playButtons(g, xs, ty);
    int over = playButtonAt(g, gcx, gcy);
    for (int i = 0; i < n; i++) {
        float cx = xs[i], cy = PLAY_BTN_Y;
        bool on = (ty[i] == 0) && g.fansOn();
        disc(cx, cy, 28.f, rgba(0x2F, 0x84, 0xDE, 0x80));
        ring(cx, cy, 28.f, 4.f, on ? rgba(0x60, 0xE8, 0x60, 0x80) : rgba(0xB8, 0xE0, 0xFF, 0x80));
        if (ty[i] == 0) sprite(texDecor[DEC_FAN], cx, cy, texDecor[DEC_FAN].Width * 0.62f, texDecor[DEC_FAN].Height * 0.62f, 0.f);
        else sprite(texDecor[DEC_BALLOON], cx, cy - 1, texDecor[DEC_BALLOON].Width * 0.72f, texDecor[DEC_BALLOON].Height * 0.72f, 0.f);
        if (ty[i] == 1 && g.balloonsLeft() > 1) { char b[8]; snprintf(b, sizeof(b), "x%d", g.balloonsLeft()); textLeft(cx + 14, cy + 12, b, 0.6f, 255, 255, 255); }
        if (over == ty[i]) ring(cx, cy, 34.f, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
    }
}

// ---------------------------------------------------------------- interfaz tactil (como el juego original)
static bool g_paused = false, g_trash = false;        // menu de pausa abierto / modo tacho (X sobre una casilla borra)
static void thickLine(float x1, float y1, float x2, float y2, float th, u64 col) {
    float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy); if (l < 0.01f) return;
    float nx = -dy / l * th * 0.5f, ny = dx / l * th * 0.5f;
    tri(x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, col); tri(x1 + nx, y1 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny, col);
}
static void roundButton(float cx, float cy, float r, bool blue, bool hot, bool dim = false) {
    disc(cx, cy, r, dim ? rgba(0x50, 0x60, 0x70, 0x80) : (blue ? rgba(0x2F, 0x84, 0xDE, 0x80) : rgba(0x6C, 0xBF, 0x3A, 0x80)));
    ring(cx, cy, r, 3.f, dim ? rgba(0x90, 0xA0, 0xB0, 0x80) : (blue ? rgba(0xB8, 0xE0, 0xFF, 0x80) : rgba(0x3A, 0x7A, 0x22, 0x80)));
    if (hot) ring(cx, cy, r + 6.f, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
}
static void glyphPause(float cx, float cy) { u64 w = rgba(0xFF, 0xFF, 0xFF, 0x80); rect(cx - 9, cy - 11, cx - 3, cy + 11, w); rect(cx + 3, cy - 11, cx + 9, cy + 11, w); }
static void glyphRestart(float cx, float cy) { u64 w = rgba(0xFF, 0xFF, 0xFF, 0x80); ring(cx, cy, 11.f, 4.f, w); tri(cx + 4, cy - 17, cx + 4, cy - 5, cx + 15, cy - 11, w); }
static void glyphTrash(float cx, float cy) {
    u64 w = rgba(0xFF, 0xFF, 0xFF, 0x80), d = rgba(0x2F, 0x84, 0xDE, 0x80);
    rect(cx - 10, cy - 11, cx + 10, cy - 7, w); rect(cx - 3, cy - 14, cx + 3, cy - 11, w);
    rect(cx - 8, cy - 5, cx + 8, cy + 13, w); rect(cx - 4, cy - 2, cx - 2, cy + 10, d); rect(cx + 2, cy - 2, cx + 4, cy + 10, d);
}
static void glyphCheck(float cx, float cy) { u64 w = rgba(0xFF, 0xFF, 0xFF, 0x80); thickLine(cx - 12, cy + 1, cx - 4, cy + 10, 6.f, w); thickLine(cx - 4, cy + 10, cx + 13, cy - 10, 6.f, w); }
static void glyphPlay(float cx, float cy, float s) { tri(cx - 8 * s, cy - 13 * s, cx - 8 * s, cy + 13 * s, cx + 14 * s, cy, rgba(0xFF, 0xFF, 0xFF, 0x80)); }
static void glyphSound(float cx, float cy, bool on) {
    u64 w = rgba(0xFF, 0xFF, 0xFF, 0x80);
    rect(cx - 12, cy - 6, cx - 5, cy + 6, w); tri(cx - 5, cy - 6, cx - 5, cy + 6, cx + 6, cy + 14, w); tri(cx - 5, cy - 6, cx + 6, cy + 14, cx + 6, cy - 14, w);
    if (on) { thickLine(cx + 10, cy - 7, cx + 14, cy - 11, 3.f, w); thickLine(cx + 10, cy, cx + 17, cy, 3.f, w); thickLine(cx + 10, cy + 7, cx + 14, cy + 11, 3.f, w); }
    else { thickLine(cx + 9, cy - 8, cx + 19, cy + 8, 4.f, rgba(0xFF, 0x60, 0x60, 0x80)); thickLine(cx + 19, cy - 8, cx + 9, cy + 8, 4.f, rgba(0xFF, 0x60, 0x60, 0x80)); }
}
static void glyphGrid(float cx, float cy) { u64 w = rgba(0xFF, 0xFF, 0xFF, 0x80); for (int i = -1; i <= 1; i++) for (int j = -1; j <= 1; j++) rect(cx + i * 10 - 4, cy + j * 10 - 4, cx + i * 10 + 4, cy + j * 10 + 4, w); }
static const float HUD_PX = 50.f, HUD_PY = 66.f, HUD_RY = 122.f;                 // pausa y reiniciar (arriba a la izquierda)
static bool buildButtons(const Game& g, float& tx, float& kx, float& by) {     // tacho a la izquierda de la rejilla y tilde a la derecha
    if (g.gridCols() < 1) return false;
    float ax, ay, bx, bY, half = 31.f * g.zoomNow() * 0.5f;
    g.cellScreen(0, 0, ax, ay); g.cellScreen(g.gridCols() - 1, g.gridRows() - 1, bx, bY);
    by = (ay + bY) * 0.5f; tx = ax - half - 42.f; kx = bx + half + 42.f;
    if (tx < 70.f) tx = 70.f;
    if (kx > 570.f) kx = 570.f;
    return true;
}
// que boton de la interfaz esta bajo (x,y): 1 pausa, 2 reiniciar, 3 tacho, 4 tilde; en pausa: 11 seguir, 12 sonido, 13 niveles
static int hudHit(const Game& g, bool paused, float x, float y) {
    auto in = [&](float cx, float cy, float r) { float dx = x - cx, dy = y - cy; return dx * dx + dy * dy < r * r; };
    if (paused) { if (in(100, 150, 44)) return 11; if (in(100, 250, 38)) return 12; if (in(100, 350, 38)) return 13; return 0; }
    if (g.mode() != M_BUILD && g.mode() != M_PLAY) return 0;
    if (in(HUD_PX, HUD_PY, 30)) return 1;
    if (in(HUD_PX, HUD_RY, 30)) return 2;
    if (g.mode() == M_BUILD) { float tx, kx, by; if (buildButtons(g, tx, kx, by)) { if (in(tx, by, 34)) return 3; if (in(kx, by, 34)) return 4; } }
    return 0;
}
static void drawTouchHud(const Game& g, float hx, float hy) {
    if (g.mode() != M_BUILD && g.mode() != M_PLAY) return;
    int h = hudHit(g, false, hx, hy);
    roundButton(HUD_PX, HUD_PY, 24.f, false, h == 1); glyphPause(HUD_PX, HUD_PY);
    roundButton(HUD_PX, HUD_RY, 24.f, false, h == 2); glyphRestart(HUD_PX, HUD_RY);
    if (g.mode() == M_BUILD) {
        float tx, kx, by;
        if (buildButtons(g, tx, kx, by)) {
            roundButton(tx, by, 28.f, true, h == 3 || g_trash); glyphTrash(tx, by);
            roundButton(kx, by, 28.f, true, h == 4, !g.pigPlaced()); glyphCheck(kx, by);
        }
    }
}
static void drawPauseMenu(const Game& g) {
    rect(0, 0, 200, SCREEN_H, rgba(0x00, 0x00, 0x00, 0x68));
    char b[24]; if (g.isSandbox()) snprintf(b, sizeof(b), "Sandbox"); else snprintf(b, sizeof(b), "Nivel %d", g.levelNumber());
    textLeft(26, 30, b, 1.5f, 255, 255, 255);
    int h = hudHit(g, true, gcx, gcy);
    roundButton(100, 150, 40.f, false, h == 11); glyphPlay(100, 150, 1.3f);
    roundButton(100, 250, 34.f, false, h == 12); glyphSound(100, 250, optMusic || optSfx);
    roundButton(100, 350, 34.f, false, h == 13); glyphGrid(100, 350);
}
// filas del cuadro de fin de nivel: victoria 1 siguiente / 2 repetir / 3 elegir nivel; derrota 2 repetir / 3 elegir nivel
static int endRowAt(bool win, float x, float y) {
    if (x < 150 || x > 490) return 0;
    if (win) { if (y >= 288 && y < 320) return 1; if (y >= 320 && y < 352) return 2; if (y >= 352 && y < 384) return 3; }
    else { if (y >= 211 && y < 249) return 2; if (y >= 256 && y < 294) return 3; }
    return 0;
}

// ---------------------------------------------------------------- pantallas originales
static void screenInit() {
    memset(&texScreen, 0, sizeof(texScreen));
    texScreen.Width = SCR_W; texScreen.Height = SCR_H; texScreen.PSM = GS_PSM_CT16; texScreen.Filter = GS_FILTER_LINEAR;
    texScreen.Mem = (u32*)memalign(128, SCR_W * SCR_H * 2);
    texScreen.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(SCR_W, SCR_H, GS_PSM_CT16), GSKIT_ALLOC_USERBUFFER);
    texTerr = texScreen;                                          // el terreno usa el mismo espacio de video (en el juego no se ven los menus)
    texTerr.PSM = GS_PSM_CT32;
    texTerr.Mem = (u32*)memalign(128, 512000);
}
static void terrainLoad(const Game& g) {
    const LevelData& L = g.levelData(); int idx = g.levelIndex();
    if (L.texW <= 0 || !TERRAIN_RAW[idx] || slotOwner == 10 + idx) return;
    texTerr.Width = L.texW; texTerr.Height = L.texH; texTerr.PSM = GS_PSM_CT32; texTerr.Filter = GS_FILTER_LINEAR;
    memcpy(texTerr.Mem, TERRAIN_RAW[idx], (size_t)L.texW * L.texH * 4);
    FlushCache(0);
    gsKit_texture_upload(gs, &texTerr);
    slotOwner = 10 + idx;
}
static void screenLoad(int idx) {            // sube la imagen pedida a la memoria de video (se hace al cambiar de pantalla)
    if (idx == slotOwner) return;
    const unsigned char* src = idx == SCR_TITLE ? screen_title : (idx == SCR_WORLDS ? screen_worlds : (idx == SCR_LEVELS2 ? screen_levels2 : screen_levels));
    memcpy(texScreen.Mem, src, SCR_W * SCR_H * 2);
    FlushCache(0);
    gsKit_texture_upload(gs, &texScreen);
    slotOwner = idx;
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
    float z = g.zoomNow();
    sx = (wx - cx) * z + SCREEN_W * 0.5f; sy = (wy - cy) * z + SCREEN_H * 0.5f;
}

static void drawDecor(const Game& g, int layer) {
    const LevelData& L = g.levelData();
    for (int i = 0; i < L.ndecor; i++) {
        const DecorItem& d = L.decor[i];
        if (d.layer != layer) continue;
        float sx, sy; worldToScreen(g, d.x, d.y, sx, sy);
        GSTEXTURE& t = texDecor[d.type];
        float zf = g.zoomNow() / ZOOM;
        float w = t.Width * d.scale * zf, h = t.Height * d.scale * zf, hh = h * 0.5f;
        if (sx < -w - 40 || sx > SCREEN_W + w + 40) continue;     // fuera de pantalla
        sprite(t, sx + hh * sinf(d.angle), sy - hh * cosf(d.angle), w, h, d.angle);   // la base queda apoyada en (x,y)
    }
}

static void drawTerrain(const Game& g) {
    const LevelData& L = g.levelData();
    bool textured = L.texW > 0;
    GSTEXTURE* tt = textured ? &texTerr : NULL;
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
        if (textured) {       // (con llaves: en la libreria real esta funcion se expande a varias instrucciones)
            gsKit_prim_triangle_texture(gs, tt, X[a] * kx, Y[a] * ky, U[a], V[a], X[b] * kx, Y[b] * ky, U[b], V[b], X[c] * kx, Y[c] * ky, U[c], V[c], 1, white);
        } else {
            tri(X[a], Y[a], X[b], Y[b], X[c], Y[c], flat);
        }
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
            float zf = g.zoomNow() / ZOOM;
            const float O = 5.f * zf, M = -1.f * zf, I = -9.f * zf;
            float ox1 = X[i] + nx * O, oy1 = Y[i] + ny * O, ox2 = X[j] + nx * O, oy2 = Y[j] + ny * O;
            float mx1 = X[i] + nx * M, my1 = Y[i] + ny * M, mx2 = X[j] + nx * M, my2 = Y[j] + ny * M;
            float ix1 = X[i] + nx * I, iy1 = Y[i] + ny * I, ix2 = X[j] + nx * I, iy2 = Y[j] + ny * I;
            tri(ox1, oy1, ox2, oy2, mx2, my2, lightG); tri(ox1, oy1, mx2, my2, mx1, my1, lightG);
            tri(mx1, my1, mx2, my2, ix2, iy2, darkG);  tri(mx1, my1, ix2, iy2, ix1, iy1, darkG);
        }
    }
}

static void endRowDraw(float ytop, float h, const char* text, float scale, int code, bool win) {   // fila del cuadro de fin: boton en modo tactil
    if (optTouch) {
        bool hot = endRowAt(win, gcx, gcy) == code;
        rect(150, ytop, 490, ytop + h - 2, rgba(0x6C, 0xBF, 0x3A, 0x50));
        frameRect(150, ytop, 490, ytop + h - 2, 2.f, hot ? rgba(0xFF, 0xE0, 0x20, 0x80) : rgba(0x3A, 0x7A, 0x22, 0x80));
    }
    textCentered(false, 320, ytop + 2, text, scale, 255, 255, 255);
}
static void drawGameUI(const Game& g) {
    char buf[64];
    if (g.mode() == M_BUILD) {
        u64 cellc = rgba(0xFF, 0xFF, 0xFF, 0x24), cellb = rgba(0xFF, 0xFF, 0xFF, 0x60);
        if (g.gridCols() * g.gridRows() > 60) {   // rejilla muy grande: un fondo y lineas finas, en vez de un recuadro por casilla
            float ax, ay, bx, by, half = 31.f * g.zoomNow() * 0.5f;
            g.cellScreen(0, 0, ax, ay); g.cellScreen(g.gridCols() - 1, g.gridRows() - 1, bx, by);
            rect(ax - half, ay - half, bx + half, by + half, cellc);
            for (int c = 0; c <= g.gridCols(); c++) { float x = ax - half + c * 2.f * half; rect(x - 0.6f, ay - half, x + 0.6f, by + half, cellb); }
            for (int r = 0; r <= g.gridRows(); r++) { float y = ay - half + r * 2.f * half; rect(ax - half, y - 0.6f, bx + half, y + 0.6f, cellb); }
        } else {
            float half = 31.f * g.zoomNow() * 0.5f;
            for (int r = 0; r < g.gridRows(); r++) for (int c = 0; c < g.gridCols(); c++) {
                float sx, sy; g.cellScreen(c, r, sx, sy);
                rect(sx - half, sy - half, sx + half, sy + half, cellc);
                frameRect(sx - half, sy - half, sx + half, sy + half, 1.5f, cellb);
            }
        }
        for (int slot = 0; slot < g.trayCount(); slot++) {   // bandeja: solo las piezas que ofrece el nivel; puntos = las que quedan
            int i = g.trayTypeAt(slot);
            float sx, sy; g.traySlot(i, sx, sy);
            rect(sx - 30, sy - 30, sx + 30, sy + 30, rgba(0x10, 0x20, 0x30, 0x50));
            spriteOf(i, sx, sy - 4, 0.8f, 0.f);
            if (g.remaining(i) > 6) textCentered(false, sx, sy + 10, "inf", 0.7f, 255, 255, 255);   // piezas ilimitadas (sandbox)
            else for (int k = 0; k < g.remaining(i); k++) rect(sx - 24 + k * 10, sy + 20, sx - 18 + k * 10, sy + 26, rgba(0xFF, 0xFF, 0xFF, 0x80));
            if (g.selected() == i) frameRect(sx - 30, sy - 30, sx + 30, sy + 30, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
        }
        textCentered(false, 320, 412, optTouch ? "X: poner   Tacho: borrar   Tilde: iniciar   START: pausa" : "X: poner   Cuadrado: quitar   Triángulo: iniciar   START: menú", 0.62f, 255, 255, 255);   // el cursor se dibuja al final, encima de los botones
    } else if (g.mode() == M_PLAY) {
        if (optTouch && (g.fanCount() > 0 || g.balloonsLeft() > 0)) drawPlayButtons(g);
        if (g.fanCount() > 0 || g.balloonsLeft() > 0) textCentered(false, 320, 408, optTouch ? "Cursor + X sobre los botones azules   Círculo: construir" : "R1: ventilador   L1: pinchar globo   Círculo: construir", 0.62f, 255, 255, 255);
        else if (g.isSandbox()) textCentered(false, 320, 408, "Círculo: volver a construir   Stick derecho: cámara", 0.62f, 255, 255, 255);
        if (g.isSandbox()) {
            char b2[48];
            if (g.fanCount() > 0) textLeft(30, 48, g.fansOn() ? "Ventilador: ENCENDIDO" : "Ventilador: APAGADO", 0.75f, g.fansOn() ? 150 : 255, g.fansOn() ? 255 : 150, 150);
            if (g.balloonsLeft() > 0) { snprintf(b2, sizeof(b2), "Globos: %d", g.balloonsLeft()); textLeft(30, 72, b2, 0.75f, 255, 200, 200); }
        }
        else if (g.time() < 5.f && g.fanCount() == 0 && g.balloonsLeft() == 0) textCentered(false, 320, 408, "Stick derecho: mover la cámara", 0.7f, 255, 255, 255);
    } else if (g.mode() == M_WIN) {
        rect(100, 60, 540, 396, rgba(0x10, 0x20, 0x30, 0x60));
        frameRect(100, 60, 540, 396, 3.f, rgba(0x40, 0xD0, 0x40, 0x80));
        textCentered(false, 320, 68, "¡Nivel completado!", 1.45f, 140, 255, 140);
        const LevelData& LD = g.levelData();
        static const char* CAP[3] = { "Meta", "Caja estrella", "Tiempo" };
        bool got[3] = { true, g.starCollected(), LD.parTime > 0.f && g.finalTime() <= LD.parTime };
        for (int i = 0; i < 3; i++) {
            float cx = 320 + (i - 1) * 130;
            sprite(texDecor[DEC_GOLDSTAR], cx, 142, 70, 67, 0, got[i] ? rgba(0x80, 0x80, 0x80, 0x80) : rgba(0x30, 0x30, 0x30, 0x70));
            textCentered(false, cx, 178, CAP[i], 0.62f, got[i] ? 255 : 160, got[i] ? 255 : 160, got[i] ? 255 : 160);
        }
        snprintf(buf, sizeof(buf), "Tiempo: %.1f s", g.finalTime());
        textCentered(false, 320, 206, buf, 1.0f, 255, 255, 255);
        if (g_newRecord) textCentered(false, 320, 234, "¡Nuevo récord!", 0.9f, 255, 230, 90);
        else if (g_bestShown > 0.f) { snprintf(buf, sizeof(buf), "Mejor tiempo: %.1f s", g_bestShown); textCentered(false, 320, 234, buf, 0.8f, 220, 220, 220); }
        if (LD.parTime > 0.f) { snprintf(buf, sizeof(buf), "Estrella de tiempo: %.1f s o menos", LD.parTime); textCentered(false, 320, 262, buf, 0.6f, 200, 200, 200); }
        endRowDraw(288, 32, optTouch ? "Siguiente nivel" : "X: siguiente nivel", 1.0f, 1, true);
        endRowDraw(320, 32, optTouch ? "Repetir este nivel" : "Círculo: repetir este nivel", 1.0f, 2, true);
        endRowDraw(352, 32, optTouch ? "Elegir nivel" : "Triángulo: elegir nivel", 1.0f, 3, true);
    } else if (g.mode() == M_LOSE) {
        rect(110, 110, 530, 330, rgba(0x10, 0x20, 0x30, 0x60));
        frameRect(110, 110, 530, 330, 3.f, rgba(0xD0, 0x40, 0x40, 0x80));
        textCentered(false, 320, 128, "¡Perdiste!", 1.45f, 255, 140, 140);
        endRowDraw(211, 38, optTouch ? "Reintentar" : "Círculo: reintentar", 1.1f, 2, false);
        endRowDraw(256, 38, optTouch ? "Elegir nivel" : "Triángulo: elegir nivel", 1.1f, 3, false);
    }
    if (g.mode() == M_PLAY || g.mode() == M_WIN) {          // cronometro
        snprintf(buf, sizeof(buf), "Tiempo: %.1f s", g.mode() == M_WIN ? g.finalTime() : g.time());
        textLeft(468, 22, buf, 0.9f, 255, 255, 255);
    }
    if (g.levelData().starX > -1e8f && g.mode() != M_BUILD)   // la caja estrella: icono dorado si ya se recogio
        sprite(texDecor[DEC_GOLDSTAR], 446, 35, 28, 27, 0, g.starCollected() ? rgba(0x80, 0x80, 0x80, 0x80) : rgba(0x38, 0x38, 0x38, 0x70));
    if (g.isSandbox()) snprintf(buf, sizeof(buf), "Sandbox");
    else snprintf(buf, sizeof(buf), "Nivel %d", g.levelNumber());
    textLeft(100, 22, buf, 0.9f, 255, 255, 255);
}

// ---------------------------------------------------------------- principal
struct Pad { unsigned btn = 0, edge = 0; float ax = 0, ay = 0, lx = 0, ly = 0; bool dirL = false, dirR = false, dirU = false, dirD = false; bool eL = false, eR = false, eU = false, eD = false; };

int main(int, char**) {
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    SifLoadModule("rom0:MCMAN", 0, NULL);
    SifLoadModule("rom0:MCSERV", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, padBuf);
    saveInit();
    if (saveLoad()) { optCursor = sv.opt[0] != 0; optMusic = sv.opt[1] != 0; optSfx = sv.opt[2] != 0; optTouch = sv.opt[3] == 0; }
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
    uploadFont(texFontS, FONT_SMALL);
    screenInit();

    Screen screen = S_TITLE;
    int sel = 0;                 // nivel elegido en el selector (0..14)
    int selWorld = 0;            // mundo cuyo selector de niveles se muestra
    int wsel = 0;                // mundo elegido (0..3)
    int titleSel = 0;            // 0 = jugar, 1 = opciones
    int optSel = 0;              // fila elegida en opciones
    float mcx = 320.f, mcy = 224.f;   // cursor de los menus (opcion)
    float msgTimer = 0.f;        // aviso "aguarde a la siguiente actualizacion"
    bool keepMsg = false;        // conservar el aviso al cambiar de pantalla
    Screen prevScreen = S_TITLE;
    float clock = 0.f;
    Pad pad;
    bool wasPlaying = false;
    bool confirmOpen = false; int confirmSel = 1; float saveMsgTimer = 0.f; const char* saveMsgText = "";
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
        clock += dt; g_clock = clock;
        if (saveMsgTimer > 0) saveMsgTimer -= dt;
        if (msgTimer > 0) msgTimer -= dt;
        bool pX = pad.edge & PAD_CROSS, pO = pad.edge & PAD_CIRCLE, pT = pad.edge & PAD_TRIANGLE, pS = pad.edge & PAD_SQUARE, pStart = pad.edge & PAD_START;

        // ---------------- logica por pantalla ----------------
        bool useCur = (optCursor || optTouch) && screen != S_GAME;       // cursor libre en los menus (opcion)
        int hover = -1;
        if (useCur) {
            mcx += pad.ax * 320.f * dt; mcy += pad.ay * 320.f * dt;
            if (mcx < 0) { mcx = 0; }
            if (mcx > SCREEN_W) { mcx = SCREEN_W; }
            if (mcy < 0) { mcy = 0; }
            if (mcy > SCREEN_H) { mcy = SCREEN_H; }
            hover = hitTest(screen, mcx, mcy);
            if (hover >= 0 && hover < 10) {
                if (screen == S_TITLE) { if (!confirmOpen) titleSel = hover; } else if (screen == S_WORLDS) wsel = hover;
                else if (screen == S_SELECT) sel = hover; else if (screen == S_OPTIONS) optSel = hover;
            }
        }
        bool nav = !useCur;                                 // sin cursor: se navega con las direcciones
        bool act = pX && (!useCur || hover >= 0);           // con cursor: X actua sobre lo que esta debajo
        if (screen == S_TITLE) {
            if (confirmOpen) {                                              // ventana "¿seguro?" del guardado
                int cs = confirmSel; bool hv = false;
                if (useCur) {
                    if (mcx >= 200 && mcx <= 300 && mcy >= 262 && mcy <= 302) { confirmSel = 0; hv = true; }
                    else if (mcx >= 340 && mcx <= 440 && mcy >= 262 && mcy <= 302) { confirmSel = 1; hv = true; }
                }
                if (nav) { if (pad.eL) confirmSel = 0; if (pad.eR) confirmSel = 1; }
                if (confirmSel != cs) sndPlay(SND_CLICK);
                if (pO) { confirmOpen = false; sndPlay(SND_CLICK); }
                else if (pX && (!useCur || hv)) {
                    confirmOpen = false;
                    if (confirmSel == 0) {
                        saveOptions();
                        if (!mcOK) saveMsgText = "No se encontró la memory card";
                        else if (saveWrite()) saveMsgText = "¡Partida guardada!";
                        else saveMsgText = "No se pudo guardar";
                        saveMsgTimer = 3.0f;
                    }
                    sndPlay(SND_CLICK);
                }
            } else {
                static const int ORD[3] = { 2, 0, 1 };                      // de izquierda a derecha: disquete, jugar, opciones
                if (nav) {
                    int p = 1; for (int k = 0; k < 3; k++) if (ORD[k] == titleSel) p = k;
                    if (pad.eR && p < 2) titleSel = ORD[p + 1];
                    if (pad.eL && p > 0) titleSel = ORD[p - 1];
                }
                if (pStart) { sndPlay(SND_CLICK); screen = S_WORLDS; }
                else if (act) {
                    sndPlay(SND_CLICK);
                    if (titleSel == 0) screen = S_WORLDS;
                    else if (titleSel == 1) { screen = S_OPTIONS; optSel = 0; }
                    else { confirmOpen = true; confirmSel = 1; }
                }
            }
        } else if (screen == S_OPTIONS) {
            if (nav) {
                if (pad.eU && optSel > 0) { optSel--; sndPlay(SND_CLICK); }
                if (pad.eD && optSel < 4) { optSel++; sndPlay(SND_CLICK); }
            }
            if (pO || (act && hover == 10)) { sndPlay(SND_CLICK); saveOptions(); screen = S_TITLE; }
            else if (act && hover < 10) {
                if (optSel == 0) optCursor = !optCursor;
                else if (optSel == 1) optTouch = !optTouch;
                else if (optSel == 2) optMusic = !optMusic;
                else if (optSel == 3) optSfx = !optSfx;
                else { saveOptions(); screen = S_TITLE; }
                sndPlay(SND_CLICK);
            }
        } else if (screen == S_WORLDS) {
            if (nav) {
                if (pad.eL && wsel > 0) { wsel--; sndPlay(SND_CLICK); }
                if (pad.eR && wsel < 3) { wsel++; sndPlay(SND_CLICK); }
            }
            if (pO || (act && hover == 10)) { sndPlay(SND_CLICK); screen = S_TITLE; }
            else if (act && hover < 10) {
                if (wsel == 0 || wsel == 1) { selWorld = wsel; sel = 0; sndPlay(SND_CLICK); screen = S_SELECT; }
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
            if (pO || (act && hover == 10)) { sndPlay(SND_CLICK); screen = S_WORLDS; }
            else if (act && hover < 10) {
                int li = Game::levelAt(selWorld, sel);
                if (li >= 0 && game.loadLevel(li)) { sndPlay(SND_CLICK); screen = S_GAME; }
                else { msgTimer = 3.0f; sndPlay(SND_LOSE); }
            }
        } else {   // S_GAME
            bool handled = false;
            const bool endPanel = (game.mode() == M_WIN || game.mode() == M_LOSE);
            bool cX = pX, cO = pO, cT = pT, cS = pS, toMenu = false;     // acciones efectivas: el cursor tactil tambien las genera
            if (optTouch && (g_paused || game.mode() == M_PLAY || endPanel)) {     // cursor tactil
                gcx += pad.ax * 320.f * dt; gcy += pad.ay * 320.f * dt;
                if (gcx < 0) { gcx = 0; }
                if (gcx > SCREEN_W) { gcx = SCREEN_W; }
                if (gcy < 0) { gcy = 0; }
                if (gcy > SCREEN_H) { gcy = SCREEN_H; }
            }
            if (g_paused) {                                              // menu de pausa
                int h = hudHit(game, true, gcx, gcy);
                if (pO || pStart) g_paused = false;
                else if (pX) {
                    if (h == 11) g_paused = false;
                    else if (h == 12) { bool on = optMusic || optSfx; optMusic = optSfx = !on; sndPlay(SND_CLICK); }
                    else if (h == 13) toMenu = true;
                }
                handled = true;
            } else if (optTouch) {                                       // modo tactil: botones de la pantalla
                float hx = gcx, hy = gcy; if (game.mode() == M_BUILD) game.cursor(hx, hy);
                int h = hudHit(game, false, hx, hy);
                if (pX && h) {
                    cX = false;                                          // este X lo consume el boton
                    if (h == 1) { g_paused = true; gcx = 100.f; gcy = 150.f; }
                    else if (h == 2) { if (game.mode() == M_BUILD) { game.reset(); g_trash = false; } else cO = true; sndPlay(SND_CLICK); }   // reiniciar: limpia la construccion / vuelve a construir
                    else if (h == 3) { g_trash = !g_trash; sndPlay(SND_CLICK); }
                    else if (h == 4) cT = true;                          // tilde: arrancar
                } else if (game.mode() == M_BUILD && g_trash && pX) {    // modo tacho: X sobre una casilla borra su pieza
                    if (game.trayHit(hx, hy) >= 0) g_trash = false; else { cX = false; cS = true; }
                }
                if (pStart) { g_paused = true; gcx = 100.f; gcy = 150.f; }
                if (endPanel) {                                          // cuadro de fin de nivel: filas clicables
                    int row = endRowAt(game.mode() == M_WIN, gcx, gcy);
                    cX = false;
                    if (pX && row == 1) cX = true; else if (pX && row == 2) cO = true; else if (pX && row == 3) toMenu = true;
                }
            }
            if (toMenu || (pT && endPanel) || (!optTouch && pStart)) {
                if (game.isSandbox()) screen = S_WORLDS; else { selWorld = game.worldNow(); sel = Game::slotOf(game.levelIndex()); screen = S_SELECT; }
                g_paused = false; handled = true;
            }
            else if (!handled && game.mode() == M_WIN && cX) {
                int w = game.worldNow(), slot = Game::slotOf(game.levelIndex()) + 1, next = Game::levelAt(w, slot);
                if (next >= 0 && game.loadLevel(next)) { sndPlay(SND_CLICK); }
                else { selWorld = w; sel = slot < 15 ? slot : 14; screen = S_SELECT; msgTimer = 3.0f; keepMsg = true; }
                handled = true;
            }
            if (!handled && game.mode() == M_PLAY && !optTouch) {         // modo fisico: R1 enciende/apaga el ventilador, L1 pincha un globo
                if (pad.edge & PAD_R1) game.toggleFans();
                if (pad.edge & PAD_L1) game.popOneBalloon();
            }
            if (!handled && game.mode() == M_PLAY && optTouch) {          // modo tactil: botones azules de los dispositivos
                if (cX) { int b = playButtonAt(game, gcx, gcy); if (b == 0) game.toggleFans(); else if (b == 1) game.popOneBalloon(); }
            }
            if (game.mode() != M_BUILD) g_trash = false;
            if (!handled) {
                GameInput in;
                in.stickX = pad.ax; in.stickY = pad.ay; in.lookX = pad.lx; in.lookY = pad.ly;
                in.pressX = cX; in.pressSquare = cS; in.pressTriangle = cT; in.pressCircle = cO;
                game.update(dt, in);
                if (game.mode() == M_PLAY && !wasPlaying) { gcx = 320.f; gcy = 300.f; }
                wasPlaying = (game.mode() == M_PLAY);
                unsigned ev = game.takeEvents();
                if (ev & EV_WIN) { sndPlay(SND_WIN); recordWin(game); }
                else if (ev & EV_STAR) sndPlay(SND_STAR);
                else if (ev & EV_LOSE) sndPlay(SND_LOSE);
                else if (ev & EV_GO) sndPlay(SND_GO);
                else if (ev & EV_BAD) sndPlay(SND_CLICK);
                else if (ev & EV_PLACE) sndPlay(SND_PLACE);
                else if (ev & EV_CLICK) sndPlay(SND_CLICK);
            }
        }
        if (screen != S_GAME) { g_paused = false; g_trash = false; }
        if (screen != prevScreen) { if (!keepMsg) msgTimer = 0.f; keepMsg = false; prevScreen = screen; }
        // musica segun la pantalla (como el juego original): menus = main, nivel = hai, victoria = final
        if (screen != S_GAME) musicWant(MUS_MAIN);
        else musicWant(game.mode() == M_WIN ? MUS_FINAL : MUS_HAI);
        sndPump();

        // ---------------- dibujo ----------------
        if (screen == S_GAME) terrainLoad(game);
        if (screen == S_TITLE || screen == S_OPTIONS) screenLoad(SCR_TITLE);
        else if (screen == S_WORLDS) screenLoad(SCR_WORLDS);
        else if (screen == S_SELECT) screenLoad(selWorld == 1 ? SCR_LEVELS2 : SCR_LEVELS);
        gsKit_clear(gs, rgba(0xB4, 0xDC, 0xFF, 0x80));   // alfa 0x80 = opaco (con 0x00 no se borra la pantalla)
        if (screen == S_TITLE) {
            screenDraw(SCR_TITLE);                                           // pantalla de inicio original
            rect(400, 271, 560, 311, rgba(0x6C, 0xBF, 0x3A, 0x80));          // boton "Opciones" (el original no lo tiene)
            frameRect(400, 271, 560, 311, 3.f, rgba(0x3A, 0x7A, 0x22, 0x80));
            textCentered(false, 480, 275, "Opciones", 1.0f, 255, 255, 255);
            drawFloppy(40, 330, titleSel == 2 && !confirmOpen);                // boton de guardar (disquete)
            if (!confirmOpen) {
                float pulse = 2.f + 2.f * sinf(clock * 6.f);
                if (titleSel == 0) ring(335, 234, 58.f + pulse, 4.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                else if (titleSel == 1) frameRect(400 - pulse, 271 - pulse, 560 + pulse, 311 + pulse, 4.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                if (((int)(clock * 2.f)) % 2 == 0) textCentered(false, 320, 330, titleSel == 0 ? "Pulsá X para jugar" : (titleSel == 1 ? "Pulsá X para abrir las opciones" : "Pulsá X: guardar"), 1.2f, 255, 255, 255);
            } else {
                rect(0, 24, SCREEN_W, 424, rgba(0x00, 0x00, 0x00, 0x50));
                rect(90, 140, 550, 320, rgba(0x10, 0x20, 0x30, 0x78));
                frameRect(90, 140, 550, 320, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                if (saveExists) { textCentered(false, 320, 158, "¿Seguro que querés sobrescribir", 1.05f, 255, 255, 255); textCentered(false, 320, 194, "los datos ya guardados?", 1.05f, 255, 255, 255); }
                else { textCentered(false, 320, 158, "¿Querés guardar la partida", 1.05f, 255, 255, 255); textCentered(false, 320, 194, "en la memory card?", 1.05f, 255, 255, 255); }
                rect(200, 262, 300, 302, confirmSel == 0 ? rgba(0x40, 0xB0, 0x40, 0x80) : rgba(0x30, 0x50, 0x30, 0x80));
                rect(340, 262, 440, 302, confirmSel == 1 ? rgba(0xC0, 0x50, 0x50, 0x80) : rgba(0x50, 0x30, 0x30, 0x80));
                frameRect(confirmSel == 0 ? 200 : 340, 262, confirmSel == 0 ? 300 : 440, 302, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                textCentered(false, 250, 268, "Sí", 1.1f, 255, 255, 255); textCentered(false, 390, 268, "No", 1.1f, 255, 255, 255);
            }
            if (saveMsgTimer > 0) {
                rect(70, 90, 570, 150, rgba(0x10, 0x20, 0x30, 0x78));
                frameRect(70, 90, 570, 150, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                textCentered(false, 320, 102, saveMsgText, 1.1f, 255, 240, 120);
            }
        } else if (screen == S_WORLDS) {
            screenDraw(SCR_WORLDS);                                          // selector de mundos original
            rect(MENU_PX[2], 24 + 40, MENU_PX[2] + 102, 24 + 328, rgba(0x10, 0x20, 0x30, 0x48));   // velo: solo el mundo que sigue bloqueado
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
            screenDraw(selWorld == 1 ? SCR_LEVELS2 : SCR_LEVELS);            // selector de niveles original (el 2.o mundo empieza en 16)
            for (int i = 0; i < 15; i++) {
                float cx = MENU_TX[i % 5], cy = 24 + MENU_TY[i / 5];
                if (i >= Game::levelCount(selWorld) && selWorld == 0) {        // nivel que no existe: candado (en el 2.o mundo el dibujo ya los trae)
                    rect(cx - 33, cy - 33, cx + 33, cy + 33, rgba(0x6C, 0xBF, 0x3A, 0x80));
                    frameRect(cx - 12, cy - 30, cx + 12, cy - 4, 5.f, rgba(0xC8, 0xCC, 0xD2, 0x80));
                    rect(cx - 19, cy - 8, cx + 19, cy + 22, rgba(0xF0, 0xC8, 0x20, 0x80));
                    rect(cx - 3, cy + 3, cx + 3, cy + 15, rgba(0x60, 0x48, 0x10, 0x80));
                } else if (i >= Game::levelCount(selWorld)) { }
                else { int li = Game::levelAt(selWorld, i); for (int k = 0; li >= 0 && k < (int)sv.stars[li] && k < 3; k++) sprite(texDecor[DEC_GOLDSTAR], cx + (k - 1) * 17.f, cy + 47, 17, 16, 0); }   // estrellas ganadas
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
            static const char* LABEL[5] = { "Cursor en menús", "Modo de control", "Música", "Efectos de sonido", "Volver" };
            for (int r = 0; r < 5; r++) {
                float y = OPT_ROWY[r];
                if (r == optSel) {
                    float pulse = 1.5f + 1.5f * sinf(clock * 6.f);
                    frameRect(118 - pulse, y - 6 - pulse, 522 + pulse, y + 30 + pulse, 3.f, rgba(0xFF, 0xE0, 0x20, 0x80));
                }
                textLeft(135, y, LABEL[r], 1.0f, 255, 255, 255);
                if (r == 0) { bool on = optCursor || optTouch; textLeft(420, y, optTouch ? "Sí (táctil)" : (optCursor ? "Sí" : "No"), 1.0f, on ? 140 : 255, on ? 255 : 140, 140); }
                else if (r == 1) textLeft(420, y, optTouch ? "Táctil" : "Físico", 1.0f, 140, 220, 255);
                else if (r == 2) textLeft(420, y, optMusic ? "Sí" : "No", 1.0f, optMusic ? 140 : 255, optMusic ? 255 : 140, 140);
                else if (r == 3) textLeft(420, y, optSfx ? "Sí" : "No", 1.0f, optSfx ? 140 : 255, optSfx ? 255 : 140, 140);
            }
            rect(16, 374, 94, 412, rgba(0x6C, 0xBF, 0x3A, 0x80));              // flecha de volver
            frameRect(16, 374, 94, 412, 3.f, rgba(0x3A, 0x7A, 0x22, 0x80));
            tri(30, 393, 52, 380, 52, 406, rgba(0xFF, 0xFF, 0xFF, 0x80)); rect(50, 389, 80, 397, rgba(0xFF, 0xFF, 0xFF, 0x80));
            textCentered(false, 320, 314, optTouch ? "Táctil: cursor y botones azules en pantalla" : "Físico: R1 ventilador, L1 pinchar globo", 0.6f, 200, 220, 255);
            textCentered(false, 320, 340, mcOK ? "Guardado manual: botón del disquete en el inicio" : "Sin memory card: no se puede guardar", 0.6f, mcOK ? 170 : 255, mcOK ? 255 : 170, 170);
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
            {   // caja estrella
                float bx, by;
                if (game.starBox(bx, by)) {
                    float sx, sy; worldToScreen(game, bx, by, sx, sy);
                    GSTEXTURE& tb = texDecor[DEC_STARBOX];
                    sprite(tb, sx, sy + sinf(clock * 3.f) * 3.f, tb.Width * 1.1f * (game.zoomNow() / ZOOM), tb.Height * 1.1f * (game.zoomNow() / ZOOM), 0.f);
                }
            }
            static DrawItem items[768]; int n = game.drawList(items, 768);
            for (int i = 0; i < n; i++) {
                float sx, sy; worldToScreen(game, items[i].x, items[i].y, sx, sy);
                if (items[i].type == P_FAN) { float cs = 31.f * game.zoomNow() * 0.98f; sprite(texDecor[DEC_FAN], sx, sy, cs, cs, items[i].angle); continue; }
                spriteOf(items[i].type, sx, sy, (items[i].type == P_PIG ? 0.8f : (items[i].type == P_FAN ? 0.9f : 1.f)) * (game.zoomNow() / ZOOM), items[i].angle);
            }
            drawGameUI(game);
            if (optTouch && !g_paused) { float hx = gcx, hy = gcy; if (game.mode() == M_BUILD) game.cursor(hx, hy); drawTouchHud(game, hx, hy); }
            if (g_paused) drawPauseMenu(game);
            {   // cursor: en construccion es el del juego; en el resto (modo tactil) el de la partida
                float cx = gcx, cy = gcy; bool show = false;
                if (game.mode() == M_BUILD && !g_paused) { game.cursor(cx, cy); show = true; }
                else if (optTouch && (g_paused || game.mode() == M_PLAY || game.mode() == M_WIN || game.mode() == M_LOSE)) show = true;
                if (show) {
                    tri(cx, cy, cx + 14, cy + 6, cx + 6, cy + 14, rgba(0xFF, 0xFF, 0xFF, 0x80));
                    tri(cx + 1, cy + 1, cx + 11, cy + 6, cx + 6, cy + 11, rgba(0x20, 0x20, 0x20, 0x80));
                }
            }
        }
        if ((optCursor || optTouch) && screen != S_GAME) {   // cursor de los menus (siempre visible cuando los menus lo usan: modo tactil o la opcion del cursor)
            tri(mcx, mcy, mcx + 14, mcy + 6, mcx + 6, mcy + 14, rgba(0xFF, 0xFF, 0xFF, 0x80));
            tri(mcx + 1, mcy + 1, mcx + 11, mcy + 6, mcx + 6, mcy + 11, rgba(0x20, 0x20, 0x20, 0x80));
        }
        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
