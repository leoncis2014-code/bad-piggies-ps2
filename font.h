// Generado por tools/gen_font.py
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { int code, x, y, w, h, xoff, yoff, adv; } Glyph;
typedef struct { int tw, th, nglyphs, lineh; const Glyph* g; const unsigned char* a; } FontData;
extern const FontData FONT_SMALL, FONT_BIG;
#ifdef __cplusplus
}
#endif
