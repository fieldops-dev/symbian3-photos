#include "avsync.h"

void FitVideo(TInt aViewW, TInt aViewH, TInt aDisplayW, TInt aDisplayH, TInt aRotation, TReal aUMax, TReal aVMax,
              TReal& aX0, TReal& aY0, TReal& aX1, TReal& aY1, TReal* aUv)
    {
    const TInt turn = ((aRotation % 360 + 360) % 360 + 45) / 90 % 4;
    TReal w = aDisplayW > 0 ? aDisplayW : 1, h = aDisplayH > 0 ? aDisplayH : 1;
    if (turn & 1)
        {
        const TReal t = w;
        w = h;
        h = t;
        }
    TReal fw = aViewW, fh = aViewW * h / w;
    if (fh > aViewH)
        {
        fh = aViewH;
        fw = aViewH * w / h;
        }
    aX0 = (aViewW - fw) / 2;
    aY0 = (aViewH - fh) / 2;
    aX1 = aX0 + fw;
    aY1 = aY0 + fh;

    const TReal corner[4][2] = { { 0, 0 }, { aUMax, 0 }, { 0, aVMax }, { aUMax, aVMax } };
    static const TInt KFrom[4][4] = {
        { 0, 1, 2, 3 },
        { 2, 0, 3, 1 },
        { 3, 2, 1, 0 },
        { 1, 3, 0, 2 } };
    for (TInt c = 0; c < 4; c++)
        {
        aUv[2 * c] = corner[KFrom[turn][c]][0];
        aUv[2 * c + 1] = corner[KFrom[turn][c]][1];
        }
    }

TInt PickFrame(const TInt64* aPtsMs, TInt aCount, TInt64 aClockMs, TInt aEarlyMs, TInt& aDrop)
    {
    aDrop = 0;
    TInt pick = -1;
    for (TInt i = 0; i < aCount; i++)
        if (aPtsMs[i] <= aClockMs + aEarlyMs)
            pick = i;
    if (pick > 0)
        aDrop = pick;
    return pick;
    }

void TAvClock::Start(TInt64 aBaseMs)
    {
    iBaseMs = aBaseMs;
    iPlayedUs = 0;
    iChangedUs = 0;
    iLastMs = aBaseMs;
    iMoving = EFalse;
    }

TInt64 TAvClock::Update(TInt64 aPlayedUs, TInt64 aNowUs)
    {
    if (aPlayedUs != iPlayedUs)
        {
        iMoving = aPlayedUs > iPlayedUs;
        iPlayedUs = aPlayedUs;
        iChangedUs = aNowUs;
        }
    TInt64 glide = 0;
    if (iMoving)
        {
        glide = aNowUs - iChangedUs;
        if (glide > KMaxGlideUs)
            glide = KMaxGlideUs;
        if (glide < 0)
            glide = 0;
        }
    const TInt64 ms = iBaseMs + (iPlayedUs + glide) / 1000;
    if (ms > iLastMs)
        iLastMs = ms;
    return iLastMs;
    }

void VideoTextureSize(TInt aWidth, TInt aHeight, TInt& aLumaW, TInt& aLumaH)
    {
    aLumaW = (aWidth + 7) & ~7;
    aLumaH = (aHeight + 1) & ~1;
    }
