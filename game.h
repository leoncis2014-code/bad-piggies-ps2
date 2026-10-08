// Nucleo del juego (portable). Sin dependencias de PS2: se prueba en PC y se usa igual en la consola.
#pragma once
#include <vector>
#include "level.h"

enum PieceType { P_PIG = 0, P_BOX = 1, P_WHEEL = 2, P_COUNT = 3 };
enum Mode { M_BUILD, M_PLAY, M_WIN, M_LOSE };

struct GameInput {
    float stickX = 0, stickY = 0;          // -1..1 (cursor)
    float lookX = 0, lookY = 0;            // -1..1 (camara libre, analogico derecho)
    bool pressX = false, pressSquare = false, pressTriangle = false, pressCircle = false; // flancos
};

enum GameEvent { EV_CLICK = 1, EV_PLACE = 2, EV_GO = 4, EV_WIN = 8, EV_LOSE = 16 };

struct DrawItem { int type; float x, y, angle; };   // en pixeles de nivel, angulo en radianes (horario en pantalla)

// Pantalla virtual 640x448; zoom del mundo: 2 pixeles de pantalla por pixel de nivel.
static const float SCREEN_W = 640.f, SCREEN_H = 448.f, ZOOM = 2.f;

class GameImpl;
class Game {
public:
    Game();
    ~Game();
    void reset();                              // vuelve al modo construccion
    bool loadLevel(int index);                 // false si ese nivel todavia no existe
    int  levelIndex() const;
    static int levelCount();                   // niveles disponibles
    const LevelData& levelData() const;
    unsigned takeEvents();                     // eventos de sonido desde la ultima llamada
    void update(float dt, const GameInput& in);
    int  drawList(DrawItem* out, int max) const;
    Mode mode() const;
    void cursor(float& x, float& y) const;
    void camera(float& x, float& y) const;     // centro de la camara en pixeles de nivel
    int  selected() const;
    int  remaining(int type) const;
    bool cellHasPig(int col, int row) const;
    bool cellPiece(int col, int row, int& type) const;   // fila 0 arriba, fila 1 abajo
    void cellScreen(int col, int row, float& sx, float& sy) const;
    void traySlot(int i, float& sx, float& sy) const;     // centro del icono de bandeja en pantalla
    float time() const;
private:
    GameImpl* g;
};
