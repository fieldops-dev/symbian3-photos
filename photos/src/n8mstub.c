#include <stdlib.h>
#include <string.h>

#include "n8media.h"

#define STUB_W 320
#define STUB_H 240
#define STUB_FPS 25
#define STUB_MS 60000
#define STUB_RATE 44100
#define STUB_GOP_MS 2000
#define STUB_UNSUPPORTED (-2)
#define STUB_NOMEM (-3)
#define STUB_ARG (-4)

struct n8m_file {
    unsigned char *y, *u, *v;
    long long frame;
    long long sample;
};

static int is_test(const char *path, int *rot90)
{
    static const char key[] = "n8mtest", rot[] = "rot90";
    const char *name = path, *p;
    int found = 0, turned = 0;
    for (p = path; *p; p++)
        if (*p == '\\' || *p == '/')
            name = p + 1;
    for (p = name; *p; p++) {
        size_t i;
        for (i = 0; key[i] && p[i] && (p[i] | 0x20) == key[i]; i++)
            ;
        if (!key[i])
            found = 1;
        for (i = 0; rot[i] && p[i] && (p[i] | 0x20) == rot[i]; i++)
            ;
        if (!rot[i])
            turned = 1;
    }
    *rot90 = turned;
    return found;
}

static int sine(unsigned phase)
{
    long long x = phase & 0x7FFF, s;
    s = 16 * x * (32768 - x);
    s = s * 32767 / (5LL * 32768 * 32768 - 4 * x * (32768 - x));
    return (phase & 0x8000) ? -(int)s : (int)s;
}

static void draw_frame(struct n8m_file *f, long long n)
{
    static const unsigned char bars[8][3] = {
        { 235, 128, 128 }, { 210, 16, 146 }, { 170, 166, 16 }, { 145, 54, 34 },
        { 106, 202, 222 }, { 81, 90, 240 }, { 41, 240, 110 }, { 16, 128, 128 } };
    const long long ms = n * 1000 / STUB_FPS;
    const int shift = (int)(n * 4 % STUB_W), flash = ms % 1000 < 80;
    const int sx = (int)(ms * STUB_W / 4000 % (STUB_W - 48)), sy = STUB_H / 2 - 24;
    int x, y;
    for (y = 0; y < STUB_H; y++)
        for (x = 0; x < STUB_W; x++) {
            const int bar = (x + shift) % STUB_W * 8 / STUB_W;
            int value = bars[bar][0];
            if (flash && y < STUB_H / 4)
                value = 235;
            if (x >= sx && x < sx + 48 && y >= sy && y < sy + 48)
                value = (n & 1) ? 235 : 16;
            f->y[y * STUB_W + x] = (unsigned char)value;
            if (!(x & 1) && !(y & 1)) {
                f->u[y / 2 * (STUB_W / 2) + x / 2] = bars[bar][1];
                f->v[y / 2 * (STUB_W / 2) + x / 2] = bars[bar][2];
            }
        }
}

static void fill_info(n8m_info *info, int rot90)
{
    memset(info, 0, sizeof(*info));
    info->has_video = info->has_audio = 1;
    info->width = info->display_width = STUB_W;
    info->height = info->display_height = STUB_H;
    info->rotation = rot90 ? 90 : 0;
    info->duration_ms = STUB_MS;
    info->fps_num = STUB_FPS;
    info->fps_den = 1;
    info->audio_rate = STUB_RATE;
    info->audio_channels = 2;
    strcpy(info->video_codec, "stub");
    strcpy(info->audio_codec, "stub");
    strcpy(info->container, "stub");
}

int n8m_open(const char *path_utf8, n8m_file **out, n8m_info *info)
{
    struct n8m_file *f;
    int rot90;
    if (!path_utf8 || !out || !info)
        return STUB_ARG;
    *out = NULL;
    if (!is_test(path_utf8, &rot90))
        return STUB_UNSUPPORTED;
    f = (struct n8m_file *)calloc(1, sizeof(*f));
    if (!f)
        return STUB_NOMEM;
    f->y = (unsigned char *)malloc(STUB_W * STUB_H * 3 / 2);
    if (!f->y) {
        free(f);
        return STUB_NOMEM;
    }
    f->u = f->y + STUB_W * STUB_H;
    f->v = f->u + STUB_W * STUB_H / 4;
    fill_info(info, rot90);
    *out = f;
    return 0;
}

