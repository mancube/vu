#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bar zone colors use the terminal's base-16 palette, which any 256-color
 * terminal remaps to match its own scheme (dark or light). Scale and text
 * use -1 (the terminal's default foreground) so they stay readable on any
 * palette — hard-coded white/black text is invisible on half of them. */
const theme_preset VU_PRESETS[] = {
    { "default", { 2,   3,   1,   -1, -1  } },
    { "warm",    { 3,   9,   1,   -1, -1  } },
    { "mono",    { -1,  -1,  -1,  -1, -1  } },
    { "neon",    { 51,  190, 196, -1,  -1  } },
};
const int VU_PRESET_COUNT = (int)(sizeof VU_PRESETS / sizeof VU_PRESETS[0]);

static const struct { const char *name; int num; } NAMED[] = {
    { "black", 0 },           { "red", 1 },           { "green", 2 },
    { "yellow", 3 },          { "blue", 4 },          { "magenta", 5 },
    { "cyan", 6 },            { "white", 7 },         { "brightblack", 8 },
    { "brightred", 9 },       { "brightgreen", 10 },  { "brightyellow", 11 },
    { "brightblue", 12 },     { "brightmagenta", 13 }, { "brightcyan", 14 },
    { "brightwhite", 15 },
};

int theme_parse_color(const char *s)
{
    if (strcasecmp(s, "default") == 0) return -1; /* terminal default color */
    for (size_t i = 0; i < sizeof NAMED / sizeof NAMED[0]; i++)
        if (strcasecmp(s, NAMED[i].name) == 0) return NAMED[i].num;
    size_t len = strlen(s);
    if (len > 0 && strspn(s, "0123456789") == len) {
        long v = strtol(s, NULL, 10);
        if (v >= 0 && v <= 255) return (int)v;
    }
    return -1;
}

int theme_find_preset(const char *name)
{
    for (int i = 0; i < VU_PRESET_COUNT; i++)
        if (strcasecmp(name, VU_PRESETS[i].name) == 0) return i;
    return -1;
}

const char *theme_preset_name(int idx)
{
    return idx >= 0 && idx < VU_PRESET_COUNT ? VU_PRESETS[idx].name : "custom";
}

void theme_apply_preset(config_t *c, int idx)
{
    if (idx < 0 || idx >= VU_PRESET_COUNT) return;
    c->colors = VU_PRESETS[idx].c;
    c->preset_idx = idx;
}

void config_defaults(config_t *c)
{
    theme_apply_preset(c, 0);
    c->bar_length = 40;
    c->db_min = -60;
    c->db_max = 0;
    c->release_dbps = 60.0f;    /* bar falls 20 dB in ~0.3 s (standard PPM) */
    c->hold_decay_dbps = 30.0f; /* peak marker clears ~1.7 s after silence */
    c->rms_window = 0.01f;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    size_t i = 0;
    while (i < n && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, n - i + 1);
}

int config_load_file(config_t *c, const char *path, char *err, int errsz)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0; /* no config file = defaults */
    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *eq = strchr(line, '=');
        if (!eq) {
            if (strspn(line, " \t\r\n") != strlen(line)) {
                snprintf(err, errsz, "%s:%d: expected 'key = value'", path, lineno);
                fclose(f);
                return -1;
            }
            continue;
        }
        *eq = 0;
        char *key = line, *val = eq + 1;
        trim(key);
        trim(val);
        if (*key == 0) continue;
        if (!strcasecmp(key, "theme")) {
            int p = theme_find_preset(val);
            if (p < 0) {
                snprintf(err, errsz, "%s:%d: unknown theme '%s'", path, lineno, val);
                fclose(f);
                return -1;
            }
            theme_apply_preset(c, p);
        } else if (!strcasecmp(key, "bar_length")) {
            c->bar_length = atoi(val);
            if (c->bar_length < 4 || c->bar_length > 200) {
                snprintf(err, errsz, "%s:%d: bar_length out of range 4..200", path, lineno);
                fclose(f);
                return -1;
            }
        } else if (!strcasecmp(key, "db_min")) {
            c->db_min = atoi(val);
            if (c->db_min < -120 || c->db_min > -6) {
                snprintf(err, errsz, "%s:%d: db_min out of range -120..-6", path, lineno);
                fclose(f);
                return -1;
            }
        } else if (!strcasecmp(key, "db_max")) {
            c->db_max = atoi(val);
            if (c->db_max < c->db_min + 6 || c->db_max > 0) {
                snprintf(err, errsz, "%s:%d: db_max must be db_min+6..0", path, lineno);
                fclose(f);
                return -1;
            }
        } else if (!strcasecmp(key, "release_dbps")) {
            c->release_dbps = strtof(val, NULL);
        } else if (!strcasecmp(key, "hold_decay_dbps")) {
            c->hold_decay_dbps = strtof(val, NULL);
        } else if (!strcasecmp(key, "rms_window")) {
            c->rms_window = strtof(val, NULL);
        } else if (!strcasecmp(key, "color_normal") || !strcasecmp(key, "color_hot") ||
                   !strcasecmp(key, "color_peak") || !strcasecmp(key, "color_scale") ||
                   !strcasecmp(key, "color_text")) {
            int color = theme_parse_color(val);
            if (color < 0 && strcasecmp(val, "default") != 0) {
                snprintf(err, errsz, "%s:%d: bad color '%s'", path, lineno, val);
                fclose(f);
                return -1;
            }
            if (!strcasecmp(key, "color_normal"))      c->colors.normal = color;
            else if (!strcasecmp(key, "color_hot"))     c->colors.hot = color;
            else if (!strcasecmp(key, "color_peak"))    c->colors.peak = color;
            else if (!strcasecmp(key, "color_scale"))   c->colors.scale = color;
            else                                        c->colors.text = color;
            c->preset_idx = -1;
        } else {
            snprintf(err, errsz, "%s:%d: unknown key '%s'", path, lineno, key);
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}
