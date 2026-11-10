#include "decoder.h"

#include <f32file.h>
#include <fbs.h>
#include <imageconversion.h>
#include <utf.h>

#include "exif.h"
#include "library.h"
#include "texcodec.h"
#include "trace.h"

extern "C"
    {
#include "n8media.h"
    }

namespace
    {
    const TUint32 KRecordMagic = 0x31485450;
    const TInt KRecordHeader = 16;
    const TInt KPosterSide = 256;
    _LIT(KCacheName, "thumbs");
    _LIT(KCacheExt, ".dat");

    const TText* const KKindSuffix[EThumbKinds] = { _S(""), _S("s"), _S("e"), _S("es") };

    class CWaiter : public CActive
        {
    public:
        CWaiter() : CActive(EPriorityStandard) { CActiveScheduler::Add(this); }
        ~CWaiter() { Cancel(); }
        TRequestStatus& Status() { return iStatus; }
        TInt Wait()
            {
            SetActive();
            iWait.Start();
            return iStatus.Int();
            }

    private:
        void RunL() { iWait.AsyncStop(); }
        void DoCancel() {}
        CActiveSchedulerWait iWait;
        };

    CFbsBitmap* ConvertL(CImageDecoder& aDecoder, TSize aSize)
        {
        CFbsBitmap* bitmap = new (ELeave) CFbsBitmap;
        CleanupStack::PushL(bitmap);
        User::LeaveIfError(bitmap->Create(aSize, EColor64K));
        CWaiter* waiter = new (ELeave) CWaiter;
        CleanupStack::PushL(waiter);
        aDecoder.Convert(&waiter->Status(), *bitmap, 0);
        TInt err = waiter->Wait();
        CleanupStack::PopAndDestroy(waiter);
        User::LeaveIfError(err);
        CleanupStack::Pop(bitmap);
        return bitmap;
        }

    TInt PickReduction(CImageDecoder& aDecoder, TSize aFull, TSize aMin, TSize& aSize)
        {
        aSize = aFull;
        for (TInt r = 3; r > 0; r--)
            {
            TSize reduced;
            if (aDecoder.ReducedSize(aFull, r, reduced) == KErrNone && reduced.iWidth >= aMin.iWidth &&
                reduced.iHeight >= aMin.iHeight && reduced.iWidth > 0)
                {
                aSize = reduced;
                return r;
                }
            }
        return 0;
        }

    inline TUint Pack(TUint aR, TUint aG, TUint aB) { return (aR << 11) | (aG << 5) | aB; }

    void SquareThumb(const TUint16* aPixels, TInt aStridePixels, const TRect& aRect, TInt aOrientation, TUint16* aOut)
        {
        const TInt side = Min(aRect.Width(), aRect.Height());
        const TInt x0 = aRect.iTl.iX + (aRect.Width() - side) / 2, y0 = aRect.iTl.iY + (aRect.Height() - side) / 2;
        TInt from[KThumbSize + 1];
        for (TInt k = 0; k <= KThumbSize; k++)
            from[k] = k * side / KThumbSize;
        const TInt last = KThumbSize - 1;
        for (TInt y = 0; y < KThumbSize; y++)
            for (TInt x = 0; x < KThumbSize; x++)
                {

                TInt sx, sy;
                switch (aOrientation)
                    {
                    case 2: sx = last - x; sy = y; break;
                    case 3: sx = last - x; sy = last - y; break;
                    case 4: sx = x; sy = last - y; break;
                    case 5: sx = y; sy = x; break;
                    case 6: sx = y; sy = last - x; break;
                    case 7: sx = last - y; sy = last - x; break;
                    case 8: sx = last - y; sy = x; break;
                    default: sx = x; sy = y; break;
                    }
                const TInt ax = from[sx], bx = Max(ax + 1, from[sx + 1]);
                const TInt ay = from[sy], by = Max(ay + 1, from[sy + 1]);
                TUint r = 0, g = 0, b = 0, n = 0;
                for (TInt j = ay; j < by; j++)
                    {
                    const TUint16* row = aPixels + (y0 + j) * aStridePixels + x0;
                    for (TInt i = ax; i < bx; i++)
                        {
                        const TUint p = row[i];
                        r += p >> 11;
                        g += (p >> 5) & 63;
                        b += p & 31;
                        n++;
                        }
                    }
                aOut[y * KThumbSize + x] = (TUint16)Pack((r + n / 2) / n, (g + n / 2) / n, (b + n / 2) / n);
                }
        }

    void ShrinkToFit(TUint16* aPixels, TSize& aSize, TInt& aStride, TInt aMaxSide, TInt aMaxPixels)
        {
        while (aSize.iWidth > 1 && aSize.iHeight > 1 &&
               (aSize.iWidth > aMaxSide || aSize.iHeight > aMaxSide || aSize.iWidth * aSize.iHeight > aMaxPixels))
            {
            const TSize half(aSize.iWidth / 2, aSize.iHeight / 2);
            const TInt halfStride = (half.iWidth * 2 + 3) & ~3;
            const TInt stridePixels = aStride / 2;
            for (TInt y = 0; y < half.iHeight; y++)
                {
                const TUint16* a = aPixels + y * 2 * stridePixels;
                const TUint16* b = a + stridePixels;
                TUint16* out = aPixels + y * (halfStride / 2);
                for (TInt x = 0; x < half.iWidth; x++)
                    {
                    const TUint p = a[2 * x], q = a[2 * x + 1], s = b[2 * x], t = b[2 * x + 1];
                    const TUint red = (p >> 11) + (q >> 11) + (s >> 11) + (t >> 11);
                    const TUint green = ((p >> 5) & 63) + ((q >> 5) & 63) + ((s >> 5) & 63) + ((t >> 5) & 63);
                    const TUint blue = (p & 31) + (q & 31) + (s & 31) + (t & 31);
                    out[x] = (TUint16)Pack((red + 2) / 4, (green + 2) / 4, (blue + 2) / 4);
                    }
                }
            aSize = half;
            aStride = halfStride;
            }
        }

    TInt CacheFolder(RFs& aFs, TInt aIndex, TDes& aFolder)
        {
        const TInt drives[2] = { EDriveE, EDriveC };
        TDriveInfo info;
        if (aFs.Drive(info, drives[aIndex]) != KErrNone || info.iType == EMediaNotPresent)
            return KErrNotReady;
        const TInt made = aFs.CreatePrivatePath(drives[aIndex]);
        if (made != KErrNone && made != KErrAlreadyExists)
            return made;
        TFileName dir;
        aFs.PrivatePath(dir);
        TChar letter;
        RFs::DriveToChar(drives[aIndex], letter);
        aFolder.Zero();
        aFolder.Append(letter);
        aFolder.Append(':');
        aFolder.Append(dir);
        return KErrNone;
        }

    TBool IsJpeg(const TDesC& aPath)
        {
        TParsePtrC parse(aPath);
        return parse.Ext().CompareF(_L(".jpg")) == 0 || parse.Ext().CompareF(_L(".jpeg")) == 0;
        }

    TBool Dark(const TUint16* aPixels, TInt aStridePixels, const TRect& aRect)
        {
        if (aRect.Width() <= 0 || aRect.Height() <= 0)
            return EFalse;
        TUint sum = 0;
        for (TInt y = aRect.iTl.iY; y < aRect.iBr.iY; y++)
            for (TInt x = aRect.iTl.iX; x < aRect.iBr.iX; x++)
                {
                const TUint p = aPixels[y * aStridePixels + x];
                const TUint r = (p >> 11) << 3, g = ((p >> 5) & 63) << 2, b = (p & 31) << 3;
                sum += r > g ? (r > b ? r : b) : (g > b ? g : b);
                }
        return sum < (TUint)(aRect.Width() * aRect.Height() * 28);
        }

    TRect PreviewContent(const CFbsBitmap& aBitmap, TSize aSize, TInt aWidth, TInt aHeight)
        {
        const TRect all(aSize);
        if (aWidth <= 0 || aHeight <= 0)
            return all;
        const TInt64 preview = (TInt64)aSize.iWidth * aHeight, picture = (TInt64)aSize.iHeight * aWidth;
        const TInt64 skew = preview > picture ? preview - picture : picture - preview;
        if (skew * 33 <= picture)
            return all;
        const TUint16* pixels = reinterpret_cast<const TUint16*>(aBitmap.DataAddress());
        const TInt stride = CFbsBitmap::ScanLineLength(aSize.iWidth, EColor64K) / 2;
        TRect content, first, second;
        if (preview > picture)
            {

            const TInt h = (TInt)((TInt64)aSize.iWidth * aHeight / aWidth);
            const TInt band = (aSize.iHeight - h) / 2;
            content = TRect(0, band, aSize.iWidth, band + h);
            first = TRect(0, 0, aSize.iWidth, Max(0, band - 1));
            second = TRect(0, band + h + 1, aSize.iWidth, aSize.iHeight);
            }
        else
            {
            const TInt w = (TInt)((TInt64)aSize.iHeight * aWidth / aHeight);
            const TInt band = (aSize.iWidth - w) / 2;
            content = TRect(band, 0, band + w, aSize.iHeight);
            first = TRect(0, 0, Max(0, band - 1), aSize.iHeight);
            second = TRect(band + w + 1, 0, aSize.iWidth, aSize.iHeight);
            }
        return Dark(pixels, stride, first) && Dark(pixels, stride, second) ? content : all;
        }

    CFbsBitmap* DecodePreviewL(RFs& aFs, const TDesC& aPath, TRect& aContent, TImageHeader& aHeader)
        {
        const TInt KHeadBytes = 64 * 1024;
        const TInt KMaxPreview = 256 * 1024;
        RFile file;
        User::LeaveIfError(file.Open(aFs, aPath, EFileRead | EFileShareReadersOnly));
        CleanupClosePushL(file);
        HBufC8* head = HBufC8::NewLC(KHeadBytes);
        TPtr8 bytes = head->Des();
        User::LeaveIfError(file.Read(bytes, KHeadBytes));
        if (ParseImageHeader(bytes.Ptr(), bytes.Length(), aHeader) != EFormatJpeg || aHeader.iThumbLength <= 0 ||
            aHeader.iThumbLength > KMaxPreview)
            {
            CleanupStack::PopAndDestroy(2, &file);
            return NULL;
            }
        TPtrC8 preview;
        TInt pushed = 2;
        if (aHeader.iThumbOffset + aHeader.iThumbLength <= bytes.Length())
            preview.Set(bytes.Mid(aHeader.iThumbOffset, aHeader.iThumbLength));
        else
            {
            HBufC8* far = HBufC8::NewLC(aHeader.iThumbLength);
            pushed++;
            TPtr8 farBytes = far->Des();
            User::LeaveIfError(file.Read(aHeader.iThumbOffset, farBytes, aHeader.iThumbLength));
            preview.Set(farBytes);
            }

        CImageDecoder* decoder = CImageDecoder::DataNewL(aFs, preview, CImageDecoder::EOptionNoDither);
        CleanupStack::PushL(decoder);
        pushed++;
        const TSize size = decoder->FrameInfo(0).iOverallSizeInPixels;
        CFbsBitmap* bitmap = ConvertL(*decoder, size);
        CleanupStack::PopAndDestroy(pushed);
        bitmap->BeginDataAccess();
        aContent = PreviewContent(*bitmap, size, aHeader.iWidth, aHeader.iHeight);
        bitmap->EndDataAccess(ETrue);
        return bitmap;
        }
    }

