// Formato de un nivel: terreno (anillos de puntos + triangulos para dibujar) y posiciones clave.
#pragma once
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
};
