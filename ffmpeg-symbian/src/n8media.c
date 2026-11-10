#include "n8media.h"
#include "n8m_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/display.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"

typedef struct pkt_node {
    AVPacket *pkt;
    struct pkt_node *next;
} pkt_node;

typedef struct {
    pkt_node *head, *tail;
    int count;
} pkt_queue;

struct n8m_file {
    AVFormatContext *fmt;
    AVCodecContext *vctx, *actx;
    AVStream *vst, *ast;
    int vidx, aidx;
    AVPacket *pkt;
    AVFrame *vframe, *aframe;
    pkt_queue vq, aq;
    int demux_eof;
    int vflushed, aflushed;
    int veof, aeof;
    int apos, anum;
    long long apts_ms;
    unsigned char *u2, *v2;
    int chroma_size;
    long long start_ms;
};

static void *(*alloc_malloc)(size_t) = 0;
static void *(*alloc_realloc)(void *, size_t) = 0;
static void (*alloc_free)(void *) = 0;
static void (*log_cb)(int, const char *);

void *n8m_hook_malloc(size_t size) { return alloc_malloc ? alloc_malloc(size) : malloc(size); }
void *n8m_hook_realloc(void *ptr, size_t size) { return alloc_realloc ? alloc_realloc(ptr, size) : realloc(ptr, size); }
void n8m_hook_free(void *ptr) { if (alloc_free) alloc_free(ptr); else free(ptr); }

void n8m_set_allocator(void *(*malloc_fn)(size_t), void *(*realloc_fn)(void *, size_t), void (*free_fn)(void *))
{
    alloc_malloc = malloc_fn;
    alloc_realloc = realloc_fn;
    alloc_free = free_fn;
}

static void log_to_app(void *avcl, int level, const char *fmt, va_list vl)
{
    char line[256];
    (void)avcl;
    if (!log_cb || level > AV_LOG_VERBOSE)
        return;
    vsnprintf(line, sizeof line, fmt, vl);
    log_cb(level <= AV_LOG_ERROR ? 0 : level <= AV_LOG_WARNING ? 1 : level <= AV_LOG_INFO ? 2 : 3, line);
}

void n8m_set_log(void (*log_fn)(int level, const char *message))
{
    log_cb = log_fn;
    av_log_set_level(log_fn ? AV_LOG_VERBOSE : AV_LOG_QUIET);
}

const char *n8m_error(int code)
{
    switch (code) {
    case 0: return "ok";
    case N8M_EOF: return "end of stream";
    case N8M_EINVAL: return "bad argument";
    case N8M_ENOMEM: return "out of memory";
    case N8M_EOPEN: return "cannot open file";
    case N8M_ECODEC: return "no decoder for the stream";
    case N8M_EDECODE: return "decode error";
    case N8M_ENOSTREAM: return "no such stream";
    case N8M_ESEEK: return "seek failed";
    default: return "unknown error";
    }
}

static void queue_clear(pkt_queue *q)
{
    while (q->head) {
        pkt_node *n = q->head;
        q->head = n->next;
        av_packet_free(&n->pkt);
        av_free(n);
    }
    q->tail = NULL;
    q->count = 0;
}

static AVPacket *queue_pop(pkt_queue *q)
{
    pkt_node *n = q->head;
    AVPacket *p;
    if (!n)
        return NULL;
    q->head = n->next;
    if (!q->head)
        q->tail = NULL;
    q->count--;
    p = n->pkt;
    av_free(n);
    return p;
}

static int queue_push(pkt_queue *q, AVPacket *src)
{
    pkt_node *n = av_mallocz(sizeof *n);
    if (!n)
        return -1;
    n->pkt = av_packet_alloc();
    if (!n->pkt) {
        av_free(n);
        return -1;
    }
    av_packet_move_ref(n->pkt, src);
    if (q->tail)
        q->tail->next = n;
    else
        q->head = n;
    q->tail = n;
    q->count++;
    if (q->count > N8M_QUEUE_PACKETS) {
        AVPacket *old = queue_pop(q);
        av_packet_free(&old);
    }
    return 0;
}

