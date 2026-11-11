#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
#define MAXA 24 // сколько муравьев рисуем
#define MAXB 12 // яйца + личинки одновременно
#define MAXT 8 // куски мусора в углу
#define MAXC 8 // крошки на поверхности
#define MAXANTS 999
#define CRUMB_FOOD 8 // сколько еды дает одна крошка
 
// поверхность (арена)
#define SX0 2
#define SX1 124
#define SY0 13
#define SY1 23
#define GROUND_Y 27
// вход в муравейник (дырка)
#define HOLE_X 56
#define HOLE_W 15
#define HOLE_CX 63
// коридор под землей
#define CORR_Y 38
// матка
#define QX 76
#define QY 55
 
#define EGG_MS 5000
#define LARVA_MS 6000
#define WATER_MS 1200
#define CLEAN_MS 2000
#define REFILL_MS 20000
#define PREP_MS 4000
 
enum { M_WALK, M_GO, M_REST };
enum { T_NONE, T_FETCH, T_DELIVER };
enum { R_SURF, R_CORR, R_REST1, R_FOOD, R_QUEEN, R_REST2 };
 
typedef struct {
    int x, y;
} Pt;
 
typedef struct {
    int x0, y0, x1, y1;
} Rect;
 
// где муравьи могут ходить (положение левого верхнего угла муравья)
static const Rect ROOM[6] = {
    {SX0, SY0, SX1, SY1}, // поверхность
    {10, 37, 114, 38}, // коридор
    {6, 47, 22, 57}, // отдых 1
    {32, 46, 48, 50}, // склад еды
    {58, 52, 92, 57}, // камера матки
    {101, 47, 117, 57}, // отдых 2
};
// x соединения камеры с коридором
static const int CONX[6] = {HOLE_CX, 0, 15, 41, 76, 110};
// рамки камер: x, ширина
static const int CHX[4] = {4, 30, 56, 99};
static const int CHW[4] = {24, 24, 41, 24};
 
typedef struct {
    int x, y, dx, dy;
    uint8_t mode, task, room, then, carry, n, i, ci;
    uint32_t until;
    Pt p[4];
} Ant;
 
typedef struct {
    int x, y;
    bool on, claimed;
    uint32_t t;
} Crumb;
 
typedef struct {
    int x, y;
    uint8_t stage; // 0 пусто, 1 яйцо, 2 личинка
    uint32_t born;
} Brood;
 
typedef struct {
    Ant a[MAXA];
    Brood b[MAXB];
    Crumb cr[MAXC];
    int shown;
    uint32_t ants;
    int food; // запас еды на складе 0..100
    int hum; // влажность 0..100
    int happy; // 0..100
    int trash; // сколько кусков мусора в углу
    int tanks; // полных емкостей с водой (0..2)
    bool prep; // воду набрали, ждем второе нажатие
    uint32_t prep_t, last_refill;
    uint32_t water_t, clean_t; // старты анимаций (0 = нет)
    uint32_t fx, fy, ft; // вспышка рождения муравья
    uint32_t last_lay, last_hunger, last_trash, last_starve, last_dry;
} S;
 
static int rnd(int lo, int hi) {
    return lo + rand() % (hi - lo + 1);
}
 
static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
 
static bool active(uint32_t t, uint32_t dur, uint32_t now) {
    return t != 0 && now - t < dur;
}
 
// ---------- муравьи ----------
static void spawn_ant(S* s, int room) {
    Ant* n = &s->a[s->shown++];
    memset(n, 0, sizeof(Ant));
    n->room = room;
    n->mode = M_WALK;
    n->x = rnd(ROOM[room].x0, ROOM[room].x1);
    n->y = rnd(ROOM[room].y0, ROOM[room].y1);
    n->dx = rnd(-1, 1);
    n->dy = rnd(-1, 1);
}
 
