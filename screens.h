// Generado por tools/gen_screens.py
#pragma once
enum { SCR_TITLE, SCR_WORLDS, SCR_LEVELS, SCR_COUNT };
static const int SCR_W = 640, SCR_H = 400;
// color de las franjas de arriba y abajo (la imagen es 16:10 y la pantalla es casi 4:3)
static const int SCR_BARS[SCR_COUNT][6] = {
  { 144, 145, 144, 66, 171, 156 },
  { 144, 145, 144, 66, 176, 160 },
  { 144, 145, 144, 68, 186, 170 }
};
