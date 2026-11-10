#include "labelworker.h"

#include <bitdev.h>
#include <bitstd.h>
#include <fbs.h>

#include "trace.h"

class CLabelWorker::TShared
    {
public:
    TWorkerLink iLink;
    TLabelJob iJob;
    TFileName iFiles[KMaxFiles];
    TInt iFileCount;
    };

namespace
    {
    class TLabelMaker
        {
    public:
        TLabelMaker(CLabelWorker::TShared& aShared) : iShared(aShared), iBitmap(NULL), iDevice(NULL), iIdCount(0)
            {
            for (TInt i = 0; i < CLabelWorker::KMaxFonts; i++)
                iFonts[i] = NULL;
            }

        ~TLabelMaker()
            {
            if (iDevice)
                {
                for (TInt i = 0; i < CLabelWorker::KMaxFonts; i++)
                    if (iFonts[i])
                        iDevice->ReleaseFont(iFonts[i]);
                for (TInt i = 0; i < iIdCount; i++)
                    iDevice->RemoveFile(iIds[i]);
                }
            delete iDevice;
            delete iBitmap;
            }

        void ConstructL()
            {
            iBitmap = new (ELeave) CFbsBitmap;
            User::LeaveIfError(iBitmap->Create(TSize(256, 32), EColor16MU));
            iDevice = CFbsBitmapDevice::NewL(iBitmap);
            for (TInt i = 0; i < iShared.iFileCount; i++)
                {
                TInt id = 0;
                const TInt err = iDevice->AddFile(iShared.iFiles[i], id);
                if (err == KErrNone && iIdCount < CLabelWorker::KMaxFiles)
                    iIds[iIdCount++] = id;
                iShared.iLink.Log(_L("labels: add %S err %d"), &iShared.iFiles[i], err);
                }
            }

        void DrawL(TLabelJob& aJob)
            {
            if (aJob.iFont < 0 || aJob.iFont >= CLabelWorker::KMaxFonts)
                User::Leave(KErrArgument);
            CFont*& font = iFonts[aJob.iFont];
            if (!font)
                User::LeaveIfError(iDevice->GetNearestFontToDesignHeightInPixels(font, aJob.iSpec));
            const TSize size = aJob.iSize;
            const TSize have = iBitmap->SizeInPixels();
            if (have.iWidth < size.iWidth || have.iHeight < size.iHeight)
                {
                User::LeaveIfError(iBitmap->Resize(TSize(Max(have.iWidth, size.iWidth),
                                                         Max(have.iHeight, size.iHeight))));
                User::LeaveIfError(iDevice->Resize(iBitmap->SizeInPixels()));
                }
            CFbsBitGc* gc = NULL;
            User::LeaveIfError(iDevice->CreateContext(gc));
            gc->SetBrushStyle(CGraphicsContext::ESolidBrush);
            gc->SetBrushColor(KRgbBlack);
            gc->Clear(TRect(size));
            gc->SetBrushStyle(CGraphicsContext::ENullBrush);
            gc->UseFont(font);
            gc->SetPenColor(KRgbWhite);
            for (TInt i = 0; i < aJob.iText.Length(); i++)
                if (aJob.iText[i] != ' ')
                    gc->DrawText(aJob.iText.Mid(i, 1), TPoint(aJob.iPad + aJob.iPen[i], aJob.iBaseline));
            gc->DiscardFont();
            delete gc;
            aJob.iStride = (size.iWidth + 3) & ~3;
            aJob.iPixels = static_cast<TUint8*>(User::AllocL(aJob.iStride * size.iHeight));
            iBitmap->BeginDataAccess();
            const TUint32* source = iBitmap->DataAddress();
            const TInt sourceStride = CFbsBitmap::ScanLineLength(iBitmap->SizeInPixels().iWidth, EColor16MU) / 4;
            for (TInt y = 0; y < size.iHeight; y++)
                {
                const TUint32* row = source + y * sourceStride;
                TUint8* out = aJob.iPixels + y * aJob.iStride;
                for (TInt x = 0; x < size.iWidth; x++)
                    out[x] = (TUint8)((row[x] >> 8) & 0xFF);
                }
            iBitmap->EndDataAccess(ETrue);
            }

    private:
        CLabelWorker::TShared& iShared;
        CFbsBitmap* iBitmap;
        CFbsBitmapDevice* iDevice;
        CFont* iFonts[CLabelWorker::KMaxFonts];
        TInt iIds[CLabelWorker::KMaxFiles];
        TInt iIdCount;
        };

    void LabelBody(TAny* aShared)
        {
        CLabelWorker::TShared& shared = *static_cast<CLabelWorker::TShared*>(aShared);
        const TInt fbsErr = RFbsSession::Connect();
        TLabelMaker* maker = fbsErr == KErrNone ? new TLabelMaker(shared) : NULL;
        TInt err = maker ? KErrNone : KErrNoMemory;
        if (maker)
            TRAP(err, maker->ConstructL());
        while (shared.iLink.WaitForJob())
            {
            TLabelJob& job = shared.iJob;
            const TUint32 start = User::FastCounter();
            job.iPixels = NULL;
            job.iResult = err;
            if (err == KErrNone)
                TRAP(job.iResult, maker->DrawL(job));
            if (job.iResult != KErrNone)
                {
                User::Free(job.iPixels);
                job.iPixels = NULL;
                }
            job.iMicros = PerfCounterUs(start, User::FastCounter());
            shared.iLink.JobDone(job.iResult);
            }
        delete maker;
        if (fbsErr == KErrNone)
            RFbsSession::Disconnect();
        }

    TInt LabelThread(TAny* aShared)
        {
        return RunWorkerThread(LabelBody, aShared);
        }
    }

CLabelWorker* CLabelWorker::NewL(MLabelObserver& aObserver, const TDesC* const* aFiles, TInt aFileCount)
    {
    TShared* shared = new (ELeave) TShared;
    shared->iLink.iDone = NULL;
    shared->iLink.iQuit = EFalse;
    shared->iJob.iPixels = NULL;
    shared->iFileCount = Min(aFileCount, (TInt)KMaxFiles);
    for (TInt i = 0; i < shared->iFileCount; i++)
        shared->iFiles[i] = *aFiles[i];
    CLabelWorker* self = new CLabelWorker(aObserver, shared);
    if (!self)
        {
        delete shared;
        User::Leave(KErrNoMemory);
        }
    return self;
    }

CLabelWorker::CLabelWorker(MLabelObserver& aObserver, TShared* aShared)
    : CWorker(aShared->iLink, LabelThread, aShared, _L("labels")), iObserver(aObserver), iShared(aShared)
    {
    }

CLabelWorker::~CLabelWorker()
    {
    Shutdown();
    User::Free(iShared->iJob.iPixels);
    delete iShared;
    }

TInt CLabelWorker::Request(const TLabelJob& aJob)
    {
    if (Busy())
        return KErrInUse;
    iShared->iJob = aJob;
    iShared->iJob.iPixels = NULL;
    return Submit();
    }

void CLabelWorker::RunL()
    {
    WriteLog();
    TLabelJob& job = iShared->iJob;
    if (iStatus.Int() != KErrNone && job.iResult == KErrNone)
        job.iResult = iStatus.Int();
    iObserver.LabelReady(job);
    User::Free(job.iPixels);
    job.iPixels = NULL;
    }
