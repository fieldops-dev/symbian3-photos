#include "mp4.h"

namespace
    {
    const TUint32 KMvhd = 0x6D766864;
    const TUint32 KTrak = 0x7472616B;
    const TUint32 KTkhd = 0x746B6864;
    const TUint32 KMdia = 0x6D646961;
    const TUint32 KMdhd = 0x6D646864;
    const TUint32 KHdlr = 0x68646C72;
    const TUint32 KMinf = 0x6D696E66;
    const TUint32 KStbl = 0x7374626C;
    const TUint32 KStts = 0x73747473;
    const TUint32 KStss = 0x73747373;
    const TUint32 KVide = 0x76696465;

    struct TTrackTables
        {
        TBool iVideo;
        TUint32 iScale;
        const TUint8* iStts;
        TInt iSttsLength;
        const TUint8* iStss;
        TInt iStssLength;
        };

    TUint32 Read32(const TUint8* aP)
        {
        return ((TUint32)aP[0] << 24) | ((TUint32)aP[1] << 16) | ((TUint32)aP[2] << 8) | aP[3];
        }

    TInt Least(TInt aA, TInt aB)
        {
        return aA < aB ? aA : aB;
        }

    TInt64 Read64(const TUint8* aP)
        {
        return ((TInt64)Read32(aP) << 32) | Read32(aP + 4);
        }

    void ParseMvhd(const TUint8* aData, TInt aLength, TVideoHeader& aHeader)
        {
        if (aLength < 4)
            return;
        const TInt version = aData[0];
        if (version == 1 && aLength >= 32)
            {
            aHeader.iCreated = Read64(aData + 4);
            const TUint32 scale = Read32(aData + 20);
            const TInt64 duration = Read64(aData + 24);
            if (scale)
                aHeader.iDurationMs = (TInt)(duration * 1000 / scale);
            }
        else if (version == 0 && aLength >= 20)
            {
            aHeader.iCreated = Read32(aData + 4);
            const TUint32 scale = Read32(aData + 12);
            const TUint32 duration = Read32(aData + 16);
            if (scale)
                aHeader.iDurationMs = (TInt)((TInt64)duration * 1000 / scale);
            }
        }

    void ParseTkhd(const TUint8* aData, TInt aLength, TVideoHeader& aHeader)
        {
        if (aLength < 4 || aHeader.iWidth > 0)
            return;
        const TInt matrix = aData[0] == 1 ? 52 : 40;
        if (aLength < matrix + 44)
            return;
        const TInt width = Read32(aData + matrix + 36) >> 16, height = Read32(aData + matrix + 40) >> 16;
        if (width <= 0 || height <= 0)
            return;
        const TBool quarter = Read32(aData + matrix) == 0 && Read32(aData + matrix + 4) != 0;
        aHeader.iWidth = quarter ? height : width;
        aHeader.iHeight = quarter ? width : height;
        }

    void FindTables(const TUint8* aData, TInt aLength, TTrackTables& aTables, TInt aDepth)
        {
        TInt at = 0;
        while (at < aLength && aDepth < 4)
            {
            TUint32 type;
            TInt64 size;
            TInt header;
            if (!Mp4BoxHeader(aData + at, aLength - at, type, size, header))
                break;
            const TInt64 left = aLength - at;
            const TInt body = (TInt)((size == 0 || size > left ? left : size) - header);
            const TUint8* content = aData + at + header;
            if (type == KMdia || type == KMinf || type == KStbl)
                FindTables(content, body, aTables, aDepth + 1);
            else if (type == KHdlr && body >= 12)
                aTables.iVideo = Read32(content + 8) == KVide;
            else if (type == KMdhd && body >= 4)
                aTables.iScale = content[0] == 1 ? (body >= 24 ? Read32(content + 20) : 0)
                                                 : (body >= 16 ? Read32(content + 12) : 0);
            else if (type == KStts)
                {
                aTables.iStts = content;
                aTables.iSttsLength = body;
                }
            else if (type == KStss)
                {
                aTables.iStss = content;
                aTables.iStssLength = body;
                }
            if (size == 0 || size > left)
                break;
            at += (TInt)size;
            }
        }

    TBool ParseBoxes(const TUint8* aData, TInt aLength, TVideoHeader& aHeader, TInt aDepth)
        {
        TBool found = EFalse;
        TInt at = 0;
        while (at < aLength && aDepth < 4)
            {
            TUint32 type;
            TInt64 size;
            TInt header;
            if (!Mp4BoxHeader(aData + at, aLength - at, type, size, header))
                break;
            const TInt64 left = aLength - at;
            const TInt body = (TInt)((size == 0 || size > left ? left : size) - header);
            const TUint8* content = aData + at + header;
            if (type == KMvhd)
                {
                ParseMvhd(content, body, aHeader);
                found = ETrue;
                }
            else if (type == KTkhd)
                ParseTkhd(content, body, aHeader);
            else if (type == KTrak)
                found |= ParseBoxes(content, body, aHeader, aDepth + 1);
            if (size == 0 || size > left)
                break;
            at += (TInt)size;
            }
        return found;
        }
    }