static int next_packet(n8m_file *f, int idx)
{
    pkt_queue *mine = idx == f->vidx ? &f->vq : &f->aq;
    AVPacket *q = queue_pop(mine);
    if (q) {
        av_packet_move_ref(f->pkt, q);
        av_packet_free(&q);
        return 1;
    }
    while (!f->demux_eof) {
        int ret = av_read_frame(f->fmt, f->pkt);
        if (ret == AVERROR_EOF) {
            f->demux_eof = 1;
            break;
        }
        if (ret < 0)
            return (ret == AVERROR(ENOMEM)) ? N8M_ENOMEM : N8M_EDECODE;
        if (f->pkt->stream_index == idx)
            return 1;
        if (f->pkt->stream_index == f->vidx && f->vctx)
            queue_push(&f->vq, f->pkt);
        else if (f->pkt->stream_index == f->aidx && f->actx)
            queue_push(&f->aq, f->pkt);
        av_packet_unref(f->pkt);
    }
    return 0;
}

static int decode_next(n8m_file *f, int idx, AVCodecContext *ctx, AVFrame *frame, int *flushed, int *eof)
{
    if (*eof)
        return N8M_EOF;
    for (;;) {
        int ret = avcodec_receive_frame(ctx, frame);
        if (ret == 0)
            return 0;
        if (ret == AVERROR_EOF) {
            *eof = 1;
            return N8M_EOF;
        }
        if (ret != AVERROR(EAGAIN))
            return N8M_EDECODE;
        if (*flushed)
            return N8M_EOF;
        ret = next_packet(f, idx);
        if (ret < 0)
            return ret;
        if (ret == 0) {
            avcodec_send_packet(ctx, NULL);
            *flushed = 1;
            continue;
        }
        ret = avcodec_send_packet(ctx, f->pkt);
        av_packet_unref(f->pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_INVALIDDATA)
            return ret == AVERROR(ENOMEM) ? N8M_ENOMEM : N8M_EDECODE;
    }
}

static long long to_ms(n8m_file *f, AVStream *st, long long ts)
{
    if (ts == AV_NOPTS_VALUE)
        return 0;
    return av_rescale_q(ts, st->time_base, (AVRational){1, 1000}) - f->start_ms;
}

static AVCodecContext *open_decoder(AVStream *st)
{
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext *ctx;
    if (!codec)
        return NULL;
    ctx = avcodec_alloc_context3(codec);
    if (!ctx)
        return NULL;
    if (avcodec_parameters_to_context(ctx, st->codecpar) < 0) {
        avcodec_free_context(&ctx);
        return NULL;
    }
    ctx->pkt_timebase = st->time_base;
    ctx->thread_count = 1;
    ctx->flags |= AV_CODEC_FLAG_BITEXACT;
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        return NULL;
    }
    return ctx;
}

static void copy_name(char *dst, size_t size, const char *src)
{
    size_t n = 0;
    while (src[n] && src[n] != ',' && n + 1 < size) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
}

static int rotation_of(AVStream *st)
{
    const int32_t *matrix = (const int32_t *)av_stream_get_side_data(st, AV_PKT_DATA_DISPLAYMATRIX, NULL);
    const AVDictionaryEntry *tag = av_dict_get(st->metadata, "rotate", NULL, 0);
    double angle = 0;
    int deg;
    if (matrix)
        angle = av_display_rotation_get(matrix);
    else if (tag)
        angle = -atof(tag->value);

    deg = (int)lrint(-angle);
    deg = ((deg % 360) + 360) % 360;
    return (deg + 45) / 90 % 4 * 90;
}

