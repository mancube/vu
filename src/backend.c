#include "backend.h"
#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>
#include <pulse/sample.h>
#include <pulse/simple.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Target API: PulseAudio-4-style headers/lib (this distro). PipeWire serves
 * the same Pulse protocol, so these calls work against it as well. */

static char errbuf[256] = "no backend error";
static void set_err(const char *s) { snprintf(errbuf, sizeof errbuf, "%s", s); }
const char *backend_error(void) { return errbuf; }

static float conv_sample(const uint8_t *p, int format)
{
    switch (format) {
    case PA_SAMPLE_FLOAT32LE: { float f; memcpy(&f, p, 4); return f; }
    case PA_SAMPLE_S32LE: { int32_t v; memcpy(&v, p, 4); return (float)v / 2147483648.0f; }
    default:              { int16_t v; memcpy(&v, p, 2); return (float)v / 32768.0f; }
    }
}

struct ctx_cleanup {
    pa_mainloop *ml;
    pa_context *ctx;
};

static int ctx_setup(struct ctx_cleanup *C, const char *name)
{
    C->ml = pa_mainloop_new();
    C->ctx = pa_context_new(pa_mainloop_get_api(C->ml), name);
    int ok = 1;
    if (pa_context_connect(C->ctx, NULL, 0, NULL) < 0) {
        set_err("cannot connect to Pulse/PipeWire server");
        ok = 0;
    }
    while (ok && pa_context_get_state(C->ctx) != PA_CONTEXT_READY)
        pa_mainloop_iterate(C->ml, 0, &ok);
    if (!ok) set_err("Pulse context error");
    return ok ? 0 : -1;
}

static void ctx_cleanup(struct ctx_cleanup *C)
{
    if (C->ctx) { pa_context_disconnect(C->ctx); pa_context_unref(C->ctx); }
    if (C->ml) pa_mainloop_free(C->ml);
}

struct src_query {
    pa_sample_spec ss;
    pa_channel_map cm;
    int got;
};

static void on_src_info(pa_context *c, const pa_source_info *i, int eol, void *ud)
{
    (void)c; (void)eol;
    struct src_query *q = ud;
    if (i) {
        q->ss = i->sample_spec;
        q->cm = i->channel_map;
        q->got = 1;
    }
}

static int query_source(const char *name, pa_sample_spec *ss, pa_channel_map *cm)
{
    struct ctx_cleanup C;
    struct src_query q = { 0 };
    if (ctx_setup(&C, "vu-query") != 0) return -1;
    pa_context_get_source_info_by_name(C.ctx, name, on_src_info, &q);
    int ok = 1;
    while (ok && !q.got) pa_mainloop_iterate(C.ml, 0, &ok);
    ctx_cleanup(&C);
    if (!q.got) { set_err("source not found"); return -1; }
    *ss = q.ss;
    *cm = q.cm;
    return 0;
}

int backend_open(backend_t *b, const char *device)
{
    memset(b, 0, sizeof *b);
    pa_sample_spec ss;
    pa_channel_map cm;
    if (query_source(device, &ss, &cm) != 0) return -1;

    int fmt_ok = 0;
    switch (ss.format) {
    case PA_SAMPLE_FLOAT32LE:
    case PA_SAMPLE_S32LE:
    case PA_SAMPLE_S16LE:
        fmt_ok = 1;
        break;
    default:
        break;
    }
    if (!fmt_ok || ss.channels < 1) {
        set_err("unsupported monitor sample format or channel count");
        return -1;
    }

    int err = 0;
    pa_simple *s = pa_simple_new(NULL, "vu", PA_STREAM_RECORD, device, "vu monitor",
                                 &ss, &cm, NULL, &err);
    if (!s) {
        set_err(pa_strerror(err));
        return -1;
    }
    b->simple = s;
    b->channels = ss.channels;
    b->ch_bytes = pa_frame_size(&ss);
    b->frame_bytes = b->ch_bytes * ss.channels;
    b->format = ss.format;
    b->rate = ss.rate;
    return 0;
}