class CThumbWorker::TShared
    {
public:
    TWorkerLink iLink;
    TThumbItem iItems[KThumbBatch];
    TInt iCount;
    TInt iGeneration;
    TInt iHalf;
    TUint8* iData;

    };

namespace
    {
    class TThumbMaker
        {
    public:
        TThumbMaker(CThumbWorker::TShared& aShared, RFs& aFs)
            : iShared(aShared), iFs(aFs), iGeneration(-1), iPreviewMisses(0), iDecodeLogs(0), iPosterLogs(0) {}
        ~TThumbMaker()
            {
            for (TInt k = 0; k < EThumbKinds; k++)
                iCache[k].Close();
            }

        RFile* Cache(TInt aKind)
            {
            if (iGeneration != iShared.iGeneration)
                {
                for (TInt k = 0; k < EThumbKinds; k++)
                    iCache[k].Close();
                iGeneration = iShared.iGeneration;
                iFolder.Zero();
                for (TInt i = 0; i < 2 && iFolder.Length() == 0; i++)
                    if (CacheFolder(iFs, i, iFolder) != KErrNone)
                        iFolder.Zero();
                if (iFolder.Length() == 0)
                    iShared.iLink.Log(_L("thumbs: no cache folder"));
                else
                    DeleteOtherGenerations();
                }
            RFile& file = iCache[aKind];
            if (file.SubSessionHandle() == 0 && iFolder.Length() > 0)
                {
                TFileName name(iFolder);
                name.Append(KCacheName);
                name.AppendNum(iGeneration);
                name.Append(TPtrC(KKindSuffix[aKind]));
                name.Append(KCacheExt);
                TInt err = file.Open(iFs, name, EFileRead | EFileWrite | EFileShareAny);
                if (err == KErrNotFound)
                    err = file.Create(iFs, name, EFileRead | EFileWrite | EFileShareAny);
                iShared.iLink.Log(_L("thumbs: cache %S err %d"), &name, err);
                }
            return file.SubSessionHandle() ? &file : NULL;
            }

        void DeleteOtherGenerations()
            {
            TFileName pattern(iFolder);
            pattern.Append(KCacheName);
            pattern.Append('*');
            CDir* files = NULL;
            if (iFs.GetDir(pattern, KEntryAttNormal, ESortNone, files) != KErrNone || !files)
                return;
            for (TInt i = 0; i < files->Count(); i++)
                {
                const TDesC& entry = (*files)[i].iName;
                TLex lex(entry.Mid(Min(entry.Length(), KCacheName().Length())));
                TInt generation = -1;
                if (lex.Val(generation) == KErrNone && generation == iGeneration)
                    continue;
                TFileName name(iFolder);
                name.Append(entry);
                iFs.Delete(name);
                }
            delete files;
            }

        TBool ReadCached(TInt aKind, const TThumbItem& aItem, TUint8* aOut)
            {
            RFile* file = Cache(aKind);
            if (!file || aItem.iSlot < 0)
                return EFalse;
            const TInt bytes = ThumbKindBytes(aKind);
            TBuf8<KRecordHeader> header;
            const TInt at = aItem.iSlot * (KRecordHeader + bytes);
            if (file->Read(at, header) != KErrNone || header.Length() < KRecordHeader)
                return EFalse;
            const TUint32* h = reinterpret_cast<const TUint32*>(header.Ptr());
            if (h[0] != KRecordMagic || h[1] != (TUint32)aItem.iSize || h[2] != aItem.iModified ||
                h[3] != (TUint32)aItem.iSlot)
                return EFalse;
            TPtr8 data(aOut, bytes);
            return file->Read(at + KRecordHeader, data, bytes) == KErrNone && data.Length() == bytes;
            }

        void WriteCached(TInt aKind, const TThumbItem& aItem, const TUint8* aData)
            {
            RFile* file = Cache(aKind);
            if (!file || aItem.iSlot < 0)
                return;
            const TInt bytes = ThumbKindBytes(aKind);
            TUint32 h[4] = { KRecordMagic, (TUint32)aItem.iSize, aItem.iModified, (TUint32)aItem.iSlot };
            const TInt at = aItem.iSlot * (KRecordHeader + bytes);
            TInt size = 0;

            file->Size(size);
            if (at > size)
                file->SetSize(at);
            if (file->Write(at + KRecordHeader, TPtrC8(aData, bytes)) == KErrNone)
                file->Write(at, TPtrC8(reinterpret_cast<const TUint8*>(h), KRecordHeader));
            }

        void Convert(TInt aKind, const TUint16* aLarge, TUint8* aOut)
            {
            if (aKind == EThumbLargeEtc1)
                Etc1Encode(aLarge, KThumbSize, KThumbSize, aOut);
            else if (aKind == EThumbSmall)
                HalveRgb565(aLarge, KThumbSize, reinterpret_cast<TUint16*>(aOut));
            else
                {
                HalveRgb565(aLarge, KThumbSize, iSmall);
                Etc1Encode(iSmall, KSmallThumbSize, KSmallThumbSize, aOut);
                }
            }

        void DecodeL(TThumbItem& aItem, TUint16* aOut)
            {
            if (CLibrary::IsVideoPath(aItem.iPath))
                {
                PosterL(aItem, aOut);
                return;
                }
            CFbsBitmap* bitmap = NULL;
            TRect content;
            if (IsJpeg(aItem.iPath))
                {
                TImageHeader header;
                TRAPD(err, bitmap = DecodePreviewL(iFs, aItem.iPath, content, header));
                if (err != KErrNone || (bitmap && Min(content.Width(), content.Height()) < KThumbSize / 2))
                    {
                    if (iPreviewMisses++ < 3)
                        iShared.iLink.Log(_L("thumbs: no preview in %S: err %d, %dx%d"), &aItem.iPath, err,
                                          content.Width(), content.Height());
                    delete bitmap;
                    bitmap = NULL;
                    }
                }
            if (bitmap)
                aItem.iSource = TThumbItem::EExif;
            else
                {
                const CImageDecoder::TOptions options = (CImageDecoder::TOptions)(
                    CImageDecoder::EOptionAlwaysThread | CImageDecoder::EOptionNoDither |
                    CImageDecoder::EPreferFastDecode);
                CImageDecoder* decoder = CImageDecoder::FileNewL(iFs, aItem.iPath, options);
                CleanupStack::PushL(decoder);
                decoder->SetDecoderThreadPriority(EPriorityMuchLess);
                const TSize full = decoder->FrameInfo(0).iOverallSizeInPixels;
                TSize size;
                const TInt r = PickReduction(*decoder, full, TSize(KThumbSize, KThumbSize), size);
                const TUint32 start = User::FastCounter();
                bitmap = ConvertL(*decoder, size);
                CleanupStack::PopAndDestroy(decoder);
                if (iDecodeLogs++ < 5)
                    iShared.iLink.Log(_L("thumbs: decoded %S %dx%d at 1/%d (%dx%d) in %d ms"), &aItem.iPath,
                                      full.iWidth, full.iHeight, 1 << r, size.iWidth, size.iHeight,
                                      PerfCounterUs(start, User::FastCounter()) / 1000);
                content = TRect(size);
                aItem.iSource = TThumbItem::EDecoded;
                }
            CleanupStack::PushL(bitmap);
            bitmap->BeginDataAccess();
            SquareThumb(reinterpret_cast<const TUint16*>(bitmap->DataAddress()),
                        CFbsBitmap::ScanLineLength(bitmap->SizeInPixels().iWidth, EColor64K) / 2, content,
                        aItem.iOrientation, aOut);
            bitmap->EndDataAccess(ETrue);
            CleanupStack::PopAndDestroy(bitmap);
            }

        void PosterL(TThumbItem& aItem, TUint16* aOut)
            {
            TBuf8<KMaxFileName * 3 + 1> path;
            if (CnvUtfConverter::ConvertFromUnicodeToUtf8(path, aItem.iPath) != KErrNone)
                User::Leave(KErrNotSupported);
            TUint8* pixels = static_cast<TUint8*>(User::AllocL(KPosterSide * KPosterSide * 2));
            CleanupStack::PushL(TCleanupItem(User::Free, pixels));
            RThread thread;
            const TThreadPriority before = thread.Priority();
            thread.SetPriority(EPriorityMuchLess);
            TInt w = 0, h = 0;
            const TUint32 start = User::FastCounter();
            const TInt err = n8m_poster(reinterpret_cast<const char*>(path.PtrZ()), KPosterSide, pixels, &w, &h);
            const TInt ms = PerfCounterUs(start, User::FastCounter()) / 1000;
            thread.SetPriority(before);
            if (err != 0 || w <= 0 || h <= 0 || w > KPosterSide || h > KPosterSide)
                {
                if (err != 0 && iPosterLogs++ < 3)
                    iShared.iLink.Log(_L("thumbs: no poster for %S: %d in %d ms"), &aItem.iPath, err, ms);
                User::Leave(KErrNotSupported);
                }
            if (iPosterLogs++ < 5)
                iShared.iLink.Log(_L("thumbs: poster %S %dx%d in %d ms"), &aItem.iPath, w, h, ms);

            SquareThumb(reinterpret_cast<const TUint16*>(pixels), w, TRect(0, 0, w, h), 1, aOut);
            CleanupStack::PopAndDestroy(pixels);
            aItem.iSource = TThumbItem::EDecoded;
            }

        void RunJob()
            {
            for (TInt i = 0; i < iShared.iCount; i++)
                {
                TThumbItem& item = iShared.iItems[i];
                TUint8* out = iShared.iData + (iShared.iHalf * KThumbBatch + i) * KThumbBytes;
                item.iSource = TThumbItem::ENone;
                if (iShared.iLink.iQuit)
                    {
                    item.iResult = KErrCancel;
                    continue;
                    }
                const TUint32 start = User::FastCounter();
                item.iResult = KErrNone;
                if (ReadCached(item.iKind, item, out))
                    item.iSource = TThumbItem::ECache;
                else
                    {
                    TUint8* large = item.iKind == EThumbLarge ? out : reinterpret_cast<TUint8*>(iLarge);
                    if (ReadCached(EThumbLarge, item, large))
                        item.iSource = TThumbItem::EConverted;
                    else
                        {
                        TRAP(item.iResult, DecodeL(item, reinterpret_cast<TUint16*>(large)));
                        if (item.iResult == KErrNone)
                            WriteCached(EThumbLarge, item, large);
                        }
                    if (item.iResult == KErrNone && item.iKind != EThumbLarge)
                        {
                        Convert(item.iKind, iLarge, out);
                        WriteCached(item.iKind, item, out);
                        }
                    }
                item.iMicros = PerfCounterUs(start, User::FastCounter());
                }
            }

        CThumbWorker::TShared& iShared;
        RFs& iFs;
        RFile iCache[EThumbKinds];
        TInt iGeneration;
        TUint16 iLarge[KThumbSize * KThumbSize];
        TUint16 iSmall[KSmallThumbSize * KSmallThumbSize];
        TFileName iFolder;
        TInt iPreviewMisses;
        TInt iDecodeLogs;
        TInt iPosterLogs;
        };

    void ThumbBody(TAny* aShared)
        {
        CThumbWorker::TShared& shared = *static_cast<CThumbWorker::TShared*>(aShared);
        RFs fs;
        const TInt fsErr = fs.Connect();
        const TInt fbsErr = RFbsSession::Connect();
        TThumbMaker* maker = fsErr == KErrNone && fbsErr == KErrNone ? new TThumbMaker(shared, fs) : NULL;
        while (shared.iLink.WaitForJob())
            {
            if (maker)
                maker->RunJob();
            else
                shared.iLink.Log(_L("thumbs: no worker, fs %d fbs %d"), fsErr, fbsErr);
            shared.iLink.JobDone(maker ? KErrNone : KErrNoMemory);
            }
        delete maker;
        if (fbsErr == KErrNone)
            RFbsSession::Disconnect();
        fs.Close();
        }

    TInt ThumbThread(TAny* aShared)
        {
        return RunWorkerThread(ThumbBody, aShared);
        }
    }

