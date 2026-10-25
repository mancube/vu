#include "config.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, msg); fails++; } } while (0)

int main(void)
{
    CHECK(theme_parse_color("green") == 2, "named color");
    CHECK(theme_parse_color("190") == 190, "integer color");
    CHECK(theme_parse_color("banana") == -1, "bad color");
    CHECK(theme_parse_color("default") == -1, "default keyword");
    CHECK(theme_find_preset("neon") >= 0, "find preset");
    CHECK(theme_find_preset("nope") == -1, "unknown preset");

    config_t c;
    config_defaults(&c);
    CHECK(c.bar_length == 40 && c.db_min == -60 && c.db_max == 0, "defaults");
    CHECK(c.preset_idx == 0, "default preset");

    const char *p = "tests/tmp_config.conf";
    char err[256] = { 0 };

    FILE *f = fopen(p, "w");
    fputs("theme = neon\nbar_length = 24\n# comment\n", f);
    fclose(f);
    config_t c2;
    config_defaults(&c2);
    CHECK(config_load_file(&c2, p, err, sizeof err) == 0, "load ok");
    CHECK(c2.bar_length == 24, "bar_length from file");
    CHECK(c2.preset_idx == 3, "preset from file (neon is last)");

    f = fopen(p, "w");
    fputs("color_peak = 196\n", f);
    fclose(f);
    config_t c3;
    config_defaults(&c3);
    CHECK(config_load_file(&c3, p, err, sizeof err) == 0, "load override ok");
    CHECK(c3.colors.peak == 196, "color override");
    CHECK(c3.preset_idx == -1, "override switches to custom");

    f = fopen(p, "w");
    fputs("color_scale = default\n", f);
    fclose(f);
    config_t c4;
    config_defaults(&c4);
    CHECK(config_load_file(&c4, p, err, sizeof err) == 0, "default keyword loads");
    CHECK(c4.colors.scale == -1, "default keyword = -1");

    f = fopen(p, "w");
    fputs("bogus_key = 1\n", f);
    fclose(f);
    CHECK(config_load_file(&c2, p, err, sizeof err) == -1, "unknown key rejected");

    CHECK(config_load_file(&c2, "tests/does_not_exist.conf", err, sizeof err) == 0,
          "missing file is not an error");
    remove(p);

    if (fails) { printf("%d FAILURES\n", fails); return 1; }
    printf("all config tests passed\n");
    return 0;
}
