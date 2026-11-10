#include "video.h"

#include <coemain.h>
#include <fbs.h>
#include <gdi.h>
#include <remconcoreapitarget.h>
#include <remconinterfaceselector.h>
#include <w32std.h>

#include "glrenderer.h"
#include "iconbake.h"
#include "mp4.h"
#include "physics.h"
#include "spinner.h"
#include "trace.h"

class CRemConResponder : public CActive
    {
public:
    CRemConResponder(CRemConCoreApiTarget& aTarget) : CActive(EPriorityStandard), iTarget(aTarget)
        {
        CActiveScheduler::Add(this);
        }
    ~CRemConResponder() { Cancel(); }
    void Respond(TRemConCoreApiOperationId aOperation)
        {
        if (IsActive())
            return;
        iTarget.SendResponse(iStatus, aOperation, KErrNone);
        SetActive();
        }

private:
    void RunL() {}
    void DoCancel() {}
    CRemConCoreApiTarget& iTarget;
    };

namespace
    {

    const TInt KVideoPriority = 0;
    const TInt KVideoPreference = 0;
    const TInt KTickUs = 500000;
    const TInt KControlsMs = 3000;
    const TInt KVolumeShowMs = 1500;
    const TInt KScrubSeekMs = 250;
    const TInt KPollUs = 20000;
    const TInt KPollLimitUs = 5000000;
    const TInt KOpenLimitUs = 15000000;
    const TInt KSpinStepMs = Spinner::KPeriodMs / Spinner::KFrames;
    const TInt KSpinnerDesign = 40;
    const TInt KScrubFadeMs = 120;
    const TInt KShiftUpdateMs = 33;
    const TUint32 KLoopOn = 0x0A84FF;
    const TReal KMaxVideoZoom = 4;
    const TInt KMaxRemembered = 32;
    const TInt KMaxKeyframes = 4096;
    const TInt KMaxMoov = 4 * 1024 * 1024;

    const TInt KErrMMAudioDevice = -12013;

    const TInt KErrMMVideoDevice = -12017;

    void FormatTime(TDes& aText, TInt aMs)
        {
        const TInt s = Max(0, aMs) / 1000;
        aText.Zero();
        if (s >= 3600)
            {
            aText.AppendNum(s / 3600);
            aText.Append(':');
            aText.AppendNumFixedWidth(s / 60 % 60, EDecimal, 2);
            }
        else
            aText.AppendNum(s / 60);
        aText.Append(':');
        aText.AppendNumFixedWidth(s % 60, EDecimal, 2);
        }
    }

CVideoPlayer* CVideoPlayer::NewL(MVideoPlayerObserver& aObserver, const CCoeControl& aParent, TInt aScale)
    {
    CVideoPlayer* self = new (ELeave) CVideoPlayer(aObserver, aScale);
    CleanupStack::PushL(self);
    self->ConstructL(aParent);
    CleanupStack::Pop(self);
    return self;
    }

CVideoPlayer::CVideoPlayer(MVideoPlayerObserver& aObserver, TInt aScale)
    : iObserver(aObserver), iScale(aScale), iStage(_S("idle")), iVolume(60), iZoom(1)
    {
    }

void CVideoPlayer::ConstructL(const CCoeControl& aParent)
    {
    CreateWindowL(&aParent);

    Window().SetBackgroundColor(KRgbBlack);
    EnableDragEvents();

    Window().EnableAdvancedPointers();
    SetRect(TRect(0, 0, 16, 16));
    MakeVisible(EFalse);
    ActivateL();
    MakeBitmapsL();
    iTicker = CPeriodic::NewL(CActive::EPriorityStandard);
    iStartPoll = CPeriodic::NewL(CActive::EPriorityStandard);
    iClose = new (ELeave) CAsyncCallBack(TCallBack(Close, this), CActive::EPriorityStandard);
    iFade = CPeriodic::NewL(CActive::EPriorityStandard);

    TFontSpec spec(_L("Nokia Pure Text"), Scale(16));
    spec.iFontStyle.SetBitmapType(EAntiAliasedGlyphBitmap);
    spec.iFontStyle.SetStrokeWeight(EStrokeWeightBold);
    if (ControlEnv()->ScreenDevice()->GetNearestFontToDesignHeightInPixels(iFont, spec) == KErrNone && iFont)
        iOwnFont = ETrue;
    else
        iFont = const_cast<CFont*>(ControlEnv()->NormalFont());
    TFontSpec scrub(_L("Nokia Pure Text T"), Scale(30));
    scrub.iFontStyle.SetBitmapType(EAntiAliasedGlyphBitmap);
    scrub.iFontStyle.SetStrokeWeight(EStrokeWeightBold);
    if (ControlEnv()->ScreenDevice()->GetNearestFontToDesignHeightInPixels(iScrubFont, scrub) == KErrNone && iScrubFont)
        iOwnScrubFont = ETrue;
    else
        iScrubFont = iFont;
    }

CVideoPlayer::~CVideoPlayer()
    {
    Stop();
    delete iUtility;
    delete iTicker;
    delete iStartPoll;
    delete iFade;
    delete iClose;
    iMmfFailed.ResetAndDestroy();
    delete iLoopMask;
    delete iLoopOn;
    delete iLoopOff;
    delete iLoopEdge;
    delete iSpinMask;
    User::Free(iSpinFrames);
    User::Free(iKeys);
    delete iSpinWhite;
    if (iOwnFont)
        ControlEnv()->ScreenDevice()->ReleaseFont(iFont);
    if (iOwnScrubFont)
        ControlEnv()->ScreenDevice()->ReleaseFont(iScrubFont);
    }

TBool CVideoPlayer::SoftFirst(const TDesC& aPath, TPtrC& aWhy) const
    {
    TParsePtrC parse(aPath);
    if (parse.Ext().CompareF(_L(".flv")) == 0 || parse.Ext().CompareF(_L(".mkv")) == 0)
        {
        aWhy.Set(_L("a container the phone's player doesn't read"));
        return ETrue;
        }
    for (TInt i = 0; i < iMmfFailed.Count(); i++)
        if (iMmfFailed[i]->CompareF(aPath) == 0)
            {
            aWhy.Set(_L("the phone's player failed on it before"));
            return ETrue;
            }
    return EFalse;
    }

void CVideoPlayer::PrepareL(const TDesC& aPath, const TRect& aRect, const TDesC& aTitle)
    {
    if (iState != EIdle && aPath.CompareF(iPath) == 0)
        return;

    TPtrC why;
    if (!SoftFirst(aPath, why))
        return;
    BeginL(aPath, aRect, aTitle, EFalse);
    }

