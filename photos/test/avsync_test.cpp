#include <cmath>
#include <cstdio>

#include "avsync.h"

namespace
    {
    int gFailures = 0;

    void Check(bool aOk, const char* aWhat)
        {
        if (!aOk)
            {
            std::printf("FAIL: %s\n", aWhat);
            gFailures++;
            }
        }

    bool Near(double aA, double aB)
        {
        return std::fabs(aA - aB) < 1e-6;
        }

    bool Corner(const TReal* aUv, int aCorner, double aU, double aV)
        {
        return Near(aUv[2 * aCorner], aU) && Near(aUv[2 * aCorner + 1], aV);
        }

    void TestFit()
        {
        TReal x0, y0, x1, y1, uv[8];

        FitVideo(360, 640, 1280, 720, 0, 1, 1, x0, y0, x1, y1, uv);
        Check(Near(x0, 0) && Near(x1, 360) && Near(y1 - y0, 202.5) && Near(y0, (640 - 202.5) / 2), "16:9 portrait");
        Check(Corner(uv, 0, 0, 0) && Corner(uv, 3, 1, 1), "unturned corners");

        FitVideo(640, 360, 1280, 720, 0, 1, 1, x0, y0, x1, y1, uv);
        Check(Near(x0, 0) && Near(y0, 0) && Near(x1, 640) && Near(y1, 360), "16:9 landscape fills");

        FitVideo(640, 360, 320, 240, 0, 1, 1, x0, y0, x1, y1, uv);
        Check(Near(y0, 0) && Near(y1, 360) && Near(x1 - x0, 480) && Near(x0, 80), "4:3 landscape pillarbox");

        FitVideo(360, 640, 1280, 720, 90, 0.5, 0.25, x0, y0, x1, y1, uv);
        Check(Near(x0, 0) && Near(y0, 0) && Near(x1, 360) && Near(y1, 640), "90 fills portrait");
        Check(Corner(uv, 0, 0, 0.25) && Corner(uv, 1, 0, 0) && Corner(uv, 2, 0.5, 0.25) && Corner(uv, 3, 0.5, 0),
              "90 corners: stored bottom-left at the top-left");
        FitVideo(360, 640, 1280, 720, 180, 1, 1, x0, y0, x1, y1, uv);
        Check(Corner(uv, 0, 1, 1) && Corner(uv, 1, 0, 1) && Corner(uv, 2, 1, 0) && Corner(uv, 3, 0, 0), "180 corners");
        FitVideo(360, 640, 1280, 720, 270, 1, 1, x0, y0, x1, y1, uv);
        Check(Corner(uv, 0, 1, 0) && Corner(uv, 1, 1, 1) && Corner(uv, 2, 0, 0) && Corner(uv, 3, 0, 1), "270 corners");

        FitVideo(640, 360, 1280, 720, -90, 1, 1, x0, y0, x1, y1, uv);
        Check(Near(y1 - y0, 360) && Near(x1 - x0, 202.5), "-90 (270) in landscape");

        FitVideo(360, 640, 0, 0, 0, 1, 1, x0, y0, x1, y1, uv);
        Check(Near(x1 - x0, 360) && Near(y1 - y0, 360), "zero size as square");
        }

    void TestPick()
        {
        const TInt64 pts[4] = { 0, 40, 80, 120 };
        TInt drop = -1;
        Check(PickFrame(pts, 4, -50, 5, drop) == -1 && drop == 0, "nothing due before the first");
        Check(PickFrame(pts, 4, 0, 5, drop) == 0 && drop == 0, "first due");
        Check(PickFrame(pts, 4, 36, 5, drop) == 1 && drop == 1, "second due within early margin, first dropped");
        Check(PickFrame(pts, 4, 500, 5, drop) == 3 && drop == 3, "all late: newest shown, three dropped");
        Check(PickFrame(pts, 0, 500, 5, drop) == -1 && drop == 0, "empty queue");
        }

    void TestClock()
        {
        TAvClock clock;
        clock.Start(10000);
        Check(clock.Update(0, 1000000) == 10000, "holds at the base before playing");
        Check(clock.Update(0, 1500000) == 10000, "still holds");
        Check(clock.Update(40000, 1600000) == 10040, "follows the position");
        Check(clock.Update(40000, 1620000) == 10060, "glides between steps");
        Check(clock.Update(40000, 1900000) == 10160, "glide capped");
        Check(clock.Update(80000, 1910000) == 10160, "never backwards after a glide");
        Check(clock.Update(200000, 1920000) == 10200, "catches up");
        clock.Start(0);
        Check(clock.Update(0, 5000000) == 0, "restart holds at the new base");
        }

    void TestTextures()
        {
        TInt w, h;
        VideoTextureSize(320, 240, w, h);
        Check(w == 320 && h == 240, "aligned size kept");
        VideoTextureSize(426, 241, w, h);
        Check(w == 432 && h == 242, "padded to 8 and even");
        Check((w / 2) % 4 == 0, "chroma rows 4-byte aligned");
        }
    }

int main()
    {
    TestFit();
    TestPick();
    TestClock();
    TestTextures();
    std::printf(gFailures ? "avsync: %d failed\n" : "avsync: all passed\n", gFailures);
    return gFailures ? 1 : 0;
    }
