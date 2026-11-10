#include "thumbs.h"

#include "library.h"
#include "trace.h"

namespace
    {

    const TInt KKeepFrames = 90;
    _LIT(KPartsWork, "work");
    _LIT(KPartsRefused, "refused: whole pages from their shadows");
    _LIT(KEtc1, "ETC1");
    _LIT(KRgb565, "RGB 565");
    }

CThumbCache* CThumbCache::NewL(MThumbCacheObserver& aObserver)
    {
    CThumbCache* self = new (ELeave) CThumbCache(aObserver);
    CleanupStack::PushL(self);
    self->iWorker = CThumbWorker::NewL(*self);
    CleanupStack::Pop(self);
    return self;
    }

CThumbCache::CThumbCache(MThumbCacheObserver& aObserver)
    : iObserver(aObserver), iMaxPages(KMaxPages), iUploadHalf(-1)
    {
    for (TInt i = 0; i < KMaxPages * KMaxCells; i++)
        {
        iCells[i].iKey = -1;
        iCells[i].iState = EFree;
        iCells[i].iUsedFrame = 0;
        }
    for (TInt p = 0; p < KMaxPages; p++)
        {
        iPages[p].iSet = ELarge;
        iPages[p].iUsedFrame = 0;
        iPages[p].iShadow = NULL;
        iPages[p].iDirty = EFalse;
        }
    }

CThumbCache::~CThumbCache()
    {
    delete iWorker;
    for (TInt p = 0; p < KMaxPages; p++)
        User::Free(iPages[p].iShadow);
    iCellByKey.Close();
    iAttempts.Close();
    iWanted.Close();
    }

void CThumbCache::SetLibrary(const CLibrary* aLibrary)
    {
    if (aLibrary && aLibrary->iThumbGeneration != iGeneration)
        {
        Forget();
        iGeneration = aLibrary->iThumbGeneration;
        }
    iLibrary = aLibrary;
    iWanted.Reset();
    }

void CThumbCache::SetGl(CGlRenderer* aGl)
    {
    if (aGl == iGl)
        return;
    for (TInt p = 0; p < KMaxPages; p++)
        {
        if (iGl && !aGl)
            iGl->DeleteTexture(iPages[p].iTexture);
        iPages[p].iTexture = TGlTexture();
        User::Free(iPages[p].iShadow);
        iPages[p].iShadow = NULL;
        iPages[p].iDirty = EFalse;
        }
    iGl = aGl;
    iMaxPages = KMaxPages;
    if (aGl)
        {
        iEtc1 = aGl->HasEtc1();
        iSubImage = ESubUnknown;
        }
    Forget();
    }

void CThumbCache::Forget()
    {
    iEpoch++;
    for (TInt i = 0; i < KMaxPages * KMaxCells; i++)
        {
        iCells[i].iKey = -1;
        iCells[i].iState = EFree;
        }
    iCellByKey.Close();
    for (TInt h = 0; h < 2; h++)
        if (iHalves[h].iBusy && iHalves[h].iCount > 0)
            {
            iHalves[h].iBusy = EFalse;
            iHalves[h].iCount = 0;
            }
    iUploadHalf = -1;
    }

void CThumbCache::BeginFrame()
    {
    iFrame++;
    iWanted.Reset();
    }

void CThumbCache::CellUv(TInt aCell, TReal* aUv) const
    {
    const TPage& page = iPages[aCell / KMaxCells];
    const TInt index = aCell % KMaxCells, side = Side(page.iSet), perRow = CellsPerRow(page.iSet);

    const TReal x = (index % perRow) * side, y = (index / perRow) * side;
    aUv[0] = (x + 0.5) / KPageSize;
    aUv[1] = (y + 0.5) / KPageSize;
    aUv[2] = (x + side - 0.5) / KPageSize;
    aUv[3] = (y + side - 0.5) / KPageSize;
    }

