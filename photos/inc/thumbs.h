#ifndef THUMBS_H
#define THUMBS_H

#include <e32base.h>
#include <e32hashtab.h>

#include "decoder.h"
#include "glrenderer.h"

class CLibrary;

class MThumbCacheObserver
    {
public:

    virtual void ThumbsChanged() = 0;
    };

class CThumbCache : public CBase, public MThumbObserver
    {
public:
    enum { KPageSize = 512, KMaxPages = 16 };
    enum { KMaxCells = (KPageSize / KSmallThumbSize) * (KPageSize / KSmallThumbSize) };
    enum TPriority { EVisible, ENear, EAhead };
    enum TSet { ELarge, ESmall, ESets };

    static CThumbCache* NewL(MThumbCacheObserver& aObserver);
    ~CThumbCache();
    void SetLibrary(const CLibrary* aLibrary);

    void SetGl(CGlRenderer* aGl);

    void BeginFrame();

    TBool Get(TInt aPhoto, TPriority aPriority, TInt& aAtlas, TReal* aUv, TInt aSide = KThumbSize);
    const TGlTexture& Atlas(TInt aIndex) const { return iPages[aIndex].iTexture; }
    TInt AtlasCount() const { return KMaxPages; }

    TBool Pump(TInt aBudget);

    void Idle();

    void SetHold(TBool aHold) { iHold = aHold; }

    TInt Trim(TInt aIdleFrames);

    TInt Bytes(TSet aSet) const;
    TInt Pages(TSet aSet) const;
    TBool Etc1() const { return iEtc1; }
    void TraceStats();

private:
    enum TState { EFree, ELoading, EUploading, EReady, EFailed };
    class TCell
        {
    public:
        TInt iKey;
        TState iState;
        TInt iUsedFrame;
        };
    class TPage
        {
    public:
        TGlTexture iTexture;
        TSet iSet;
        TInt iUsedFrame;
        TUint8* iShadow;
        TBool iDirty;
        };
    class TWanted
        {
    public:
        TInt iPhoto;
        TPriority iPriority;
        TSet iSet;
        };
    class THalf
        {
    public:
        TBool iBusy;
        TInt iCount;
        TInt iNext;
        TInt iCells[KThumbBatch];
        TInt iKeys[KThumbBatch];
        TInt iEpoch;
        const TUint8* iData;
        };

    enum TSubImage { ESubUnknown, ESubYes, ESubNo };

    CThumbCache(MThumbCacheObserver& aObserver);
    void ThumbsReady(const TThumbItem* aItems, TInt aCount, const TUint8* aData, TInt aHalf);
    static TInt Side(TSet aSet) { return aSet == ESmall ? KSmallThumbSize : KThumbSize; }
    static TInt CellsPerRow(TSet aSet) { return KPageSize / Side(aSet); }
    void CellUv(TInt aCell, TReal* aUv) const;
    void Touch(TInt aCell);
    TBool Settled(TInt aPage) const;
    void EmptyPage(TInt aPage);
    void FreePage(TInt aPage);
    TBool MakePage(TInt aPage, TSet aSet);
    TInt AllocateCell(TSet aSet);
    void Dispatch();
    void UploadCell(TInt aCell, const TUint8* aData);
    void Upload(TInt aBudget);
    void Forget();

    MThumbCacheObserver& iObserver;
    CThumbWorker* iWorker;
    const CLibrary* iLibrary;
    CGlRenderer* iGl;
    TBool iEtc1;
    TSubImage iSubImage;
    TBool iHold;
    TInt iMaxPages;
    TPage iPages[KMaxPages];
    TCell iCells[KMaxPages * KMaxCells];
    RHashMap<TInt, TInt> iCellByKey;
    RHashMap<TInt, TInt> iAttempts;
    RArray<TWanted> iWanted;
    TThumbItem iItems[KThumbBatch];
    THalf iHalves[2];
    TInt iUploadHalf;
    TInt iFrame;
    TInt iEpoch;
    TInt iGeneration;

    TInt iFromCache;
    TInt iConverted;
    TInt iFromExif;
    TInt iDecoded;
    TInt iFailed;
    TInt iWorkerUs;
    TInt iDecodeUs;
    TInt iMaxDecodeUs;
    TInt iUploaded;
    TInt64 iUploadUs;
    TInt iPagesMade;
    TInt iPagesFreed;
    };

#endif
