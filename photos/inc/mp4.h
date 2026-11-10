#ifndef MP4_H
#define MP4_H

#include <e32def.h>
#include <e32const.h>

class TVideoHeader
    {
public:
    void Reset();
    TInt64 iCreated;
    TInt iDurationMs;
    TInt iWidth;
    TInt iHeight;
    };

TBool Mp4BoxHeader(const TUint8* aData, TInt aLength, TUint32& aType, TInt64& aSize, TInt& aHeaderBytes);

TBool ParseMoov(const TUint8* aData, TInt aLength, TVideoHeader& aHeader);

TInt Mp4Keyframes(const TUint8* aMoov, TInt aLength, TInt* aMs, TInt aMax);

const TUint32 KMp4Moov = 0x6D6F6F76;

#endif
