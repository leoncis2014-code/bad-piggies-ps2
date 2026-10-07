#include "game.h"
#include "level1_data.h"
#include <box2d/box2d.h>
#include <math.h>
#include <string.h>

static const float CELL = 31.f;           // px por celda (= 1 m)
static const int   COLS = 3, ROWS = 2;
static const float WHEEL_R = 0.355f;     // radio de la rueda (m)
static const float WHEEL_DROP = 0.19f;   // la rueda cuelga: su fondo queda al nivel del fondo de la celda (m)
static const float TRAY_Y = 400.f, TRAY_X0 = 250.f, TRAY_DX = 70.f, TRAY_R = 28.f;

struct Piece { int type; int col, row; };

class GameImpl {
public:
    int cells[ROWS][COLS];      // base: -1 vacio, P_BOX o P_WHEEL
    bool pig[ROWS][COLS];       // cerdito colocado dentro de una caja
    int left[P_COUNT];
    int sel = P_BOX;
    Mode mode = M_BUILD;
    float cx = 320, cy = 224;
    float camx, camy;
    float t = 0, stuck = 0;
    b2World* world = nullptr;
    b2Body* chassis = nullptr;
    b2Body* wheels[ROWS*COLS];
    int nwheels = 0;
    int wheelCell[ROWS*COLS][2];

    GameImpl() { memset(wheels, 0, sizeof(wheels)); reset(true); }
    ~GameImpl() { delete world; }

    float gx() const { return L1_GRID_X0 + CELL; }                 // centro horizontal de la rejilla (px)
    float gy() const { return L1_GRID_YB - CELL * 0.5f; }          // centro vertical (px)
    float cellX(int col) const { return L1_GRID_X0 + col * CELL; }
    float cellY(int row) const { return L1_GRID_YB - (ROWS - 1 - row) * CELL; }

    void toScreen(float wx, float wy, float& sx, float& sy) const {
        sx = (wx - camx) * ZOOM + SCREEN_W * 0.5f;
        sy = (wy - camy) * ZOOM + SCREEN_H * 0.5f;
    }