void CThumbCache::Touch(TInt aCell)
    {
    iCells[aCell].iUsedFrame = iFrame;
    iPages[aCell / KMaxCells].iUsedFrame = iFrame;
    }

TBool CThumbCache::Get(TInt aPhoto, TPriority aPriority, TInt& aAtlas, TReal* aUv, TInt aSide)
    {
    if (!iLibrary || aPhoto < 0 || aPhoto >= iLibrary->Count())
        return EFalse;
    const TInt slot = iLibrary->Photo(aPhoto).iThumb;
    const TSet set = aSide <= KSmallThumbSize ? ESmall : ELarge;
    TInt* found = iCellByKey.Find(slot * 2 + set);
    if (!found)
        {
        TWanted wanted;
        wanted.iPhoto = aPhoto;
        wanted.iPriority = aPriority;
        wanted.iSet = set;
        iWanted.Append(wanted);
        }
    else
        {
        Touch(*found);
        if (iCells[*found].iState == EReady)
            {
            aAtlas = *found / KMaxCells;
            CellUv(*found, aUv);
            return ETrue;
            }
        }

    TInt* other = iCellByKey.Find(slot * 2 + (set == ESmall ? ELarge : ESmall));
    if (!other || iCells[*other].iState != EReady)
        return EFalse;
    Touch(*other);
    aAtlas = *other / KMaxCells;
    CellUv(*other, aUv);
    return ETrue;
    }

TBool CThumbCache::Settled(TInt aPage) const
    {
    for (TInt i = aPage * KMaxCells; i < (aPage + 1) * KMaxCells; i++)
        if (iCells[i].iState == ELoading || iCells[i].iState == EUploading)
            return EFalse;
    return ETrue;
    }

void CThumbCache::EmptyPage(TInt aPage)
    {
    for (TInt i = aPage * KMaxCells; i < (aPage + 1) * KMaxCells; i++)
        {
        if (iCells[i].iKey >= 0)
            iCellByKey.Remove(iCells[i].iKey);
        iCells[i].iKey = -1;
        iCells[i].iState = EFree;
        }
    }

void CThumbCache::FreePage(TInt aPage)
    {
    EmptyPage(aPage);
    TPage& page = iPages[aPage];
    if (iGl)
        iGl->DeleteTexture(page.iTexture);
    User::Free(page.iShadow);
    page.iShadow = NULL;
    page.iDirty = EFalse;
    iPagesFreed++;
    }

TBool CThumbCache::MakePage(TInt aPage, TSet aSet)
    {
    TPage& page = iPages[aPage];
    TBool made;
    if (iEtc1)
        {
        page.iShadow = static_cast<TUint8*>(User::AllocZ(KPageSize * KPageSize / 2));
        made = page.iShadow && iGl->CreateEtc1(page.iTexture, KPageSize, KPageSize, page.iShadow);
        if (!made)
            {
            User::Free(page.iShadow);
            page.iShadow = NULL;
            }
        }
    else
        made = iGl->CreateTexture(page.iTexture, KPageSize, KPageSize, TGlTexture::ERgb565, ETrue, ETrue);
    if (!made)
        {
        iMaxPages = Pages(ELarge) + Pages(ESmall);
        Trace(_L("thumbs: page %d failed (graphics memory); at most %d now"), aPage, iMaxPages);
        return EFalse;
        }
    page.iSet = aSet;
    page.iUsedFrame = iFrame;
    page.iDirty = EFalse;
    iPagesMade++;
    return ETrue;
    }

