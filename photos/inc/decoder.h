#ifndef DECODER_H
#define DECODER_H

#include "worker.h"

const TInt KThumbSize = 128;
const TInt KSmallThumbSize = 64;
const TInt KThumbBytes = KThumbSize * KThumbSize * 2;
const TInt KThumbBatch = 12;
enum TThumbKind { EThumbLarge, EThumbSmall, EThumbLargeEtc1, EThumbSmallEtc1, EThumbKinds };
inline TInt ThumbSide(TInt aKind)
    {
    return aKind == EThumbSmall || aKind == EThumbSmallEtc1 ? KSmallThumbSize : KThumbSize;
    }
inline TInt ThumbKindBytes(TInt aKind)
    {
    const TInt side = ThumbSide(aKind);
    return aKind >= EThumbLargeEtc1 ? side * side / 2 : side * side * 2;
    }

class TThumbItem
    {
public:

    enum TSource { ENone, ECache, EConverted, EExif, EDecoded };

    TFileName iPath;
    TThumbKind iKind;
    TInt iSlot;
    TInt iSize;
    TUint32 iModified;
    TInt iOrientation;

    TInt iResult;
    TSource iSource;
    TInt iMicros;
    };

class MThumbObserver
    {
public:

    virtual void ThumbsReady(const TThumbItem* aItems, TInt aCount, const TUint8* aData, TInt aHalf) = 0;
    };

class CThumbWorker : public CWorker
    {
public:
    static CThumbWorker* NewL(MThumbObserver& aObserver);
    ~CThumbWorker();

    TInt Request(const TThumbItem* aItems, TInt aCount, TInt aGeneration, TInt aHalf);

    class TShared;

private:
    CThumbWorker(MThumbObserver& aObserver, TShared* aShared);
    void RunL();

    MThumbObserver& iObserver;
    TShared* iShared;
    };

class TImageJob
    {
public:

    TFileName iPath;
    TInt iTag;
    TBool iPreview;
    TSize iMinSize;
    TInt iMaxSide;
    TInt iMaxPixels;

    TInt iResult;
    TUint16* iPixels;
    TSize iSize;
    TInt iStride;
    TSize iFullSize;
    TInt iMicros;
    };

class MImageObserver
    {
public:
    virtual void ImageReady(TImageJob& aJob) = 0;
    };

class CImageWorker : public CWorker
    {
public:
    static CImageWorker* NewL(MImageObserver& aObserver);
    ~CImageWorker();
    TInt Request(const TDesC& aPath, TInt aTag, TBool aPreview, TSize aMinSize, TInt aMaxSide, TInt aMaxPixels);

    class TShared;

private:
    CImageWorker(MImageObserver& aObserver, TShared* aShared);
    void RunL();

    MImageObserver& iObserver;
    TShared* iShared;
    };

class TGifJob
    {
public:
    enum { KBatch = 2 };

    TFileName iPath;
    TInt iTag;
    TBool iRestart;
    TInt iMaxSide;

    TInt iResult;
    TInt iFrameCount;
    TSize iCanvas;
    TSize iSize;
    TInt iStride;
    TInt iCount;
    TInt iIndex[KBatch];
    TUint16* iPixels[KBatch];
    TInt iDelayMs[KBatch];
    TInt iMicros;
    };

class MGifObserver
    {
public:
    virtual void GifReady(TGifJob& aJob) = 0;
    };

class CGifWorker : public CWorker
    {
public:
    static CGifWorker* NewL(MGifObserver& aObserver);
    ~CGifWorker();
    TInt Request(const TDesC& aPath, TInt aTag, TBool aRestart, TInt aMaxSide);

    class TShared;

private:
    CGifWorker(MGifObserver& aObserver, TShared* aShared);
    void RunL();

    MGifObserver& iObserver;
    TShared* iShared;
    };

#endif