static void sync_shown(S* s, bool born) {
    static const uint8_t home[6] = {R_QUEEN, R_SURF, R_REST1, R_FOOD, R_REST2, R_QUEEN};
    int want = s->ants > MAXA ? MAXA : (int)s->ants;
    while(s->shown < want) spawn_ant(s, born ? R_QUEEN : home[s->shown % 6]);
    while(s->shown > want) {
        s->shown--;
        Ant* a = &s->a[s->shown];
        if(a->task == T_FETCH) s->cr[a->ci].claimed = false;
    }
}
 
// проложить путь в другую камеру через коридор
static void go_room(Ant* a, int target) {
    int n = 0;
    int r = a->room;
    if(r != R_CORR) {
        int ex = CONX[r];
        a->p[n++] = (Pt){ex, a->y};
        a->p[n++] = (Pt){ex, CORR_Y};
    }
    int tx = CONX[target];
    a->p[n++] = (Pt){tx, CORR_Y};
    a->p[n++] = (Pt){tx, rnd(ROOM[target].y0, ROOM[target].y1)};
    a->n = n;
    a->i = 0;
    a->then = target;
    a->mode = M_GO;
}
 
static void ant_arrive(S* s, Ant* a, uint32_t now) {
    a->mode = M_WALK;
    if(a->task == T_FETCH) { // дошли до крошки, берем и несем на склад
        Crumb* cr = &s->cr[a->ci];
        cr->on = false;
        cr->claimed = false;
        a->carry = 1;
        a->task = T_DELIVER;
        a->room = R_SURF;
        go_room(a, R_FOOD);
        return;
    }
    a->room = a->then;
    if(a->task == T_DELIVER && a->room == R_FOOD) { // положили еду на склад
        a->carry = 0;
        a->task = T_NONE;
        s->food = clampi(s->food + CRUMB_FOOD, 0, 100);
    }
    if(a->room == R_REST1 || a->room == R_REST2) { // легли отдыхать
        a->mode = M_REST;
        a->until = now + rnd(4000, 9000);
    }
}
 
static void ant_go(S* s, Ant* a, uint32_t now) {
    for(int k = 0; k < 2; k++) {
        Pt t = a->p[a->i];
        if(a->x != t.x) {
            a->dx = t.x > a->x ? 1 : -1;
            a->dy = 0;
            a->x += a->dx;
        } else if(a->y != t.y) {
            a->dy = t.y > a->y ? 1 : -1;
            a->dx = 0;
            a->y += a->dy;
        }
        if(a->x == t.x && a->y == t.y) {
            a->i++;
            if(a->i >= a->n) {
                ant_arrive(s, a, now);
                return;
            }
        }
    }
}
 
static void ant_idle(S* s, Ant* a, uint32_t now) {
    if(a->mode == M_REST) {
        if((int32_t)(now - a->until) >= 0) a->mode = M_WALK;
        return;
    }
    Rect r = ROOM[a->room];
    if(rand() % 5 == 0) {
        a->dx = rnd(-1, 1);
        a->dy = rnd(-1, 1);
    }
    a->x += a->dx;
    a->y += a->dy;
    if(a->x < r.x0) { a->x = r.x0; a->dx = 1; }
    if(a->x > r.x1) { a->x = r.x1; a->dx = -1; }
    if(a->y < r.y0) { a->y = r.y0; a->dy = 1; }
    if(a->y > r.y1) { a->y = r.y1; a->dy = -1; }
 
    if(rand() % 25 == 0) {
        int ci = -1;
        for(int k = 0; k < MAXC; k++) {
            if(s->cr[k].on && !s->cr[k].claimed) {
                ci = k;
                break;
            }
        }
        if(ci >= 0 && a->room == R_SURF) { // идем за крошкой
            s->cr[ci].claimed = true;
            a->task = T_FETCH;
            a->ci = ci;
            a->p[0] = (Pt){s->cr[ci].x, s->cr[ci].y};
            a->n = 1;
            a->i = 0;
            a->then = R_SURF;
            a->mode = M_GO;
        } else if(ci >= 0) { // выходим наверх за едой
            go_room(a, R_SURF);
        } else if(rand() % 3 == 0) { // просто сменить камеру
            static const uint8_t opts[5] = {R_SURF, R_REST1, R_FOOD, R_QUEEN, R_REST2};
            int t = opts[rand() % 5];
            if(t != a->room) go_room(a, t);
        }
    } else if((a->room == R_REST1 || a->room == R_REST2) && rand() % 30 == 0) {
        a->mode = M_REST;
        a->until = now + rnd(4000, 9000);
    }
}
 