TInt CThumbCache::AllocateCell(TSet aSet)
    {
    const TInt cells = CellsPerRow(aSet) * CellsPerRow(aSet);
    TInt best = -1, bestPage = -1;
    for (TInt p = 0; p < KMaxPages; p++)
        {
        const TPage& page = iPages[p];
        if (!page.iTexture.Valid() || page.iSet != aSet ||
            (bestPage >= 0 && page.iUsedFrame <= iPages[bestPage].iUsedFrame))
            continue;
        for (TInt i = p * KMaxCells; i < p * KMaxCells + cells; i++)
            if (iCells[i].iState == EFree)
                {
                best = i;
                bestPage = p;
                break;
                }
        }
    if (best >= 0)
        return best;
    if (!iHold)
        {
        TInt oldest = -1;
        for (TInt p = 0; p < KMaxPages; p++)
            if (iPages[p].iTexture.Valid() && iPages[p].iUsedFrame < iFrame &&
                (oldest < 0 || iPages[p].iUsedFrame < iPages[oldest].iUsedFrame) && Settled(p))
                oldest = p;
        if (oldest >= 0)
            {
            EmptyPage(oldest);
            iPages[oldest].iSet = aSet;
            return oldest * KMaxCells;
            }
        }
    if (iGl && Pages(ELarge) + Pages(ESmall) < iMaxPages)
        for (TInt p = 0; p < KMaxPages; p++)
            if (!iPages[p].iTexture.Valid())
                {
                if (MakePage(p, aSet))
                    return p * KMaxCells;
                break;
                }
    for (TInt p = 0; p < KMaxPages; p++)
        {
        if (!iPages[p].iTexture.Valid() || iPages[p].iSet != aSet)
            continue;
        for (TInt i = p * KMaxCells; i < p * KMaxCells + cells; i++)
            {
            const TCell& cell = iCells[i];
            if ((cell.iState == EReady || cell.iState == EFailed) && cell.iUsedFrame < iFrame &&
                (best < 0 || cell.iUsedFrame < iCells[best].iUsedFrame))
                best = i;
            }
        }
    if (best >= 0)
        {
        iCellByKey.Remove(iCells[best].iKey);
        iCells[best].iState = EFree;
        iCells[best].iKey = -1;
        }
    return best;
    }

void CThumbCache::Dispatch()
    {
    if (!iGl || !iLibrary || iWorker->Busy() || iWanted.Count() == 0)
        return;
    TInt half = -1;
    for (TInt h = 0; h < 2 && half < 0; h++)
        if (!iHalves[h].iBusy)
            half = h;
    if (half < 0)
        return;
    THalf& target = iHalves[half];
    TInt count = 0;
    for (TInt priority = EVisible; priority <= EAhead && count < KThumbBatch; priority++)
        for (TInt w = 0; w < iWanted.Count() && count < KThumbBatch; w++)
            {
            const TWanted& wanted = iWanted[w];
            if (wanted.iPriority != priority)
                continue;
            const TPhoto& photo = iLibrary->Photo(wanted.iPhoto);
            const TInt key = photo.iThumb * 2 + wanted.iSet;
            if (iCellByKey.Find(key))
                continue;
            const TInt cell = AllocateCell(wanted.iSet);
            if (cell < 0)
                break;
            if (iCellByKey.Insert(key, cell) != KErrNone)
                break;
            iCells[cell].iKey = key;
            iCells[cell].iState = ELoading;
            Touch(cell);
            TThumbItem& item = iItems[count];
            item.iPath.Copy(photo.iPath->Left(KMaxFileName));
            item.iKind = (TThumbKind)(wanted.iSet + (iEtc1 ? EThumbLargeEtc1 : EThumbLarge));
            item.iSlot = photo.iThumb;
            item.iSize = photo.iSize;
            item.iModified = I64LOW(photo.iModified);
            item.iOrientation = photo.iOrientation;
            target.iCells[count] = cell;
            target.iKeys[count] = key;
            count++;
            }
    if (count == 0)
        return;
    target.iBusy = ETrue;
    target.iCount = 0;
    target.iEpoch = iEpoch;
    if (iWorker->Request(iItems, count, iGeneration, half) != KErrNone)
        {
        target.iBusy = EFalse;
        for (TInt i = 0; i < count; i++)
            {
            iCellByKey.Remove(target.iKeys[i]);
            iCells[target.iCells[i]].iState = EFree;
            iCells[target.iCells[i]].iKey = -1;
            }
        }
    }

