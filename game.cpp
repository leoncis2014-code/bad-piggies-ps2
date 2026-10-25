#include "game.h"
#include "levels_data.h"
#include <box2d/box2d.h>
#include <math.h>
#include <string.h>

static const float CELL = 31.f;           // px por celda (= 1 m)
static const int   MAXC = 16, MAXR = 16;          // tamano maximo de la rejilla (el sandbox usa 16x16; los niveles, 3x2)
static const float BUILD_ZOOM = 0.65f;             // zoom de construccion en el sandbox (para que entren las 16x16 casillas)
static const float WHEEL_R = 0.355f;     // radio de la rueda (m)
static const float WHEEL_DROP = 0.25f;   // la rueda cuelga: su fondo queda al nivel del fondo de la celda (m)
static const float TRAY_Y = 380.f, TRAY_DX = 70.f, TRAY_R = 28.f;
static float trayX(int i, int n) { return 320.f + ((float)i - (float)(n - 1) * 0.5f) * TRAY_DX; }

struct Piece { int type; int col, row; };

class GameImpl {
public:
    int cells[MAXR][MAXC];      // base: -1 vacio, P_BOX o P_WHEEL
    bool pig[MAXR][MAXC];       // cerdito colocado dentro de una caja
    int left[P_COUNT];
    int sel = P_BOX;
    Mode mode = M_BUILD;
    float cx = 320, cy = 224;
    float camx, camy;
    float t = 0, stuck = 0, flip = 0;
    float lookOX = 0, lookOY = 0;
    bool starGot = false; float finalT = 0; int resStars = 0;
    float totalMass = 1.f;                  // masa del vehiculo (chasis + ruedas), para escalar el empuje           // desplazamiento de la camara libre (px)
    const LevelData* L = &LEVELS[0];
    int levelIdx = 0;
    unsigned events = 0;
    b2World* world = nullptr;
    b2Body* chassis = nullptr;
    b2Body* wheels[MAXR*MAXC];
    int nwheels = 0;
    bool fansOn = false;
    unsigned char rot[MAXR][MAXC];            // orientacion de cada ventilador (0..3 = 0, 90, 180, 270 grados)
    struct Seg { float x1, y1, x2, y2; };
    std::vector<Seg> segs;                    // aristas del terreno: los globos revientan al tocarlo
    static constexpr float FAN_N = 6.0f;      // empuje de cada ventilador (newtons): no depende del tamano del vehiculo
    bool popped[MAXR][MAXC];
    b2Fixture* thrFx[MAXR][MAXC];
    int wheelCell[MAXR*MAXC][2];

    GameImpl() { memset(wheels, 0, sizeof(wheels)); reset(true); }
    ~GameImpl() { delete world; }