CThumbWorker* CThumbWorker::NewL(MThumbObserver& aObserver)
    {
    TShared* shared = new (ELeave) TShared;
    shared->iLink.iDone = NULL;
    shared->iLink.iQuit = EFalse;
    shared->iCount = 0;
    shared->iGeneration = 0;
    shared->iHalf = 0;
    shared->iData = static_cast<TUint8*>(User::Alloc(2 * KThumbBatch * KThumbBytes));
    CThumbWorker* self = shared->iData ? new CThumbWorker(aObserver, shared) : NULL;
    if (!self)
        {
        User::Free(shared->iData);
        delete shared;
        User::Leave(KErrNoMemory);
        }
    return self;
    }

CThumbWorker::CThumbWorker(MThumbObserver& aObserver, TShared* aShared)
    : CWorker(aShared->iLink, ThumbThread, aShared, _L("thumbs")), iObserver(aObserver), iShared(aShared)
    {
    }

CThumbWorker::~CThumbWorker()
    {
    Shutdown();
    User::Free(iShared->iData);
    delete iShared;
    }

TInt CThumbWorker::Request(const TThumbItem* aItems, TInt aCount, TInt aGeneration, TInt aHalf)
    {
    if (Busy())
        return KErrInUse;
    aCount = Min(aCount, (TInt)KThumbBatch);
    for (TInt i = 0; i < aCount; i++)
        iShared->iItems[i] = aItems[i];
    iShared->iCount = aCount;
    iShared->iGeneration = aGeneration;
    iShared->iHalf = aHalf;
    return Submit();
    }