    void reset(bool full) {
        delete world; world = nullptr; chassis = nullptr; nwheels = 0;
        if (full) {
            for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) { cells[r][c] = -1; pig[r][c] = false; }
            left[P_PIG] = 1; left[P_BOX] = 3; left[P_WHEEL] = 2; sel = P_BOX;
        }
        mode = M_BUILD; t = 0; stuck = 0;
        camx = gx(); camy = gy() - 10.f;
        cx = SCREEN_W * 0.5f; cy = SCREEN_H * 0.5f;
    }

    bool hasPig() const {
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) if (pig[r][c]) return true;
        return false;
    }

    int cellAt(float sx, float sy, int& col, int& row) const {
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
            float px, py; toScreen(cellX(c), cellY(r), px, py);
            if (fabsf(sx - px) <= CELL * ZOOM * 0.5f && fabsf(sy - py) <= CELL * ZOOM * 0.5f) { col = c; row = r; return 1; }
        }
        return 0;
    }
    int trayAt(float sx, float sy) const {
        for (int i = 0; i < P_COUNT; i++)
            if (fabsf(sx - (TRAY_X0 + i * TRAY_DX)) <= TRAY_R && fabsf(sy - TRAY_Y) <= TRAY_R) return i;
        return -1;
    }

    void startPlay() {
        world = new b2World(b2Vec2(0.f, 10.f));
        // terreno
        b2BodyDef gd; b2Body* ground = world->CreateBody(&gd);
        b2Vec2 v[64]; int n = L1_NPTS < 64 ? L1_NPTS : 64;
        for (int i = 0; i < n; i++) v[i].Set(L1_PTS[i][0] / PPM, L1_PTS[i][1] / PPM);
        b2ChainShape chain; chain.CreateLoop(v, n);
        b2FixtureDef gf; gf.shape = &chain; gf.friction = 0.6f; gf.restitution = 0.1f;
        ground->CreateFixture(&gf);
        // chasis: una sola pieza rigida con las cajas y el cerdito
        b2BodyDef cd; cd.type = b2_dynamicBody; cd.position.Set(gx() / PPM, gy() / PPM);
        cd.allowSleep = false;
        chassis = world->CreateBody(&cd);
        nwheels = 0;
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
            int tp = cells[r][c];
            if (tp != P_BOX) continue;
            float lx = (c - 1) * 1.f, ly = (r - 0.5f) * 1.f;
            b2PolygonShape box; box.SetAsBox(0.46f, 0.46f, b2Vec2(lx, ly), 0.f);
            b2FixtureDef fd; fd.shape = &box; fd.density = 1.f; fd.friction = 0.5f; fd.restitution = 0.1f;
            chassis->CreateFixture(&fd);
            if (pig[r][c]) {
                b2PolygonShape ps; ps.SetAsBox(0.28f, 0.28f, b2Vec2(lx, ly), 0.f);
                b2FixtureDef pf; pf.shape = &ps; pf.density = 0.8f; pf.friction = 0.2f; pf.restitution = 0.1f;
                chassis->CreateFixture(&pf);
            }
        }
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
            if (cells[r][c] != P_WHEEL) continue;
            b2BodyDef wd; wd.type = b2_dynamicBody; wd.allowSleep = false;
            wd.position.Set(cellX(c) / PPM, cellY(r) / PPM + WHEEL_DROP);
            b2Body* w = world->CreateBody(&wd);
            b2CircleShape cs; cs.m_radius = WHEEL_R;
            b2FixtureDef fd; fd.shape = &cs; fd.density = 0.6f; fd.friction = 0.9f; fd.restitution = 0.1f;
            w->CreateFixture(&fd);
            b2RevoluteJointDef jd; jd.Initialize(chassis, w, w->GetPosition());
            world->CreateJoint(&jd);
            wheels[nwheels] = w; wheelCell[nwheels][0] = c; wheelCell[nwheels][1] = r; nwheels++;
        }
        // empujon inicial para salir de la plataforma
        b2Vec2 kick(4.0f, 0.f);
        chassis->SetLinearVelocity(kick);
        for (int i = 0; i < nwheels; i++) wheels[i]->SetLinearVelocity(kick);
        mode = M_PLAY; t = 0; stuck = 0;
    }

    b2Vec2 pigWorld() const {
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++)
            if (pig[r][c]) return chassis->GetWorldPoint(b2Vec2((c - 1) * 1.f, (r - 0.5f) * 1.f));
        return chassis->GetPosition();
    }

    void update(float dt, const GameInput& in) {
        if (mode == M_BUILD) {
            cx += in.stickX * 320.f * dt; cy += in.stickY * 320.f * dt;
            if (cx < 0) { cx = 0; } if (cx > SCREEN_W) { cx = SCREEN_W; }
            if (cy < 0) { cy = 0; } if (cy > SCREEN_H) { cy = SCREEN_H; }
            int col, row;
            if (in.pressX) {
                int ti = trayAt(cx, cy);
                if (ti >= 0) sel = ti;
                else if (cellAt(cx, cy, col, row) && left[sel] > 0) {
                    if (sel == P_PIG) {
                        if (cells[row][col] == P_BOX && !pig[row][col]) { pig[row][col] = true; left[P_PIG]--; }
                    } else if (cells[row][col] < 0) { cells[row][col] = sel; left[sel]--; }
                }
            }
            if (in.pressSquare && cellAt(cx, cy, col, row)) {
                if (pig[row][col]) { pig[row][col] = false; left[P_PIG]++; }
                else if (cells[row][col] >= 0) { left[cells[row][col]]++; cells[row][col] = -1; }
            }
            if (in.pressTriangle && hasPig()) startPlay();
            return;
        }
        if (in.pressCircle) { reset(false); return; }
        if (mode != M_PLAY) return;
        world->Step(dt, 8, 3);
        t += dt;
        b2Vec2 p = pigWorld();
        float px = p.x * PPM, py = p.y * PPM;
        // camara suave
        camx += (px - camx) * 0.08f; camy += (py - 20.f - camy) * 0.08f;
        if (px >= L1_GOAL_X) { mode = M_WIN; return; }
        if (py > L1_DEATH_Y || t > 30.f) { mode = M_LOSE; return; }
        if (t > 4.f) {
            float sp = chassis->GetLinearVelocity().Length();
            stuck = (sp < 0.3f) ? stuck + dt : 0.f;
            if (stuck > 2.5f) mode = M_LOSE;
        }
    }

    int draw(DrawItem* out, int max) const {
        int n = 0;
        bool playing = (mode != M_BUILD) && chassis;
        // pasada 1: cajas y ruedas (build) | pasada 2: cerditos
        for (int pass = 0; pass < 2; pass++)
            for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
                if (n >= max) break;
                float lx = (c - 1) * 1.f, ly = (r - 0.5f) * 1.f;
                if (pass == 0) {
                    int tp = cells[r][c];
                    if (tp < 0) continue;
                    if (playing) {
                        if (tp == P_WHEEL) continue;                  // las ruedas se dibujan aparte
                        b2Vec2 w = chassis->GetWorldPoint(b2Vec2(lx, ly));
                        out[n].type = tp; out[n].x = w.x * PPM; out[n].y = w.y * PPM; out[n].angle = chassis->GetAngle(); n++;
                    } else {
                        out[n].type = tp; out[n].x = cellX(c); out[n].y = cellY(r) + (tp == P_WHEEL ? WHEEL_DROP * PPM : 0.f); out[n].angle = 0; n++;
                    }
                } else if (pig[r][c]) {
                    if (playing) {
                        b2Vec2 w = chassis->GetWorldPoint(b2Vec2(lx, ly));
                        out[n].type = P_PIG; out[n].x = w.x * PPM; out[n].y = w.y * PPM; out[n].angle = chassis->GetAngle(); n++;
                    } else { out[n].type = P_PIG; out[n].x = cellX(c); out[n].y = cellY(r); out[n].angle = 0; n++; }
                }
            }
        if (playing)
            for (int i = 0; i < nwheels && n < max; i++) {
                b2Vec2 w = wheels[i]->GetPosition();
                out[n].type = P_WHEEL; out[n].x = w.x * PPM; out[n].y = w.y * PPM; out[n].angle = wheels[i]->GetAngle(); n++;
            }
        return n;
    }
};

Game::Game() { g = new GameImpl(); }
Game::~Game() { delete g; }
void Game::reset() { g->reset(true); }
void Game::update(float dt, const GameInput& in) { g->update(dt, in); }
int Game::drawList(DrawItem* out, int max) const { return g->draw(out, max); }
Mode Game::mode() const { return g->mode; }
void Game::cursor(float& x, float& y) const { x = g->cx; y = g->cy; }
void Game::camera(float& x, float& y) const { x = g->camx; y = g->camy; }
int Game::selected() const { return g->sel; }
int Game::remaining(int t) const { return g->left[t]; }
bool Game::cellPiece(int col, int row, int& type) const { type = g->cells[row][col]; return type >= 0; }
bool Game::cellHasPig(int col, int row) const { return g->pig[row][col]; }
void Game::cellScreen(int col, int row, float& sx, float& sy) const { g->toScreen(g->cellX(col), g->cellY(row), sx, sy); }
void Game::traySlot(int i, float& sx, float& sy) const { sx = TRAY_X0 + i * TRAY_DX; sy = TRAY_Y; }
float Game::time() const { return g->t; }