void CVideoPlayer::PlayL(const TDesC& aPath, const TRect& aRect, const TDesC& aTitle)
    {
    const TBool same = aPath.CompareF(iPath) == 0;
    if (same && iState == EPaused)
        {
        Resume();
        return;
        }
    iTapUs = PerfNowUs();
    if (same && iPrepareFailed)
        {
        TraceNow(_L("video: prepared ahead, neither path plays %S"), &iPath);
        iPlayWhenReady = ETrue;
        Finish(EFailed, iError);
        return;
        }
    if (same && (iState == EPreparing || iState == EReady))
        {
        iControls = ETrue;
        iControlsMs = NowMs();
        iTicker->Cancel();
        iTicker->Start(KTickUs, KTickUs, TCallBack(Tick, this));
        StartKeys();
        TraceNow(_L("video: play %S, %s ahead"), &iPath, iState == EReady ? _S("prepared") : _S("still preparing"));
        iPlayWhenReady = ETrue;
        Place(aRect);
        BeginWait();
        if (iState == EReady)
            StartPlayback();
        else
            {
            iState = EOpening;
            if (!iSoft)
                {
                MakeVisible(ETrue);
                DrawNow();
                }
            }
        return;
        }
    BeginL(aPath, aRect, aTitle, ETrue);
    }

void CVideoPlayer::BeginL(const TDesC& aPath, const TRect& aRect, const TDesC& aTitle, TBool aPlay)
    {
    const TInt64 tap = iTapUs;
    Stop();
    if (aPlay)
        {
        iTapUs = tap;
        iControls = ETrue;
        iControlsMs = NowMs();
        iTicker->Start(KTickUs, KTickUs, TCallBack(Tick, this));
        StartKeys();
        }
    iPath.Copy(aPath.Left(KMaxFileName));
    iTitle.Copy(aTitle.Left(iTitle.MaxLength()));
    iError = KErrNone;
    iSilent = EFalse;
    iTriedSoft = EFalse;
    iShownRetry = EFalse;
    iPrepareFailed = EFalse;
    iPlayWhenReady = aPlay;
    iBeginUs = PerfNowUs();
    iZoom = 1;
    iPanX = iPanY = 0;
    iShown = EFalse;
    iMidRetries = 0;
    iKnownMs = 0;
    iResumeMs = -1;
    iKeyCount = -2;
    Place(aRect);
    if (aPlay)
        BeginWait();
    TPtrC why;
    if (SoftFirst(iPath, why))
        {
        TraceNow(_L("video: path n8media for %S (%S)%s"), &iPath, &why, aPlay ? _S("") : _S(", ahead"));
        StartSoft();
        return;
        }
    TraceNow(_L("video: path MMF for %S%s"), &iPath, aPlay ? _S("") : _S(", ahead"));
    iState = aPlay ? EOpening : EPreparing;
    if (aPlay)
        {
        MakeVisible(ETrue);
        DrawNow();
        }
    TRAPD(err, OpenL());
    if (err != KErrNone)
        Fail(err);
    }

void CVideoPlayer::OpenL()
    {
    const TRect screen(PositionRelativeToScreen(), Size());
    if (!iUtility)
        {
        const TInt64 startUs = PerfNowUs();
        iUtility = CVideoPlayerUtility2::NewL(*this, KVideoPriority, KVideoPreference);
        TraceNow(_L("video: MMF surface player made in %d ms"), (TInt)((PerfNowUs() - startUs) / 1000));
        }
    iStage = _S("open");
    TraceNow(_L("video: open %S, window %dx%d at %d,%d on screen, silent %d"), &iPath, screen.Width(), screen.Height(),
             screen.iTl.iX, screen.iTl.iY, iSilent);
    iFileOpen = ETrue;
    iUtility->OpenFileL(iPath);
    }

void CVideoPlayer::StartSoft()
    {
    iTriedSoft = ETrue;
    iWaitUs = PerfNowUs();
    if (IsVisible())
        MakeVisible(EFalse);
    if (iGl)
        iGl->PrepareVideo();
    iStage = _S("n8media open");
    TRAPD(err, iSoft = CSoftVideo::NewL(*this, iPath, iVolume));
    if (err != KErrNone)
        {
        iSoft = NULL;
        TraceNow(_L("video: n8media couldn't start: %d"), err);
        Finish(EFailed, err);
        return;
        }
    if (!iPlayWhenReady)
        iSoft->Pause();
    iState = iPlayWhenReady ? EOpening : EPreparing;
    }

void CVideoPlayer::StartPlayback()
    {
    iState = EPlaying;
    iStage = _S("play");
    if (iSoft)
        iSoft->Play();
    else
        {
        MakeVisible(ETrue);
        DrawNow();
        iUtility->Play();
        iWaitUs = PerfNowUs();
        if (iTapUs || iWaiting)
            {
            iStartPoll->Cancel();
            iStartPoll->Start(KPollUs, KPollUs, TCallBack(PollStart, this));
            }
        }
    ShowControls(ETrue);
    iObserver.VideoChanged();
    }

void CVideoPlayer::FirstFrame(const TDesC& aPath)
    {
    if (!iTapUs)
        return;
    TraceNow(_L("video: tap to first frame %d ms (%S)"), (TInt)((PerfNowUs() - iTapUs) / 1000), &aPath);
    iTapUs = 0;
    }

TInt CVideoPlayer::PollStart(TAny* aSelf)
    {
    CVideoPlayer* self = static_cast<CVideoPlayer*>(aSelf);
    const TBool spinning = self->iWaiting && !self->iSoft && (self->iState == EOpening || self->iState == EPlaying);
    const TBool timing = self->iTapUs && self->iState == EPlaying && self->iUtility;
    if (!spinning && !timing)
        {
        self->iStartPoll->Cancel();
        return EFalse;
        }
    if (self->iState == EOpening && !self->iSoft && PerfNowUs() - self->iWaitUs > KOpenLimitUs)
        {
        TraceNow(_L("video: MMF still at %s after %d s"), self->iStage, KOpenLimitUs / 1000000);
        self->iStartPoll->Cancel();
        self->Fail(KErrTimedOut);
        return EFalse;
        }
    if (self->iState == EPlaying && self->iUtility)
        {
        TInt64 us = 0;
        TRAP_IGNORE(us = self->iUtility->PositionL().Int64());
        const TBool late = PerfNowUs() - self->iWaitUs > KPollLimitUs;
        if (us > 0 || late)
            {
            if (us > 0)
                {
                self->FirstFrame(_L("MMF, position moving"));
                self->iShown = ETrue;
                }
            else
                TraceNow(_L("video: MMF position still 0 after 5 s"));
            self->iTapUs = 0;
            self->EndWait();
            self->iStartPoll->Cancel();
            return EFalse;
            }
        }
    if (spinning && NowMs() - self->iSpinMs >= (TUint32)KSpinStepMs)
        {
        self->iSpinMs = NowMs();
        self->DrawNow();
        }
    return ETrue;
    }

