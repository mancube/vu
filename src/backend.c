#include "backend.h"
#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>
#include <pulse/sample.h>
#include <pulse/stream.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This system's libpulse-simple runtime (old 4.x-era .so) never delivers
 * data — pa_simple_read always returns 0 — so we use the full
 * pa_context/pa_stream API instead. Signatures are the old-style API that
 * this system's Pulse headers expose. */

struct backend_priv {
    pa_mainloop *ml;
    pa_context  *ctx;
    pa_stream   *stream;
    int          failed;
    float       *stage;      /* peeked audio staged as L,R float pairs */
    size_t       staged_n;   /* frames currently staged */
    size_t       stage_cap;  /* frame capacity of stage */
};

#define STAGE_CAP 16384 /* frames; 343 ms @48 kHz — beyond any wire chunk */

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
    if (pa_context_connect(C->ctx, NULL, 0, NULL) < 0) {
        set_err("cannot connect to Pulse/PipeWire server");
        return -1;
    }
    while (pa_context_get_state(C->ctx) != PA_CONTEXT_READY)
        if (pa_mainloop_iterate(C->ml, 1, NULL) < 0) {
            set_err("Pulse context error");
            return -1;
        }
    return 0;
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
    int rv = 0;
    while (!q.got)
        if ((rv = pa_mainloop_iterate(C.ml, 1, NULL)) < 0) break;
    ctx_cleanup(&C);
    if (rv < 0) { set_err("Pulse context error"); return -1; }
    if (!q.got) { set_err("source not found"); return -1; }
    *ss = q.ss;
    *cm = q.cm;
    return 0;
}

static void on_stream_state(pa_stream *s, void *ud)
{
    struct backend_priv *p = ud;
    if (pa_stream_get_state(s) == PA_STREAM_FAILED) p->failed = 1;
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

    struct backend_priv *p = calloc(1, sizeof *p);
    if (!p) { set_err("out of memory"); return -1; }
    b->p = p;
    b->channels = ss.channels;
    /* bytes per sample: only S16LE(2) / S32LE, FLOAT32LE(4) are accepted above */
    b->ch_bytes = (ss.format == PA_SAMPLE_S16LE) ? 2 : 4;
    b->frame_bytes = b->ch_bytes * ss.channels; /* all channels, one frame */
    b->format = ss.format;
    b->rate = ss.rate;
    p->stage_cap = STAGE_CAP;
    p->stage = malloc(STAGE_CAP * 2 * sizeof(float));
    if (!p->stage) { set_err("out of memory"); goto fail; }

    p->ml = pa_mainloop_new();
    p->ctx = pa_context_new(pa_mainloop_get_api(p->ml), "vu");
    if (pa_context_connect(p->ctx, NULL, 0, NULL) < 0) {
        set_err("cannot connect to Pulse/PipeWire server");
        goto fail;
    }
    while (pa_context_get_state(p->ctx) != PA_CONTEXT_READY)
        if (pa_mainloop_iterate(p->ml, 1, NULL) < 0) {
            set_err("Pulse context error");
            goto fail;
        }

    p->stream = pa_stream_new(p->ctx, "vu meter", &ss, &cm);
    pa_stream_set_state_callback(p->stream, on_stream_state, p);
    pa_buffer_attr ba;
    memset(&ba, 0, sizeof ba);
    /* One wire quantum (~1024 frames): measured staleness ~= tlength/2, so a
     * 200 ms buffer meant the meter showed 100+ ms-old audio. A quantum
     * costs only ~10 ms and still delivers full-rate chunks.
     * maxlength must be >0: with 0 this PipeWire 1.6.10 server trickles 1
     * frame per wire write; tlength==maxlength gives ~full-rate quanta. */
    ba.tlength = (uint32_t)(ss.rate / 48) * b->frame_bytes; /* ~1000 frames */
    ba.maxlength = ba.tlength;
    if (pa_stream_connect_record(p->stream, device, &ba, 0) < 0) {
        set_err(pa_strerror(pa_context_errno(p->ctx)));
        goto fail;
    }
    int rv = 0;
    while (pa_stream_get_state(p->stream) != PA_STREAM_READY && !p->failed)
        if ((rv = pa_mainloop_iterate(p->ml, 1, NULL)) < 0) break;
    if (p->failed) { set_err("stream failed"); goto fail; }
    if (rv < 0) { set_err("Pulse mainloop error"); goto fail; }
    return 0;

fail:
    backend_close(b);
    return -1;
}