int n8m_video(n8m_file *f, n8m_frame *frame)
{
    if (!f || !frame)
        return STUB_ARG;
    if (f->frame * 1000 / STUB_FPS >= STUB_MS)
        return N8M_EOF;
    draw_frame(f, f->frame);
    frame->y = f->y;
    frame->u = f->u;
    frame->v = f->v;
    frame->y_stride = STUB_W;
    frame->uv_stride = STUB_W / 2;
    frame->width = STUB_W;
    frame->height = STUB_H;
    frame->pts_ms = f->frame * 1000 / STUB_FPS;
    f->frame++;
    return 0;
}

int n8m_audio(n8m_file *f, short *pcm, int max_samples_per_channel, int *got, long long *pts_ms)
{
    const long long total = (long long)STUB_MS * STUB_RATE / 1000;
    int i, n;
    if (!f || !pcm || !got || !pts_ms || max_samples_per_channel <= 0)
        return STUB_ARG;
    if (f->sample >= total)
        return N8M_EOF;
    n = max_samples_per_channel;
    if (n > total - f->sample)
        n = (int)(total - f->sample);
    *pts_ms = f->sample * 1000 / STUB_RATE;
    for (i = 0; i < n; i++) {
        const long long s = f->sample + i;
        const int beep = s % STUB_RATE < STUB_RATE / 10;
        const int value = beep ? sine((unsigned)(s * 65536 * 1000 / STUB_RATE)) / 8 : 0;
        pcm[2 * i] = pcm[2 * i + 1] = (short)value;
    }
    f->sample += n;
    *got = n;
    return 0;
}

int n8m_seek(n8m_file *f, long long ms)
{
    if (!f)
        return STUB_ARG;
    if (ms < 0)
        ms = 0;
    if (ms > STUB_MS)
        ms = STUB_MS;
    ms -= ms % STUB_GOP_MS;
    f->frame = ms * STUB_FPS / 1000;
    f->sample = ms * STUB_RATE / 1000;
    return 0;
}

int n8m_poster(const char *path_utf8, int max_side, unsigned char *rgb565, int *w, int *h)
{
    n8m_file *f;
    n8m_info info;
    n8m_frame frame;
    int err, x, y, ow, oh;
    if (!rgb565 || !w || !h || max_side <= 0)
        return STUB_ARG;
    err = n8m_open(path_utf8, &f, &info);
    if (err)
        return err;
    f->frame = STUB_FPS * 3 / 2;
    err = n8m_video(f, &frame);
    if (err) {
        n8m_close(f);
        return err;
    }
    ow = STUB_W >= STUB_H ? max_side : max_side * STUB_W / STUB_H;
    oh = STUB_W >= STUB_H ? max_side * STUB_H / STUB_W : max_side;
    for (y = 0; y < oh; y++)
        for (x = 0; x < ow; x++) {
            const int sx = x * STUB_W / ow, sy = y * STUB_H / oh;
            const int c = (frame.y[sy * frame.y_stride + sx] - 16) * 298;
            const int d = frame.u[sy / 2 * frame.uv_stride + sx / 2] - 128;
            const int e = frame.v[sy / 2 * frame.uv_stride + sx / 2] - 128;
            int r = (c + 409 * e + 128) >> 8, g = (c - 100 * d - 208 * e + 128) >> 8, b = (c + 516 * d + 128) >> 8;
            unsigned p;
            r = r < 0 ? 0 : r > 255 ? 255 : r;
            g = g < 0 ? 0 : g > 255 ? 255 : g;
            b = b < 0 ? 0 : b > 255 ? 255 : b;
            p = ((unsigned)(r >> 3) << 11) | ((unsigned)(g >> 2) << 5) | (unsigned)(b >> 3);
            rgb565[(y * ow + x) * 2] = (unsigned char)(p & 0xFF);
            rgb565[(y * ow + x) * 2 + 1] = (unsigned char)(p >> 8);
        }
    *w = ow;
    *h = oh;
    n8m_close(f);
    return 0;
}

void n8m_close(n8m_file *f)
{
    if (!f)
        return;
    free(f->y);
    free(f);
}

const char *n8m_error(int code)
{
    switch (code) {
    case 0: return "ok";
    case N8M_EOF: return "end of stream";
    case STUB_UNSUPPORTED: return "not a test clip (n8media stub)";
    case STUB_NOMEM: return "out of memory";
    case STUB_ARG: return "bad argument";
    default: return "error";
    }
}

void n8m_set_allocator(void *(*malloc_fn)(size_t), void *(*realloc_fn)(void *, size_t), void (*free_fn)(void *))
{
    (void)malloc_fn;
    (void)realloc_fn;
    (void)free_fn;
}

void n8m_set_log(void (*log_fn)(int level, const char *message))
{
    (void)log_fn;
}