    int cols() const { return L->gridCols; }
    int rows() const { return L->gridRows; }
    float lxOf(int c) const { return (float)c - (float)(cols() - 1) * 0.5f; }      // posicion de la celda respecto del centro de la rejilla (m)
    float lyOf(int r) const { return (float)r - (float)(rows() - 1) * 0.5f; }
    bool viewerOn = false;                    // modo visor (lupa): se ve todo el nivel
    float vz = 1.f, vcx = 0.f, vcy = 0.f;
    void computeViewer() {
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (int i = 0; i < L->npts; i++) { float x = L->pts[i][0], y = L->pts[i][1]; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        if (L->starX > -1e8f) { if (L->starX < x0) x0 = L->starX; if (L->starX > x1) x1 = L->starX; if (L->starY < y0) y0 = L->starY; }
        if (L->goalY0 > -1e8f) { /* la meta ya esta dentro del terreno */ }
        float w = x1 - x0, h = y1 - y0; if (w < 1.f) w = 1.f; if (h < 1.f) h = 1.f;
        float zw = 590.f / w, zh = 330.f / h; vz = zw < zh ? zw : zh; if (vz > ZOOM) vz = ZOOM;
        vcx = (x0 + x1) * 0.5f; vcy = (y0 + y1) * 0.5f + 14.f / vz;
    }
    float zoom() const {                      // al construir se aleja lo necesario para que entre toda la rejilla
        if (viewerOn) return vz;
        if (mode != M_BUILD) return ZOOM;
        float z = 420.f / ((float)cols() * CELL);       // deja lugar a los botones del costado
        return z < ZOOM ? z : ZOOM;
    }
    float gx() const { return L->gridX0 + (cols() - 1) * 0.5f * CELL; }                 // centro horizontal de la rejilla (px)
    float gy() const { return L->gridYB - (rows() - 1) * 0.5f * CELL; }          // centro vertical (px)
    float cellX(int col) const { return L->gridX0 + col * CELL; }
    float cellY(int row) const { return L->gridYB - (rows() - 1 - row) * CELL; }

    void toScreen(float wx, float wy, float& sx, float& sy) const {
        sx = (wx - (viewerOn ? vcx : camx)) * zoom() + SCREEN_W * 0.5f;
        sy = (wy - (viewerOn ? vcy : camy)) * zoom() + SCREEN_H * 0.5f;
    }

    void reset(bool full) {
        viewerOn = false;
        delete world; world = nullptr; chassis = nullptr; nwheels = 0;
        if (full) {
            for (int r = 0; r < MAXR; r++) for (int c = 0; c < MAXC; c++) { cells[r][c] = -1; pig[r][c] = false; rot[r][c] = 0; }
            for (int k = 0; k < P_COUNT; k++) left[k] = L->pieces[k];
            sel = L->pieces[P_BOX] > 0 ? P_BOX : typeAtSlot(0);
        }
        mode = M_BUILD; t = 0; stuck = 0; flip = 0; lookOX = lookOY = 0; starGot = false; resStars = 0; finalT = 0;
        camx = gx(); camy = gy() - 10.f;
        fansOn = false;                       // los ventiladores arrancan apagados: se encienden con su boton
        for (int r = 0; r < MAXR; r++) for (int c = 0; c < MAXC; c++) { popped[r][c] = false; thrFx[r][c] = nullptr; }
        cx = SCREEN_W * 0.5f; cy = SCREEN_H * 0.5f;
    }

    // ---- reglas de construccion: todo debe estar pegado a una caja (o al cerdito)
    bool isAnchor(int r, int c) const { return r >= 0 && c >= 0 && r < rows() && c < cols() && (cells[r][c] == P_BOX || pig[r][c]); }
    bool adjAnchor(int r, int c) const { return isAnchor(r - 1, c) || isAnchor(r + 1, c) || isAnchor(r, c - 1) || isAnchor(r, c + 1); }
    int anchorCount() const { int n = 0; for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++) if (isAnchor(r, c)) n++; return n; }
    bool canPlace(int type, int r, int c) const {
        if (type == P_PIG) {
            if (pig[r][c]) return false;
            if (cells[r][c] == P_BOX) return true;                      // adentro de una caja
            if (cells[r][c] >= 0) return false;
            return anchorCount() == 0 || adjAnchor(r, c);
        }
        if (cells[r][c] >= 0) return false;
        if (type == P_BOX) return anchorCount() == 0 || adjAnchor(r, c);
        return adjAnchor(r, c);                                          // ruedas, ventiladores y globos: solo pegados a una caja
    }
    void cleanup() {                                                    // quita (y devuelve a la bandeja) lo que quedo suelto
        static bool seen[MAXR][MAXC], keep[MAXR][MAXC];
        for (int r = 0; r < MAXR; r++) for (int c = 0; c < MAXC; c++) { seen[r][c] = false; keep[r][c] = false; }
        int sr = -1, sc = -1;
        for (int r = 0; r < rows() && sr < 0; r++) for (int c = 0; c < cols(); c++) if (pig[r][c]) { sr = r; sc = c; break; }
        for (int r = 0; r < rows() && sr < 0; r++) for (int c = 0; c < cols(); c++) if (cells[r][c] == P_BOX) { sr = r; sc = c; break; }
        if (sr >= 0) {
            static int qr[MAXR * MAXC], qc[MAXR * MAXC]; int qh = 0, qt = 0;
            seen[sr][sc] = true; qr[qt] = sr; qc[qt++] = sc;
            while (qh < qt) {
                int r = qr[qh], c = qc[qh++]; keep[r][c] = true;
                const int dr[4] = { -1, 1, 0, 0 }, dc[4] = { 0, 0, -1, 1 };
                for (int k = 0; k < 4; k++) { int nr = r + dr[k], nc = c + dc[k]; if (isAnchor(nr, nc) && !seen[nr][nc]) { seen[nr][nc] = true; qr[qt] = nr; qc[qt++] = nc; } }
            }
            for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++)          // las piezas pegadas a una caja conectada tambien se quedan
                if (cells[r][c] >= 0 && !isAnchor(r, c))
                    for (int k = 0; k < 4; k++) { int nr = r + (k == 0 ? -1 : (k == 1 ? 1 : 0)), nc = c + (k == 2 ? -1 : (k == 3 ? 1 : 0)); if (nr >= 0 && nc >= 0 && nr < rows() && nc < cols() && keep[nr][nc]) keep[r][c] = true; }
        }
        for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++) {
            if (keep[r][c]) continue;
            if (pig[r][c]) { pig[r][c] = false; left[P_PIG]++; }
            if (cells[r][c] >= 0) { left[cells[r][c]]++; cells[r][c] = -1; rot[r][c] = 0; }
        }
    }
    bool hasPig() const {
        for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++) if (pig[r][c]) return true;
        return false;
    }

    int cellAt(float sx, float sy, int& col, int& row) const {
        for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++) {
            float px, py; toScreen(cellX(c), cellY(r), px, py);
            if (fabsf(sx - px) <= CELL * zoom() * 0.5f && fabsf(sy - py) <= CELL * zoom() * 0.5f) { col = c; row = r; return 1; }
        }
        return 0;
    }
    int trayN() const { int n = 0; for (int k = 0; k < P_COUNT; k++) if (L->pieces[k] > 0) n++; return n; }   // solo las piezas que ofrece el nivel
    int typeAtSlot(int slot) const { int n = 0; for (int k = 0; k < P_COUNT; k++) if (L->pieces[k] > 0) { if (n == slot) return k; n++; } return -1; }
    int slotOfType(int type) const { int n = 0; for (int k = 0; k < P_COUNT; k++) if (L->pieces[k] > 0) { if (k == type) return n; n++; } return 0; }
    int trayAt(float sx, float sy) const {
        int n = trayN();
        for (int i = 0; i < n; i++)
            if (fabsf(sx - trayX(i, n)) <= TRAY_R && fabsf(sy - TRAY_Y) <= TRAY_R) return typeAtSlot(i);
        return -1;
    }

    void startPlay() {
        world = new b2World(b2Vec2(0.f, 10.f));
        // terreno
        b2BodyDef gd; b2Body* ground = world->CreateBody(&gd);
        for (int ri = 0; ri < L->nrings; ri++) {
            int a = L->ringStart[ri], b = L->ringStart[ri + 1], n = b - a;
            if (n < 3) continue;
            std::vector<b2Vec2> v(n);
            for (int i = 0; i < n; i++) v[i].Set(L->pts[a + i][0] / PPM, L->pts[a + i][1] / PPM);
            b2ChainShape chain; chain.CreateLoop(&v[0], n);
            b2FixtureDef gf; gf.shape = &chain; gf.friction = 0.6f; gf.restitution = 0.1f;
            ground->CreateFixture(&gf);
        }
        // chasis: una sola pieza rigida con las cajas y el cerdito
        segs.clear();                                              // aristas del terreno en metros
        for (int ri = 0; ri < L->nrings; ri++) { int a0 = L->ringStart[ri], b0 = L->ringStart[ri + 1]; for (int i = a0; i < b0; i++) { int j = (i + 1 < b0) ? i + 1 : a0; segs.push_back({ L->pts[i][0] / PPM, L->pts[i][1] / PPM, L->pts[j][0] / PPM, L->pts[j][1] / PPM }); } }
        b2BodyDef cd; cd.type = b2_dynamicBody; cd.position.Set(gx() / PPM, gy() / PPM);
        cd.allowSleep = false;
        chassis = world->CreateBody(&cd);
        nwheels = 0;
        for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++) {
            int tp = cells[r][c];
            float lx = lxOf(c), ly = lyOf(r);
            if (tp == P_FAN || tp == P_BALLOON) {   // piezas nuevas del sandbox: bloque liviano que ademas empuja o levanta
                b2PolygonShape fs; fs.SetAsBox(0.36f, 0.36f, b2Vec2(lx, ly), 0.f);
                b2FixtureDef ff; ff.shape = &fs; ff.density = 0.35f; ff.friction = 0.3f; ff.restitution = 0.1f;
                if (tp == P_BALLOON) { ff.isSensor = true; ff.density = 0.2f; }       // el globo no es solido: no choca ni estorba
                thrFx[r][c] = chassis->CreateFixture(&ff);
                continue;
            }
            if (tp != P_BOX) {
                if (tp < 0 && pig[r][c]) {   // cerdito suelto, sin caja: es un blanco facil
                    b2PolygonShape ps; ps.SetAsBox(0.40f, 0.40f, b2Vec2(lx, ly), 0.f);
                    b2FixtureDef pf; pf.shape = &ps; pf.density = 0.8f; pf.friction = 0.2f; pf.restitution = 0.1f;
                    chassis->CreateFixture(&pf);
                }
                continue;
            }
            // caja con las esquinas cortadas, para que no se enganche en los bordes del terreno
            const float a = 0.46f, k = 0.18f;
            b2Vec2 bv[8] = { b2Vec2(lx - a + k, ly - a), b2Vec2(lx + a - k, ly - a), b2Vec2(lx + a, ly - a + k), b2Vec2(lx + a, ly + a - k),
                             b2Vec2(lx + a - k, ly + a), b2Vec2(lx - a + k, ly + a), b2Vec2(lx - a, ly + a - k), b2Vec2(lx - a, ly - a + k) };
            b2PolygonShape box; box.Set(bv, 8);
            b2FixtureDef fd; fd.shape = &box; fd.density = 1.f; fd.friction = 0.5f; fd.restitution = 0.1f;
            chassis->CreateFixture(&fd);
            if (pig[r][c]) {
                b2PolygonShape ps; ps.SetAsBox(0.28f, 0.28f, b2Vec2(lx, ly), 0.f);
                b2FixtureDef pf; pf.shape = &ps; pf.density = 0.8f; pf.friction = 0.2f; pf.restitution = 0.1f;
                chassis->CreateFixture(&pf);
            }
        }
        for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++) {
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
        totalMass = chassis->GetMass();
        for (int i = 0; i < nwheels; i++) totalMass += wheels[i]->GetMass();
        if (totalMass < 0.5f) totalMass = 0.5f;
        // arranque: solo con 2 o mas ruedas el vehiculo sale rodando; sin ruedas se queda parado y pierdes
        if (nwheels >= 2) {
            b2Vec2 kick(3.5f, 0.f);
            chassis->SetLinearVelocity(kick);
            for (int i = 0; i < nwheels; i++) wheels[i]->SetLinearVelocity(kick);
        }
        mode = M_PLAY; t = 0; stuck = 0; events |= EV_GO;
    }

    void popBalloon() {
        for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++)
            if (cells[r][c] == P_BALLOON && !popped[r][c] && thrFx[r][c]) {
                popped[r][c] = true; chassis->DestroyFixture(thrFx[r][c]); thrFx[r][c] = nullptr; events |= EV_PLACE; return;
            }
    }
    b2Vec2 pigWorld() const {
        for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++)
            if (pig[r][c]) return chassis->GetWorldPoint(b2Vec2(lxOf(c), lyOf(r)));
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
                if (ti >= 0) { sel = ti; events |= EV_CLICK; }
                else if (cellAt(cx, cy, col, row)) {
                    if (sel == P_FAN && cells[row][col] == P_FAN) { rot[row][col] = (unsigned char)((rot[row][col] + 1) & 3); events |= EV_CLICK; }   // X sobre un ventilador: lo gira
                    else if (left[sel] > 0) {
                        if (canPlace(sel, row, col)) {
                            if (sel == P_PIG) pig[row][col] = true; else { cells[row][col] = sel; rot[row][col] = 0; }
                            left[sel]--; events |= EV_PLACE;
                        } else events |= EV_BAD;                                     // no se puede poner ahi (no toca ninguna caja)
                    }
                }
            }
            if (in.pressSquare && cellAt(cx, cy, col, row)) {
                if (pig[row][col]) { pig[row][col] = false; left[P_PIG]++; events |= EV_CLICK; }
                else if (cells[row][col] >= 0) { left[cells[row][col]]++; cells[row][col] = -1; rot[row][col] = 0; events |= EV_CLICK; }
                cleanup();
            }
            if (in.pressTriangle && hasPig()) startPlay();
            return;
        }
        if (in.pressCircle) { reset(false); return; }
        if (mode != M_PLAY) return;
        // ventilador: empuja hacia adelante; globo: levanta
        int nthr = 0;
        for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++) {
            int tp = cells[r][c];
            if (tp != P_FAN && tp != P_BALLOON) continue;
            if (tp == P_BALLOON && popped[r][c]) continue;
            nthr++;
            if (tp == P_FAN && !fansOn) continue;
            if (tp == P_FAN) { float a = chassis->GetAngle() + (float)rot[r][c] * 1.5707963f; chassis->ApplyForceToCenter(FAN_N * b2Vec2(cosf(a), sinf(a)), true); }   // empuje fijo en la direccion del ventilador
            else chassis->ApplyForceToCenter(b2Vec2(0.f, -L->balloonLift), true);                                                                          // cada globo levanta una fuerza fija (no una parte del peso)
        }
        // un globo revienta al tocar el terreno
        for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++) {
            if (cells[r][c] != P_BALLOON || popped[r][c]) continue;
            b2Vec2 bc = chassis->GetWorldPoint(b2Vec2(lxOf(c), lyOf(r)));
            const float rr = 0.40f; bool hit = false;
            for (size_t k = 0; k < segs.size() && !hit; k++) {
                const Seg& sg = segs[k];
                if (bc.x + rr < fminf(sg.x1, sg.x2) || bc.x - rr > fmaxf(sg.x1, sg.x2) || bc.y + rr < fminf(sg.y1, sg.y2) || bc.y - rr > fmaxf(sg.y1, sg.y2)) continue;
                float dx = sg.x2 - sg.x1, dy = sg.y2 - sg.y1, l2 = dx * dx + dy * dy;
                float t = l2 > 0.f ? ((bc.x - sg.x1) * dx + (bc.y - sg.y1) * dy) / l2 : 0.f; t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
                float px = sg.x1 + t * dx - bc.x, py = sg.y1 + t * dy - bc.y;
                if (px * px + py * py < rr * rr) hit = true;
            }
            if (hit && thrFx[r][c]) { popped[r][c] = true; chassis->DestroyFixture(thrFx[r][c]); thrFx[r][c] = nullptr; events |= EV_PLACE; }
        }
        chassis->SetLinearDamping(nthr > 0 ? 0.25f : 0.f);
        world->Step(dt, 8, 3);
        t += dt;
        b2Vec2 p = pigWorld();
        float px = p.x * PPM, py = p.y * PPM;
        // camara suave + camara libre con el analogico derecho
        lookOX += (in.lookX * 220.f - lookOX) * 0.12f; lookOY += (in.lookY * 140.f - lookOY) * 0.12f;
        camx += (px + lookOX - camx) * 0.08f; camy += (py + 30.f + lookOY - camy) * 0.08f;
        if (L->flags & 1) {                       // sandbox: se juega libremente, sin meta ni derrota
            if (py > 1200.f) reset(false);        // si cae al vacio, vuelve a construir
            return;
        }
        if (!starGot && L->starX > -1e8f) {     // caja estrella: se recoge al tocarla con el vehiculo
            float sx = L->starX / PPM, sy = L->starY / PPM, rr = 0.95f;
            bool hit = (chassis->GetPosition() - b2Vec2(sx, sy)).Length() < rr;
            for (int i = 0; i < nwheels && !hit; i++) hit = (wheels[i]->GetPosition() - b2Vec2(sx, sy)).Length() < rr;
            if (hit) { starGot = true; events |= EV_STAR; }
        }
        if (px >= L->goalX && py >= L->goalY0 && py <= L->goalY1) {
            mode = M_WIN; events |= EV_WIN; finalT = t;
            resStars = 1 + (starGot ? 1 : 0) + ((L->parTime > 0.f && t <= L->parTime) ? 1 : 0);
            return;
        }
        // volcado: boca abajo mas de 1.5 s = pierdes
        flip = (cosf(chassis->GetAngle()) < -0.25f) ? flip + dt : 0.f;
        if (flip > 1.5f) { mode = M_LOSE; events |= EV_LOSE; return; }
        if (py > L->deathY || t > 30.f) { mode = M_LOSE; events |= EV_LOSE; return; }
        if (t > 4.f) {
            float sp = chassis->GetLinearVelocity().Length();
            stuck = (sp < 0.3f) ? stuck + dt : 0.f;
            if (stuck > 2.5f) { mode = M_LOSE; events |= EV_LOSE; }
        }
    }

    int draw(DrawItem* out, int max) const {
        int n = 0;
        bool playing = (mode != M_BUILD) && chassis;
        // pasada 1: cajas y ruedas (build) | pasada 2: cerditos
        for (int pass = 0; pass < 2; pass++)
            for (int r = 0; r < rows(); r++) for (int c = 0; c < cols(); c++) {
                if (n >= max) break;
                float lx = lxOf(c), ly = lyOf(r);
                if (pass == 0) {
                    int tp = cells[r][c];
                    if (tp < 0) continue;
                    if (playing) {
                        if (tp == P_WHEEL) continue;                  // las ruedas se dibujan aparte
                        if (tp == P_BALLOON && popped[r][c]) continue;  // globo pinchado
                        b2Vec2 w = chassis->GetWorldPoint(b2Vec2(lx, ly));
                        out[n].type = tp; out[n].x = w.x * PPM; out[n].y = w.y * PPM; out[n].angle = chassis->GetAngle() + (tp == P_FAN ? (float)rot[r][c] * 1.5707963f : 0.f); n++;
                    } else {
                        out[n].type = tp; out[n].x = cellX(c); out[n].y = cellY(r) + (tp == P_WHEEL ? WHEEL_DROP * PPM : 0.f); out[n].angle = (tp == P_FAN ? (float)rot[r][c] * 1.5707963f : 0.f); n++;
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
bool Game::loadLevel(int i) { if (i < 0 || i >= NUM_LEVELS_DATA) return false; g->levelIdx = i; g->L = &LEVELS[i]; g->reset(true); return true; }
int Game::levelIndex() const { return g->levelIdx; }
int Game::levelCount(int world) { int n = 0; for (int i = 0; i < NUM_LEVELS_DATA; i++) if (LEVELS[i].world == world && !(LEVELS[i].flags & 1)) n++; return n; }
int Game::levelAt(int world, int slot) { int n = 0; for (int i = 0; i < NUM_LEVELS_DATA; i++) if (LEVELS[i].world == world && !(LEVELS[i].flags & 1)) { if (n == slot) return i; n++; } return -1; }
int Game::slotOf(int levelIndex) { int n = 0; for (int i = 0; i < levelIndex; i++) if (LEVELS[i].world == LEVELS[levelIndex].world && !(LEVELS[i].flags & 1)) n++; return n; }
int Game::worldNow() const { return g->L->world; }
void Game::toggleFans() { if (g->mode == M_PLAY) { g->fansOn = !g->fansOn; g->events |= EV_CLICK; } }
void Game::popOneBalloon() { if (g->mode == M_PLAY) g->popBalloon(); }
int Game::sandboxIndex() { return NUM_LEVELS_DATA - 1; }
int Game::levelNumber() const { return g->L->world == 1 ? 15 + Game::slotOf(g->levelIdx) + 1 : Game::slotOf(g->levelIdx) + 1; }
float Game::vehicleMass() const { return g->totalMass; }
int Game::trayHit(float sx, float sy) const { return g->trayAt(sx, sy); }
bool Game::pigPlaced() const { return g->hasPig(); }
float Game::zoomNow() const { return g->zoom(); }
int Game::gridCols() const { return g->cols(); }
int Game::gridRows() const { return g->rows(); }
bool Game::fansOn() const { return g->fansOn; }
int Game::balloonsLeft() const { int n = 0; for (int r = 0; r < MAXR; r++) for (int c = 0; c < MAXC; c++) if (g->cells[r][c] == P_BALLOON && !g->popped[r][c]) n++; return n; }
int Game::fanCount() const { int n = 0; for (int r = 0; r < MAXR; r++) for (int c = 0; c < MAXC; c++) if (g->cells[r][c] == P_FAN) n++; return n; }
bool Game::isSandbox() const { return (g->L->flags & 1) != 0; }
const LevelData& Game::levelData() const { return *g->L; }
unsigned Game::takeEvents() { unsigned e = g->events; g->events = 0; return e; }
void Game::update(float dt, const GameInput& in) { g->update(dt, in); }
int Game::drawList(DrawItem* out, int max) const { return g->draw(out, max); }
Mode Game::mode() const { return g->mode; }
void Game::cursor(float& x, float& y) const { x = g->cx; y = g->cy; }
void Game::camera(float& x, float& y) const { x = g->viewerOn ? g->vcx : g->camx; y = g->viewerOn ? g->vcy : g->camy; }
void Game::setViewer(bool on) { if (on && g->mode != M_BUILD) return; g->viewerOn = on; if (on) g->computeViewer(); }
bool Game::viewer() const { return g->viewerOn; }
int Game::selected() const { return g->sel; }
int Game::remaining(int t) const { return g->left[t]; }
bool Game::cellPiece(int col, int row, int& type) const { type = g->cells[row][col]; return type >= 0; }
bool Game::cellHasPig(int col, int row) const { return g->pig[row][col]; }
void Game::cellScreen(int col, int row, float& sx, float& sy) const { g->toScreen(g->cellX(col), g->cellY(row), sx, sy); }
void Game::traySlot(int type, float& sx, float& sy) const { sx = trayX(g->slotOfType(type), g->trayN()); sy = TRAY_Y; }
int Game::trayTypeAt(int slot) const { return g->typeAtSlot(slot); }
int Game::trayCount() const { return g->trayN(); }
bool Game::starBox(float& x, float& y) const { if (g->L->starX < -1e8f || g->starGot) return false; x = g->L->starX; y = g->L->starY; return true; }
bool Game::starCollected() const { return g->starGot; }
int Game::resultStars() const { return g->resStars; }
float Game::finalTime() const { return g->finalT; }
float Game::time() const { return g->t; }