void CVideoPlayer::BeginWait()
    {
    if (!iSpinFrames && iSpinSide > 0)
        {

        const TInt64 startUs = PerfNowUs();
        TUint8* shape = static_cast<TUint8*>(User::Alloc(iSpinSide * iSpinSide * 2));
        iSpinFrames = static_cast<TUint8*>(User::Alloc(iSpinSide * iSpinSide * Spinner::KFrames));
        if (shape && iSpinFrames)
            Spinner::Bake(iSpinSide, shape, iSpinFrames);
        else
            {
            User::Free(iSpinFrames);
            iSpinFrames = NULL;
            }
        User::Free(shape);
        TraceNow(_L("video: spinner baked, %d frames of %d px in %d ms"), Spinner::KFrames, iSpinSide,
                 (TInt)((PerfNowUs() - startUs) / 1000));
        }
    iWaiting = ETrue;
    iWaitUs = PerfNowUs();
    if (!iStartPoll->IsActive())
        iStartPoll->Start(KPollUs, KPollUs, TCallBack(PollStart, this));
    }

void CVideoPlayer::EndWait()
    {
    if (!iWaiting)
        return;
    iWaiting = EFalse;
    if (IsVisible())
        DrawNow();
    }

const TUint8* CVideoPlayer::SpinnerFrame(TInt& aSide) const
    {
    aSide = iSpinSide;
    return iSpinFrames ? iSpinFrames + Spinner::FrameAt(NowMs()) * iSpinSide * iSpinSide : NULL;
    }

TBool CVideoPlayer::Waiting() const
    {
    if (Soft())
        return !SoftHasPicture() && iState != EPaused;
    return iWaiting && (iState == EOpening || iState == EPlaying);
    }

void CVideoPlayer::Pause()
    {
    if (iState != EPlaying)
        return;
    if (iSoft)
        iSoft->Pause();
    else
        {
        TRAPD(err, iUtility->PauseL());
        if (err != KErrNone)
            return;
        }
    iState = EPaused;
    ShowControls(ETrue);
    iObserver.VideoChanged();
    }

void CVideoPlayer::Resume()
    {
    if (iState != EPaused)
        return;
    if (iSoft)
        iSoft->Play();
    else
        iUtility->Play();
    iState = EPlaying;
    if (iWaiting)
        BeginWait();
    ShowControls(ETrue);
    Redraw();
    iObserver.VideoChanged();
    }

void CVideoPlayer::Stop()
    {
    iTicker->Cancel();
    iStartPoll->Cancel();
    iClose->Cancel();
    iRetrying = EFalse;
    iFreshPlayer = EFalse;
    iSwitchToSoft = EFalse;
    iScrubbing = EFalse;
    iTapUs = 0;
    iWaiting = EFalse;
    iZoom = 1;
    iPanX = iPanY = 0;
    CloseFile();
    if (iSoft)
        {
        iSoft->ReleaseGl(iGl);
        delete iSoft;
        iSoft = NULL;
        }
    iSoftDoomed = EFalse;
    StopKeys();
    if (IsVisible())
        MakeVisible(EFalse);
    iState = EIdle;
    iPrepareFailed = EFalse;
    }

void CVideoPlayer::Relayout(const TRect& aRect)
    {
    if (iState == EIdle)
        return;
    Place(aRect);
    const TRect screen(PositionRelativeToScreen(), Size());
    TInt err = KErrNone;
    if (iUtility && iDisplayAdded)
        {
        CVideoPlayerUtility2& player = *static_cast<CVideoPlayerUtility2*>(iUtility);
        const TRect area(Size());
        TRAP(err, player.SetVideoExtentL(Window(), VideoExtent()); player.SetWindowClipRectL(Window(), area);
                  player.SetAutoScaleL(Window(), EAutoScaleBestFit));
        }
    TraceNow(_L("video: turned to %dx%d, display err %d, n8media %d"), screen.Width(), screen.Height(), err,
             iSoft != NULL);
    if (IsVisible())
        DrawNow();
    }

void CVideoPlayer::SetTopInset(TInt aTopInset)
    {
    if (aTopInset == iTopInset)
        return;
    iTopInset = aTopInset;
    if (IsVisible())
        DrawNow();
    }

void CVideoPlayer::Finish(TState aState, TInt aError)
    {
    iTicker->Cancel();
    iStartPoll->Cancel();
    iWaiting = EFalse;
    StopKeys();
    if (IsVisible())
        MakeVisible(EFalse);
    iState = aState;
    iError = aError;
    iSoftDoomed = iSoft != NULL;
    iClose->CallBack();
    if (aState == EFailed && !iPlayWhenReady)
        {

        iPrepareFailed = ETrue;
        TraceNow(_L("video: preparing %S ahead failed, %d"), &iPath, aError);
        return;
        }
    iObserver.VideoChanged();
    }

void CVideoPlayer::CloseFile()
    {
    if (iUtility && iDisplayAdded)
        static_cast<CVideoPlayerUtility2*>(iUtility)->RemoveDisplayWindow(Window());
    iDisplayAdded = EFalse;
    if (iUtility && iFileOpen)
        iUtility->Close();
    iFileOpen = EFalse;
    }

void CVideoPlayer::ShowOnWindowL()
    {
    if (iDisplayAdded)
        return;
    CVideoPlayerUtility2& player = *static_cast<CVideoPlayerUtility2*>(iUtility);
    const TRect area(Size());
    player.AddDisplayWindowL(ControlEnv()->WsSession(), *ControlEnv()->ScreenDevice(), Window(), area, area);
    iDisplayAdded = ETrue;
    player.SetAutoScaleL(Window(), EAutoScaleBestFit);
    }

TInt CVideoPlayer::Close(TAny* aSelf)
    {
    CVideoPlayer* self = static_cast<CVideoPlayer*>(aSelf);
    self->CloseFile();
    if (self->iSoftDoomed)
        {
        if (self->iSoft)
            self->iSoft->ReleaseGl(self->iGl);
        delete self->iSoft;
        self->iSoft = NULL;
        self->iSoftDoomed = EFalse;
        }
    if (self->iRetrying)
        {
        self->iRetrying = EFalse;
        if (self->iFreshPlayer)
            {
            delete self->iUtility;
            self->iUtility = NULL;
            self->iFreshPlayer = EFalse;
            }
        TRAPD(err, self->OpenL());
        if (err != KErrNone)
            self->Finish(EFailed, err);
        }
    else if (self->iSwitchToSoft)
        {

        delete self->iUtility;
        self->iUtility = NULL;
        self->iSwitchToSoft = EFalse;
        self->StartSoft();
        }
    return EFalse;
    }

