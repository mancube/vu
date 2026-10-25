#ifndef VU_BACKEND_H
#define VU_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#define VU_MAX_MONITORS 64

typedef struct {
    void *simple;      /* pa_simple* */
    int  channels;     /* native channel count of the source */
    int  frame_bytes;  /* all channels, one frame */
    int  ch_bytes;     /* one channel */
    int  format;       /* pa_sample_format_t */
    double rate;       /* sample rate */
} backend_t;

int  backend_open(backend_t *b, const char *device);
void backend_close(backend_t *b);
/* Monitor sources stream continuously (silence included), so reads
 * self-pace at ~max_frames/rate seconds. */
int  backend_read(backend_t *b, float *out, size_t max_frames);
int  backend_list_monitors(char names[VU_MAX_MONITORS][128], int *count);
int  backend_default_monitor(char names[VU_MAX_MONITORS][128], int *count, char *def, int defsz);
const char *backend_error(void);

#endif
