#include "spinner.h"

#include <e32math.h>

namespace
    {
    const TReal KTurn = 6.28318530717959;
    const TReal KRing = 0.9;
    const TReal KWidth = 0.11;
    const TReal KPower = 3.0;
    const TReal KDim = 0.2;
    const TReal KTail = 0.42;
    const TReal KLead = 0.08;

    TReal SquircleDistance(TReal aX, TReal aY, TReal aR)
        {
        const TReal x = aX < 0 ? -aX : aX, y = aY < 0 ? -aY : aY;
        if (x + y <= 0)
            return aR;
        TReal px, py, g;
        Math::Pow(px, x, KPower);
        Math::Pow(py, y, KPower);
        Math::Pow(g, px + py, 1 / KPower);
        TReal gx, gy;
        Math::Pow(gx, x / g, KPower - 1);
        Math::Pow(gy, y / g, KPower - 1);
        TReal grad;
        Math::Sqrt(grad, gx * gx + gy * gy);
        return (g - aR) / (grad > 0.01 ? grad : 0.01);
        }
    }

void Spinner::Shape(TInt aSide, TUint8* aShape)
    {
    const TReal half = aSide / 2.0, radius = KRing * half, width = KWidth * half;
    TUint8* coverageOut = aShape;
    TUint8* angleOut = aShape + aSide * aSide;
    for (TInt y = 0; y < aSide; y++)
        for (TInt x = 0; x < aSide; x++)
            {
            TReal coverage = 0;
            for (TInt s = 0; s < 4; s++)
                {
                const TReal sx = x + 0.25 + 0.5 * (s & 1) - half, sy = y + 0.25 + 0.5 * (s >> 1) - half;
                TReal d = SquircleDistance(sx, sy, radius);
                d = (d < 0 ? -d : d) - width / 2;
                const TReal c = 0.5 - d * 2;
                coverage += c < 0 ? 0 : (c > 1 ? 1 : c);
                }
            coverageOut[y * aSide + x] = (TUint8)(coverage / 4 * 255 + 0.5);

            TReal angle;
            Math::ATan(angle, y + 0.5 - half, x + 0.5 - half);
            TReal turn = angle / KTurn;
            if (turn < 0)
                turn += 1;
            angleOut[y * aSide + x] = (TUint8)((TInt)(turn * 256 + 0.5) & 255);
            }
    }

void Spinner::Frame(TInt aSide, TReal aTurn, const TUint8* aShape, TUint8* aPixels, TInt aStride)
    {

    TUint8 light[256];
    const TReal head = aTurn - (TInt)aTurn;
    for (TInt a = 0; a < 256; a++)
        {
        TReal behind = head - a / 256.0;
        behind -= (TInt)behind;
        if (behind < 0)
            behind += 1;
        TReal t = 0;
        if (behind < KTail)
            t = 1 - behind / KTail;
        else if (behind > 1 - KLead)
            t = (behind - (1 - KLead)) / KLead;
        light[a] = (TUint8)((KDim + (1 - KDim) * t * t) * 255 + 0.5);
        }
    const TUint8* coverage = aShape;
    const TUint8* angle = aShape + aSide * aSide;
    for (TInt y = 0; y < aSide; y++)
        {
        TUint8* row = aPixels + y * aStride;
        const TInt o = y * aSide;
        for (TInt x = 0; x < aSide; x++)
            row[x] = (TUint8)((coverage[o + x] * light[angle[o + x]] + 127) / 255);
        }
    }

void Spinner::Bake(TInt aSide, TUint8* aShape, TUint8* aFrames)
    {
    Shape(aSide, aShape);
    for (TInt f = 0; f < KFrames; f++)
        Frame(aSide, f / (TReal)KFrames, aShape, aFrames + f * aSide * aSide, aSide);
    }