void CThumbWorker::RunL()
    {
    WriteLog();
    if (iStatus.Int() != KErrNone)
        {

        for (TInt i = 0; i < iShared->iCount; i++)
            iShared->iItems[i].iResult = iStatus.Int();
        }
    iObserver.ThumbsReady(iShared->iItems, iShared->iCount, iShared->iData + iShared->iHalf * KThumbBatch * KThumbBytes,
                          iShared->iHalf);
    }

class CImageWorker::TShared
    {
public:
    TWorkerLink iLink;
    TImageJob iJob;
    };

namespace
    {

    void DecodePreviewImageL(RFs& aFs, TImageJob& aJob)
        {
        TRect content;
        TImageHeader header;
        CFbsBitmap* bitmap = DecodePreviewL(aFs, aJob.iPath, content, header);
        if (!bitmap)
            User::Leave(KErrNotFound);
        CleanupStack::PushL(bitmap);
        aJob.iFullSize = TSize(header.iWidth, header.iHeight);
        const TInt stride = (content.Width() * 2 + 3) & ~3;
        aJob.iPixels = static_cast<TUint16*>(User::AllocL(stride * content.Height()));
        bitmap->BeginDataAccess();
        const TUint8* source = reinterpret_cast<const TUint8*>(bitmap->DataAddress());
        const TInt sourceStride = CFbsBitmap::ScanLineLength(bitmap->SizeInPixels().iWidth, EColor64K);
        for (TInt y = 0; y < content.Height(); y++)
            Mem::Copy(reinterpret_cast<TUint8*>(aJob.iPixels) + y * stride,
                      source + (content.iTl.iY + y) * sourceStride + content.iTl.iX * 2, content.Width() * 2);
        bitmap->EndDataAccess(ETrue);
        CleanupStack::PopAndDestroy(bitmap);
        aJob.iStride = stride;
        aJob.iSize = content.Size();
        }

    void DecodeImageL(RFs& aFs, TImageJob& aJob)
        {
        if (aJob.iPreview)
            {
            DecodePreviewImageL(aFs, aJob);
            return;
            }
        CImageDecoder* decoder = CImageDecoder::FileNewL(aFs, aJob.iPath, CImageDecoder::EOptionAlwaysThread);
        CleanupStack::PushL(decoder);
        decoder->SetDecoderThreadPriority(EPriorityLess);
        const TSize full = decoder->FrameInfo(0).iOverallSizeInPixels;
        aJob.iFullSize = full;
        TSize size;
        TInt r = PickReduction(*decoder, full, aJob.iMinSize, size);

        while (r < 3 && (size.iWidth > aJob.iMaxSide || size.iHeight > aJob.iMaxSide ||
                         size.iWidth * size.iHeight > aJob.iMaxPixels))
            {
            TSize smaller;
            if (decoder->ReducedSize(full, r + 1, smaller) != KErrNone)
                break;
            size = smaller;
            r++;
            }
        CFbsBitmap* bitmap = ConvertL(*decoder, size);
        CleanupStack::PopAndDestroy(decoder);
        CleanupStack::PushL(bitmap);
        const TInt stride = CFbsBitmap::ScanLineLength(size.iWidth, EColor64K);
        aJob.iPixels = static_cast<TUint16*>(User::AllocL(stride * size.iHeight));
        bitmap->BeginDataAccess();
        Mem::Copy(aJob.iPixels, bitmap->DataAddress(), stride * size.iHeight);
        bitmap->EndDataAccess(ETrue);
        CleanupStack::PopAndDestroy(bitmap);
        aJob.iStride = stride;
        aJob.iSize = size;
        ShrinkToFit(aJob.iPixels, aJob.iSize, aJob.iStride, aJob.iMaxSide, aJob.iMaxPixels);
        }

    void ImageBody(TAny* aShared)
        {
        CImageWorker::TShared& shared = *static_cast<CImageWorker::TShared*>(aShared);
        RFs fs;
        const TInt fsErr = fs.Connect();
        const TInt fbsErr = RFbsSession::Connect();
        while (shared.iLink.WaitForJob())
            {
            TImageJob& job = shared.iJob;
            const TUint32 start = User::FastCounter();
            job.iPixels = NULL;
            if (fsErr != KErrNone || fbsErr != KErrNone)
                job.iResult = fsErr != KErrNone ? fsErr : fbsErr;
            else
                TRAP(job.iResult, DecodeImageL(fs, job));
            if (job.iResult != KErrNone)
                {
                User::Free(job.iPixels);
                job.iPixels = NULL;
                }
            job.iMicros = PerfCounterUs(start, User::FastCounter());
            shared.iLink.JobDone(job.iResult);
            }
        if (fbsErr == KErrNone)
            RFbsSession::Disconnect();
        fs.Close();
        }

    TInt ImageThread(TAny* aShared)
        {
        return RunWorkerThread(ImageBody, aShared);
        }
    }