void CThumbCache::ThumbsReady(const TThumbItem* aItems, TInt aCount, const TUint8* aData, TInt aHalf)
    {
    THalf& half = iHalves[aHalf];
    if (half.iEpoch != iEpoch)
        {
        half.iBusy = EFalse;
        half.iCount = 0;
        iObserver.ThumbsChanged();
        return;
        }
    for (TInt i = 0; i < aCount; i++)
        {
        const TThumbItem& item = aItems[i];
        iWorkerUs += item.iMicros;
        if (item.iResult == KErrNone)
            {
            if (item.iSource == TThumbItem::ECache)
                iFromCache++;
            else if (item.iSource == TThumbItem::EConverted)
                iConverted++;
            else
                {
                if (item.iSource == TThumbItem::EExif)
                    iFromExif++;
                else
                    iDecoded++;
                iDecodeUs += item.iMicros;
                iMaxDecodeUs = Max(iMaxDecodeUs, item.iMicros);
                }
            }
        TCell& cell = iCells[half.iCells[i]];
        if (cell.iKey != half.iKeys[i] || cell.iState != ELoading)
            continue;
        if (item.iResult == KErrNone)
            {
            cell.iState = EUploading;
            continue;
            }
        iFailed++;
        TInt* tries = iAttempts.Find(item.iSlot);
        if (item.iResult == KErrDied && !tries)
            {

            iAttempts.Insert(item.iSlot, 1);
            iCellByKey.Remove(cell.iKey);
            cell.iState = EFree;
            cell.iKey = -1;
            }
        else
            {
            if (item.iResult != KErrNotSupported)
                Trace(_L("thumbs: %S failed %d"), &item.iPath, item.iResult);
            cell.iState = EFailed;
            }
        }
    half.iCount = aCount;
    half.iNext = 0;
    half.iData = aData;
    iObserver.ThumbsChanged();
    }

void CThumbCache::UploadCell(TInt aCell, const TUint8* aData)
    {
    TPage& page = iPages[aCell / KMaxCells];
    const TInt index = aCell % KMaxCells, side = Side(page.iSet), perRow = CellsPerRow(page.iSet);
    const TInt x = (index % perRow) * side, y = (index / perRow) * side;
    if (!iEtc1)
        {
        iGl->Upload(page.iTexture, x, y, side, side, aData);
        return;
        }
    const TInt rowBytes = side / 4 * 8, pageRowBytes = KPageSize / 4 * 8;
    for (TInt r = 0; r < side / 4; r++)
        Mem::Copy(page.iShadow + (y / 4 + r) * pageRowBytes + x / 4 * 8, aData + r * rowBytes, rowBytes);
    if (iSubImage == ESubNo)
        {
        page.iDirty = ETrue;
        return;
        }
    if (iSubImage == ESubUnknown)
        iGl->TakeError();
    iGl->UploadEtc1(page.iTexture, x, y, side, side, aData);
    if (iSubImage == ESubUnknown)
        {
        iSubImage = iGl->TakeError() ? ESubNo : ESubYes;
        Trace(_L("thumbs: ETC1 pages, part uploads %S"), iSubImage == ESubYes ? &KPartsWork() : &KPartsRefused());
        if (iSubImage == ESubNo)
            page.iDirty = ETrue;
        }
    }

