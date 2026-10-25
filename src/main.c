#include "backend.h"
#include "config.h"
#include "meter.h"
#include "ui.h"
#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_CHUNK 960 /* 20 ms @ 48 kHz; per-loop read cap */

static const char *USAGE =
    "usage: vu [options]\n"
    "  -d, --device NAME   monitor source to meter (default: default sink's monitor)\n"
    "  -t, --theme NAME    default|warm|mono|neon\n"
    "  -b, --bar-length N  bar cells (4..200, default 40)\n"
    "      --db-min N      scale minimum (default -60)\n"
    "      --db-max N      scale maximum (default 0)\n"
    "  -c, --config PATH   config file (default ~/.config/vu/vu.conf)\n"
    "  -l, --list          list monitor sources and exit\n"
    "  -h, --help          this help\n";

int main(int argc, char **argv)
{
    char *config_path = NULL;
    char *cli_device = NULL;
    int have_theme = -2, have_len = 0, have_min = 0, have_max = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--list") || !strcmp(a, "-l")) {
            char names[VU_MAX_MONITORS][128];
            int count = 0;
            if (backend_list_monitors(names, &count) != 0 || count == 0) {
                fprintf(stderr, "vu: no monitor sources found (%s)\n", backend_error());
                return 1;
            }
            for (int j = 0; j < count; j++) printf("%s\n", names[j]);
            return 0;
        }
        else if ((!strcmp(a, "--device") || !strcmp(a, "-d")) && i + 1 < argc) cli_device = argv[++i];
        else if ((!strcmp(a, "--theme") || !strcmp(a, "-t")) && i + 1 < argc) {
            int p = theme_find_preset(argv[++i]);
            if (p < 0) { fprintf(stderr, "vu: unknown theme '%s'\n", argv[i]); return 1; }
            have_theme = p;
        }
        else if ((!strcmp(a, "--bar-length") || !strcmp(a, "-b")) && i + 1 < argc) {
            have_len = atoi(argv[++i]);
            if (have_len < 4 || have_len > 200) { fprintf(stderr, "vu: bar-length out of range 4..200\n"); return 1; }
        }
        else if (!strcmp(a, "--db-min") && i + 1 < argc) have_min = atoi(argv[++i]);
        else if (!strcmp(a, "--db-max") && i + 1 < argc) have_max = atoi(argv[++i]);
        else if ((!strcmp(a, "--config") || !strcmp(a, "-c")) && i + 1 < argc) config_path = argv[++i];
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { printf(USAGE); return 0; }
        else { fprintf(stderr, "%s", USAGE); return 1; }
    }

    /* config: defaults <- file <- CLI */
    config_t cfg;
    config_defaults(&cfg);
    if (!config_path) {
        const char *home = getenv("HOME");
        if (home) {
            static char p[4096];
            snprintf(p, sizeof p, "%s/.config/vu/vu.conf", home);
            config_path = p;
        }
    }
    char err[256] = { 0 };
    if (config_path && config_load_file(&cfg, config_path, err, sizeof err) != 0) {
        fprintf(stderr, "vu: config error: %s\n", err);
        return 1;
    }
    if (have_theme >= 0) theme_apply_preset(&cfg, have_theme);
    if (have_len) cfg.bar_length = have_len;
    if (have_min) cfg.db_min = have_min;
    if (have_max) cfg.db_max = have_max;
    if (cfg.db_min >= cfg.db_max) { fprintf(stderr, "vu: db_min must be < db_max\n"); return 1; }

    /* device */
    char names[VU_MAX_MONITORS][128];
    int count = 0;
    char device[256];
    if (cli_device) {
        snprintf(device, sizeof device, "%s", cli_device);
    } else if (backend_default_monitor(names, &count, device, sizeof device) != 0) {
        fprintf(stderr, "vu: %s\n", backend_error());
        return 1;
    }

    backend_t be;
    if (backend_open(&be, device) != 0) {
        fprintf(stderr, "vu: %s\n", backend_error());
        return 1;
    }

    ui_init();
    ui_apply_theme(&cfg.colors);

    meter_settings ms = { cfg.release_dbps, cfg.hold_decay_dbps, cfg.rms_window };
    channel_meter meters[2];
    meter_init(&meters[0]);
    meter_init(&meters[1]);

    int units_rel = 0;
    int paused = 0;
    int dead = 0; /* set when no device will open; blocks backend_read until 'd' recovers */
    char status[128] = "";
    int last = -1; /* index of current device in names[] */
    for (int i = 0; i < count; i++)
        if (strcmp(names[i], device) == 0) { last = i; break; }

    float *stereo = malloc((size_t)READ_CHUNK * 2 * sizeof(float));
    if (!stereo) { ui_shutdown(); fprintf(stderr, "vu: out of memory\n"); return 1; }

    for (;;) {
        if (!paused && !dead) {
            int frames = backend_read(&be, stereo, READ_CHUNK);
            if (frames < 0) {
                /* stream dead: flag it, then hop to the next device that opens;
                 * if none opens, stop retrying (avoids an open/close spin) */
                snprintf(status, sizeof status, " | error: %s", backend_error());
                int recovered = 0;
                for (int k = 1; k <= count; k++) {
                    int j = (last + k) % count;
                    backend_close(&be);
                    if (backend_open(&be, names[j]) == 0) {
                        snprintf(device, sizeof device, "%s", names[j]);
                        last = j;
                        status[0] = 0;
                        recovered = 1;
                        break;
                    }
                }
                if (!recovered) {
                    dead = 1;
                    snprintf(status, sizeof status, " | error: %s (no device available)", backend_error());
                }
            } else {
                status[0] = 0;
                for (int f = 0; f < frames; f++) {
                    meter_process(&meters[0], &stereo[f * 2], 1, be.rate, &ms);
                    meter_process(&meters[1], &stereo[f * 2 + 1], 1, be.rate, &ms);
                }
            }
        }
        ui_draw(meters, &cfg, units_rel, paused, device, status);

        /* poll keys for 20 ms; otherwise the next backend_read paces the loop
         * at the device's chunk rate (~50 fps). Queued keys drain on later passes. */
        timeout(20);
        int key = getch();
        if (key == ERR) continue;
        if (key == 'q' || key == 'Q' || key == 27) goto done;
        if (key == ' ') { paused = !paused; continue; }
        if (key == 't') {
            units_rel = !units_rel;
            meter_reset_ref(&meters[0]);
            meter_reset_ref(&meters[1]);
            continue;
        }
        if (key == 'c') {
            int next = cfg.preset_idx < 0 ? 0 : (cfg.preset_idx + 1) % VU_PRESET_COUNT;
            theme_apply_preset(&cfg, next);
            ui_apply_theme(&cfg.colors);
            continue;
        }
        if (key == 'd') {
            if (count == 0) { snprintf(status, sizeof status, " | no monitor sources"); continue; }
            char names2[VU_MAX_MONITORS][128];
            int count2 = 0;
            if (backend_list_monitors(names2, &count2) == 0 && count2 > 0) {
                memcpy(names, names2, sizeof names);
                count = count2;
                last = -1;
                for (int i = 0; i < count; i++)
                    if (strcmp(names[i], device) == 0) { last = i; break; }
            }
            last = (last + 1) % count;
            backend_close(&be);
            if (backend_open(&be, names[last]) == 0) {
                snprintf(device, sizeof device, "%s", names[last]);
                dead = 0;
            } else
                snprintf(status, sizeof status, " | cannot open %s", names[last]);
            continue;
        }
        /* KEY_RESIZE: fall through and redraw next frame */
    }
done:
    ui_shutdown();
    free(stereo);
    backend_close(&be);
    return 0;
}