CImageWorker* CImageWorker::NewL(MImageObserver& aObserver)
    {
    TShared* shared = new (ELeave) TShared;
    shared->iLink.iDone = NULL;
    shared->iLink.iQuit = EFalse;
    shared->iJob.iPixels = NULL;
    shared->iJob.iTag = -1;
    CImageWorker* self = new CImageWorker(aObserver, shared);
    if (!self)
        {
        delete shared;
        User::Leave(KErrNoMemory);
        }
    return self;
    }

CImageWorker::CImageWorker(MImageObserver& aObserver, TShared* aShared)
    : CWorker(aShared->iLink, ImageThread, aShared, _L("image")), iObserver(aObserver), iShared(aShared)
    {
    }

CImageWorker::~CImageWorker()
    {
    Shutdown();
    User::Free(iShared->iJob.iPixels);
    delete iShared;
    }

TInt CImageWorker::Request(const TDesC& aPath, TInt aTag, TBool aPreview, TSize aMinSize, TInt aMaxSide,
                           TInt aMaxPixels)
    {
    if (Busy())
        return KErrInUse;
    TImageJob& job = iShared->iJob;
    job.iPath.Copy(aPath.Left(KMaxFileName));
    job.iTag = aTag;
    job.iPreview = aPreview;
    job.iMinSize = aMinSize;
    job.iMaxSide = aMaxSide;
    job.iMaxPixels = aMaxPixels;
    job.iPixels = NULL;
    job.iResult = KErrNone;
    return Submit();
    }

