#ifndef VIDEO_H
#define VIDEO_H

#include <coecntrl.h>
#include <remconcoreapitargetobserver.h>
#include "videoplayer2.h"

#include "softvideo.h"

class CFbsBitmap;
class CFont;
class CGlRenderer;
class CRemConCoreApiTarget;
class CRemConInterfaceSelector;
class CRemConResponder;

class MVideoPlayerObserver
    {
public:

    virtual void VideoChanged() = 0;

    virtual void VideoPointerL(const TPointerEvent& aEvent, TInt aPointer) = 0;

    virtual void VideoFrameReady() = 0;

    virtual void VideoControlsChanged() = 0;
    };

class CVideoPlayer : public CCoeControl, public MVideoPlayerUtilityObserver, public MSoftVideoObserver,
                     public MRemConCoreApiTargetObserver
    {
public:

    enum TState { EIdle, EPreparing, EReady, EOpening, EPlaying, EPaused, EEnded, EFailed };

    enum TTap { ETapNone, ETapBack, ETapPlayPause, ETapLoop, ETapControls };
    static CVideoPlayer* NewL(MVideoPlayerObserver& aObserver, const CCoeControl& aParent, TInt aScale);
    ~CVideoPlayer();

    void PrepareL(const TDesC& aPath, const TRect& aRect, const TDesC& aTitle);

    void PlayL(const TDesC& aPath, const TRect& aRect, const TDesC& aTitle);
    void Pause();

    void Stop();

    void Relayout(const TRect& aRect);

    void SetTopInset(TInt aTopInset);
    TTap Tap(const TPoint& aPos);
    TState State() const { return iState; }
    TInt Error() const { return iError; }
    const TDesC& Path() const { return iPath; }
    TInt PositionMs() const;
    TInt DurationMs() const;

    TBool ControlsShown() const { return iControls; }
    void ShowControls(TBool aShow);
    TRect PlayPauseRect() const;

    TRect LoopRect() const;
    TBool Looping() const { return iLoop; }

    TBool Waiting() const;

    const TUint8* SpinnerFrame(TInt& aSide) const;

    void SetZoom(TReal aZoom, TReal aPanX, TReal aPanY);
    TReal Zoom() const { return iZoom; }

    TReal FillZoom() const;
    TReal PanX() const { return iPanX; }
    TReal PanY() const { return iPanY; }

    void SetShift(TInt aDx);
    void ProgressLine(TInt& aY, TInt& aX0, TInt& aX1) const;

    const TDesC& Title() const;

    TBool OnProgress(const TPoint& aPos) const;

    TBool Scrubbing() const { return iScrubbing; }
    TInt ScrubMs() const { return iScrubMs; }

    TReal ScrubAlpha() const;
    void BeginScrub(const TPoint& aPos);
    void ScrubTo(const TPoint& aPos);
    void EndScrub();

    TBool Soft() const;
    TBool SoftHasPicture() const { return iSoft && iSoft->HasPicture(); }
    void SetGl(CGlRenderer* aGl);
    TBool UpdateSoft();

    void DrawSoft(TInt aWidth, TInt aHeight, TReal aAlpha, TReal aShiftX = 0);
    TInt SoftWaitUs();
    void LogStats();

private:
    CVideoPlayer(MVideoPlayerObserver& aObserver, TInt aScale);
    void ConstructL(const CCoeControl& aParent);
    TInt Scale(TInt aDesign) const { return aDesign * iScale / 256; }
    void BeginL(const TDesC& aPath, const TRect& aRect, const TDesC& aTitle, TBool aPlay);
    TBool SoftFirst(const TDesC& aPath, TPtrC& aWhy) const;
    void StartSoft();
    void OpenL();
    void CloseFile();
    void ShowOnWindowL();
    void StartPlayback();
    void Resume();
    void SeekNow(TInt aMs);
    void Fail(TInt aError);
    void Finish(TState aState, TInt aError);
    void Redraw();
    void StartKeys();
    void StopKeys();
    void ChangeVolume(TInt aSteps);
    void FirstFrame(const TDesC& aPath);
    TInt KeyframeAtOrBefore(TInt aMs);
    void ReadKeyframes();
    void BeginWait();
    void EndWait();
    static TInt Close(TAny* aSelf);
    static TInt Tick(TAny* aSelf);
    static TInt PollStart(TAny* aSelf);
    void HandlePointerEventL(const TPointerEvent& aEvent);
    void Draw(const TRect& aRect) const;
    void DrawShadowedText(CWindowGc& aGc, const TDesC& aText, const TPoint& aAt) const;
    void DrawSpinner(CWindowGc& aGc) const;
    void DrawLoop(CWindowGc& aGc) const;
    void DrawScrubTime(CWindowGc& aGc) const;
    static TInt Fade(TAny* aSelf);
    void StartFade();
    void MakeBitmapsL();
    void ApplyZoom();
    void Place(const TRect& aRect);
    TRect VideoExtent() const;
    void MvpuoOpenComplete(TInt aError);
    void MvpuoPrepareComplete(TInt aError);
    void MvpuoFrameReady(CFbsBitmap& aFrame, TInt aError);
    void MvpuoPlayComplete(TInt aError);
    void MvpuoEvent(const TMMFEvent& aEvent);
    void SoftVideoChanged();
    void SoftVideoFrame();
    void MrccatoCommand(TRemConCoreApiOperationId aOperationId, TRemConCoreApiButtonAction aButtonAct);
    void MrccatoPlay(TRemConCoreApiPlaybackSpeed aSpeed, TRemConCoreApiButtonAction aButtonAct);

    MVideoPlayerObserver& iObserver;
    TInt iScale;
    TInt iTopInset;
    CVideoPlayerUtility* iUtility;
    TBool iDisplayAdded;
    TBool iFileOpen;
    CSoftVideo* iSoft;
    CGlRenderer* iGl;
    TBool iPlayWhenReady;
    TBool iTriedSoft;
    TBool iShownRetry;
    TBool iSwitchToSoft;
    TBool iSoftDoomed;
    TBool iPrepareFailed;
    RPointerArray<HBufC> iMmfFailed;
    CPeriodic* iTicker;
    CPeriodic* iStartPoll;
    CAsyncCallBack* iClose;
    TState iState;
    TInt iError;
    const TText* iStage;
    TBool iSilent;
    TBool iRetrying;
    TBool iControls;
    TUint32 iControlsMs;
    TInt64 iBeginUs;
    TInt64 iTapUs;
    TBool iScrubbing;
    TBool iScrubResume;
    TInt iScrubMs;
    TUint32 iScrubSeekMs;
    TInt iVolume;
    TUint32 iVolumeMs;
    TBuf<24> iVolumeText;
    CRemConInterfaceSelector* iSelector;
    CRemConCoreApiTarget* iTarget;
    CRemConResponder* iResponder;
    CFont* iFont;
    TBool iOwnFont;
    CPeriodic* iFade;
    TUint32 iScrubChangeMs;
    TInt iShiftCalls;
    TInt iShiftUpdates;
    TInt iShiftUs;
    TInt iShiftMaxUs;
    TUint32 iShiftUpdateMs;
    CFont* iScrubFont;
    TBool iOwnScrubFont;
    TBool iWaiting;
    TInt64 iWaitUs;
    TUint32 iSpinMs;
    TBool iLoop;
    TReal iZoom;
    TReal iPanX;
    TReal iPanY;
    TSize iMmfSize;
    TPoint iBaseTl;
    TSize iBaseSize;
    TInt iShift;
    TBool iShown;
    TInt iMidRetries;
    TBool iFreshPlayer;
    TInt iKnownMs;
    TInt iResumeMs;
    TInt* iKeys;
    TInt iKeyCount;
    CFbsBitmap* iLoopMask;
    CFbsBitmap* iLoopOn;
    CFbsBitmap* iLoopOff;
    CFbsBitmap* iLoopEdge;
    CFbsBitmap* iSpinMask;
    CFbsBitmap* iSpinWhite;
    TUint8* iSpinFrames;
    TInt iSpinSide;
    TFileName iPath;
    TBuf<40> iTitle;
    };

#endif
