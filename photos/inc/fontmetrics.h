#ifndef FONTMETRICS_H
#define FONTMETRICS_H

#include <e32def.h>
#include <e32const.h>

class TFontMetrics
    {
public:

    enum { KFirst = 0x20, KLast = 0x17F, KCount = KLast - KFirst + 1, KPairChars = 0x100 - KFirst };
    enum { KMaxPairs = 4096 };
    TFontMetrics();

    TBool Parse(const TUint8* aHead, TInt aHeadLength, const TUint8* aHhea, TInt aHheaLength, const TUint8* aHmtx,
                TInt aHmtxLength, const TUint8* aCmap, TInt aCmapLength, const TUint8* aKern, TInt aKernLength);
    TBool Valid() const { return iUnitsPerEm > 0; }
    TInt UnitsPerEm() const { return iUnitsPerEm; }

    TInt Advance(TUint aChar) const;

    TInt Kern(TUint aLeft, TUint aRight) const;
    TInt PairCount() const { return iPairCount; }

    static TBool FindTable(const TUint8* aDirectory, TInt aLength, TUint32 aTag, TInt& aOffset, TInt& aSize);
    static TUint32 Tag(char a, char b, char c, char d)
        {
        return ((TUint32)(TUint8)a << 24) | ((TUint32)(TUint8)b << 16) | ((TUint32)(TUint8)c << 8) | (TUint8)d;
        }

private:
    TInt Glyph(const TUint8* aCmap, TInt aLength, TUint aChar) const;
    static TInt First(const TInt* aByGlyph, TInt aCount, const TInt* aGlyphs, TInt aGlyph);

    TInt iUnitsPerEm;
    short iAdvance[KCount];
    TInt iPairCount;
    TUint32 iPairKey[KMaxPairs];
    short iPairValue[KMaxPairs];
    };

#endif