static void move_ants(S* s, uint32_t now) {
    for(int i = 0; i < s->shown; i++) {
        Ant* a = &s->a[i];
        if(a->mode == M_GO) ant_go(s, a, now);
        else ant_idle(s, a, now);
    }
}
 
// ---------- матка и расплод ----------
static void lay_egg(S* s, uint32_t now) {
    for(int i = 0; i < MAXB; i++) {
        if(s->b[i].stage == 0) {
            s->b[i].x = rnd(60, 92);
            s->b[i].y = rnd(46, 50);
            s->b[i].stage = 1;
            s->b[i].born = now;
            return;
        }
    }
}
 
static void update_brood(S* s, uint32_t now) {
    for(int i = 0; i < MAXB; i++) {
        Brood* b = &s->b[i];
        if(b->stage == 1 && now - b->born >= EGG_MS) {
            b->stage = 2; // яйцо -> личинка
            b->born = now;
        } else if(b->stage == 2 && now - b->born >= LARVA_MS) {
            b->stage = 0; // личинка -> муравей
            s->fx = b->x;
            s->fy = b->y;
            s->ft = now;
            if(s->ants < MAXANTS) s->ants++;
            sync_shown(s, true);
        }
    }
}
 
static void update_game(S* s, uint32_t now) {
    // голод: еда берется со склада
    if(now - s->last_hunger >= 2000) {
        s->last_hunger = now;
        s->food -= 1 + (int)(s->ants / 20);
        if(s->food < 0) s->food = 0;
    }
    // муравейник сохнет
    if(now - s->last_dry >= 2500) {
        s->last_dry = now;
        if(s->hum > 0) s->hum--;
    }
    // без еды муравьи гибнут
    if(s->food == 0 && now - s->last_starve >= 5000) {
        s->last_starve = now;
        if(s->ants > 2) {
            s->ants--;
            sync_shown(s, false);
        }
    }
    // мусор: чем больше колония, тем чаще
    int ti = 9000 - (int)(s->ants > 60 ? 60 : s->ants) * 100;
    if(now - s->last_trash >= (uint32_t)ti) {
        s->last_trash = now;
        if(s->trash < MAXT) s->trash++;
    }
    // емкости с водой наполняются сами
    if(s->tanks >= 2) {
        s->last_refill = now;
    } else if(now - s->last_refill >= REFILL_MS) {
        s->tanks++;
        s->last_refill = now;
    }
    // воду набрали, но не вылили: возвращаем в емкость
    if(s->prep && now - s->prep_t >= PREP_MS) {
        s->prep = false;
        if(s->tanks < 2) s->tanks++;
    }
    // уборка: на середине анимации мусор убран
    if(s->clean_t) {
        uint32_t el = now - s->clean_t;
        if(el >= CLEAN_MS / 2) s->trash = 0;
        if(el >= CLEAN_MS) s->clean_t = 0;
    }
    // счастье: еда + чистота + влажность
    int clean = clampi(100 - s->trash * 12, 0, 100);
    int target = (s->food + clean + s->hum) / 3;
    if(s->happy < target) s->happy++;
    else if(s->happy > target) s->happy--;
    // матка откладывает яйца: чем счастливее, тем быстрее
    int iv = 7000 - s->happy * 50;
    if(s->happy >= 30 && s->food >= 10 && now - s->last_lay >= (uint32_t)iv) {
        s->last_lay = now;
        lay_egg(s, now);
    }
    update_brood(s, now);
}
 