void CVideoPlayer::Fail(TInt aError)
    {
    TraceNow(_L("video: %S failed at %s: %d"), &iPath, iStage, aError);

    if (!iSoft && iShown && iPlayWhenReady && iMidRetries < 1)
        {
        iMidRetries++;
        iResumeMs = KeyframeAtOrBefore(iKnownMs);
        TraceNow(_L("video: MMF failed mid-play: open again, on at %d ms (try %d)"), iResumeMs, iMidRetries);
        iState = EOpening;
        BeginWait();
        iRetrying = ETrue;
        iFreshPlayer = ETrue;
        iClose->CallBack();
        return;
        }
    if (iShown)
        iResumeMs = KeyframeAtOrBefore(iKnownMs);

    if (!iSilent && (aError == KErrMMAudioDevice || aError == KErrAccessDenied || aError == KErrInUse))
        {
        iSilent = ETrue;
        iRetrying = ETrue;
        iClose->CallBack();
        return;
        }

    if (aError == KErrMMVideoDevice && !iShownRetry)
        {
        iShownRetry = ETrue;
        if (!IsVisible())
            {
            MakeVisible(ETrue);
            DrawNow();
            }
        TraceNow(_L("video: again with the window shown"));
        iRetrying = ETrue;
        iClose->CallBack();
        return;
        }
    if (!iTriedSoft)
        {
        TraceNow(_L("video: path n8media for %S (the phone's player failed: %d)"), &iPath, aError);
        if (iMmfFailed.Count() < KMaxRemembered)
            {
            HBufC* path = iPath.Alloc();
            if (path && iMmfFailed.Append(path) != KErrNone)
                delete path;
            }
        iSwitchToSoft = ETrue;
        iClose->CallBack();
        return;
        }
    Finish(EFailed, aError);
    }

TInt CVideoPlayer::PositionMs() const
    {
    if (iScrubbing)
        return iScrubMs;
    if (iSoft)
        return iSoft->PositionMs();
    TInt64 us = 0;
    if (iUtility && (iState == EPlaying || iState == EPaused))
        TRAP_IGNORE(us = iUtility->PositionL().Int64());
    return (TInt)(us / 1000);
    }

TInt CVideoPlayer::DurationMs() const
    {
    if (iSoft)
        return iSoft->DurationMs();
    TInt64 us = 0;
    if (iUtility && (iState == EPlaying || iState == EPaused))
        TRAP_IGNORE(us = iUtility->DurationL().Int64());
    return (TInt)(us / 1000);
    }

void CVideoPlayer::ProgressLine(TInt& aY, TInt& aX0, TInt& aX1) const
    {
    aY = Size().iHeight - Scale(70);
    aX0 = Scale(16);
    aX1 = Size().iWidth - Scale(16);
    }

TBool CVideoPlayer::OnProgress(const TPoint& aPos) const
    {
    if (!iControls || (iState != EPlaying && iState != EPaused) || DurationMs() <= 0)
        return EFalse;
    const TPoint p = aPos - Position();
    TInt y, x0, x1;
    ProgressLine(y, x0, x1);

    return p.iY >= y - Scale(40) && p.iY <= y + Scale(14) && p.iX >= x0 - Scale(12) && p.iX <= x1 + Scale(12);
    }

void CVideoPlayer::BeginScrub(const TPoint& aPos)
    {
    iScrubChangeMs = NowMs();
    StartFade();
    iScrubResume = iState == EPlaying;
    Pause();
    iScrubbing = ETrue;
    iScrubSeekMs = 0;
    ScrubTo(aPos);
    }

void CVideoPlayer::ScrubTo(const TPoint& aPos)
    {
    if (!iScrubbing)
        return;
    TInt y, x0, x1;
    ProgressLine(y, x0, x1);
    const TInt x = Max(x0, Min(x1, aPos.iX - Position().iX));
    iScrubMs = (TInt)((TInt64)DurationMs() * (x - x0) / Max(1, x1 - x0));
    iControlsMs = NowMs();

    if (iSoft && NowMs() - iScrubSeekMs >= (TUint32)KScrubSeekMs)
        {
        iScrubSeekMs = NowMs();
        SeekNow(iScrubMs);
        }
    Redraw();
    }

void CVideoPlayer::EndScrub()
    {
    if (!iScrubbing)
        return;
    SeekNow(iScrubMs);
    TraceNow(_L("video: scrubbed to %d ms"), iScrubMs);
    iScrubbing = EFalse;
    iScrubChangeMs = NowMs();
    StartFade();
    if (iScrubResume)
        Resume();
    Redraw();
    }

void CVideoPlayer::SeekNow(TInt aMs)
    {

    aMs = KeyframeAtOrBefore(aMs);
    if (iSoft)
        iSoft->SeekTo(aMs);
    else if (iUtility && iFileOpen)
        {
        iKnownMs = aMs;
        TraceNow(_L("video: MMF seek to %d ms"), aMs);
        TRAPD(err, iUtility->SetPositionL(TTimeIntervalMicroSeconds((TInt64)aMs * 1000)));
        if (err != KErrNone)
            TraceNow(_L("video: MMF seek failed: %d"), err);
        }
    }

TInt CVideoPlayer::KeyframeAtOrBefore(TInt aMs)
    {
    if (iKeyCount == -2)
        ReadKeyframes();
    if (iKeyCount <= 0 || aMs <= 0)
        return Max(0, aMs);
    TInt lo = 0, hi = iKeyCount - 1;
    if (iKeys[0] > aMs)
        return 0;
    while (lo < hi)
        {
        const TInt mid = (lo + hi + 1) / 2;
        if (iKeys[mid] <= aMs)
            lo = mid;
        else
            hi = mid - 1;
        }
    return iKeys[lo];
    }

void CVideoPlayer::ReadKeyframes()
    {
    iKeyCount = -1;
    RFile file;
    if (file.Open(ControlEnv()->FsSession(), iPath, EFileRead | EFileShareReadersOrWriters) != KErrNone)
        return;
    TInt fileSize = 0;
    file.Size(fileSize);
    TInt64 at = 0;
    TBuf8<16> head;
    while (at + 8 <= fileSize)
        {
        if (file.Read((TInt)at, head, 16) != KErrNone || head.Length() < 8)
            break;
        TUint32 type;
        TInt64 size;
        TInt headerBytes;
        if (!Mp4BoxHeader(head.Ptr(), head.Length(), type, size, headerBytes))
            break;
        if (size == 0)
            size = fileSize - at;
        if (type == KMp4Moov)
            {
            const TInt body = (TInt)(size - headerBytes);
            if (body <= 0 || body > KMaxMoov)
                break;
            HBufC8* moov = HBufC8::New(body);
            if (!moov)
                break;
            TPtr8 data = moov->Des();
            if (file.Read((TInt)at + headerBytes, data, body) == KErrNone)
                {
                if (!iKeys)
                    iKeys = static_cast<TInt*>(User::Alloc(KMaxKeyframes * sizeof(TInt)));
                if (iKeys)
                    iKeyCount = Mp4Keyframes(data.Ptr(), data.Length(), iKeys, KMaxKeyframes);
                }
            delete moov;
            break;
            }
        at += size;
        }
    file.Close();
    TraceNow(_L("video: keyframes of %S: %d (%d ms to the second)"), &iPath, iKeyCount,
             iKeyCount > 1 ? iKeys[1] - iKeys[0] : 0);
    }

