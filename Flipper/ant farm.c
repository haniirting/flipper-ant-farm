#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>
#include <stdlib.h>

#define MAXA 30 // сколько муравьев рисуем на экране
#define MAXB 12 // яйца + личинки одновременно
#define MAXT 8 // мусор на арене
#define MAXANTS 999

#define X0 1
#define X1 124
#define Y0 13
#define Y1 61
#define QX 62
#define QY 37

#define EGG_MS 5000
#define LARVA_MS 6000

typedef struct {
    int x, y, dx, dy;
} Ant;

typedef struct {
    int x, y;
    uint8_t stage; // 0 пусто, 1 яйцо, 2 личинка
    uint32_t born;
} Brood;

typedef struct {
    int x, y;
    bool on;
} Trash;

typedef struct {
    Ant a[MAXA];
    Brood b[MAXB];
    Trash t[MAXT];
    int shown;
    uint32_t ants;
    int food; // 0..100
    int happy; // 0..100
    uint32_t last_lay, last_hunger, last_trash, last_starve;
} S;

static int rnd(int lo, int hi) {
    return lo + rand() % (hi - lo + 1);
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int trash_count(S* s) {
    int n = 0;
    for(int i = 0; i < MAXT; i++)
        if(s->t[i].on) n++;
    return n;
}

static void spawn_ant(S* s) {
    Ant* n = &s->a[s->shown++];
    n->x = clampi(QX + rnd(-12, 12), X0, X1);
    n->y = clampi(QY + rnd(-8, 8), Y0, Y1);
    n->dx = rnd(-1, 1);
    n->dy = rnd(-1, 1);
}

static void sync_shown(S* s) {
    int want = s->ants > MAXA ? MAXA : (int)s->ants;
    while(s->shown < want) spawn_ant(s);
    if(s->shown > want) s->shown = want;
}

static void lay_egg(S* s, uint32_t now) {
    for(int i = 0; i < MAXB; i++) {
        if(s->b[i].stage == 0) {
            s->b[i].x = clampi(QX + rnd(-18, 18), X0 + 2, X1 - 2);
            s->b[i].y = clampi(QY + rnd(-10, 10), Y0 + 2, Y1 - 2);
            s->b[i].stage = 1;
            s->b[i].born = now;
            return;
        }
    }
}

static void spawn_trash(S* s) {
    for(int i = 0; i < MAXT; i++) {
        if(!s->t[i].on) {
            s->t[i].on = true;
            s->t[i].x = rnd(X0 + 2, X1 - 4);
            s->t[i].y = rnd(Y0 + 2, Y1 - 4);
            return;
        }
    }
}

static void update_brood(S* s, uint32_t now) {
    for(int i = 0; i < MAXB; i++) {
        Brood* b = &s->b[i];
        if(b->stage == 1 && now - b->born >= EGG_MS) {
            b->stage = 2;
            b->born = now;
        } else if(b->stage == 2 && now - b->born >= LARVA_MS) {
            b->stage = 0;
            if(s->ants < MAXANTS) s->ants++;
            sync_shown(s);
        }
    }
}

static void update_game(S* s, uint32_t now) {
    // голод
    if(now - s->last_hunger >= 2000) {
        s->last_hunger = now;
        s->food -= 1 + (int)(s->ants / 20);
        if(s->food < 0) s->food = 0;
    }
    // без еды муравьи гибнут
    if(s->food == 0 && now - s->last_starve >= 5000) {
        s->last_starve = now;
        if(s->ants > 2) {
            s->ants--;
            sync_shown(s);
        }
    }
    // мусор появляется чаще при большой колонии
    int ti = 9000 - (int)(s->ants > 60 ? 60 : s->ants) * 100;
    if(now - s->last_trash >= (uint32_t)ti) {
        s->last_trash = now;
        spawn_trash(s);
    }
    // счастье: еда + чистота
    int clean = clampi(100 - trash_count(s) * 15, 0, 100);
    int target = (s->food + clean) / 2;
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

static void move_ants(S* s) {
    for(int i = 0; i < s->shown; i++) {
        Ant* a = &s->a[i];
        if(rand() % 5 == 0) {
            a->dx = rnd(-1, 1);
            a->dy = rnd(-1, 1);
        }
        a->x += a->dx;
        a->y += a->dy;
        if(a->x < X0) { a->x = X0; a->dx = 1; }
        if(a->x > X1) { a->x = X1; a->dx = -1; }
        if(a->y < Y0) { a->y = Y0; a->dy = 1; }
        if(a->y > Y1) { a->y = Y1; a->dy = -1; }
    }
}

static void draw(Canvas* c, void* ctx) {
    S* s = ctx;
    char buf[32];
    canvas_clear(c);
    canvas_set_font(c, FontSecondary);

    // верхняя панель
    canvas_draw_frame(c, 0, 0, 30, 8); // полоска еды
    canvas_draw_box(c, 1, 1, (s->food * 28) / 100, 6);
    snprintf(buf, sizeof(buf), "Ants:%lu Joy:%d", (unsigned long)s->ants, s->happy);
    canvas_draw_str_aligned(c, 127, 0, AlignRight, AlignTop, buf);
    canvas_draw_line(c, 0, 10, 127, 10);

    // фон фермы
    for(int y = 14; y < 64; y += 8)
        for(int x = ((y / 8) % 2) * 8; x < 128; x += 16) canvas_draw_dot(c, x, y);

    // мусор
    for(int i = 0; i < MAXT; i++)
        if(s->t[i].on) {
            canvas_draw_frame(c, s->t[i].x, s->t[i].y, 4, 4);
            canvas_draw_dot(c, s->t[i].x + 1, s->t[i].y + 1);
        }

    // яйца и личинки
    for(int i = 0; i < MAXB; i++) {
        Brood* b = &s->b[i];
        if(b->stage == 1) {
            canvas_draw_disc(c, b->x, b->y, 1);
        } else if(b->stage == 2) {
            canvas_draw_line(c, b->x - 2, b->y, b->x + 2, b->y);
            canvas_draw_dot(c, b->x, b->y - 1);
        }
    }

    // матка
    canvas_draw_box(c, QX - 4, QY - 2, 9, 5);
    canvas_set_color(c, ColorWhite);
    canvas_draw_dot(c, QX, QY);
    canvas_set_color(c, ColorBlack);
    canvas_draw_dot(c, QX - 2, QY - 4);
    canvas_draw_dot(c, QX, QY - 4);
    canvas_draw_dot(c, QX + 2, QY - 4);

    // рабочие муравьи
    for(int i = 0; i < s->shown; i++) {
        canvas_draw_box(c, s->a[i].x, s->a[i].y, 3, 2);
        canvas_draw_dot(c, s->a[i].x + (s->a[i].dx < 0 ? -1 : 3), s->a[i].y);
    }
}

static void input_cb(InputEvent* e, void* ctx) {
    furi_message_queue_put((FuriMessageQueue*)ctx, e, 0);
}

int32_t ant_main(void* p) {
    UNUSED(p);
    S* s = malloc(sizeof(S));
    for(size_t i = 0; i < sizeof(S); i++) ((uint8_t*)s)[i] = 0;

    uint32_t now = furi_get_tick();
    s->ants = 3;
    s->food = 60;
    s->happy = 60;
    s->last_lay = s->last_hunger = s->last_trash = s->last_starve = now;
    sync_shown(s);

    FuriMessageQueue* q = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw, s);
    view_port_input_callback_set(vp, input_cb, q);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    InputEvent e;
    uint32_t last_move = now;
    uint32_t last_tick = now;
    bool run = true;

    while(run) {
        if(furi_message_queue_get(q, &e, 20) == FuriStatusOk && e.type == InputTypePress) {
            if(e.key == InputKeyBack) run = false;
            if(e.key == InputKeyOk) {
                s->food = clampi(s->food + 30, 0, 100); // кормим
            }
            if(e.key == InputKeyLeft) {
                for(int i = 0; i < MAXT; i++) s->t[i].on = false; // чистим мусор
            }
        }
        now = furi_get_tick();
        if(now - last_tick >= 100) {
            last_tick = now;
            update_game(s, now);
        }
        if(now - last_move >= 100) {
            last_move = now;
            move_ants(s);
            view_port_update(vp);
        }
    }

    gui_remove_view_port(gui, vp);
    furi_record_close(RECORD_GUI);
    view_port_free(vp);
    furi_message_queue_free(q);
    free(s);
    return 0;
}