static int open_file(const char *path, int want_audio, n8m_file **out, n8m_info *info)
{
    n8m_file *f;
    int ret;
    static int registered;

    if (!path || !out)
        return N8M_EINVAL;
    *out = NULL;
    if (!registered) {
        if (!log_cb) {
            av_log_set_level(AV_LOG_QUIET);
            av_log_set_callback(log_to_app);
        } else {
            av_log_set_callback(log_to_app);
        }
        registered = 1;
    }
    f = av_mallocz(sizeof *f);
    if (!f)
        return N8M_ENOMEM;
    f->vidx = f->aidx = -1;
    f->fmt = avformat_alloc_context();
    f->pkt = av_packet_alloc();
    f->vframe = av_frame_alloc();
    f->aframe = av_frame_alloc();
    if (!f->fmt || !f->pkt || !f->vframe || !f->aframe) {
        n8m_close(f);
        return N8M_ENOMEM;
    }

    f->fmt->probesize = 512 * 1024;
    f->fmt->max_analyze_duration = AV_TIME_BASE;
    ret = avformat_open_input(&f->fmt, path, NULL, NULL);
    if (ret < 0) {
        f->fmt = NULL;
        n8m_close(f);
        return ret == AVERROR(ENOMEM) ? N8M_ENOMEM : N8M_EOPEN;
    }
    if (avformat_find_stream_info(f->fmt, NULL) < 0) {
        n8m_close(f);
        return N8M_EOPEN;
    }
    f->vidx = av_find_best_stream(f->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    f->aidx = want_audio ? av_find_best_stream(f->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0) : -1;
    if (f->vidx < 0)
        f->vidx = -1;
    if (f->aidx < 0)
        f->aidx = -1;
    if (f->vidx >= 0) {
        f->vst = f->fmt->streams[f->vidx];
        f->vctx = open_decoder(f->vst);
    }
    if (f->aidx >= 0) {
        f->ast = f->fmt->streams[f->aidx];
        f->actx = open_decoder(f->ast);
    }
    if (!f->vctx && !f->actx) {
        n8m_close(f);
        return f->vidx >= 0 || f->aidx >= 0 ? N8M_ECODEC : N8M_ENOSTREAM;
    }
    if (f->fmt->start_time != AV_NOPTS_VALUE && f->fmt->start_time > 0)
        f->start_ms = f->fmt->start_time / (AV_TIME_BASE / 1000);

    if (info) {
        memset(info, 0, sizeof *info);
        copy_name(info->container, sizeof info->container, f->fmt->iformat->name);
        if (f->fmt->duration != AV_NOPTS_VALUE && f->fmt->duration > 0)
            info->duration_ms = f->fmt->duration / (AV_TIME_BASE / 1000);
        if (f->vst) {
            AVRational sar = f->vst->sample_aspect_ratio.num ? f->vst->sample_aspect_ratio
                                                               : f->vst->codecpar->sample_aspect_ratio;
            AVRational fps = av_guess_frame_rate(f->fmt, f->vst, NULL);
            copy_name(info->video_codec, sizeof info->video_codec, avcodec_get_name(f->vst->codecpar->codec_id));
            info->has_video = f->vctx != NULL;
            info->width = f->vst->codecpar->width;
            info->height = f->vst->codecpar->height;
            info->display_width = info->width;
            info->display_height = info->height;
            if (sar.num > 0 && sar.den > 0 && sar.num != sar.den) {
                if (sar.num > sar.den)
                    info->display_width = (int)av_rescale(info->width, sar.num, sar.den);
                else
                    info->display_height = (int)av_rescale(info->height, sar.den, sar.num);
            }
            info->rotation = rotation_of(f->vst);
            info->fps_num = fps.num;
            info->fps_den = fps.den ? fps.den : 1;
            if (info->duration_ms == 0 && f->vst->duration != AV_NOPTS_VALUE)
                info->duration_ms = av_rescale_q(f->vst->duration, f->vst->time_base, (AVRational){1, 1000});
        }
        if (f->ast) {
            copy_name(info->audio_codec, sizeof info->audio_codec, avcodec_get_name(f->ast->codecpar->codec_id));
            info->has_audio = f->actx != NULL;
            if (f->actx) {
                info->audio_rate = f->actx->sample_rate;
                info->audio_channels = f->actx->channels > 1 ? 2 : 1;
            }
            if (info->duration_ms == 0 && f->ast->duration != AV_NOPTS_VALUE)
                info->duration_ms = av_rescale_q(f->ast->duration, f->ast->time_base, (AVRational){1, 1000});
        }
    }
    *out = f;
    return 0;
}

int n8m_open(const char *path_utf8, n8m_file **out, n8m_info *info)
{
    return open_file(path_utf8, 1, out, info);
}

void n8m_close(n8m_file *f)
{
    if (!f)
        return;
    queue_clear(&f->vq);
    queue_clear(&f->aq);
    av_packet_free(&f->pkt);
    av_frame_free(&f->vframe);
    av_frame_free(&f->aframe);
    avcodec_free_context(&f->vctx);
    avcodec_free_context(&f->actx);
    if (f->fmt)
        avformat_close_input(&f->fmt);
    av_free(f->u2);
    av_free(f->v2);
    av_free(f);
}

static int present_frame(n8m_file *f, const AVFrame *src, n8m_frame *frame)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(src->format);
    int cw = (src->width + 1) / 2, ch = (src->height + 1) / 2;
    int gray = desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA) == 0 && desc->nb_components == 1;
    int sub_w, sub_h;

    if (!desc || (desc->flags & (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_PAL)) ||
        desc->comp[0].depth != 8 || (!gray && desc->nb_components < 3))
        return N8M_EDECODE;
    frame->y = src->data[0];
    frame->y_stride = src->linesize[0];
    frame->width = src->width;
    frame->height = src->height;
    frame->pts_ms = to_ms(f, f->vst, src->best_effort_timestamp);
    sub_w = gray ? 1 : desc->log2_chroma_w;
    sub_h = gray ? 1 : desc->log2_chroma_h;
    if (sub_w == 1 && sub_h == 1 && !gray) {
        frame->u = src->data[1];
        frame->v = src->data[2];
        frame->uv_stride = src->linesize[1];
        return 0;
    }
    if (f->chroma_size < cw * ch) {
        av_free(f->u2);
        av_free(f->v2);
        f->u2 = av_malloc(cw * ch);
        f->v2 = av_malloc(cw * ch);
        f->chroma_size = cw * ch;
        if (!f->u2 || !f->v2) {
            f->chroma_size = 0;
            return N8M_ENOMEM;
        }
    }
    if (gray) {
        memset(f->u2, 128, cw * ch);
        memset(f->v2, 128, cw * ch);
    } else {

        int x, y, p;
        for (p = 0; p < 2; p++) {
            const unsigned char *s = src->data[1 + p];
            unsigned char *d = p ? f->v2 : f->u2;
            int stride = src->linesize[1 + p];
            for (y = 0; y < ch; y++) {
                const unsigned char *row = s + (size_t)((y * 2) >> sub_h) * stride;
                for (x = 0; x < cw; x++)
                    d[y * cw + x] = row[(x * 2) >> sub_w];
            }
        }
    }
    frame->u = f->u2;
    frame->v = f->v2;
    frame->uv_stride = cw;
    return 0;
}