// ---------- сохранение ----------
#define SAVE_DIR "/ext/apps_data/ant_farm"
#define SAVE_PATH SAVE_DIR "/save.bin"
#define SAVE_MAGIC 0x41465232
 
typedef struct {
    int x, y;
    uint8_t stage;
    uint32_t age;
} SaveBrood;
 
typedef struct {
    uint32_t magic;
    uint32_t ants;
    int food, happy, hum, trash, tanks;
    SaveBrood b[MAXB];
} SaveData;
 
static void save_game(S* s, uint32_t now) {
    SaveData* d = malloc(sizeof(SaveData));
    memset(d, 0, sizeof(SaveData));
    d->magic = SAVE_MAGIC;
    d->ants = s->ants;
    d->food = s->food;
    d->happy = s->happy;
    d->hum = s->hum;
    d->trash = s->trash;
    d->tanks = s->tanks + (s->prep ? 1 : 0);
    for(int i = 0; i < MAXB; i++) {
        d->b[i].x = s->b[i].x;
        d->b[i].y = s->b[i].y;
        d->b[i].stage = s->b[i].stage;
        d->b[i].age = now - s->b[i].born;
    }
    Storage* st = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(st, "/ext/apps_data");
    storage_simply_mkdir(st, SAVE_DIR);
    File* f = storage_file_alloc(st);
    if(storage_file_open(f, SAVE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, d, sizeof(SaveData));
    }
    storage_file_close(f);
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    free(d);
}
 