void CImageWorker::RunL()
    {
    WriteLog();
    TImageJob& job = iShared->iJob;
    if (iStatus.Int() != KErrNone && job.iResult == KErrNone)
        job.iResult = iStatus.Int();
    iObserver.ImageReady(job);
    if (job.iPixels)
        {

        User::Free(job.iPixels);
        job.iPixels = NULL;
        }
    }

class CGifWorker::TShared
    {
public:
    TWorkerLink iLink;
    TGifJob iJob;
    };

namespace
    {

    class TGifMaker
        {
    public:
        TGifMaker(RFs& aFs)
            : iFs(aFs), iDecoder(NULL), iFrame(NULL), iMask(NULL), iCanvas(NULL), iSaved(NULL), iNext(0),
              iCount(0), iLastFlags(0) {}
        ~TGifMaker() { Close(); }

        void Close()
            {
            delete iDecoder;
            iDecoder = NULL;
            delete iFrame;
            iFrame = NULL;
            delete iMask;
            iMask = NULL;
            User::Free(iCanvas);
            iCanvas = NULL;
            User::Free(iSaved);
            iSaved = NULL;
            iPath.Zero();
            }

        void OpenL(const TDesC& aPath)
            {
            Close();
            iDecoder = CImageDecoder::FileNewL(iFs, aPath, (CImageDecoder::TOptions)(
                CImageDecoder::EOptionAlwaysThread | CImageDecoder::EOptionUseFrameSizeInPixels));
            iDecoder->SetDecoderThreadPriority(EPriorityLess);
            iCount = iDecoder->FrameCount();
            if (iCount <= 0)
                User::Leave(KErrCorrupt);
            iSize = iDecoder->FrameInfo(0).iOverallSizeInPixels;
            if (iSize.iWidth <= 0 || iSize.iHeight <= 0)
                User::Leave(KErrCorrupt);
            iCanvas = static_cast<TUint16*>(User::AllocL(iSize.iWidth * iSize.iHeight * 2));
            iFrame = new (ELeave) CFbsBitmap;
            iMask = new (ELeave) CFbsBitmap;
            iPath.Copy(aPath.Left(KMaxFileName));
            iNext = 0;
            }

        void ComposeL(TInt& aDelayMs)
            {
            const TFrameInfo& info = iDecoder->FrameInfo(iNext);
            const TRect area = info.iFrameCoordsInPixels;
            if (iNext == 0)
                Mem::FillZ(iCanvas, iSize.iWidth * iSize.iHeight * 2);
            else if (iLastFlags & TFrameInfo::ERestoreToBackground)
                Fill(iLastArea);
            else if ((iLastFlags & TFrameInfo::ERestoreToPrevious) && iSaved)
                Mem::Copy(iCanvas, iSaved, iSize.iWidth * iSize.iHeight * 2);
            if (info.iFlags & TFrameInfo::ERestoreToPrevious)
                {
                if (!iSaved)
                    iSaved = static_cast<TUint16*>(User::AllocL(iSize.iWidth * iSize.iHeight * 2));
                Mem::Copy(iSaved, iCanvas, iSize.iWidth * iSize.iHeight * 2);
                }

            TSize size = info.iOverallSizeInPixels;
            if (size.iWidth <= 0 || size.iHeight <= 0)
                size = area.Size();
            const TBool masked = info.iFlags & TFrameInfo::ETransparencyPossible;
            iFrame->Reset();
            User::LeaveIfError(iFrame->Create(size, EColor64K));
            if (masked)
                {
                iMask->Reset();
                User::LeaveIfError(iMask->Create(size, EGray2));
                }
            CWaiter* waiter = new (ELeave) CWaiter;
            CleanupStack::PushL(waiter);
            if (masked)
                iDecoder->Convert(&waiter->Status(), *iFrame, *iMask, iNext);
            else
                iDecoder->Convert(&waiter->Status(), *iFrame, iNext);
            User::LeaveIfError(waiter->Wait());
            CleanupStack::PopAndDestroy(waiter);
            const TPoint at = size == iSize ? TPoint(0, 0) : area.iTl;
            HBufC8* maskRow = masked ? HBufC8::NewLC(size.iWidth) : NULL;
            iFrame->BeginDataAccess();
            const TUint16* pixels = reinterpret_cast<const TUint16*>(iFrame->DataAddress());
            const TInt stride = CFbsBitmap::ScanLineLength(size.iWidth, EColor64K) / 2;
            for (TInt y = 0; y < size.iHeight; y++)
                {
                const TInt cy = at.iY + y;
                if (cy < 0 || cy >= iSize.iHeight || cy < area.iTl.iY || cy >= area.iBr.iY)
                    continue;
                TPtr8 row(NULL, 0);
                if (maskRow)
                    {
                    row.Set(maskRow->Des());
                    iMask->GetScanLine(row, TPoint(0, y), size.iWidth, EGray256);
                    }
                for (TInt x = 0; x < size.iWidth; x++)
                    {
                    const TInt cx = at.iX + x;
                    if (cx < 0 || cx >= iSize.iWidth || cx < area.iTl.iX || cx >= area.iBr.iX)
                        continue;
                    if (maskRow && (x >= row.Length() || row[x] < 128))
                        continue;
                    iCanvas[cy * iSize.iWidth + cx] = pixels[y * stride + x];
                    }
                }
            iFrame->EndDataAccess(ETrue);
            if (maskRow)
                CleanupStack::PopAndDestroy(maskRow);
            iLastFlags = info.iFlags;
            iLastArea = area;
            aDelayMs = (TInt)(info.iDelay.Int64() / 1000);
            }

        TUint16* CopyL(TSize aSize, TInt aStride)
            {
            TUint16* out = static_cast<TUint16*>(User::AllocL(aStride * aSize.iHeight));
            for (TInt y = 0; y < aSize.iHeight; y++)
                {
                const TUint16* source = iCanvas + (y * iSize.iHeight / aSize.iHeight) * iSize.iWidth;
                TUint16* row = reinterpret_cast<TUint16*>(reinterpret_cast<TUint8*>(out) + y * aStride);
                for (TInt x = 0; x < aSize.iWidth; x++)
                    row[x] = source[x * iSize.iWidth / aSize.iWidth];
                }
            return out;
            }

        void RunL(TGifJob& aJob)
            {
            if (aJob.iRestart || iPath.CompareF(aJob.iPath) != 0 || !iDecoder)
                OpenL(aJob.iPath);
            aJob.iFrameCount = iCount;
            aJob.iCanvas = iSize;
            const TInt side = Max(iSize.iWidth, iSize.iHeight);
            aJob.iSize = side > aJob.iMaxSide ?
                TSize(Max(1, iSize.iWidth * aJob.iMaxSide / side), Max(1, iSize.iHeight * aJob.iMaxSide / side)) :
                iSize;
            aJob.iStride = (aJob.iSize.iWidth * 2 + 3) & ~3;
            while (aJob.iCount < TGifJob::KBatch)
                {
                TInt delay = 0;
                ComposeL(delay);
                const TInt i = aJob.iCount;
                aJob.iPixels[i] = CopyL(aJob.iSize, aJob.iStride);
                aJob.iIndex[i] = iNext;
                aJob.iDelayMs[i] = delay;
                aJob.iCount++;
                iNext = (iNext + 1) % iCount;
                if (iCount == 1)
                    break;
                }
            }

    private:
        void Fill(const TRect& aArea)
            {
            TRect area(aArea);
            area.Intersection(TRect(iSize));
            for (TInt y = area.iTl.iY; y < area.iBr.iY; y++)
                Mem::FillZ(iCanvas + y * iSize.iWidth + area.iTl.iX, area.Width() * 2);
            }

        RFs& iFs;
        CImageDecoder* iDecoder;
        CFbsBitmap* iFrame;
        CFbsBitmap* iMask;
        TUint16* iCanvas;
        TUint16* iSaved;
        TSize iSize;
        TFileName iPath;
        TInt iNext;
        TInt iCount;
        TUint32 iLastFlags;
        TRect iLastArea;
        };

    void GifBody(TAny* aShared)
        {
        CGifWorker::TShared& shared = *static_cast<CGifWorker::TShared*>(aShared);
        RFs fs;
        const TInt fsErr = fs.Connect();
        const TInt fbsErr = RFbsSession::Connect();
        TGifMaker* maker = fsErr == KErrNone && fbsErr == KErrNone ? new TGifMaker(fs) : NULL;
        while (shared.iLink.WaitForJob())
            {
            TGifJob& job = shared.iJob;
            const TUint32 start = User::FastCounter();
            job.iCount = 0;
            if (!maker)
                job.iResult = KErrNoMemory;
            else
                {
                TRAP(job.iResult, maker->RunL(job));
                if (job.iResult != KErrNone)
                    maker->Close();
                }
            if (job.iResult != KErrNone && job.iCount == 0)
                job.iFrameCount = 0;
            job.iMicros = PerfCounterUs(start, User::FastCounter());
            shared.iLink.JobDone(job.iCount > 0 ? KErrNone : job.iResult);
            }
        delete maker;
        if (fbsErr == KErrNone)
            RFbsSession::Disconnect();
        fs.Close();
        }

    TInt GifThread(TAny* aShared)
        {
        return RunWorkerThread(GifBody, aShared);
        }
    }

