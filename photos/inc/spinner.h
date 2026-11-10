#ifndef SPINNER_H
#define SPINNER_H

#include <e32std.h>

namespace Spinner
    {
    const TInt KPeriodMs = 1600;
    const TInt KFrames = 32;

    inline TInt FrameAt(TUint32 aMs) { return (TInt)(aMs % KPeriodMs) * KFrames / KPeriodMs; }

    void Shape(TInt aSide, TUint8* aShape);

    void Frame(TInt aSide, TReal aTurn, const TUint8* aShape, TUint8* aPixels, TInt aStride);

    void Bake(TInt aSide, TUint8* aShape, TUint8* aFrames);
    }

#endif