static bool load_game(S* s, uint32_t now) {
    bool ok = false;
    SaveData* d = malloc(sizeof(SaveData));
    Storage* st = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(st);
    if(storage_file_open(f, SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        if(storage_file_read(f, d, sizeof(SaveData)) == sizeof(SaveData) &&
           d->magic == SAVE_MAGIC)
            ok = true;
    }
    storage_file_close(f);
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
 
    if(ok) {
        s->ants = d->ants > MAXANTS ? MAXANTS : d->ants;
        if(s->ants < 2) s->ants = 2;
        s->food = clampi(d->food, 0, 100);
        s->happy = clampi(d->happy, 0, 100);
        s->hum = clampi(d->hum, 0, 100);
        s->trash = clampi(d->trash, 0, MAXT);
        s->tanks = clampi(d->tanks, 0, 2);
        s->last_refill = now;
        for(int i = 0; i < MAXB; i++) {
            s->b[i].stage = d->b[i].stage > 2 ? 0 : d->b[i].stage;
            s->b[i].x = clampi(d->b[i].x, 60, 92);
            s->b[i].y = clampi(d->b[i].y, 46, 50);
            s->b[i].born = now - d->b[i].age;
        }
    }
    free(d);
    return ok;
}
 
// ---------- рисование ----------
static void draw(Canvas* c, void* ctx) {
    S* s = ctx;
    uint32_t now = furi_get_tick();
    char buf[32];
    canvas_clear(c);
    canvas_set_font(c, FontSecondary);
 
    // анимация уборки: прогресс 0..100 и позиция лопатки
    bool cl = active(s->clean_t, CLEAN_MS, now);
    int cp = cl ? (int)((now - s->clean_t) * 100 / CLEAN_MS) : 0;
    int cxv = 126 - (cp < 50 ? cp : 50) * 28 / 50;
 
    // --- панель: еда на складе, влажность, счетчики ---
    canvas_draw_frame(c, 0, 0, 16, 8);
    canvas_draw_box(c, 1, 1, (s->food * 14) / 100, 6);
    canvas_draw_frame(c, 18, 0, 16, 8);
    canvas_draw_box(c, 19, 1, (s->hum * 14) / 100, 6);
    snprintf(buf, sizeof(buf), "Ants:%lu Joy:%d", (unsigned long)s->ants, s->happy);
    canvas_draw_str_aligned(c, 127, 0, AlignRight, AlignTop, buf);
    canvas_draw_line(c, 0, 10, 127, 10);
 
    // --- поверхность: земля с дыркой, трава ---
    canvas_draw_line(c, 0, GROUND_Y, HOLE_X - 1, GROUND_Y);
    canvas_draw_line(c, HOLE_X + HOLE_W, GROUND_Y, 127, GROUND_Y);
    static const int tufts[] = {30, 44, 90, 98};
    for(int i = 0; i < 4; i++) {
        canvas_draw_line(c, tufts[i], GROUND_Y - 1, tufts[i] - 1, GROUND_Y - 3);
        canvas_draw_line(c, tufts[i], GROUND_Y - 1, tufts[i] + 1, GROUND_Y - 3);
    }
 
    // две емкости с водой
    for(int i = 0; i < 2; i++) {
        int tx = 4 + i * 10;
        int lv = 0;
        if(i < s->tanks) {
            lv = 8;
        } else if(i == s->tanks) {
            lv = clampi((int)((now - s->last_refill) * 8 / REFILL_MS), 0, 8);
        }
        canvas_draw_frame(c, tx, 16, 7, 10);
        canvas_draw_line(c, tx + 2, 15, tx + 4, 15);
        if(lv > 0) canvas_draw_box(c, tx + 1, 25 - lv, 5, lv);
    }
 
    // мусор в правом углу арены
    for(int i = 0; i < s->trash; i++) {
        int px = 104 + (i % 4) * 5;
        int py = 24 - (i / 4) * 4;
        if(cl && (cp >= 50 || px + 3 >= cxv)) continue;
        canvas_draw_frame(c, px, py, 4, 3);
    }
 
    // крошки (падают сверху)
    for(int i = 0; i < MAXC; i++) {
        Crumb* k = &s->cr[i];
        if(!k->on) continue;
        int yy = 12 + (int)((now - k->t) / 30);
        if(yy > k->y) yy = k->y;
        canvas_draw_box(c, k->x, yy, 2, 2);
    }
 
    // --- под землей: грунт, коридор, 4 камеры, шахта ---
    for(int y = GROUND_Y + 3; y < 64; y += 4)
        for(int x = ((y / 4) % 2) * 4; x < 128; x += 8) canvas_draw_dot(c, x, y);
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, 8, 36, 112, 6);
    for(int i = 0; i < 4; i++) canvas_draw_box(c, CHX[i], 44, CHW[i], 18);
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, 8, 36, 112, 6);
    for(int i = 0; i < 4; i++) canvas_draw_frame(c, CHX[i], 44, CHW[i], 18);
    // проходы из коридора в камеры
    canvas_set_color(c, ColorWhite);
    for(int i = 0; i < 4; i++) canvas_draw_box(c, CONX[i + 2] - 2, 41, 5, 4);
    canvas_draw_box(c, HOLE_X + 2, GROUND_Y + 2, HOLE_W - 4, 10);
    canvas_set_color(c, ColorBlack);
    for(int i = 0; i < 4; i++) {
        canvas_draw_line(c, CONX[i + 2] - 3, 41, CONX[i + 2] - 3, 44);
        canvas_draw_line(c, CONX[i + 2] + 3, 41, CONX[i + 2] + 3, 44);
    }
    canvas_draw_line(c, HOLE_X + 1, GROUND_Y + 2, HOLE_X + 1, 36);
    canvas_draw_line(c, HOLE_X + HOLE_W - 2, GROUND_Y + 2, HOLE_X + HOLE_W - 2, 36);
    canvas_draw_box(c, HOLE_X, GROUND_Y, HOLE_W, 2); // вход-дырка
 
    // влажные капли на потолке коридора
    for(int k = 0; k < s->hum / 20; k++) canvas_draw_dot(c, 20 + k * 20, 37);
 
    // камеры отдыха: подстилка
    canvas_draw_line(c, CHX[0] + 3, 59, CHX[0] + CHW[0] - 4, 59);
    canvas_draw_line(c, CHX[3] + 3, 59, CHX[3] + CHW[3] - 4, 59);
 
    // склад еды: куча зерна растет с запасом еды
    int hh = (s->food * 8) / 100;
    for(int r = 0; r < hh; r++) {
        int w = 20 - r * 2;
        if(w < 2) w = 2;
        int x0 = CONX[R_FOOD] - w / 2;
        canvas_draw_line(c, x0, 59 - r, x0 + w - 1, 59 - r);
    }
 
    // матка (усики шевелятся)
    canvas_draw_box(c, QX - 4, QY - 2, 9, 5);
    canvas_draw_box(c, QX + 5, QY - 1, 3, 3);
    canvas_set_color(c, ColorWhite);
    canvas_draw_dot(c, QX, QY);
    canvas_set_color(c, ColorBlack);
    canvas_draw_dot(c, QX + 8, QY - 2 - (int)((now / 300) & 1));
    canvas_draw_dot(c, QX - 2, QY - 4);
    canvas_draw_dot(c, QX, QY - 4);
    canvas_draw_dot(c, QX + 2, QY - 4);
 
    // яйца и личинки
    for(int i = 0; i < MAXB; i++) {
        Brood* b = &s->b[i];
        if(b->stage == 1) {
            canvas_draw_disc(c, b->x, b->y, 1);
            if(now - b->born < 400) canvas_draw_circle(c, b->x, b->y, 3); // отложено
        } else if(b->stage == 2) {
            int wy = (int)(((now / 250) + i) & 1);
            canvas_draw_line(c, b->x - 2, b->y + wy, b->x + 2, b->y + wy);
            canvas_draw_dot(c, b->x, b->y - 1 + wy);
            if(now - b->born < 400) canvas_draw_circle(c, b->x, b->y, 3); // вылупилась
        }
    }
 
    // вспышка: родился новый муравей
    if(active(s->ft, 400, now))
        canvas_draw_circle(c, (int)s->fx, (int)s->fy, 1 + (int)((now - s->ft) / 100));
 
    // муравьи (лапки шевелятся, несут крошки, спят)
    for(int i = 0; i < s->shown; i++) {
        Ant* a = &s->a[i];
        canvas_draw_box(c, a->x, a->y, 3, 2);
        canvas_draw_dot(c, a->x + (a->dx < 0 ? -1 : 3), a->y);
        if(a->mode == M_REST) {
            canvas_draw_dot(c, a->x + 1, a->y + 2);
            canvas_draw_str(c, a->x + 4, a->y - 1, ((now / 600) & 1) ? "Z" : "z");
            continue;
        }
        if(((now / 150) + i) & 1) {
            canvas_draw_dot(c, a->x, a->y + 2);
            canvas_draw_dot(c, a->x + 2, a->y + 2);
        } else {
            canvas_draw_dot(c, a->x + 1, a->y + 2);
        }
        if(a->carry) canvas_draw_box(c, a->x, a->y - 2, 2, 2);
    }
 
    // --- анимации действий ---
    // лейка ждет второго нажатия
    if(s->prep && !active(s->water_t, WATER_MS, now)) {
        canvas_draw_frame(c, 74, 15, 8, 6);
        canvas_draw_line(c, 73, 16, 70, 18);
        canvas_draw_line(c, 82, 16, 84, 19);
        canvas_draw_dot(c, 70 + (int)((now / 250) & 1), 20);
    }
    // полив: лейка над дыркой, капли падают в муравейник
    if(active(s->water_t, WATER_MS, now)) {
        int p = (int)((now - s->water_t) * 100 / WATER_MS);
        canvas_draw_frame(c, 64, 15, 8, 6);
        canvas_draw_line(c, 63, 18, 61, 22);
        for(int j = 0; j < 5; j++) {
            int y = 24 + (((p + j * 20) % 100) * 16) / 100;
            canvas_draw_line(c, 59 + j * 2, y, 59 + j * 2, y + 1);
        }
    }
    // уборка: совок и маленькая лопатка
    if(cl) {
        int xs = 95, xv = cxv, yo = 0;
        if(cp >= 50) {
            int dx = ((cp - 50) * 40) / 50;
            xs += dx;
            xv = 98 + dx;
            yo = -((cp - 50) / 8);
        }
        canvas_draw_line(c, xs, 26 + yo, xs + 9, 26 + yo);
        canvas_draw_line(c, xs, 22 + yo, xs, 26 + yo);
        canvas_draw_line(c, xs, 22 + yo, xs - 6, 17 + yo);
        canvas_draw_box(c, xv, 23 + yo, 4, 3);
        canvas_draw_line(c, xv + 3, 23 + yo, xv + 11, 18 + yo);
        if(cp >= 25) {
            canvas_draw_dot(c, xs + 2, 25 + yo);
            canvas_draw_dot(c, xs + 4, 24 + yo);
            canvas_draw_dot(c, xs + 6, 25 + yo);
        }
    }
}
 