int n8m_video(n8m_file *f, n8m_frame *frame)
{
    int ret;
    if (!f || !frame)
        return N8M_EINVAL;
    if (!f->vctx)
        return N8M_ENOSTREAM;
    ret = decode_next(f, f->vidx, f->vctx, f->vframe, &f->vflushed, &f->veof);
    if (ret < 0)
        return ret;
    return present_frame(f, f->vframe, frame);
}

static int clip16(int v)
{
    return v < -32768 ? -32768 : v > 32767 ? 32767 : v;
}

static void convert_samples(n8m_file *f, short *out, int count, int out_ch)
{
    const AVFrame *fr = f->aframe;
    int planar = av_sample_fmt_is_planar(fr->format);
    int bps = av_get_bytes_per_sample(fr->format);
    int in_ch = fr->channels;
    int c, i;
    for (c = 0; c < out_ch; c++) {
        int src_ch = c < in_ch ? c : 0;
        const unsigned char *base = planar ? fr->data[src_ch] + (size_t)f->apos * bps
                                           : fr->data[0] + ((size_t)f->apos * in_ch + src_ch) * bps;
        int step = planar ? bps : bps * in_ch;
        short *dst = out + c;
        switch (av_get_packed_sample_fmt(fr->format)) {
        case AV_SAMPLE_FMT_S16:
            for (i = 0; i < count; i++, base += step, dst += out_ch)
                *dst = *(const short *)base;
            break;
        case AV_SAMPLE_FMT_S32:
            for (i = 0; i < count; i++, base += step, dst += out_ch)
                *dst = (short)(*(const int *)base >> 16);
            break;
        case AV_SAMPLE_FMT_U8:
            for (i = 0; i < count; i++, base += step, dst += out_ch)
                *dst = (short)((*base - 128) << 8);
            break;
        case AV_SAMPLE_FMT_FLT:
            for (i = 0; i < count; i++, base += step, dst += out_ch)
                *dst = (short)clip16((int)lrintf(*(const float *)base * 32768.0f));
            break;
        case AV_SAMPLE_FMT_DBL:
            for (i = 0; i < count; i++, base += step, dst += out_ch)
                *dst = (short)clip16((int)lrint(*(const double *)base * 32768.0));
            break;
        default:
            for (i = 0; i < count; i++, dst += out_ch)
                *dst = 0;
            break;
        }
    }
}