CGifWorker* CGifWorker::NewL(MGifObserver& aObserver)
    {
    TShared* shared = new (ELeave) TShared;
    shared->iLink.iDone = NULL;
    shared->iLink.iQuit = EFalse;
    shared->iJob.iCount = 0;
    CGifWorker* self = new CGifWorker(aObserver, shared);
    if (!self)
        {
        delete shared;
        User::Leave(KErrNoMemory);
        }
    return self;
    }

CGifWorker::CGifWorker(MGifObserver& aObserver, TShared* aShared)
    : CWorker(aShared->iLink, GifThread, aShared, _L("gif")), iObserver(aObserver), iShared(aShared)
    {
    }

CGifWorker::~CGifWorker()
    {
    Shutdown();
    for (TInt i = 0; i < iShared->iJob.iCount; i++)
        User::Free(iShared->iJob.iPixels[i]);
    delete iShared;
    }

TInt CGifWorker::Request(const TDesC& aPath, TInt aTag, TBool aRestart, TInt aMaxSide)
    {
    if (Busy())
        return KErrInUse;
    TGifJob& job = iShared->iJob;
    job.iPath.Copy(aPath.Left(KMaxFileName));
    job.iTag = aTag;
    job.iRestart = aRestart;
    job.iMaxSide = aMaxSide;
    job.iCount = 0;
    job.iResult = KErrNone;
    return Submit();
    }

void CGifWorker::RunL()
    {
    WriteLog();
    TGifJob& job = iShared->iJob;
    if (iStatus.Int() != KErrNone && job.iResult == KErrNone)
        job.iResult = iStatus.Int();
    iObserver.GifReady(job);
    for (TInt i = 0; i < job.iCount; i++)
        User::Free(job.iPixels[i]);
    job.iCount = 0;
    }