static void input_cb(InputEvent* e, void* ctx) {
    furi_message_queue_put((FuriMessageQueue*)ctx, e, 0);
}
 
static void drop_crumbs(S* s, uint32_t now) {
    int added = 0;
    for(int i = 0; i < MAXC && added < 5; i++) {
        if(!s->cr[i].on) {
            int x = rnd(26, 100);
            if(x >= HOLE_X - 4 && x <= HOLE_X + HOLE_W + 2) x += 24;
            s->cr[i].on = true;
            s->cr[i].claimed = false;
            s->cr[i].x = x;
            s->cr[i].y = rnd(19, 24);
            s->cr[i].t = now;
            added++;
        }
    }
}
 
int32_t ant_main(void* p) {
    UNUSED(p);
    S* s = malloc(sizeof(S));
    memset(s, 0, sizeof(S));
 
    uint32_t now = furi_get_tick();
    s->ants = 3;
    s->food = 60;
    s->happy = 60;
    s->hum = 50;
    s->tanks = 2;
    s->last_lay = s->last_hunger = s->last_trash = s->last_starve = now;
    s->last_dry = s->last_refill = now;
    load_game(s, now); // загрузить прогресс, если он есть
    sync_shown(s, false);
 
    FuriMessageQueue* q = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw, s);
    view_port_input_callback_set(vp, input_cb, q);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);
 
    InputEvent e;
    uint32_t last_move = now;
    uint32_t last_tick = now;
    uint32_t last_save = now;
    bool run = true;
 
    while(run) {
        if(furi_message_queue_get(q, &e, 20) == FuriStatusOk && e.type == InputTypePress) {
            now = furi_get_tick();
            if(e.key == InputKeyBack) run = false;
            if(e.key == InputKeyOk) drop_crumbs(s, now); // насыпать крошки, муравьи сами унесут
            if(e.key == InputKeyLeft) { // убрать мусор совком и лопаткой
                if(s->trash > 0 && !active(s->clean_t, CLEAN_MS, now)) s->clean_t = now;
            }
            if(e.key == InputKeyRight) { // увлажнить: два нажатия
                if(s->prep) { // 2-е нажатие: поливаем
                    s->prep = false;
                    s->hum = clampi(s->hum + 45, 0, 100);
                    s->water_t = now;
                } else if(s->tanks > 0) { // 1-е нажатие: набираем воду
                    if(s->tanks == 2) s->last_refill = now;
                    s->tanks--;
                    s->prep = true;
                    s->prep_t = now;
                }
            }
        }
        now = furi_get_tick();
        if(now - last_tick >= 100) {
            last_tick = now;
            update_game(s, now);
        }
        if(now - last_save >= 10000) { // автосохранение
            last_save = now;
            save_game(s, now);
        }
        if(now - last_move >= 100) {
            last_move = now;
            move_ants(s, now);
            view_port_update(vp);
        }
    }
 
    save_game(s, furi_get_tick()); // сохранить при выходе
    gui_remove_view_port(gui, vp);
    furi_record_close(RECORD_GUI);
    view_port_free(vp);
    furi_message_queue_free(q);
    free(s);
    return 0;
}
 