int n8m_audio(n8m_file *f, short *pcm, int max_samples_per_channel, int *got, long long *pts_ms)
{
    int total = 0, out_ch, ret = 0;
    if (!f || !pcm || !got || max_samples_per_channel <= 0)
        return N8M_EINVAL;
    *got = 0;
    if (!f->actx)
        return N8M_ENOSTREAM;
    out_ch = f->actx->channels > 1 ? 2 : 1;
    while (total < max_samples_per_channel) {
        int n;
        if (f->apos >= f->anum) {
            ret = decode_next(f, f->aidx, f->actx, f->aframe, &f->aflushed, &f->aeof);
            if (ret < 0)
                break;
            f->apos = 0;
            f->anum = f->aframe->nb_samples;
            f->apts_ms = to_ms(f, f->ast, f->aframe->best_effort_timestamp);
            if (f->aframe->channels < 1 || f->anum <= 0)
                continue;
        }
        if (total == 0 && pts_ms)
            *pts_ms = f->apts_ms + (long long)f->apos * 1000 / (f->actx->sample_rate > 0 ? f->actx->sample_rate : 1);
        n = f->anum - f->apos;
        if (n > max_samples_per_channel - total)
            n = max_samples_per_channel - total;
        convert_samples(f, pcm + (size_t)total * out_ch, n, out_ch);
        f->apos += n;
        total += n;
    }
    *got = total;
    if (total > 0)
        return 0;
    if (pts_ms)
        *pts_ms = 0;
    return ret < 0 ? ret : N8M_EOF;
}

int n8m_seek(n8m_file *f, long long ms)
{
    AVStream *st;
    int idx;
    long long ts;
    if (!f)
        return N8M_EINVAL;
    if (ms < 0)
        ms = 0;
    st = f->vst ? f->vst : f->ast;
    idx = f->vst ? f->vidx : f->aidx;
    ts = av_rescale_q(ms + f->start_ms, (AVRational){1, 1000}, st->time_base);
    if (av_seek_frame(f->fmt, idx, ts, AVSEEK_FLAG_BACKWARD) < 0)
        return N8M_ESEEK;
    queue_clear(&f->vq);
    queue_clear(&f->aq);
    if (f->vctx)
        avcodec_flush_buffers(f->vctx);
    if (f->actx)
        avcodec_flush_buffers(f->actx);
    f->demux_eof = f->vflushed = f->aflushed = f->veof = f->aeof = 0;
    f->apos = f->anum = 0;
    return 0;
}