void TVideoHeader::Reset()
    {
    iCreated = 0;
    iDurationMs = 0;
    iWidth = 0;
    iHeight = 0;
    }

TBool Mp4BoxHeader(const TUint8* aData, TInt aLength, TUint32& aType, TInt64& aSize, TInt& aHeaderBytes)
    {
    if (aLength < 8)
        return EFalse;
    aType = Read32(aData + 4);
    aSize = Read32(aData);
    aHeaderBytes = 8;
    if (aSize == 1)
        {
        if (aLength < 16)
            return EFalse;
        aSize = Read64(aData + 8);
        aHeaderBytes = 16;
        }
    return aSize == 0 || aSize >= aHeaderBytes;
    }

TBool ParseMoov(const TUint8* aData, TInt aLength, TVideoHeader& aHeader)
    {
    aHeader.Reset();
    return ParseBoxes(aData, aLength, aHeader, 0);
    }

TInt Mp4Keyframes(const TUint8* aMoov, TInt aLength, TInt* aMs, TInt aMax)
    {
    TInt at = 0;
    while (at < aLength)
        {
        TUint32 type;
        TInt64 size;
        TInt header;
        if (!Mp4BoxHeader(aMoov + at, aLength - at, type, size, header))
            return -1;
        const TInt64 left = aLength - at;
        const TInt body = (TInt)((size == 0 || size > left ? left : size) - header);
        if (type == KTrak)
            {
            TTrackTables t = { EFalse, 0, 0, 0, 0, 0 };
            FindTables(aMoov + at + header, body, t, 0);
            if (t.iVideo)
                {
                if (!t.iStss)
                    return 0;
                if (!t.iStts || t.iScale == 0 || t.iSttsLength < 8 || t.iStssLength < 8)
                    return -1;
                const TInt runs = Least((TInt)Read32(t.iStts + 4), (t.iSttsLength - 8) / 8);
                const TInt syncs = Least((TInt)Read32(t.iStss + 4), (t.iStssLength - 8) / 4);

                TInt run = 0, inRun = 0, count = 0;
                TUint32 sample = 1;
                TInt64 time = 0;
                for (TInt s = 0; s < syncs && count < aMax; s++)
                    {
                    const TUint32 want = Read32(t.iStss + 8 + 4 * s);
                    while (sample < want && run < runs)
                        {
                        const TUint32 runCount = Read32(t.iStts + 8 + 8 * run);
                        const TUint32 delta = Read32(t.iStts + 12 + 8 * run);
                        const TUint32 step = want - sample < runCount - inRun ? want - sample : runCount - inRun;
                        time += (TInt64)step * delta;
                        sample += step;
                        inRun += step;
                        if ((TUint32)inRun >= runCount)
                            {
                            run++;
                            inRun = 0;
                            }
                        }
                    if (sample != want)
                        break;
                    aMs[count++] = (TInt)(time * 1000 / t.iScale);
                    }
                return count;
                }
            }
        if (size == 0 || size > left)
            break;
        at += (TInt)size;
        }
    return -1;
    }