void CThumbCache::Upload(TInt aBudget)
    {
    while (aBudget > 0)
        {
        if (iUploadHalf < 0)
            {
            for (TInt h = 0; h < 2 && iUploadHalf < 0; h++)
                if (iHalves[h].iBusy && iHalves[h].iCount > 0)
                    iUploadHalf = h;
            if (iUploadHalf < 0)
                break;
            }
        THalf& half = iHalves[iUploadHalf];
        while (half.iNext < half.iCount && aBudget > 0)
            {
            const TInt i = half.iNext++;
            TCell& cell = iCells[half.iCells[i]];
            if (cell.iKey != half.iKeys[i] || cell.iState != EUploading || !iGl)
                continue;
            const TInt64 startUs = PerfNowUs();
            UploadCell(half.iCells[i], half.iData + i * KThumbBytes);
            iUploadUs += PerfNowUs() - startUs;
            iUploaded++;
            cell.iState = EReady;
            aBudget--;
            }
        if (half.iNext >= half.iCount)
            {
            half.iBusy = EFalse;
            half.iCount = 0;
            iUploadHalf = -1;
            }
        }
    if (!iGl)
        return;
    for (TInt p = 0; p < KMaxPages; p++)
        if (iPages[p].iDirty)
            {
            const TInt64 startUs = PerfNowUs();
            iGl->ReplaceEtc1(iPages[p].iTexture, iPages[p].iShadow);
            iUploadUs += PerfNowUs() - startUs;
            iPages[p].iDirty = EFalse;
            }
    }

TBool CThumbCache::Pump(TInt aBudget)
    {
    if (!iHold)
        for (TInt p = 0; p < KMaxPages; p++)
            if (iPages[p].iTexture.Valid() && iFrame - iPages[p].iUsedFrame > KKeepFrames && Settled(p))
                FreePage(p);
    Dispatch();
    Upload(aBudget);
    Dispatch();
    TBool uploads = EFalse;
    for (TInt h = 0; h < 2; h++)
        uploads |= iHalves[h].iBusy && iHalves[h].iCount > 0;
    return uploads || iWorker->Busy();
    }

void CThumbCache::Idle()
    {
    if (iHold)
        return;
    for (TInt p = 0; p < KMaxPages; p++)
        if (iPages[p].iTexture.Valid() && iPages[p].iUsedFrame < iFrame && Settled(p))
            FreePage(p);
    }

TInt CThumbCache::Trim(TInt aIdleFrames)
    {
    if (!iGl)
        return 0;
    TInt freed = 0;
    for (TInt p = 0; p < KMaxPages; p++)
        if (iPages[p].iTexture.Valid() && iFrame - iPages[p].iUsedFrame > aIdleFrames && Settled(p))
            {
            freed += iPages[p].iTexture.Bytes();
            FreePage(p);
            }
    return freed;
    }

TInt CThumbCache::Bytes(TSet aSet) const
    {
    TInt bytes = 0;
    for (TInt p = 0; p < KMaxPages; p++)
        if (iPages[p].iTexture.Valid() && iPages[p].iSet == aSet)
            bytes += iPages[p].iTexture.Bytes();
    return bytes;
    }

TInt CThumbCache::Pages(TSet aSet) const
    {
    TInt count = 0;
    for (TInt p = 0; p < KMaxPages; p++)
        if (iPages[p].iTexture.Valid() && iPages[p].iSet == aSet)
            count++;
    return count;
    }

void CThumbCache::TraceStats()
    {
    const TInt made = iFromExif + iDecoded, read = iFromCache + iConverted;
    if (read + made + iFailed + iUploaded + iPagesMade + iPagesFreed == 0)
        return;
    Trace(_L("thumbs: %d from cache, %d converted, %d exif, %d decoded (avg %d max %d ms), %d failed; worker avg %d "
             "ms/thumb; %d uploaded avg %d us; pages %d large + %d small (%S), %d made, %d freed"),
          iFromCache, iConverted, iFromExif, iDecoded, made ? iDecodeUs / made / 1000 : 0, iMaxDecodeUs / 1000,
          iFailed, read + made ? iWorkerUs / (read + made) / 1000 : 0, iUploaded,
          iUploaded ? (TInt)(iUploadUs / iUploaded) : 0, Pages(ELarge), Pages(ESmall),
          iEtc1 ? &KEtc1() : &KRgb565(), iPagesMade, iPagesFreed);
    iFromCache = iConverted = iFromExif = iDecoded = iFailed = iWorkerUs = iDecodeUs = iMaxDecodeUs = iUploaded = 0;
    iPagesMade = iPagesFreed = 0;
    iUploadUs = 0;
    }
