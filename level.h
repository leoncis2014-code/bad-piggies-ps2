// Formato de un nivel: terreno (anillos de puntos + triangulos para dibujar) y posiciones clave.
#pragma once
struct DecorItem {
    unsigned char type;                // DEC_* (ver decor.h)
    unsigned char layer;               // 0 = detras del terreno, 1 = delante
    float x, y;                        // punto de apoyo (centro de la base) en pixeles del nivel
    float scale, angle;
};

struct LevelData {
    int npts;                          // puntos de todos los anillos juntos
    const float (*pts)[2];             // x,y en pixeles del nivel
    const unsigned char* top;          // 1 si la arista i->i+1 (dentro de su anillo) es superficie (hierba)
    int nrings;
    const int* ringStart;              // ringStart[nrings] = npts
    int ntris;
    const unsigned short (*tris)[3];
    float gridX0, gridYB;              // rejilla de construccion: centro de la columna 0 y de la fila inferior
    float goalX, goalY;                // meta: x y suelo
    float deathY;                      // por debajo de esto se pierde
    int ndecor;
    const DecorItem* decor;
    int texW, texH;                    // textura de la roca (la mitad de resolucion)
    float texOX, texOY, texScale;      // u = (x - texOX) * texScale, v = (y - texOY) * texScale
};