const TDesC& CVideoPlayer::Title() const
    {
    if (iVolumeMs && NowMs() - iVolumeMs < (TUint32)KVolumeShowMs)
        return iVolumeText;
    return iTitle;
    }

void CVideoPlayer::Redraw()
    {
    if (iSoft)
        iObserver.VideoFrameReady();
    else if (IsVisible())
        DrawNow();
    }

void CVideoPlayer::ShowControls(TBool aShow)
    {
    iControlsMs = NowMs();
    if (aShow == iControls)
        return;
    iControls = aShow;
    Redraw();
    iObserver.VideoControlsChanged();
    }

TInt CVideoPlayer::Tick(TAny* aSelf)
    {
    CVideoPlayer* self = static_cast<CVideoPlayer*>(aSelf);
    if (self->iState == EPlaying && !self->iSoft && !self->iScrubbing)
        {
        const TInt ms = self->PositionMs();
        if (ms > 0)
            {
            self->iKnownMs = ms;
            self->iShown = ETrue;
            }
        }
    if (self->iControls)
        {
        if (self->iState == EPlaying && !self->iScrubbing && NowMs() - self->iControlsMs > (TUint32)KControlsMs)
            self->ShowControls(EFalse);
        else
            self->Redraw();
        }
    return ETrue;
    }

TRect CVideoPlayer::PlayPauseRect() const
    {
    const TPoint c(Size().iWidth / 2, Size().iHeight - Scale(30));
    return TRect(c.iX - Scale(32), c.iY - Scale(26), c.iX + Scale(32), c.iY + Scale(26));
    }

TRect CVideoPlayer::LoopRect() const
    {

    const TPoint c(Size().iWidth - Scale(28), iTopInset + Scale(28));
    return TRect(c.iX - Scale(28), c.iY - Scale(26), c.iX + Scale(28), c.iY + Scale(26));
    }

void CVideoPlayer::SetZoom(TReal aZoom, TReal aPanX, TReal aPanY)
    {
    const TReal zoom = aZoom < 1 ? 1 : (aZoom > KMaxVideoZoom ? KMaxVideoZoom : aZoom);
    const TReal maxX = Size().iWidth * (zoom - 1) / 2, maxY = Size().iHeight * (zoom - 1) / 2;
    iZoom = zoom;
    iPanX = aPanX < -maxX ? -maxX : (aPanX > maxX ? maxX : aPanX);
    iPanY = aPanY < -maxY ? -maxY : (aPanY > maxY ? maxY : aPanY);
    ApplyZoom();
    }

void CVideoPlayer::ApplyZoom()
    {
    if (iSoft)
        {
        iObserver.VideoFrameReady();
        return;
        }
    if (!iUtility || !iDisplayAdded)
        return;
    CVideoPlayerUtility2& player = *static_cast<CVideoPlayerUtility2*>(iUtility);
    TRAP_IGNORE(player.SetVideoExtentL(Window(), VideoExtent()); player.SetWindowClipRectL(Window(), TRect(Size())));
    }

TReal CVideoPlayer::FillZoom() const
    {
    const TSize video = iSoft ? iSoft->ShownSize() : iMmfSize;
    if (video.iWidth <= 0 || video.iHeight <= 0 || Size().iWidth <= 0 || Size().iHeight <= 0)
        return 1;
    const TReal a = (TReal)video.iWidth / video.iHeight, v = (TReal)Size().iWidth / Size().iHeight;
    return a > v ? a / v : v / a;
    }

TRect CVideoPlayer::VideoExtent() const
    {
    const TReal w = iBaseSize.iWidth * iZoom, h = iBaseSize.iHeight * iZoom;
    const TReal x = iBaseSize.iWidth / 2.0 + iPanX - w / 2, y = iBaseSize.iHeight / 2.0 + iPanY - h / 2;
    TRect extent(TPoint((TInt)x, (TInt)y), TSize((TInt)(w + 0.5), (TInt)(h + 0.5)));
    extent.Move(Min(iShift, 0), 0);
    return extent;
    }

CVideoPlayer::TTap CVideoPlayer::Tap(const TPoint& aPos)
    {
    const TPoint p = aPos - Position();
    if (!iControls)
        {
        ShowControls(ETrue);
        return ETapControls;
        }
    if (p.iX < Scale(80) && p.iY < iTopInset + Scale(56))
        return ETapBack;
    if (LoopRect().Contains(p))
        {
        iLoop = !iLoop;
        TraceNow(_L("video: loop %s"), iLoop ? _S("on") : _S("off"));
        ShowControls(ETrue);
        Redraw();
        return ETapLoop;
        }
    if (PlayPauseRect().Contains(p))
        {
        if (iState == EPlaying)
            Pause();
        else
            Resume();
        return ETapPlayPause;
        }
    ShowControls(EFalse);
    return ETapControls;
    }

void CVideoPlayer::HandlePointerEventL(const TPointerEvent& aEvent)
    {
    const TAdvancedPointerEvent* advanced = aEvent.AdvancedPointerEvent();
    const TInt pointer = advanced ? advanced->PointerNumber() : 0;
    if (pointer > 1)
        return;
    TPointerEvent event = aEvent;
    event.iPosition += Position();
    iObserver.VideoPointerL(event, pointer);
    }

void CVideoPlayer::DrawShadowedText(CWindowGc& aGc, const TDesC& aText, const TPoint& aAt) const
    {
    aGc.SetPenColor(KRgbBlack);
    aGc.DrawText(aText, aAt + TPoint(1, 1));
    aGc.SetPenColor(KRgbWhite);
    aGc.DrawText(aText, aAt);
    }

