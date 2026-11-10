#ifndef PHYSICS_H
#define PHYSICS_H

#include <e32base.h>

const TReal KRubber = 0.55;
const TReal KBounceOmega = 13;
const TReal KMaxVelocity = 7000;

inline TReal Abs(TReal aValue) { return aValue < 0 ? -aValue : aValue; }
inline TReal Clamp(TReal aValue, TReal aLo, TReal aHi) { return aValue < aLo ? aLo : (aValue > aHi ? aHi : aValue); }
inline TReal Lerp(TReal aFrom, TReal aTo, TReal aT) { return aFrom + (aTo - aFrom) * aT; }

TReal RubberDistance(TReal aPull, TReal aDim);
TReal RubberPull(TReal aDistance, TReal aDim);

TReal Rubber(TReal aRaw, TReal aLo, TReal aHi, TReal aDim);
TReal Unrubber(TReal aShown, TReal aLo, TReal aHi, TReal aDim);

void Spring(TReal& aValue, TReal& aVelocity, TReal aTarget, TReal aOmega, TReal aDt);

TBool SpringTo(TReal& aValue, TReal& aVelocity, TReal aTarget, TReal aOmega, TInt aMs, TReal aRest = 0.5);

TBool EaseTo(TReal& aValue, TReal aTarget, TReal aRate, TInt aMs, TReal aRest = 0.5);

TBool Coast(TReal& aValue, TReal& aVelocity, TReal aLo, TReal aHi, TInt aMs);

TUint32 NowMs();

class TVelocityTracker
    {
public:
    void Reset() { iCount = iNext = 0; }
    void Add(TReal aPos, TUint32 aMs);
    TReal Velocity(TUint32 aNow) const;

private:
    enum { KSamples = 8 };
    TReal iPos[KSamples];
    TUint32 iMs[KSamples];
    TInt iCount;
    TInt iNext;
    };

class TPointFilter
    {
public:
    void Reset(TReal aX, TReal aY, TUint32 aMs);
    void Add(TReal aX, TReal aY, TUint32 aMs);
    TReal X() const { return iX; }
    TReal Y() const { return iY; }

private:
    TReal iX;
    TReal iY;
    TReal iDx;
    TReal iDy;
    TUint32 iMs;
    };

class CFrameTimer : public CTimer
    {
public:
    static CFrameTimer* NewL(TCallBack aCallBack);
    void Schedule(TInt aDelayUs);

private:
    CFrameTimer(TCallBack aCallBack);
    void RunL();
    TCallBack iCallBack;
    };

#endif