void backend_close(backend_t *b)
{
    if (!b->p) return;
    struct backend_priv *p = b->p;
    if (p->stream) { pa_stream_disconnect(p->stream); pa_stream_unref(p->stream); }
    if (p->ctx) { pa_context_disconnect(p->ctx); pa_context_unref(p->ctx); }
    if (p->ml) pa_mainloop_free(p->ml);
    free(p->stage);
    free(p);
    b->p = NULL;
}

int backend_read(backend_t *b, float *out, size_t max_frames)
{
    struct backend_priv *p = b->p;
    if (!p) return -1;

    if (p->staged_n > 0) {
        size_t take = p->staged_n < max_frames ? p->staged_n : max_frames;
        memcpy(out, p->stage, take * 2 * sizeof(float));
        memmove(p->stage, p->stage + take * 2, (p->staged_n - take) * 2 * sizeof(float));
        p->staged_n -= take;
        return (int)take;
    }

    const void *data = NULL;
    size_t nbytes = 0;
    for (;;) {
        if (p->failed) { set_err("stream failed"); return -1; }
        if (pa_stream_peek(p->stream, &data, &nbytes) < 0) {
            set_err("stream read error");
            return -1;
        }
        if (nbytes > 0) break;
        /* block=1: sleep in the mainloop until the next wire chunk (self-pacing) */
        if (pa_mainloop_iterate(p->ml, 1, NULL) < 0) { set_err("Pulse mainloop error"); return -1; }
    }

    size_t avail = nbytes / (size_t)b->frame_bytes;
    size_t conv = avail < p->stage_cap ? avail : p->stage_cap;
    for (size_t i = 0; i < conv; i++) {
        const uint8_t *s = (const uint8_t *)data + i * (size_t)b->frame_bytes;
        float l = conv_sample(s, b->format);
        float r = b->channels > 1 ? conv_sample(s + b->ch_bytes, b->format) : l;
        p->stage[i * 2] = l;
        p->stage[i * 2 + 1] = r;
    }
    p->staged_n = conv;
    pa_stream_drop(p->stream); /* consume the whole peeked chunk */

    size_t take = p->staged_n < max_frames ? p->staged_n : max_frames;
    memcpy(out, p->stage, take * 2 * sizeof(float));
    memmove(p->stage, p->stage + take * 2, (p->staged_n - take) * 2 * sizeof(float));
    p->staged_n -= take;
    return (int)take;
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
    int rv = 0;
    while (!L.done)
        if ((rv = pa_mainloop_iterate(C.ml, 1, NULL)) < 0) break;
    ctx_cleanup(&C);
    if (rv < 0) { set_err("Pulse context error"); return -1; }
    for (int i = 0; i < L.count; i++)
        snprintf(names[i], 128, "%s", L.names[i]);
    *count = L.count;
    return 0;
}

struct srv_query {
    char default_sink[128];
    int done;
    pa_operation *op;
};

static void on_server_info(pa_context *c, const pa_server_info *i, void *ud)
{
    (void)c;
    struct srv_query *S = ud;
    if (i && i->default_sink_name)
        snprintf(S->default_sink, sizeof S->default_sink, "%s", i->default_sink_name);
    S->done = 1;
    if (S->op) pa_operation_unref(S->op);
}

int backend_default_monitor(char names[VU_MAX_MONITORS][128], int *count, char *def, int defsz)
{
    if (backend_list_monitors(names, count) != 0 || *count == 0) {
        set_err("no monitor sources found");
        return -1;
    }
    struct ctx_cleanup C;
    struct srv_query S = { 0 };
    if (ctx_setup(&C, "vu-default") != 0) return -1;
    /* by-name lookup of the "default" alias never fires on the PipeWire
     * bridge; the server-info query is the era-correct way. */
    S.op = pa_context_get_server_info(C.ctx, on_server_info, &S);
    int rv = 0;
    while (!S.done)
        if ((rv = pa_mainloop_iterate(C.ml, 1, NULL)) < 0) break;
    ctx_cleanup(&C);
    if (rv < 0 || !S.done) { set_err("Pulse context error"); return -1; }

    int found = 0;
    if (S.default_sink[0]) {
        char want[256];
        snprintf(want, sizeof want, "%s.monitor", S.default_sink);
        for (int i = 0; i < *count; i++)
            if (strcmp(names[i], want) == 0) {
                snprintf(def, defsz, "%s", names[i]);
                found = 1;
            }
    }
    if (!found) snprintf(def, defsz, "%s", names[0]);
    return 0;
}
