// Generado por tools/gen_decor.py
#pragma once
enum { DEC_TUFT0, DEC_TUFT1, DEC_TUFT2, DEC_TUFT3, DEC_FLOWER0, DEC_FLOWER1, DEC_SHELL, DEC_STAR, DEC_LOG, DEC_BUSH, DEC_COUNT };
typedef struct { int w, h, offset; } DecorTex;   // offset en bytes dentro de decor.raw
static const DecorTex DECOR_TEX[DEC_COUNT] = {
  { 55, 36, 0 },
  { 43, 37, 7920 },
  { 51, 38, 14284 },
  { 54, 39, 22036 },
  { 53, 46, 30460 },
  { 55, 39, 40212 },
  { 67, 34, 48792 },
  { 85, 45, 57904 },
  { 102, 46, 73204 },
  { 154, 156, 91972 }
};
