#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n8media.h"
#include "libavutil/md5.h"
#include "libavutil/mem.h"

static size_t allocs, live, peak;

static void *count_malloc(size_t n)
{
    size_t *p = malloc(n + 16);
    if (!p)
        return NULL;
    p[0] = n;
    allocs++;
    live += n;
    if (live > peak)
        peak = live;
    return p + 2;
}

static void count_free(void *q)
{
    size_t *p;
    if (!q)
        return;
    p = (size_t *)q - 2;
    live -= p[0];
    free(p);
}

static void *count_realloc(void *q, size_t n)
{
    size_t *p, old = 0;
    if (!q)
        return count_malloc(n);
    p = (size_t *)q - 2;
    old = p[0];
    p = realloc(p, n + 16);
    if (!p)
        return NULL;
    p[0] = n;
    live += n - old;
    if (live > peak)
        peak = live;
    return p + 2;
}

static void log_line(int level, const char *msg)
{
    fprintf(stderr, "[ffmpeg %d] %s", level, msg);
}

static void hex(const unsigned char *d, char *out)
{
    int i;
    for (i = 0; i < 16; i++)
        sprintf(out + 2 * i, "%02x", d[i]);
    out[32] = 0;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int poster = 0, count = 0, i, ret, n = 0;
    long long seek = -1;
    n8m_file *f;
    n8m_info info;
    n8m_frame fr;
    struct AVMD5 *md5;
    unsigned char digest[16];
    char text[33];

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--poster") && i + 1 < argc)
            poster = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seek") && i + 1 < argc)
            seek = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--count"))
            count = 1;
        else if (!strcmp(argv[i], "--log"))
            n8m_set_log(log_line);
        else
            path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "usage: host_decode [--poster side] [--seek ms] [--count] [--log] <clip>\n");
        return 2;
    }
    if (count)
        n8m_set_allocator(count_malloc, count_realloc, count_free);
    md5 = av_md5_alloc();

    if (poster) {
        unsigned char *rgb = malloc((size_t)poster * poster * 2);
        int w, h;
        ret = n8m_poster(path, poster, rgb, &w, &h);
        if (ret < 0) {
            fprintf(stderr, "poster: %s\n", n8m_error(ret));
            return 1;
        }
        av_md5_init(md5);
        av_md5_update(md5, rgb, (size_t)w * h * 2);
        av_md5_final(md5, digest);
        hex(digest, text);
        printf("poster %d %d %s\n", w, h, text);
        if (getenv("N8M_POSTER_PPM")) {
            FILE *o = fopen(getenv("N8M_POSTER_PPM"), "wb");
            int p;
            fprintf(o, "P6\n%d %d\n255\n", w, h);
            for (p = 0; p < w * h; p++) {
                unsigned v = rgb[2 * p] | (rgb[2 * p + 1] << 8);
                unsigned char px[3] = { (v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3 };
                fwrite(px, 1, 3, o);
            }
            fclose(o);
        }
        free(rgb);
        if (count)
            printf("mem %zu allocations, peak %zu bytes\n", allocs, peak);
        return 0;
    }

    ret = n8m_open(path, &f, &info);
    if (ret < 0) {
        fprintf(stderr, "open: %s\n", n8m_error(ret));
        return 1;
    }
    if (count)
        printf("mem-open %zu allocations, peak %zu bytes\n", allocs, peak);
    printf("info container=%s video=%s audio=%s %dx%d display=%dx%d rot=%d dur=%lld fps=%d/%d rate=%d ch=%d\n",
           info.container, info.video_codec, info.audio_codec, info.width, info.height, info.display_width,
           info.display_height, info.rotation, info.duration_ms, info.fps_num, info.fps_den, info.audio_rate,
           info.audio_channels);
    if (seek >= 0 && (ret = n8m_seek(f, seek)) < 0) {
        fprintf(stderr, "seek: %s\n", n8m_error(ret));
        return 1;
    }
    {

        struct AVMD5 *amd5 = av_md5_alloc();
        short pcm[1024 * 2];
        long long samples = 0, apts = 0, first = -1;
        int got, video_done = !info.has_video, audio_done = !info.has_audio;
        FILE *pcm_out = getenv("N8M_PCM_OUT") ? fopen(getenv("N8M_PCM_OUT"), "wb") : NULL;
        av_md5_init(amd5);
        while (!video_done || !audio_done) {
            long long until = 1LL << 60;
            if (!video_done) {
                int y;
                ret = n8m_video(f, &fr);
                if (ret == N8M_EOF) {
                    video_done = 1;
                } else if (ret < 0) {
                    fprintf(stderr, "video frame %d: %s\n", n, n8m_error(ret));
                    return 1;
                } else {
                    char ytext[33];
                    av_md5_init(md5);
                    for (y = 0; y < fr.height; y++)
                        av_md5_update(md5, fr.y + (size_t)y * fr.y_stride, fr.width);
                    av_md5_final(md5, digest);
                    hex(digest, ytext);
                    av_md5_init(md5);
                    for (y = 0; y < fr.height; y++)
                        av_md5_update(md5, fr.y + (size_t)y * fr.y_stride, fr.width);
                    for (y = 0; y < (fr.height + 1) / 2; y++)
                        av_md5_update(md5, fr.u + (size_t)y * fr.uv_stride, (fr.width + 1) / 2);
                    for (y = 0; y < (fr.height + 1) / 2; y++)
                        av_md5_update(md5, fr.v + (size_t)y * fr.uv_stride, (fr.width + 1) / 2);
                    av_md5_final(md5, digest);
                    hex(digest, text);

                    printf("v %d %lld %s %s\n", n++, fr.pts_ms, text, ytext);
                    until = fr.pts_ms;
                }
            }
            while (!audio_done && (video_done || apts <= until)) {
                ret = n8m_audio(f, pcm, 1024, &got, &apts);
                if (ret == N8M_EOF) {
                    audio_done = 1;
                    break;
                }
                if (ret < 0) {
                    fprintf(stderr, "audio: %s\n", n8m_error(ret));
                    return 1;
                }
                if (first < 0)
                    first = apts;
                av_md5_update(amd5, (unsigned char *)pcm, (size_t)got * info.audio_channels * 2);
                if (pcm_out)
                    fwrite(pcm, 2 * info.audio_channels, got, pcm_out);
                samples += got;
                apts += got * 1000LL / (info.audio_rate ? info.audio_rate : 1);
            }
        }
        if (info.has_audio) {
            av_md5_final(amd5, digest);
            hex(digest, text);
            printf("a %lld %d %d %s first=%lld\n", samples, info.audio_channels, info.audio_rate, text, first);
        }
        av_free(amd5);
        if (pcm_out)
            fclose(pcm_out);
    }
    n8m_close(f);
    av_free(md5);
    if (count)
        printf("mem %zu allocations, peak %zu bytes, live %zu\n", allocs, peak, live);
    return 0;
}