void backend_close(backend_t *b)
{
    if (b->simple) { pa_simple_free(b->simple); b->simple = NULL; }
}

int backend_read(backend_t *b, float *out, size_t max_frames)
{
    size_t bytes = max_frames * (size_t)b->frame_bytes;
    uint8_t *raw = malloc(bytes);
    if (!raw) { set_err("out of memory"); return -1; }
    int err = 0;
    int r = pa_simple_read((pa_simple *)b->simple, raw, bytes, &err);
    if (r < 0) {
        set_err(pa_strerror(err));
        free(raw);
        return -1;
    }
    size_t frames = (size_t)r / b->frame_bytes;
    for (size_t i = 0; i < frames; i++) {
        const uint8_t *p = raw + i * (size_t)b->frame_bytes;
        float l = conv_sample(p, b->format);
        float rr = b->channels > 1 ? conv_sample(p + b->ch_bytes, b->format) : l;
        out[i * 2] = l;
        out[i * 2 + 1] = rr;
    }
    free(raw);
    return (int)frames;
}

struct list_ctx {
    char names[VU_MAX_MONITORS][128];
    int count;
    int done;   /* set on eol of the source list */
};

static void on_list_src(pa_context *c, const pa_source_info *i, int eol, void *ud)
{
    (void)c;
    struct list_ctx *L = ud;
    if (eol) { L->done = 1; return; }
    if (!i || strstr(i->name, ".monitor") == NULL) return;
    if (L->count < VU_MAX_MONITORS) {
        snprintf(L->names[L->count], 128, "%s", i->name);
        L->count++;
    }
}

int backend_list_monitors(char names[VU_MAX_MONITORS][128], int *count)
{
    *count = 0;
    struct ctx_cleanup C;
    struct list_ctx L = { 0 };
    if (ctx_setup(&C, "vu-list") != 0) return -1;
    pa_context_get_source_info_list(C.ctx, on_list_src, &L);
    int ok = 1;
    while (ok && !L.done) pa_mainloop_iterate(C.ml, 0, &ok);
    ctx_cleanup(&C);
    if (!ok) { set_err("Pulse context error"); return -1; }
    for (int i = 0; i < L.count; i++)
        snprintf(names[i], 128, "%s", L.names[i]);
    *count = L.count;
    return 0;
}

struct sink_query {
    char name[128];
    int got;
};

static void on_sink_info(pa_context *c, const pa_sink_info *i, int eol, void *ud)
{
    (void)c;
    struct sink_query *S = ud;
    if (eol) return;
    if (i) {
        snprintf(S->name, sizeof S->name, "%s", i->name);
        S->got = 1;
    }
}

int backend_default_monitor(char names[VU_MAX_MONITORS][128], int *count, char *def, int defsz)
{
    if (backend_list_monitors(names, count) != 0 || *count == 0) {
        set_err("no monitor sources found");
        return -1;
    }
    struct ctx_cleanup C;
    struct sink_query S = { 0 };
    if (ctx_setup(&C, "vu-default") != 0) return -1;
    pa_context_get_sink_info_by_name(C.ctx, "default", on_sink_info, &S);
    int ok = 1;
    while (ok && !S.got) pa_mainloop_iterate(C.ml, 0, &ok);
    ctx_cleanup(&C);

    int found = 0;
    if (S.name[0]) {
        char want[256];
        snprintf(want, sizeof want, "%s.monitor", S.name);
        for (int i = 0; i < *count; i++)
            if (strcmp(names[i], want) == 0) {
                snprintf(def, defsz, "%s", names[i]);
                found = 1;
            }
    }
    if (!found) snprintf(def, defsz, "%s", names[0]);
    if (!ok) { set_err("Pulse context error"); return -1; }
    return 0;
}
