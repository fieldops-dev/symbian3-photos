#include "texcodec.h"

namespace
    {

    const TInt KSmall[8] = { 2, 5, 9, 13, 18, 24, 33, 47 };
    const TInt KLarge[8] = { 8, 17, 29, 42, 60, 80, 106, 183 };

    inline TInt Clamp255(TInt aV) { return aV < 0 ? 0 : (aV > 255 ? 255 : aV); }

    class THalf
        {
    public:
        TInt iTable;
        TInt iError;
        TUint8 iIndex[8];
        };

    void FitHalf(const TInt* aPixels, const TInt* aBase, THalf& aHalf)
        {
        const TInt baseSum = aBase[0] + aBase[1] + aBase[2];
        TInt delta3[8];
        for (TInt i = 0; i < 8; i++)
            delta3[i] = aPixels[i * 3] + aPixels[i * 3 + 1] + aPixels[i * 3 + 2] - baseSum;
        aHalf.iError = 0x7FFFFFFF;
        for (TInt t = 0; t < 8; t++)
            {
            const TInt small = KSmall[t], large = KLarge[t];

            const TInt upper = (small + large) * 3 / 2, lower = -upper;
            TInt error = 0;
            TUint8 index[8];
            for (TInt i = 0; i < 8 && error < aHalf.iError; i++)
                {
                const TInt d = delta3[i];
                TInt m, k;
                if (d >= 0)
                    {
                    k = d > upper ? 1 : 0;
                    m = k ? large : small;
                    }
                else
                    {
                    k = d < lower ? 3 : 2;
                    m = k == 3 ? -large : -small;
                    }
                const TInt* p = aPixels + i * 3;
                const TInt er = Clamp255(aBase[0] + m) - p[0];
                const TInt eg = Clamp255(aBase[1] + m) - p[1];
                const TInt eb = Clamp255(aBase[2] + m) - p[2];
                error += er * er + eg * eg + eb * eb;
                index[i] = (TUint8)k;
                }
            if (error < aHalf.iError)
                {
                aHalf.iError = error;
                aHalf.iTable = t;
                for (TInt i = 0; i < 8; i++)
                    aHalf.iIndex[i] = index[i];
                }
            }
        }

    class TCoding
        {
    public:
        TInt iError;
        TUint32 iHigh;
        TUint32 iLow;
        THalf iHalves[2];
        };

    void Code(const TInt aPixels[2][24], const TInt* aWhere0, const TInt* aWhere1, TInt aFlip, const TInt aSums[2][3],
              TCoding& aCoding)
        {
        TInt q5[2][3], q4[2][3];
        TBool differential = ETrue;
        for (TInt h = 0; h < 2; h++)
            for (TInt c = 0; c < 3; c++)
                {
                q5[h][c] = (aSums[h][c] * 31 + 1020) / 2040;
                q4[h][c] = (aSums[h][c] * 15 + 1020) / 2040;
                }
        for (TInt c = 0; c < 3; c++)
            {
            const TInt d = q5[1][c] - q5[0][c];
            differential = differential && d >= -4 && d <= 3;
            }
        TInt base[2][3];
        for (TInt h = 0; h < 2; h++)
            for (TInt c = 0; c < 3; c++)
                base[h][c] = differential ? (q5[h][c] << 3) | (q5[h][c] >> 2) : q4[h][c] * 17;
        FitHalf(aPixels[0], base[0], aCoding.iHalves[0]);
        FitHalf(aPixels[1], base[1], aCoding.iHalves[1]);
        aCoding.iError = aCoding.iHalves[0].iError + aCoding.iHalves[1].iError;
        TUint32 high;
        if (differential)
            high = (q5[0][0] << 27) | (((q5[1][0] - q5[0][0]) & 7) << 24) | (q5[0][1] << 19) |
                   (((q5[1][1] - q5[0][1]) & 7) << 16) | (q5[0][2] << 11) | (((q5[1][2] - q5[0][2]) & 7) << 8) | 2;
        else
            high = (q4[0][0] << 28) | (q4[1][0] << 24) | (q4[0][1] << 20) | (q4[1][1] << 16) | (q4[0][2] << 12) |
                   (q4[1][2] << 8);
        aCoding.iHigh = high | (aCoding.iHalves[0].iTable << 5) | (aCoding.iHalves[1].iTable << 2) | aFlip;
        TUint32 low = 0;
        for (TInt h = 0; h < 2; h++)
            for (TInt i = 0; i < 8; i++)
                {
                const TInt j = (h ? aWhere1 : aWhere0)[i], k = aCoding.iHalves[h].iIndex[i];
                low |= ((TUint32)(k >> 1) << (16 + j)) | ((TUint32)(k & 1) << j);
                }
        aCoding.iLow = low;
        }

    void EncodeBlock(const TInt* aBlock, TUint8* aOut)
        {
        TCoding best;
        best.iError = 0x7FFFFFFF;
        best.iHigh = best.iLow = 0;
        for (TInt flip = 0; flip < 2; flip++)
            {
            TInt pixels[2][24];
            TInt where[2][8];
            TInt count[2] = { 0, 0 };
            TInt sums[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
            for (TInt j = 0; j < 16; j++)
                {
                const TInt h = flip ? ((j & 3) >= 2) : (j >= 8);
                for (TInt c = 0; c < 3; c++)
                    {
                    pixels[h][count[h] * 3 + c] = aBlock[j * 3 + c];
                    sums[h][c] += aBlock[j * 3 + c];
                    }
                where[h][count[h]++] = j;
                }
            TCoding coding;
            Code(pixels, where[0], where[1], flip, sums, coding);
            if (coding.iError < best.iError)
                best = coding;
            if (coding.iError == 0)
                break;
            const TInt* modifiers[2] = { KSmall, KLarge };
            for (TInt h = 0; h < 2; h++)
                {
                const THalf& half = coding.iHalves[h];
                for (TInt c = 0; c < 3; c++)
                    {
                    TInt sum = 0;
                    for (TInt i = 0; i < 8; i++)
                        {
                        const TInt k = half.iIndex[i];
                        const TInt m = modifiers[k & 1][half.iTable];
                        sum += pixels[h][i * 3 + c] - (k & 2 ? -m : m);
                        }
                    sums[h][c] = sum < 0 ? 0 : (sum > 2040 ? 2040 : sum);
                    }
                }
            Code(pixels, where[0], where[1], flip, sums, coding);
            if (coding.iError < best.iError)
                best = coding;
            }
        for (TInt i = 0; i < 4; i++)
            {
            aOut[i] = (TUint8)(best.iHigh >> (24 - 8 * i));
            aOut[4 + i] = (TUint8)(best.iLow >> (24 - 8 * i));
            }
        }
    }

void Etc1Encode(const TUint16* aPixels, TInt aWidth, TInt aHeight, TUint8* aOut)
    {
    TInt block[48];
    for (TInt by = 0; by < aHeight; by += 4)
        for (TInt bx = 0; bx < aWidth; bx += 4)
            {
            for (TInt x = 0; x < 4; x++)
                for (TInt y = 0; y < 4; y++)
                    {
                    const TUint p = aPixels[(by + y) * aWidth + bx + x];
                    const TUint r = p >> 11, g = (p >> 5) & 63, b = p & 31;
                    TInt* out = block + (x * 4 + y) * 3;
                    out[0] = (r << 3) | (r >> 2);
                    out[1] = (g << 2) | (g >> 4);
                    out[2] = (b << 3) | (b >> 2);
                    }
            EncodeBlock(block, aOut);
            aOut += 8;
            }
    }

void HalveRgb565(const TUint16* aPixels, TInt aSide, TUint16* aOut)
    {
    const TInt half = aSide / 2;
    for (TInt y = 0; y < half; y++)
        for (TInt x = 0; x < half; x++)
            {
            const TUint16* p = aPixels + 2 * y * aSide + 2 * x;
            const TUint a = p[0], b = p[1], c = p[aSide], d = p[aSide + 1];
            const TUint r = ((a >> 11) + (b >> 11) + (c >> 11) + (d >> 11) + 2) >> 2;
            const TUint g = (((a >> 5) & 63) + ((b >> 5) & 63) + ((c >> 5) & 63) + ((d >> 5) & 63) + 2) >> 2;
            const TUint bl = ((a & 31) + (b & 31) + (c & 31) + (d & 31) + 2) >> 2;
            aOut[y * half + x] = (TUint16)((r << 11) | (g << 5) | bl);
            }
    }
