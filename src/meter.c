#include "meter.h"
#include <math.h>
#include <string.h>

void meter_init(channel_meter *m)
{
    memset(m->remain, 0, sizeof m->remain);
    m->level_db = METER_DB_FLOOR;
    m->hold_db  = METER_DB_FLOOR;
    m->max_db   = METER_DB_FLOOR;
    m->rem_n    = 0;
}

void meter_reset_ref(channel_meter *m) { m->max_db = METER_DB_FLOOR; }

int meter_db_to_cell(float db, int db_min, int db_max, int cells)
{
    if (cells < 1) cells = 1;
    if (db <= (float)db_min) return 0;
    if (db >= (float)db_max) return cells - 1;
    int i = (int)floor((double)(db - db_min) / (db_max - db_min) * (cells - 1) + 0.5);
    return i > cells - 1 ? cells - 1 : i;
}

void meter_process(channel_meter *m, const float *s, size_t n, double rate,
                   const meter_settings *cfg)
{
    if (n == 0 || rate <= 0) return;
    size_t frame = (size_t)(rate * cfg->rms_window);
    if (frame < 1) frame = 1;

    float buf[METER_MAX_SAMPLES];
    size_t total = 0;
    if (m->rem_n) {
        total = m->rem_n < METER_MAX_SAMPLES ? m->rem_n : METER_MAX_SAMPLES;
        memcpy(buf, m->remain, total * sizeof(float));
        size_t take = n < METER_MAX_SAMPLES - total ? n : METER_MAX_SAMPLES - total;
        memcpy(buf + total, s, take * sizeof(float));
        total += take;
    } else {
        total = n < METER_MAX_SAMPLES ? n : METER_MAX_SAMPLES;
        memcpy(buf, s, total * sizeof(float));
    }

    double dt = (double)frame / rate;
    size_t i = 0;
    for (; i + frame <= total; i += frame) {
        double sum = 0.0;
        float peak = 0.0f;
        for (size_t j = 0; j < frame; j++) {
            float v = buf[i + j];
            float a = v < 0 ? -v : v;
            if (a > peak) peak = a;
            sum += (double)v * v;
        }
        float rms_db = METER_DB_FLOOR;
        if (sum > 0) rms_db = 10.0f * log10f((float)(sum / (double)frame));
        float pk = METER_DB_FLOOR;
        if (peak > 1e-7f) pk = 20.0f * log10f(peak);

        if (rms_db > m->level_db) m->level_db = rms_db;
        else m->level_db -= cfg->release_dbps * (float)dt;
        if (pk > m->hold_db) m->hold_db = pk;
        else m->hold_db -= cfg->hold_decay_dbps * (float)dt;
        if (m->level_db < METER_DB_FLOOR) m->level_db = METER_DB_FLOOR;
        if (m->hold_db < METER_DB_FLOOR) m->hold_db = METER_DB_FLOOR;
        if (m->hold_db > m->max_db) m->max_db = m->hold_db;
    }
    m->rem_n = total - i;
    if (m->rem_n) memcpy(m->remain, buf + i, m->rem_n * sizeof(float));
}