void CVideoPlayer::Draw(const TRect&  ) const
    {
    CWindowGc& gc = SystemGc();
    if (Waiting())
        {
        gc.SetPenStyle(CGraphicsContext::ENullPen);
        gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
        gc.SetBrushColor(KRgbBlack);
        gc.DrawRect(TRect(Size()));
        gc.SetPenStyle(CGraphicsContext::ESolidPen);
        DrawSpinner(gc);
        }
    if (!iControls)
        return;
    const TInt w = Size().iWidth;
    gc.SetBrushStyle(CGraphicsContext::ENullBrush);
    gc.UseFont(iFont);

    gc.SetPenSize(TSize(Scale(3), Scale(3)));
    const TInt top = iTopInset;
    const TPoint back[3] = { TPoint(Scale(24), top + Scale(18)), TPoint(Scale(14), top + Scale(28)),
                             TPoint(Scale(24), top + Scale(38)) };
    for (TInt pass = 0; pass < 2; pass++)
        {
        gc.SetPenColor(pass ? KRgbWhite : KRgbBlack);
        const TPoint shift(pass ? 0 : 1, pass ? 0 : 1);
        gc.DrawLine(back[0] + shift, back[1] + shift);
        gc.DrawLine(back[1] + shift, back[2] + shift);
        }
    gc.SetPenSize(TSize(1, 1));
    const TDesC& title = Title();
    DrawShadowedText(gc, title, TPoint((w - iFont->TextWidthInPixels(title)) / 2, top + Scale(34)));

    const TInt position = PositionMs(), duration = DurationMs();
    TInt lineY, x0, x1;
    ProgressLine(lineY, x0, x1);
    gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    gc.SetPenStyle(CGraphicsContext::ENullPen);
    gc.SetBrushColor(TRgb(0x8E, 0x8E, 0x93));
    gc.DrawRect(TRect(x0, lineY, x1, lineY + Scale(3)));
    const TInt done = duration > 0 ? x0 + (TInt)((TInt64)(x1 - x0) * Min(position, duration) / duration) : x0;
    gc.SetBrushColor(KRgbWhite);
    gc.DrawRect(TRect(x0, lineY, done, lineY + Scale(3)));
    const TInt knob = Scale(iScrubbing ? 9 : 6);
    gc.DrawEllipse(TRect(done - knob, lineY + 1 - knob, done + knob, lineY + 1 + knob));
    const TPoint c = PlayPauseRect().Center();
    if (iState == EPlaying || iState == EOpening)
        {
        gc.DrawRect(TRect(c.iX - Scale(9), c.iY - Scale(11), c.iX - Scale(3), c.iY + Scale(11)));
        gc.DrawRect(TRect(c.iX + Scale(3), c.iY - Scale(11), c.iX + Scale(9), c.iY + Scale(11)));
        }
    else
        {
        const TPoint play[3] = { TPoint(c.iX - Scale(8), c.iY - Scale(12)), TPoint(c.iX - Scale(8), c.iY + Scale(12)),
                                 TPoint(c.iX + Scale(12), c.iY) };
        gc.DrawPolygon(play, 3);
        }
    DrawLoop(gc);
    if (ScrubAlpha() > 0)
        DrawScrubTime(gc);
    gc.SetPenStyle(CGraphicsContext::ESolidPen);
    gc.SetBrushStyle(CGraphicsContext::ENullBrush);
    TBuf<16> text;
    FormatTime(text, position);
    DrawShadowedText(gc, text, TPoint(x0, lineY + Scale(24)));
    FormatTime(text, duration);
    DrawShadowedText(gc, text, TPoint(x1 - iFont->TextWidthInPixels(text), lineY + Scale(24)));
    gc.DiscardFont();
    }

void CVideoPlayer::DrawSpinner(CWindowGc& aGc) const
    {
    TInt side = 0;
    const TUint8* frame = SpinnerFrame(side);
    if (!iSpinMask || !iSpinWhite || !frame)
        return;
    const TSize size = iSpinMask->SizeInPixels();
    iSpinMask->BeginDataAccess();
    TUint8* mask = reinterpret_cast<TUint8*>(iSpinMask->DataAddress());
    const TInt stride = CFbsBitmap::ScanLineLength(size.iWidth, EGray256);
    for (TInt y = 0; y < side; y++)
        Mem::Copy(mask + y * stride, frame + y * side, side);
    iSpinMask->EndDataAccess();
    const TPoint at((Size().iWidth - size.iWidth) / 2, (Size().iHeight - size.iHeight) / 2);
    aGc.BitBltMasked(at, iSpinWhite, TRect(size), iSpinMask, EFalse);
    }

TReal CVideoPlayer::ScrubAlpha() const
    {
    if (!iScrubChangeMs)
        return 0;
    const TReal t = Min(1.0, (NowMs() - iScrubChangeMs) / (TReal)KScrubFadeMs);
    return iScrubbing ? t : 1 - t;
    }

void CVideoPlayer::StartFade()
    {
    if (iSoft)
        {
        iObserver.VideoFrameReady();
        return;
        }
    iFade->Cancel();
    iFade->Start(16000, 16000, TCallBack(Fade, this));
    }

TInt CVideoPlayer::Fade(TAny* aSelf)
    {
    CVideoPlayer* self = static_cast<CVideoPlayer*>(aSelf);
    if (self->IsVisible())
        self->DrawNow();
    if (NowMs() - self->iScrubChangeMs > (TUint32)KScrubFadeMs)
        {
        self->iFade->Cancel();
        return EFalse;
        }
    return ETrue;
    }

void CVideoPlayer::DrawScrubTime(CWindowGc& aGc) const
    {
    TBuf<16> at, length;
    FormatTime(at, iScrubMs);
    FormatTime(length, DurationMs());
    TBuf<40> text;
    text.Format(_L("%S / %S"), &at, &length);
    const TInt width = iScrubFont->TextWidthInPixels(text), ascent = iScrubFont->AscentInPixels();
    const TPoint c(Size().iWidth / 2, Size().iHeight / 2);
    const TInt padX = Scale(18), padY = Scale(12);
    const TRect pill(c.iX - width / 2 - padX, c.iY - ascent / 2 - padY, c.iX + width / 2 + padX, c.iY + ascent / 2 + padY);
    aGc.SetPenStyle(CGraphicsContext::ENullPen);
    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);

    const TInt alpha = (TInt)(ScrubAlpha() * 255);
    aGc.SetBrushColor(TRgb(0x1C, 0x1C, 0x1E, alpha * 9 / 10));
    aGc.DrawRoundRect(pill, TSize(pill.Height() / 2, pill.Height() / 2));
    aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
    aGc.SetPenStyle(CGraphicsContext::ESolidPen);
    aGc.SetPenColor(TRgb(0xFF, 0xFF, 0xFF, alpha));
    aGc.UseFont(iScrubFont);
    aGc.DrawText(text, TPoint(c.iX - width / 2, c.iY + ascent / 2));
    aGc.UseFont(iFont);
    }

