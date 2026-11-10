#ifndef AVSYNC_H
#define AVSYNC_H

#include <e32def.h>
#include <e32const.h>

void FitVideo(TInt aViewW, TInt aViewH, TInt aDisplayW, TInt aDisplayH, TInt aRotation, TReal aUMax, TReal aVMax,
              TReal& aX0, TReal& aY0, TReal& aX1, TReal& aY1, TReal* aUv);

TInt PickFrame(const TInt64* aPtsMs, TInt aCount, TInt64 aClockMs, TInt aEarlyMs, TInt& aDrop);

class TAvClock
    {
public:
    enum { KMaxGlideUs = 120000 };

    void Start(TInt64 aBaseMs);

    TInt64 Update(TInt64 aPlayedUs, TInt64 aNowUs);
    TInt64 Ms() const { return iLastMs; }

private:
    TInt64 iBaseMs;
    TInt64 iPlayedUs;
    TInt64 iChangedUs;
    TInt64 iLastMs;
    TBool iMoving;
    };

void VideoTextureSize(TInt aWidth, TInt aHeight, TInt& aLumaW, TInt& aLumaH);

#endif
