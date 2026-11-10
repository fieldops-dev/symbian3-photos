#include "fontmetrics.h"

namespace
    {
    inline TUint U16(const TUint8* p) { return (p[0] << 8) | p[1]; }
    inline TInt S16(const TUint8* p) { return (short)((p[0] << 8) | p[1]); }
    inline TUint32 U32(const TUint8* p) { return ((TUint32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
    inline TInt Smaller(TInt a, TInt b) { return a < b ? a : b; }

    TInt Format4(const TUint8* aCmap, TInt aLength)
        {
        if (aLength < 4)
            return -1;
        const TInt count = U16(aCmap + 2);
        TInt found = -1;
        for (TInt i = 0; i < count && 4 + i * 8 + 8 <= aLength; i++)
            {
            const TUint8* e = aCmap + 4 + i * 8;
            const TUint platform = U16(e), encoding = U16(e + 2);
            const TInt offset = (TInt)U32(e + 4);
            if (offset < 0 || offset + 14 > aLength || U16(aCmap + offset) != 4)
                continue;
            if (platform == 3 && encoding == 1)
                return offset;
            if (platform == 0 && found < 0)
                found = offset;
            }
        return found;
        }
    }

TFontMetrics::TFontMetrics()
    : iUnitsPerEm(0), iPairCount(0)
    {
    for (TInt i = 0; i < KCount; i++)
        iAdvance[i] = -1;
    }

TBool TFontMetrics::FindTable(const TUint8* aDirectory, TInt aLength, TUint32 aTag, TInt& aOffset, TInt& aSize)
    {
    if (aLength < 12)
        return EFalse;
    const TInt tables = U16(aDirectory + 4);
    for (TInt i = 0; i < tables && 12 + i * 16 + 16 <= aLength; i++)
        {
        const TUint8* e = aDirectory + 12 + i * 16;
        if (U32(e) == aTag)
            {
            aOffset = (TInt)U32(e + 8);
            aSize = (TInt)U32(e + 12);
            return aOffset >= 0 && aSize > 0;
            }
        }
    return EFalse;
    }

TInt TFontMetrics::First(const TInt* aByGlyph, TInt aCount, const TInt* aGlyphs, TInt aGlyph)
    {
    TInt lo = 0, hi = aCount;
    while (lo < hi)
        {
        const TInt mid = (lo + hi) / 2;
        if (aGlyphs[aByGlyph[mid]] < aGlyph)
            lo = mid + 1;
        else
            hi = mid;
        }
    return lo;
    }

TInt TFontMetrics::Glyph(const TUint8* aCmap, TInt aLength, TUint aChar) const
    {
    const TInt segments = U16(aCmap + 6) / 2;
    const TInt ends = 14, starts = ends + segments * 2 + 2, deltas = starts + segments * 2,
               ranges = deltas + segments * 2;
    if (ranges + segments * 2 > aLength)
        return 0;
    for (TInt s = 0; s < segments; s++)
        {
        if (aChar > U16(aCmap + ends + s * 2))
            continue;
        const TUint start = U16(aCmap + starts + s * 2);
        if (aChar < start)
            return 0;
        const TInt delta = S16(aCmap + deltas + s * 2);
        const TUint range = U16(aCmap + ranges + s * 2);
        if (range == 0)
            return (aChar + delta) & 0xFFFF;
        const TInt at = ranges + s * 2 + range + (aChar - start) * 2;
        if (at + 2 > aLength)
            return 0;
        const TUint glyph = U16(aCmap + at);
        return glyph ? (glyph + delta) & 0xFFFF : 0;
        }
    return 0;
    }

TBool TFontMetrics::Parse(const TUint8* aHead, TInt aHeadLength, const TUint8* aHhea, TInt aHheaLength,
                          const TUint8* aHmtx, TInt aHmtxLength, const TUint8* aCmap, TInt aCmapLength,
                          const TUint8* aKern, TInt aKernLength)
    {
    iUnitsPerEm = 0;
    iPairCount = 0;
    if (!aHead || aHeadLength < 54 || !aHhea || aHheaLength < 36 || !aHmtx || !aCmap)
        return EFalse;
    const TInt unitsPerEm = U16(aHead + 18);
    const TInt metrics = U16(aHhea + 34);
    const TInt subtable = Format4(aCmap, aCmapLength);
    if (unitsPerEm <= 0 || metrics <= 0 || metrics * 4 > aHmtxLength || subtable < 0)
        return EFalse;
    const TUint8* cmap = aCmap + subtable;
    const TInt cmapLength = aCmapLength - subtable;
    TInt glyphs[KCount];
    for (TInt i = 0; i < KCount; i++)
        {
        const TInt glyph = Glyph(cmap, cmapLength, KFirst + i);
        glyphs[i] = glyph;

        iAdvance[i] = glyph > 0 || i == 0 ? (short)U16(aHmtx + Smaller(glyph, metrics - 1) * 4) : -1;
        }
    iUnitsPerEm = unitsPerEm;

    TInt byGlyph[KPairChars];
    TInt n = 0;
    for (TInt i = 0; i < KPairChars; i++)
        if (glyphs[i] > 0)
            {
            TInt j = n++;
            while (j > 0 && glyphs[byGlyph[j - 1]] > glyphs[i])
                {
                byGlyph[j] = byGlyph[j - 1];
                j--;
                }
            byGlyph[j] = i;
            }

    if (!aKern || aKernLength < 4 || U16(aKern) != 0)
        return ETrue;
    const TInt tables = U16(aKern + 2);
    TInt at = 4;
    for (TInt t = 0; t < tables && at + 6 <= aKernLength; t++)
        {
        const TInt length = U16(aKern + at + 2), coverage = U16(aKern + at + 4);
        if ((coverage >> 8) == 0 && (coverage & 1) && at + 14 <= aKernLength)
            {
            const TInt pairs = U16(aKern + at + 6);
            for (TInt p = 0; p < pairs && at + 14 + p * 6 + 6 <= aKernLength; p++)
                {
                const TUint8* e = aKern + at + 14 + p * 6;
                const TInt left = U16(e), right = U16(e + 2), value = S16(e + 4);
                TInt a = First(byGlyph, n, glyphs, left);
                for (; a < n && glyphs[byGlyph[a]] == left; a++)
                    for (TInt b = First(byGlyph, n, glyphs, right);
                         b < n && glyphs[byGlyph[b]] == right && iPairCount < KMaxPairs; b++)
                        {
                        iPairKey[iPairCount] = ((TUint32)(KFirst + byGlyph[a]) << 16) | (KFirst + byGlyph[b]);
                        iPairValue[iPairCount++] = (short)value;
                        }
                }
            }
        at += length > 6 ? length : 6;
        }

    for (TInt i = 1; i < iPairCount; i++)
        {
        const TUint32 key = iPairKey[i];
        const short value = iPairValue[i];
        TInt j = i - 1;
        while (j >= 0 && iPairKey[j] > key)
            {
            iPairKey[j + 1] = iPairKey[j];
            iPairValue[j + 1] = iPairValue[j];
            j--;
            }
        iPairKey[j + 1] = key;
        iPairValue[j + 1] = value;
        }
    return ETrue;
    }

TInt TFontMetrics::Advance(TUint aChar) const
    {
    return aChar >= (TUint)KFirst && aChar <= (TUint)KLast ? iAdvance[aChar - KFirst] : -1;
    }

TInt TFontMetrics::Kern(TUint aLeft, TUint aRight) const
    {
    const TUint32 key = (aLeft << 16) | aRight;
    TInt lo = 0, hi = iPairCount - 1;
    while (lo <= hi)
        {
        const TInt mid = (lo + hi) / 2;
        if (iPairKey[mid] == key)
            return iPairValue[mid];
        if (iPairKey[mid] < key)
            lo = mid + 1;
        else
            hi = mid - 1;
        }
    return 0;
    }
