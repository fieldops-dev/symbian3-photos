#include "physics.h"

#include <e32math.h>
#include <hal.h>

namespace
    {

    const TReal KDecel = 0.998;
    const TReal KLnDecel = -0.002002002670673;
    const TInt KVelocityWindowMs = 100;
    const TInt KStaleVelocityMs = 60;
    }

TReal RubberDistance(TReal aPull, TReal aDim)
    {
    return aDim <= 0 ? 0 : (1 - 1 / (aPull * KRubber / aDim + 1)) * aDim;
    }

TReal RubberPull(TReal aDistance, TReal aDim)
    {
    if (aDim <= 0)
        return 0;
    if (aDistance >= aDim - 1)
        aDistance = aDim - 1;
    return aDim / KRubber * aDistance / (aDim - aDistance);
    }

TReal Rubber(TReal aRaw, TReal aLo, TReal aHi, TReal aDim)
    {
    if (aRaw < aLo)
        return aLo - RubberDistance(aLo - aRaw, aDim);
    if (aRaw > aHi)
        return aHi + RubberDistance(aRaw - aHi, aDim);
    return aRaw;
    }

TReal Unrubber(TReal aShown, TReal aLo, TReal aHi, TReal aDim)
    {
    if (aShown < aLo)
        return aLo - RubberPull(aLo - aShown, aDim);
    if (aShown > aHi)
        return aHi + RubberPull(aShown - aHi, aDim);
    return aShown;
    }

void Spring(TReal& aValue, TReal& aVelocity, TReal aTarget, TReal aOmega, TReal aDt)
    {
    TReal e;
    Math::Exp(e, -aOmega * aDt);
    const TReal r = aValue - aTarget;
    const TReal c = aVelocity + aOmega * r;
    const TReal moved = r + c * aDt;
    aValue = aTarget + moved * e;
    aVelocity = (c - aOmega * moved) * e;
    }

TBool EaseTo(TReal& aValue, TReal aTarget, TReal aRate, TInt aMs, TReal aRest)
    {
    if (aValue == aTarget)
        return EFalse;
    TReal e;
    Math::Exp(e, -aRate * aMs / 1000.0);
    aValue = aTarget + (aValue - aTarget) * e;
    if (Abs(aValue - aTarget) < aRest)
        {
        aValue = aTarget;
        return EFalse;
        }
    return ETrue;
    }

namespace
    {

    const TReal KMinCutoff = 3;
    const TReal KCutoffPerSpeed = 0.015;
    const TReal KSpeedCutoff = 2;
    const TReal KPi = 3.14159265358979;

    TReal Smoothing(TReal aCutoff, TReal aDt)
        {
        const TReal tau = 1 / (2 * KPi * aCutoff);
        return aDt / (aDt + tau);
        }
    }

void TPointFilter::Reset(TReal aX, TReal aY, TUint32 aMs)
    {
    iX = aX;
    iY = aY;
    iDx = iDy = 0;
    iMs = aMs;
    }

void TPointFilter::Add(TReal aX, TReal aY, TUint32 aMs)
    {
    const TReal dt = Max(1, (TInt)(aMs - iMs)) / 1000.0;
    iMs = aMs;
    const TReal ds = Smoothing(KSpeedCutoff, dt);
    iDx += ds * ((aX - iX) / dt - iDx);
    iDy += ds * ((aY - iY) / dt - iDy);
    TReal speed;
    Math::Sqrt(speed, iDx * iDx + iDy * iDy);
    const TReal a = Smoothing(KMinCutoff + KCutoffPerSpeed * speed, dt);
    iX += a * (aX - iX);
    iY += a * (aY - iY);
    }

TBool SpringTo(TReal& aValue, TReal& aVelocity, TReal aTarget, TReal aOmega, TInt aMs, TReal aRest)
    {
    if (aValue == aTarget && aVelocity == 0)
        return EFalse;
    Spring(aValue, aVelocity, aTarget, aOmega, aMs / 1000.0);
    if (Abs(aValue - aTarget) < aRest && Abs(aVelocity) < aRest * 40)
        {
        aValue = aTarget;
        aVelocity = 0;
        return EFalse;
        }
    return ETrue;
    }

TBool Coast(TReal& aValue, TReal& aVelocity, TReal aLo, TReal aHi, TInt aMs)
    {
    if (aHi < aLo)
        aHi = aLo;
    if (aValue < aLo || aValue > aHi)
        {
        const TReal edge = aValue < aLo ? aLo : aHi;
        Spring(aValue, aVelocity, edge, KBounceOmega, aMs / 1000.0);
        if (Abs(aValue - edge) < 0.5 && Abs(aVelocity) < 20)
            {
            aValue = edge;
            aVelocity = 0;
            return EFalse;
            }
        return ETrue;
        }
    if (aVelocity == 0)
        return EFalse;
    TReal decay;
    Math::Pow(decay, KDecel, aMs);
    aValue += aVelocity / 1000 * (decay - 1) / KLnDecel;
    aVelocity *= decay;
    if (Abs(aVelocity) < 8)
        aVelocity = 0;
    return ETrue;
    }

TUint32 NowMs()
    {
    static TInt tickUs = 0;
    if (tickUs == 0 && HAL::Get(HALData::ENanoTickPeriod, tickUs) != KErrNone)
        tickUs = 1000;
    return (TUint32)((TUint64)User::NTickCount() * (TUint)tickUs / 1000);
    }

void TVelocityTracker::Add(TReal aPos, TUint32 aMs)
    {
    iPos[iNext] = aPos;
    iMs[iNext] = aMs;
    iNext = (iNext + 1) % KSamples;
    if (iCount < KSamples)
        iCount++;
    }

TReal TVelocityTracker::Velocity(TUint32 aNow) const
    {
    if (iCount < 2)
        return 0;
    const TInt newest = (iNext + KSamples - 1) % KSamples;
    if ((TInt)(aNow - iMs[newest]) > KStaleVelocityMs)
        return 0;
    TInt oldest = newest;
    for (TInt k = 1; k < iCount; k++)
        {
        const TInt i = (newest + KSamples - k) % KSamples;
        if ((TInt)(iMs[newest] - iMs[i]) > KVelocityWindowMs)
            break;
        oldest = i;
        }
    const TInt dt = (TInt)(iMs[newest] - iMs[oldest]);
    if (oldest == newest || dt <= 0)
        return 0;
    const TReal v = (iPos[newest] - iPos[oldest]) * 1000.0 / Max(dt, 10);
    return Clamp(v, -KMaxVelocity, KMaxVelocity);
    }

CFrameTimer* CFrameTimer::NewL(TCallBack aCallBack)
    {
    CFrameTimer* self = new (ELeave) CFrameTimer(aCallBack);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop(self);
    return self;
    }

CFrameTimer::CFrameTimer(TCallBack aCallBack) : CTimer(EPriorityStandard), iCallBack(aCallBack)
    {
    CActiveScheduler::Add(this);
    }

void CFrameTimer::Schedule(TInt aDelayUs)
    {
    Cancel();
    HighRes(TTimeIntervalMicroSeconds32(Max(0, aDelayUs)));
    }

void CFrameTimer::RunL()
    {
    iCallBack.CallBack();
    }
