#include <stdio.h>

#include "n8media.h"

static int failures = 0;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    n8m_file *f = NULL;
    n8m_info info;
    n8m_frame frame;
    short pcm[2 * 1024];
    unsigned char poster[64 * 64 * 2];
    long long pts = -1;
    int got = 0, frames = 0, w = 0, h = 0, err;

    check(n8m_open("E:\\Videos\\holiday.avi", &f, &info) < 0 && f == NULL, "real file refused");
    check(n8m_open("E:\\Videos\\N8MTest-rot90.avi", &f, &info) == 0 && f, "test clip opens");
    if (!f)
        return 1;
    check(info.rotation == 90 && info.width == 320 && info.duration_ms == 60000, "info");
    check(n8m_video(f, &frame) == 0 && frame.pts_ms == 0 && frame.width == 320, "first frame");
    check(n8m_video(f, &frame) == 0 && frame.pts_ms == 40, "second frame 40 ms on");
    check(n8m_audio(f, pcm, 1024, &got, &pts) == 0 && got == 1024 && pts == 0, "first audio");
    check(pcm[0] == 0 && pcm[2 * 100] != 0, "beep starts at 0 s");
    check(n8m_audio(f, pcm, 1024, &got, &pts) == 0 && pts == 1024 * 1000 / 44100, "audio pts follows");
    check(n8m_seek(f, 5300) == 0, "seek");
    check(n8m_video(f, &frame) == 0 && frame.pts_ms == 4000, "seek lands on the keyframe before");
    check(n8m_audio(f, pcm, 1024, &got, &pts) == 0 && pts == 4000, "audio follows the seek");
    n8m_seek(f, 59900);
    while ((err = n8m_video(f, &frame)) == 0)
        frames++;
    check(err == N8M_EOF && frames == 50, "ends after the last 2 s");
    n8m_close(f);
    check(n8m_poster("n8mtest.mp4", 64, poster, &w, &h) == 0 && w == 64 && h == 48, "poster fits 64");
    check(n8m_poster("clip.mp4", 64, poster, &w, &h) < 0, "no poster for a real file");
    printf(failures ? "n8mstub: %d failed\n" : "n8mstub: all passed\n", failures);
    return failures ? 1 : 0;
}
