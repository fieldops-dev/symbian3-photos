#ifndef SOFTVIDEO_H
#define SOFTVIDEO_H

#include <e32base.h>
#include <mda/common/audio.h>
#include <mdaaudiooutputstream.h>

#include "avsync.h"
#include "glrenderer.h"

class MSoftVideoObserver
    {
public:

    virtual void SoftVideoChanged() = 0;

    virtual void SoftVideoFrame() = 0;
    };

class CSoftShared;

class CSoftVideo : public CActive, public MMdaAudioOutputStreamCallback
    {
public:
    enum TState { EOpening, EPlaying, EPaused, EEnded, EFailed };
    static CSoftVideo* NewL(MSoftVideoObserver& aObserver, const TDesC& aPath, TInt aVolume);
    ~CSoftVideo();
    void Play();
    void Pause();

    void SeekTo(TInt aMs);
    void SetVolume(TInt aPercent);
    TState State() const { return iState; }
    TInt Error() const { return iError; }
    TInt PositionMs() const;
    TInt DurationMs() const { return iDurationMs; }

    TBool Update(CGlRenderer& aGl);
    TBool HasPicture() const { return iPlanes[0].Valid() && iShownGen >= 0; }

    TSize ShownSize() const;

    void Draw(CGlRenderer& aGl, TInt aWidth, TInt aHeight, TReal aAlpha, TReal aZoom = 1, TReal aPanX = 0,
              TReal aPanY = 0);

    void ReleaseGl(CGlRenderer* aGl);

    TInt WaitUs();

    void LogStats();

private:
    class CDeathWatch;
    CSoftVideo(MSoftVideoObserver& aObserver, TInt aVolume);
    void ConstructL(const TDesC& aPath);
    void Arm();
    void RunL();
    void DoCancel();
    void ThreadDied(TExitType aType, TInt aReason, const TDesC& aCategory);
    void Opened();
    void MaybeStart();
    void StartStream();
    void DropStream();
    void WriteChunks();
    void CheckEnd();
    TInt64 Clock();
    void StartSystemClock(TInt64 aBaseMs);
    static TInt Restart(TAny* aSelf);
    void MaoscOpenComplete(TInt aError);
    void MaoscBufferCopied(TInt aError, const TDesC8& aBuffer);
    void MaoscPlayComplete(TInt aError);

    MSoftVideoObserver& iObserver;
    CSoftShared* iShared;
    RThread iThread;
    TBool iThreadMade;
    CDeathWatch* iDeath;
    TState iState;
    TInt iError;
    TBool iOpenSeen;
    TInt iDurationMs;
    TInt iRotation;
    TInt iDisplayW;
    TInt iDisplayH;
    TInt iCodedW;

    TBool iAudio;
    TBool iAudioDone;
    TInt iRate;
    TInt iRateFlag;
    TInt iChannelFlag;
    TInt iVolume;
    CMdaAudioOutputStream* iStream;
    TMdaAudioDataSettings iSettings;
    TBool iStreamOpen;
    TBool iStreamPaused;
    TInt iInFlight;
    CAsyncCallBack* iRestart;
    TBool iDropPending;
    TBool iDropping;

    TAvClock iClock;
    TBool iClockRunning;
    TBool iClockAudio;
    TInt64 iSysStartUs;
    TInt64 iSysPlayedUs;
    TInt64 iPlayUs;
    TInt iSeekMs;

    TGlTexture iPlanes[3];
    TInt iFrameW;
    TInt iFrameH;
    TInt iShownGen;
    TInt64 iShownPts;

    TUint32 iStatStartMs;
    TInt iStatShown;
    TInt iStatDropped;
    TInt iStatUploadUs;
    TInt iStatUploadMaxUs;
    TInt iStatDriftSum;
    TInt iStatDriftMax;
    TInt iStatUnderruns;
    TInt iTotalDropped;
    TInt iTotalShown;
    TBool iDrawnOnce;
    TBool iWroteOnce;
    };

#endif
