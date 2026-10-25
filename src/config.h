#ifndef VU_CONFIG_H
#define VU_CONFIG_H

typedef struct { int normal, hot, peak, scale, text; } theme_colors;

typedef struct { const char *name; theme_colors c; } theme_preset;

typedef struct {
    theme_colors colors;
    int preset_idx;            /* index into VU_PRESETS, -1 = custom overrides */
    int bar_length;
    int db_min;
    int db_max;
    float release_dbps;
    float hold_decay_dbps;
    float rms_window;
} config_t;

extern const theme_preset VU_PRESETS[];
extern const int VU_PRESET_COUNT;

void  config_defaults(config_t *c);
int   config_load_file(config_t *c, const char *path, char *err, int errsz);
int   theme_parse_color(const char *s);          /* 0..255, -1 invalid */
int   theme_find_preset(const char *name);       /* -1 not found */
const char *theme_preset_name(int idx);          /* "custom" for -1 */
void  theme_apply_preset(config_t *c, int idx);
#endif
