#include "ui.h"
#include <curses.h>
#include <stdio.h>
#include <string.h>

#define PAIR_NORMAL 1
#define PAIR_HOT    2
#define PAIR_PEAK   3
#define PAIR_SCALE  4
#define PAIR_TEXT   5

static int have_color;

void ui_init(void)
{
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        have_color = 1;
        start_color();
        use_default_colors();
    }
}

void ui_shutdown(void) { endwin(); }

void ui_apply_theme(const theme_colors *c)
{
    if (!have_color) return;
    init_pair(PAIR_NORMAL, c->normal, -1);
    init_pair(PAIR_HOT, c->hot, -1);
    init_pair(PAIR_PEAK, c->peak, -1);
    init_pair(PAIR_SCALE, c->scale, -1);
    init_pair(PAIR_TEXT, c->text, -1);
}

static int zone_pair(double db)
{
    return db > -3.0 ? PAIR_PEAK : db > -12.0 ? PAIR_HOT : PAIR_NORMAL;
}

/* Draw one horizontal meter bar: filled cells, optional peak marker, empty cells */
static void draw_bar(int y, const char *label, float db, float hold,
                     const config_t *cfg, int cells)
{
    int left = 4;
    mvaddstr(y, 0, label);
    mvaddch(y, 3, '[');
    int li = meter_db_to_cell(db, cfg->db_min, cfg->db_max, cells);
    int hi = meter_db_to_cell(hold, cfg->db_min, cfg->db_max, cells);
    for (int i = 0; i < cells; i++) {
        int x = left + i;
        if (i <= li) {
            /* A_REVERSE + space = solid colored block, no UTF-8 width issues */
            double cdb = cfg->db_min + (double)(i + 0.5) / cells * (cfg->db_max - cfg->db_min);
            attr_t at = A_REVERSE | COLOR_PAIR(zone_pair(cdb));
            if (i == li) at |= A_BOLD;
            attron(at);
            mvaddch(y, x, ' ');
            attroff(at);
        } else if (i == hi && hi > li) {
            attron(COLOR_PAIR(PAIR_PEAK) | A_BOLD);
            mvaddstr(y, x, "·");
            attroff(COLOR_PAIR(PAIR_PEAK) | A_BOLD);
        } else {
            attron(COLOR_PAIR(PAIR_SCALE) | A_DIM);
            mvaddch(y, x, ' ');
            attroff(COLOR_PAIR(PAIR_SCALE) | A_DIM);
        }
    }
    mvaddch(y, left + cells, ']');
}

void ui_draw(const channel_meter meters[2], const config_t *cfg, int units_rel,
             int paused, const char *device, const char *status)
{
    int maxy, maxx;
    getmaxyx(stdscr, maxy, maxx);
    (void)maxy;
    int left = 4;
    int right_need = 14;
    int cells = cfg->bar_length;
    if (left + cells + right_need > maxx) cells = maxx - left - right_need;
    if (cells < 4) {
        mvprintw(0, 0, "Terminal too narrow (%d cols); need %d.",
                 maxx, left + 4 + right_need);
        refresh();
        return;
    }
    erase();

    /* scale row: 7 tick labels across the range */
    for (int t = 0; t <= 6; t++) {
        int db = cfg->db_min + (cfg->db_max - cfg->db_min) * t / 6;
        int x = left + (cells - 1) * t / 6;
        char lab[8];
        snprintf(lab, sizeof lab, "%d", db);
        int w = (int)strlen(lab);
        if (x + w > maxx - 1) x = maxx - w;
        if (x < left && t != 0) x = left;
        attron(COLOR_PAIR(PAIR_SCALE));
        mvaddstr(0, x, lab);
        attroff(COLOR_PAIR(PAIR_SCALE));
    }
    char unit[8];
    snprintf(unit, sizeof unit, "%s", units_rel ? "dB rel" : "dBFS");
    attron(COLOR_PAIR(PAIR_SCALE));
    mvaddstr(0, left + cells + 2, unit);
    attroff(COLOR_PAIR(PAIR_SCALE));

    /* bars */
    for (int chn = 0; chn < 2; chn++) {
        int y = chn + 1;
        float db = meters[chn].level_db;
        float hold = meters[chn].hold_db;
        if (units_rel) { db -= meters[chn].max_db; hold -= meters[chn].max_db; }
        draw_bar(y, chn ? "R " : "L ", db, hold, cfg, cells);
        char dbtxt[16];
        snprintf(dbtxt, sizeof dbtxt, "%8.1f", db);
        mvaddstr(y, left + cells + 2, dbtxt);
    }

    /* status line */
    char line[512];
    snprintf(line, sizeof line, "monitor: %s | theme: %s%s%s",
             device, theme_preset_name(cfg->preset_idx),
             paused ? " | PAUSED" : "", status ? status : "");
    attron(COLOR_PAIR(PAIR_TEXT));
    move(3, 0); /* status on its own row; printw is cursor-relative */
    printw(line);
    attroff(COLOR_PAIR(PAIR_TEXT));
    printw("\nq quit · d device · c theme · t units · space pause");
    refresh();
}