static unsigned short to_rgb565(int y, int u, int v, int full_range)
{
    int c = full_range ? y * 256 : (y - 16) * 298;
    int d = u - 128, e = v - 128;
    int r = (c + 409 * e + 128) >> 8;
    int g = (c - 100 * d - 208 * e + 128) >> 8;
    int b = (c + 516 * d + 128) >> 8;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

int n8m_poster(const char *path_utf8, int max_side, unsigned char *rgb565, int *w, int *h)
{
    n8m_file *f = NULL;
    n8m_info info;
    n8m_frame fr;
    int ret, rot, sw, sh, rw, rh, ow, oh, ox, oy, full;
    unsigned short *out = (unsigned short *)rgb565;

    if (!path_utf8 || max_side < 1 || !rgb565 || !w || !h)
        return N8M_EINVAL;
    *w = *h = 0;
    ret = open_file(path_utf8, 0, &f, &info);
    if (ret < 0)
        return ret;
    if (!f->vctx) {
        n8m_close(f);
        return N8M_ENOSTREAM;
    }
    ret = n8m_video(f, &fr);
    if (ret < 0) {
        n8m_close(f);
        return ret == N8M_EOF ? N8M_EDECODE : ret;
    }
    full = f->vframe->color_range == AVCOL_RANGE_JPEG;
    rot = info.rotation;
    sw = fr.width;
    sh = fr.height;

    rw = info.display_width > 0 ? info.display_width : sw;
    rh = info.display_height > 0 ? info.display_height : sh;
    if (rot == 90 || rot == 270) {
        int t = rw;
        rw = rh;
        rh = t;
    }
    if (rw >= rh) {
        ow = rw > max_side ? max_side : rw;
        oh = (int)((long long)rh * ow / rw);
    } else {
        oh = rh > max_side ? max_side : rh;
        ow = (int)((long long)rw * oh / rh);
    }
    if (ow < 1)
        ow = 1;
    if (oh < 1)
        oh = 1;
    for (oy = 0; oy < oh; oy++) {
        for (ox = 0; ox < ow; ox++) {

            int ux, uy, x0, x1, y0, y1, x, y, sum = 0, n = 0, cu, cv;
            switch (rot) {
            case 90:  ux = oy; uy = ow - 1 - ox; break;
            case 180: ux = ow - 1 - ox; uy = oh - 1 - oy; break;
            case 270: ux = oh - 1 - oy; uy = ox; break;
            default:  ux = ox; uy = oy; break;
            }
            {
                int uw = (rot == 90 || rot == 270) ? oh : ow;
                int uh = (rot == 90 || rot == 270) ? ow : oh;
                x0 = (int)((long long)ux * sw / uw);
                x1 = (int)((long long)(ux + 1) * sw / uw);
                y0 = (int)((long long)uy * sh / uh);
                y1 = (int)((long long)(uy + 1) * sh / uh);
            }
            if (x1 <= x0)
                x1 = x0 + 1;
            if (y1 <= y0)
                y1 = y0 + 1;
            if (x1 > sw)
                x1 = sw;
            if (y1 > sh)
                y1 = sh;
            for (y = y0; y < y1; y++)
                for (x = x0; x < x1; x++, n++)
                    sum += fr.y[(size_t)y * fr.y_stride + x];
            cu = fr.u[(size_t)(((y0 + y1) / 2) >> 1) * fr.uv_stride + (((x0 + x1) / 2) >> 1)];
            cv = fr.v[(size_t)(((y0 + y1) / 2) >> 1) * fr.uv_stride + (((x0 + x1) / 2) >> 1)];
            out[(size_t)oy * ow + ox] = to_rgb565(n ? sum / n : 0, cu, cv, full);
        }
    }
    *w = ow;
    *h = oh;
    n8m_close(f);
    return 0;
}
