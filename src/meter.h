#ifndef VU_METER_H
#define VU_METER_H

#include <stddef.h>

#define METER_DB_FLOOR (-120.0f)
#define METER_MAX_SAMPLES 4096

typedef struct {
    float release_dbps;     /* bar fall speed after the level drops */
    float hold_decay_dbps;  /* peak-hold fall speed */
    float rms_window;       /* seconds per RMS frame */
} meter_settings;

typedef struct {
    float level_db;         /* smoothed RMS level */
    float hold_db;          /* decaying peak hold */
    float max_db;           /* highest peak since last reset_ref */
    float remain[METER_MAX_SAMPLES];
    size_t rem_n;
} channel_meter;

void  meter_init(channel_meter *m);
void  meter_reset_ref(channel_meter *m);
void  meter_process(channel_meter *m, const float *s, size_t n, double rate,
                    const meter_settings *cfg);
int   meter_db_to_cell(float db, int db_min, int db_max, int cells);

#endif
