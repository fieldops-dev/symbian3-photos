#ifndef N8MEDIA_H
#define N8MEDIA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct n8m_file n8m_file;

typedef struct {
    int has_video, has_audio;
    int width, height;
    int display_width, display_height;
    int rotation;
    long long duration_ms;
    int fps_num, fps_den;
    int audio_rate, audio_channels;
    char video_codec[16], audio_codec[16], container[16];
} n8m_info;

typedef struct {
    const unsigned char *y, *u, *v;
    int y_stride, uv_stride;
    int width, height;
    long long pts_ms;
} n8m_frame;

int  n8m_open(const char *path_utf8, n8m_file **out, n8m_info *info);
int  n8m_video(n8m_file *f, n8m_frame *frame);
int  n8m_audio(n8m_file *f, short *pcm, int max_samples_per_channel, int *got, long long *pts_ms);
int  n8m_seek(n8m_file *f, long long ms);
int  n8m_poster(const char *path_utf8, int max_side, unsigned char *rgb565, int *w, int *h);
void n8m_close(n8m_file *f);
const char *n8m_error(int code);
void n8m_set_allocator(void *(*malloc_fn)(size_t), void *(*realloc_fn)(void *, size_t), void (*free_fn)(void *));
void n8m_set_log(void (*log_fn)(int level, const char *message));
#define N8M_EOF (-1)

#ifdef __cplusplus
}
#endif

#endif