void CVideoPlayer::DrawLoop(CWindowGc& aGc) const
    {
    if (!iLoopMask)
        return;
    const TSize size = iLoopMask->SizeInPixels();
    const TPoint c = LoopRect().Center();
    const TPoint at(c.iX - size.iWidth / 2, c.iY - size.iHeight / 2);
    aGc.BitBltMasked(at + TPoint(1, 1), iLoopEdge, TRect(size), iLoopMask, EFalse);
    aGc.BitBltMasked(at, iLoop ? iLoopOn : iLoopOff, TRect(size), iLoopMask, EFalse);
    }

namespace
    {
    CFbsBitmap* SolidL(const TSize& aSize, TUint32 aRgb)
        {
        CFbsBitmap* bitmap = new (ELeave) CFbsBitmap;
        CleanupStack::PushL(bitmap);
        User::LeaveIfError(bitmap->Create(aSize, EColor16MU));
        bitmap->BeginDataAccess();
        TUint32* pixels = bitmap->DataAddress();
        const TInt words = CFbsBitmap::ScanLineLength(aSize.iWidth, EColor16MU) / 4 * aSize.iHeight;
        for (TInt i = 0; i < words; i++)
            pixels[i] = 0xFF000000 | aRgb;
        bitmap->EndDataAccess();
        CleanupStack::Pop(bitmap);
        return bitmap;
        }
    }

void CVideoPlayer::MakeBitmapsL()
    {
    const TSize size = IconShapes::PixelSize(EIconLoop, iScale);
    const TInt stride = (size.iWidth * 2 + 3) & ~3;
    const TUint8* pixels = NULL;
    TUint8* rendered = NULL;
    if (iScale == KBakedScale && KBakedSizes[EIconLoop][0] == size.iWidth && KBakedSizes[EIconLoop][1] == size.iHeight)
        pixels = KBakedPixels + KBakedOffsets[EIconLoop];
    else
        {
        rendered = static_cast<TUint8*>(User::AllocLC(stride * size.iHeight));
        IconShapes::Render(EIconLoop, iScale, rendered, stride);
        pixels = rendered;
        }
    iLoopMask = new (ELeave) CFbsBitmap;
    User::LeaveIfError(iLoopMask->Create(size, EGray256));
    iLoopMask->BeginDataAccess();
    TUint8* mask = reinterpret_cast<TUint8*>(iLoopMask->DataAddress());
    const TInt maskStride = CFbsBitmap::ScanLineLength(size.iWidth, EGray256);
    for (TInt y = 0; y < size.iHeight; y++)
        for (TInt x = 0; x < size.iWidth; x++)
            mask[y * maskStride + x] = pixels[y * stride + 2 * x + 1];
    iLoopMask->EndDataAccess();
    if (rendered)
        CleanupStack::PopAndDestroy(rendered);
    iLoopOn = SolidL(size, KLoopOn);
    iLoopOff = SolidL(size, 0xFFFFFF);
    iLoopEdge = SolidL(size, 0x000000);
    const TInt side = Max(16, Scale(KSpinnerDesign) & ~3);
    iSpinMask = new (ELeave) CFbsBitmap;
    User::LeaveIfError(iSpinMask->Create(TSize(side, side), EGray256));
    iSpinSide = side;
    iSpinWhite = SolidL(TSize(side, side), 0xFFFFFF);
    }

void CVideoPlayer::StartKeys()
    {
    if (iSelector)
        return;
    TRAPD(err,
        {
        iSelector = CRemConInterfaceSelector::NewL();
        iTarget = CRemConCoreApiTarget::NewL(*iSelector, *this);
        iSelector->OpenTargetL();
        iResponder = new (ELeave) CRemConResponder(*iTarget);
        });
    if (err != KErrNone)
        {
        TraceNow(_L("video: no volume keys, err %d"), err);
        StopKeys();
        }
    }

void CVideoPlayer::StopKeys()
    {
    delete iResponder;
    iResponder = NULL;
    delete iSelector;
    iSelector = NULL;
    iTarget = NULL;
    }

void CVideoPlayer::ChangeVolume(TInt aSteps)
    {
    iVolume = Max(0, Min(100, iVolume + aSteps * 10));
    if (iSoft)
        iSoft->SetVolume(iVolume);
    else if (iUtility && iFileOpen && !iSilent)
        TRAP_IGNORE(iUtility->SetVolumeL(iUtility->MaxVolume() * iVolume / 100));
    iVolumeText.Format(_L("Volume %d%%"), iVolume);
    iVolumeMs = NowMs();
    ShowControls(ETrue);
    Redraw();
    }

void CVideoPlayer::MrccatoCommand(TRemConCoreApiOperationId aOperationId, TRemConCoreApiButtonAction aButtonAct)
    {
    if (aButtonAct != ERemConCoreApiButtonRelease)
        {
        switch (aOperationId)
            {
            case ERemConCoreApiVolumeUp:
                ChangeVolume(1);
                break;
            case ERemConCoreApiVolumeDown:
                ChangeVolume(-1);
                break;
            case ERemConCoreApiPausePlayFunction:
                if (iState == EPlaying)
                    Pause();
                else
                    Resume();
                break;
            case ERemConCoreApiPause:
            case ERemConCoreApiStop:
                Pause();
                break;
            default:
                break;
            }
        }
    if (iResponder)
        iResponder->Respond(aOperationId);
    }

void CVideoPlayer::MrccatoPlay(TRemConCoreApiPlaybackSpeed  , TRemConCoreApiButtonAction aButtonAct)
    {
    if (aButtonAct != ERemConCoreApiButtonRelease)
        Resume();
    if (iResponder)
        iResponder->Respond(ERemConCoreApiPlay);
    }

TBool CVideoPlayer::Soft() const
    {
    return iSoft && !iSoftDoomed && (iState == EOpening || iState == EPlaying || iState == EPaused);
    }

void CVideoPlayer::SetGl(CGlRenderer* aGl)
    {
    if (!aGl && iSoft)
        iSoft->ReleaseGl(NULL);
    iGl = aGl;
    }

TBool CVideoPlayer::UpdateSoft()
    {
    if (!Soft() || !iGl)
        return EFalse;

    if (Waiting() && PerfNowUs() - iWaitUs > KOpenLimitUs)
        {
        TraceNow(_L("video: n8media showed no picture in %d s"), KOpenLimitUs / 1000000);
        Finish(EFailed, KErrTimedOut);
        return EFalse;
        }
    const TBool changed = iSoft->Update(*iGl);
    if (changed && iState == EPlaying)
        FirstFrame(_L("n8media"));
    return changed;
    }

void CVideoPlayer::DrawSoft(TInt aWidth, TInt aHeight, TReal aAlpha, TReal aShiftX)
    {
    if (Soft() && iGl)
        iSoft->Draw(*iGl, aWidth, aHeight, aAlpha, iZoom, iPanX + aShiftX, iPanY);
    }

