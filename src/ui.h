#ifndef VU_UI_H
#define VU_UI_H

#include "config.h"
#include "meter.h"

void ui_init(void);
void ui_shutdown(void);
void ui_apply_theme(const theme_colors *c);
void ui_draw(const channel_meter meters[2], const config_t *cfg, int units_rel,
             int paused, const char *device, const char *status);

#endif
