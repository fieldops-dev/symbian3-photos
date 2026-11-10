#include <e32base.h>
#include <e32cons.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/time.h>

#include "n8media.h"

static const char *const kClipDir = "E:\\n8media\\clips";
static const char *const kOut = "E:\\n8media-bench.txt";
static const int kSecondsPerStream = 6;

static size_t gLive, gPeak, gAllocs;

static void *CountMalloc(size_t n)
{
    size_t *p = (size_t *)malloc(n + 16);
    if (!p)
        return NULL;
    p[0] = n;
    gAllocs++;
    gLive += n;
    if (gLive > gPeak)
        gPeak = gLive;
    return p + 2;
}

static void CountFree(void *q)
{
    if (!q)
        return;
    size_t *p = (size_t *)q - 2;
    gLive -= p[0];
    free(p);
}

static void *CountRealloc(void *q, size_t n)
{
    if (!q)
        return CountMalloc(n);
    size_t *p = (size_t *)q - 2;
    size_t old = p[0];
    p = (size_t *)realloc(p, n + 16);
    if (!p)
        return NULL;
    p[0] = n;
    gLive += n - old;
    if (gLive > gPeak)
        gPeak = gLive;
    return p + 2;
}

static long long NowMs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static FILE *gOut;
static CConsoleBase *gConsole;

static void Say(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (gOut) {
        fputs(line, gOut);
        fputc('\n', gOut);
        fflush(gOut);
    }
    if (gConsole) {
        TBuf<256> wide;
        wide.Copy(TPtrC8((const TUint8 *)line));
        gConsole->Printf(_L("%S\n"), &wide);
    }
}

static void BenchClip(const char *path)
{
    n8m_file *f = NULL;
    n8m_info info;
    gPeak = gLive;
    long long t0 = NowMs();
    int ret = n8m_open(path, &f, &info);
    long long topen = NowMs() - t0;
    if (ret < 0) {
        Say("%s: open failed: %s", path, n8m_error(ret));
        return;
    }
    Say("%s: %s %dx%d rot %d, %s %d Hz %d ch, %lld ms, open %lld ms", path, info.video_codec, info.width,
        info.height, info.rotation, info.audio_codec, info.audio_rate, info.audio_channels, info.duration_ms, topen);

    if (info.has_video) {
        n8m_frame fr;
        int frames = 0;
        long long start = NowMs(), now = start, last_pts = 0;

        while (now - start < kSecondsPerStream * 1000) {
            ret = n8m_video(f, &fr);
            if (ret == N8M_EOF) {
                if (n8m_seek(f, 0) < 0)
                    break;
                continue;
            }
            if (ret < 0) {
                Say("  video error after %d frames: %s", frames, n8m_error(ret));
                break;
            }
            frames++;
            last_pts = fr.pts_ms;
            now = NowMs();
        }
        long long ms = now - start;
        if (frames > 0 && ms > 0)
            Say("  video: %d frames in %lld ms = %d.%d fps, %d.%d ms/frame (clip %d/%d fps), last pts %lld",
                frames, ms, (int)(frames * 1000 / ms), (int)(frames * 10000 / ms % 10), (int)(ms / frames),
                (int)(ms * 10 / frames % 10), info.fps_num, info.fps_den, last_pts);
    }
    if (info.has_audio) {
        short *pcm = (short *)malloc(2048 * 2 * sizeof(short));
        long long samples = 0, start = NowMs(), now = start, pts = 0;
        int got;
        n8m_seek(f, 0);
        while (now - start < kSecondsPerStream * 1000) {
            ret = n8m_audio(f, pcm, 2048, &got, &pts);
            if (ret == N8M_EOF) {
                if (n8m_seek(f, 0) < 0)
                    break;
                continue;
            }
            if (ret < 0) {
                Say("  audio error after %lld samples: %s", samples, n8m_error(ret));
                break;
            }
            samples += got;
            now = NowMs();
        }
        long long ms = now - start;
        if (samples > 0 && ms > 0 && info.audio_rate > 0)
            Say("  audio: %lld samples in %lld ms = %d.%02dx real time", samples, ms,
                (int)(samples * 1000 / info.audio_rate / ms), (int)(samples * 100000 / info.audio_rate / ms % 100));
        free(pcm);
    }
    n8m_close(f);
    Say("  memory: peak %u KB in %u allocations", (unsigned)(gPeak / 1024), (unsigned)gAllocs);

    if (info.has_video) {
        unsigned char *rgb = (unsigned char *)malloc(128 * 128 * 2);
        int w = 0, h = 0;
        gPeak = gLive;
        t0 = NowMs();
        ret = n8m_poster(path, 128, rgb, &w, &h);
        Say("  poster: %dx%d in %lld ms (%s), peak %u KB", w, h, NowMs() - t0, n8m_error(ret),
            (unsigned)(gPeak / 1024));
        free(rgb);
    }
}

int main()
{
    CTrapCleanup *cleanup = CTrapCleanup::New();
    TRAPD(err, gConsole = Console::NewL(_L("n8media bench"), TSize(KConsFullScreen, KConsFullScreen)));
    (void)err;
    n8m_set_allocator(CountMalloc, CountRealloc, CountFree);
    gOut = fopen(kOut, "w");
    Say("n8media bench: %d s per stream, clips in %s", kSecondsPerStream, kClipDir);

    DIR *dir = opendir(kClipDir);
    if (!dir) {
        Say("cannot open %s", kClipDir);
    } else {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (e->d_name[0] == '.')
                continue;
            char path[256];
            snprintf(path, sizeof path, "%s\\%s", kClipDir, e->d_name);
            BenchClip(path);
        }
        closedir(dir);
    }
    Say("done: %s", kOut);
    if (gOut)
        fclose(gOut);
    if (gConsole) {
        gConsole->Printf(_L("\nPress any key to close\n"));
        gConsole->Getch();
        delete gConsole;
    }
    delete cleanup;
    return 0;
}