void CVideoPlayer::Place(const TRect& aRect)
    {
    SetRect(aRect);
    iBaseTl = aRect.iTl;
    iBaseSize = aRect.Size();
    iShift = 0;
    }

void CVideoPlayer::SetShift(TInt aDx)
    {
    if (aDx == iShift)
        return;
    const TBool first = iShift == 0;
    iShift = aDx;
    if (iSoft || !IsVisible())
        return;
    if (first)
        iShiftCalls = iShiftUpdates = iShiftUs = iShiftMaxUs = 0;

    const TInt64 startUs = PerfNowUs();
    const TInt left = iBaseTl.iX + Max(aDx, 0);
    const TInt width = Max(1, iBaseSize.iWidth - Abs(aDx));
    SetExtent(TPoint(left, iBaseTl.iY), TSize(width, iBaseSize.iHeight));

    if (aDx == 0 || first || NowMs() - iShiftUpdateMs >= (TUint32)KShiftUpdateMs)
        {
        iShiftUpdateMs = NowMs();
        ApplyZoom();
        iShiftUpdates++;
        }
    ControlEnv()->WsSession().Flush();
    const TInt us = (TInt)(PerfNowUs() - startUs);
    iShiftCalls++;
    iShiftUs += us;
    iShiftMaxUs = Max(iShiftMaxUs, us);
    if (aDx == 0)
        TraceNow(_L("video: held for %d moves, %d told to MMF; avg %d us max %d"), iShiftCalls, iShiftUpdates,
                 iShiftCalls ? iShiftUs / iShiftCalls : 0, iShiftMaxUs);
    }

TInt CVideoPlayer::SoftWaitUs()
    {
    if (!Soft())
        return -1;
    const TInt wait = iSoft->WaitUs();
    if (iScrubChangeMs && NowMs() - iScrubChangeMs <= (TUint32)KScrubFadeMs)
        return wait >= 0 ? Min(wait, 16000) : 16000;
    if (Waiting())
        return wait >= 0 ? Min(wait, KSpinStepMs * 1000) : KSpinStepMs * 1000;
    return wait;
    }

void CVideoPlayer::LogStats()
    {
    if (Soft())
        iSoft->LogStats();
    }

void CVideoPlayer::SoftVideoChanged()
    {
    if (!iSoft || iSoftDoomed)
        return;
    switch (iSoft->State())
        {
        case CSoftVideo::EPlaying:
        case CSoftVideo::EPaused:
            if (iState == EPreparing)
                {
                iState = EReady;
                TraceNow(_L("video: n8media prepared %S ahead in %d ms"), &iPath,
                         (TInt)((PerfNowUs() - iBeginUs) / 1000));
                }
            else if (iState == EOpening)
                {
                if (iResumeMs > 0)
                    {
                    TraceNow(_L("video: n8media takes over at %d ms"), iResumeMs);
                    iSoft->SeekTo(iResumeMs);
                    iResumeMs = -1;
                    }
                StartPlayback();
                }
            break;
        case CSoftVideo::EFailed:
            TraceNow(_L("video: n8media failed on %S: %d"), &iPath, iSoft->Error());
            Finish(EFailed, iSoft->Error());
            break;
        case CSoftVideo::EEnded:
            if (iLoop)
                {
                iSoft->Play();
                TraceNow(_L("video: looped %S (n8media)"), &iPath);
                }
            else
                Finish(EEnded, KErrNone);
            break;
        default:
            break;
        }
    }

void CVideoPlayer::SoftVideoFrame()
    {
    if (Soft())
        iObserver.VideoFrameReady();
    }

void CVideoPlayer::MvpuoOpenComplete(TInt aError)
    {
    if (aError != KErrNone)
        {
        Fail(aError);
        return;
        }

    TRAP_IGNORE(iUtility->SetAudioEnabledL(!iSilent));
    iStage = _S("prepare");
    iUtility->Prepare();
    }

void CVideoPlayer::MvpuoPrepareComplete(TInt aError)
    {
    if (aError != KErrNone)
        {
        Fail(aError);
        return;
        }
    TSize frame;
    TRAP_IGNORE(iUtility->VideoFrameSizeL(frame));
    iMmfSize = frame;

    if (frame.iWidth <= 0 || frame.iHeight <= 0)
        {
        TraceNow(_L("video: MMF prepared %S with no picture (0x0)"), &iPath);
        Fail(KErrNotSupported);
        return;
        }
    TRAPD(displayErr, ShowOnWindowL());
    if (displayErr != KErrNone)
        {
        TraceNow(_L("video: MMF display on the window failed: %d"), displayErr);
        Fail(displayErr);
        return;
        }
    if (!iSilent)
        TRAP_IGNORE(iUtility->SetVolumeL(iUtility->MaxVolume() * iVolume / 100));
    TBuf<40> mime;
    mime.Copy(iUtility->VideoFormatMimeType().Left(mime.MaxLength()));
    TInt64 us = 0;
    TRAP_IGNORE(us = iUtility->DurationL().Int64());
    TraceNow(_L("video: MMF prepared %dx%d, %d ms, %S, in %d ms%s"), frame.iWidth, frame.iHeight, (TInt)(us / 1000),
             &mime, (TInt)((PerfNowUs() - iBeginUs) / 1000), iPlayWhenReady ? _S("") : _S(" (ahead)"));
    if (iResumeMs > 0)
        {
        TRAPD(err, iUtility->SetPositionL(TTimeIntervalMicroSeconds((TInt64)iResumeMs * 1000)));
        TraceNow(_L("video: MMF reopened, on at %d ms, err %d"), iResumeMs, err);
        iResumeMs = -1;
        }
    if (iPlayWhenReady)
        StartPlayback();
    else
        iState = EReady;
    }

void CVideoPlayer::MvpuoFrameReady(CFbsBitmap&  , TInt  )
    {
    }

void CVideoPlayer::MvpuoPlayComplete(TInt aError)
    {
    if (aError == KErrNone && iLoop && iUtility && iFileOpen)
        {
        TRAPD(err, iUtility->SetPositionL(TTimeIntervalMicroSeconds(0)));
        if (err == KErrNone)
            {
            iUtility->Play();
            TraceNow(_L("video: looped %S"), &iPath);
            Redraw();
            return;
            }
        }
    if (aError != KErrNone)
        Fail(aError);
    else
        Finish(EEnded, KErrNone);
    }

void CVideoPlayer::MvpuoEvent(const TMMFEvent& aEvent)
    {
    TraceNow(_L("video: event 0x%x err %d"), aEvent.iEventType.iUid, aEvent.iErrorCode);
    }
