#include "meter.h"
#include <math.h>
#include <stdio.h>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, msg); fails++; } } while (0)
#define APPROX(a, b, eps, msg) CHECK(fabs((a) - (b)) <= (eps), msg)

int main(void)
{
    meter_settings cfg = { 30.0f, 6.0f, 0.01f };
    const double rate = 48000;

    CHECK(meter_db_to_cell(-60, -60, 0, 40) == 0, "floor maps to cell 0");
    CHECK(meter_db_to_cell(0, -60, 0, 40) == 39, "0 dB maps to last cell");
    CHECK(meter_db_to_cell(-59, -60, 0, 40) >= 1, "just above floor >= 1");
    CHECK(meter_db_to_cell(5, -60, 0, 40) == 39, "clamp above range");

    channel_meter m;
    float buf[480];
    for (int i = 0; i < 480; i++) buf[i] = (float)sin(2 * M_PI * 440 * i / rate);

    meter_init(&m);
    meter_process(&m, buf, 0, rate, &cfg);            /* no data: untouched */
    APPROX(m.level_db, METER_DB_FLOOR, 0.01, "init level = floor");

    meter_process(&m, buf, 480, rate, &cfg);          /* FS sine, exactly one window */
    APPROX(m.hold_db, 0.0, 0.2, "FS sine peak hold ≈ 0 dB");
    APPROX(m.level_db, -3.0, 0.5, "FS sine RMS ≈ -3 dB");
    APPROX(m.max_db, 0.0, 0.2, "max tracks peak");

    float half[480];
    for (int i = 0; i < 480; i++) half[i] = 0.5f * (float)sin(2 * M_PI * 440 * i / rate);
    meter_init(&m);
    meter_process(&m, half, 480, rate, &cfg);
    APPROX(m.hold_db, -6.0, 0.3, "half-amp peak ≈ -6 dB");
    APPROX(m.level_db, -9.0, 0.5, "half-amp RMS ≈ -9 dB");

    meter_init(&m);
    meter_process(&m, buf, 480, rate, &cfg);
    float silent[480] = { 0 };
    meter_process(&m, silent, 480, rate, &cfg);       /* 10 ms of silence */
    APPROX(m.hold_db, -6.0 * 0.01, 0.05, "hold decays at 6 dB/s");
    APPROX(m.level_db, -3.0 - 30.0 * 0.01, 0.15, "level releases at 30 dB/s");

    meter_reset_ref(&m);
    APPROX(m.max_db, METER_DB_FLOOR, 0.01, "reset_ref clears max");

    meter_init(&m);
    meter_process(&m, buf, 200, rate, &cfg);
    meter_process(&m, buf + 200, 280, rate, &cfg);    /* split across calls */
    APPROX(m.hold_db, 0.0, 0.2, "split chunks keep the peak");
    CHECK(m.rem_n < 480, "remainder bounded by window");

    if (fails) { printf("%d FAILURES\n", fails); return 1; }
    printf("all meter tests passed\n");
    return 0;
}
