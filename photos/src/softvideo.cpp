#include "softvideo.h"

#include <utf.h>

#include "physics.h"
#include "trace.h"

extern "C"
    {
#include "n8media.h"
    }

namespace
    {
    const TInt KFrames = 4;
    const TInt KChunks = 16;
    const TInt KChunkSamples = 2048;
    const TInt KInFlightMax = 4;
    const TInt KStartChunks = 3;
    const TInt KSoundWaitUs = 250000;
    const TInt KAudioLowMs = 400;
    const TInt KEarlyMs = 8;
    const TInt KThreadStack = 0x80000;

    const TInt KMaxH264Pixels = 640 * 368;
    const TInt KMaxOtherPixels = 864 * 480;
    const TInt KOpenTooLarge = -1000;
    const TInt KQuitWaitUs = 1000000;

    enum TItemState { EItemFree, EItemDecoding, EItemFull, EItemInUse };

    class TFrameSlot
        {
    public:
        TItemState iState;
        TUint8* iPixels;
        TInt iCapacity;
        TInt iWidth;
        TInt iHeight;
        TInt iLumaW;
        TInt iLumaH;
        TInt64 iPts;
        TInt iGen;
        };

    class TChunk
        {
    public:
        TItemState iState;
        TInt16* iPcm;
        TInt iSamples;
        TInt64 iPts;
        TInt iGen;
        TInt iSeq;
        };

    TInt RateFlag(TInt aRate)
        {
        const TInt rates[11] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000, 96000 };
        const TInt flags[11] = {
            TMdaAudioDataSettings::ESampleRate8000Hz, TMdaAudioDataSettings::ESampleRate11025Hz,
            TMdaAudioDataSettings::ESampleRate12000Hz, TMdaAudioDataSettings::ESampleRate16000Hz,
            TMdaAudioDataSettings::ESampleRate22050Hz, TMdaAudioDataSettings::ESampleRate24000Hz,
            TMdaAudioDataSettings::ESampleRate32000Hz, TMdaAudioDataSettings::ESampleRate44100Hz,
            TMdaAudioDataSettings::ESampleRate48000Hz, TMdaAudioDataSettings::ESampleRate64000Hz,
            TMdaAudioDataSettings::ESampleRate96000Hz };
        for (TInt i = 0; i < 11; i++)
            if (rates[i] == aRate)
                return flags[i];
        return 0;
        }

    void CopyText(TDes& aTo, const char* aFrom)
        {
        const TUint8* text = reinterpret_cast<const TUint8*>(aFrom);
        aTo.Copy(TPtrC8(text, User::StringLength(text)).Left(aTo.MaxLength()));
        }
    }

class CSoftShared : public CBase
    {
public:
    RMutex iLock;
    RSemaphore iWake;
    TThreadId iUi;
    TRequestStatus* iNotify;
    TBool iArmed;
    TBool iNews;
    TBuf8<KMaxFileName * 3 + 1> iPath;
    volatile TBool iQuit;

    TBool iAbandoned;
    TBool iFinished;

    TInt iGen;
    TBool iSeekPending;
    TInt64 iSeekMs;

    TBool iOpened;
    TInt iOpenResult;
    n8m_info iInfo;
    TBool iVideoEof;
    TBool iAudioEof;
    TInt iDecodeError;
    TFrameSlot iFrames[KFrames];
    TChunk iChunks[KChunks];
    TInt iChunkSeq;

    TInt iStatFrames;
    TInt iStatFrameUs;
    TInt iStatFrameMaxUs;
    TInt iStatChunks;
    TInt iStatChunkUs;
    TInt iStatWaits;

    void Release()
        {
        for (TInt i = 0; i < KFrames; i++)
            User::Free(iFrames[i].iPixels);
        for (TInt i = 0; i < KChunks; i++)
            User::Free(iChunks[i].iPcm);
        iLock.Close();
        iWake.Close();
        }

    void Notify()
        {
        if (iAbandoned)
            return;
        if (!iArmed)
            {
            iNews = ETrue;
            return;
            }
        iArmed = EFalse;
        RThread ui;
        if (ui.Open(iUi) == KErrNone)
            {
            ui.RequestComplete(iNotify, KErrNone);
            ui.Close();
            }
        }
    };

namespace
    {

    TBool StoreFrame(TFrameSlot& aSlot, const n8m_frame& aFrame)
        {
        TInt lumaW, lumaH;

        if (aFrame.width * aFrame.height > KMaxOtherPixels || !aFrame.y || !aFrame.u || !aFrame.v ||
            aFrame.y_stride < aFrame.width || aFrame.uv_stride < (aFrame.width + 1) / 2)
            return EFalse;
        VideoTextureSize(aFrame.width, aFrame.height, lumaW, lumaH);
        const TInt bytes = lumaW * lumaH * 3 / 2;
        if (bytes > aSlot.iCapacity)
            {
            User::Free(aSlot.iPixels);
            aSlot.iPixels = static_cast<TUint8*>(User::Alloc(bytes));
            aSlot.iCapacity = aSlot.iPixels ? bytes : 0;
            if (!aSlot.iPixels)
                return EFalse;
            }
        aSlot.iWidth = aFrame.width;
        aSlot.iHeight = aFrame.height;
        aSlot.iLumaW = lumaW;
        aSlot.iLumaH = lumaH;
        aSlot.iPts = aFrame.pts_ms;
        for (TInt p = 0; p < 3; p++)
            {
            const TInt w = p ? (aFrame.width + 1) / 2 : aFrame.width;
            const TInt h = p ? (aFrame.height + 1) / 2 : aFrame.height;
            const TInt rows = p ? lumaH / 2 : lumaH, pitch = p ? lumaW / 2 : lumaW;
            const TInt stride = p ? aFrame.uv_stride : aFrame.y_stride;
            const TUint8* from = p == 0 ? aFrame.y : p == 1 ? aFrame.u : aFrame.v;
            TUint8* to = aSlot.iPixels + (p == 0 ? 0 : lumaW * lumaH + (p - 1) * (lumaW / 2) * (lumaH / 2));
            for (TInt y = 0; y < rows; y++)
                {
                TUint8* row = to + y * pitch;
                Mem::Copy(row, from + Min(y, h - 1) * stride, w);
                if (pitch > w)
                    Mem::Fill(row + w, pitch - w, row[w - 1]);
                }
            }
        return ETrue;
        }

    TInt FreeIndex(const TFrameSlot* aSlots)
        {
        for (TInt i = 0; i < KFrames; i++)
            if (aSlots[i].iState == EItemFree)
                return i;
        return -1;
        }

    TInt FreeChunk(const TChunk* aChunks)
        {
        for (TInt i = 0; i < KChunks; i++)
            if (aChunks[i].iState == EItemFree)
                return i;
        return -1;
        }

    void DecodeLoop(CSoftShared& aShared)
        {
        n8m_file* file = NULL;
        n8m_info info;
        Mem::FillZ(&info, sizeof(info));
        TInt opened = n8m_open(reinterpret_cast<const char*>(aShared.iPath.PtrZ()), &file, &info);
        if (opened == 0 && file && info.has_video)
            {
            const TPtrC8 codec(reinterpret_cast<const TUint8*>(info.video_codec),
                               User::StringLength(reinterpret_cast<const TUint8*>(info.video_codec)));
            const TInt limit = codec.CompareF(_L8("h264")) == 0 ? KMaxH264Pixels : KMaxOtherPixels;
            if (info.width <= 0 || info.height <= 0 || info.width * info.height > limit)
                {
                n8m_close(file);
                file = NULL;
                opened = KOpenTooLarge;
                }
            }
        aShared.iLock.Wait();
        aShared.iOpenResult = opened;
        aShared.iInfo = info;
        aShared.iOpened = ETrue;
        aShared.Notify();
        aShared.iLock.Signal();
        if (opened != 0 || !file)
            return;
        TBool videoEof = !info.has_video, audioEof = !info.has_audio;
        while (!aShared.iQuit)
            {
            aShared.iLock.Wait();
            if (aShared.iSeekPending)
                {
                const TInt64 ms = aShared.iSeekMs;
                aShared.iSeekPending = EFalse;
                aShared.iLock.Signal();
                const TInt err = n8m_seek(file, ms);
                videoEof = !info.has_video;
                audioEof = !info.has_audio;
                aShared.iLock.Wait();
                if (err != 0)
                    aShared.iDecodeError = err;
                aShared.iVideoEof = videoEof;
                aShared.iAudioEof = audioEof;
                aShared.iLock.Signal();
                continue;
                }
            const TInt gen = aShared.iGen;
            TInt queuedSamples = 0;
            for (TInt i = 0; i < KChunks; i++)
                if (aShared.iChunks[i].iState != EItemFree && aShared.iChunks[i].iGen == gen)
                    queuedSamples += aShared.iChunks[i].iSamples;
            const TInt chunk = audioEof ? -1 : FreeChunk(aShared.iChunks);
            const TInt slot = videoEof ? -1 : FreeIndex(aShared.iFrames);
            const TBool lowSound = info.audio_rate > 0 && (TInt64)queuedSamples * 1000 / info.audio_rate < KAudioLowMs;
            const TBool doAudio = chunk >= 0 && (lowSound || slot < 0);
            if (!doAudio && slot < 0)
                {
                aShared.iStatWaits++;
                aShared.iLock.Signal();
                aShared.iWake.Wait();
                continue;
                }
            if (doAudio)
                {
                TChunk& c = aShared.iChunks[chunk];
                c.iState = EItemDecoding;
                aShared.iLock.Signal();
                TInt got = 0;
                long long pts = 0;
                const TUint32 start = User::FastCounter();
                const TInt err = n8m_audio(file, c.iPcm, KChunkSamples, &got, &pts);
                const TInt us = PerfCounterUs(start, User::FastCounter());
                aShared.iLock.Wait();
                c.iState = EItemFree;
                if (err == N8M_EOF || (err == 0 && got <= 0))
                    audioEof = ETrue;
                else if (err != 0)
                    {
                    aShared.iDecodeError = err;
                    audioEof = ETrue;
                    }
                else if (gen == aShared.iGen && !aShared.iSeekPending)
                    {
                    c.iSamples = got;
                    c.iPts = pts;
                    c.iGen = gen;
                    c.iSeq = aShared.iChunkSeq++;
                    c.iState = EItemFull;
                    aShared.iStatChunks++;
                    aShared.iStatChunkUs += us;
                    }
                if (gen == aShared.iGen && !aShared.iSeekPending)
                    aShared.iAudioEof = audioEof;
                aShared.Notify();
                aShared.iLock.Signal();
                }
            else
                {
                TFrameSlot& s = aShared.iFrames[slot];
                s.iState = EItemDecoding;
                aShared.iLock.Signal();
                n8m_frame frame;
                Mem::FillZ(&frame, sizeof(frame));
                const TUint32 start = User::FastCounter();
                const TInt err = n8m_video(file, &frame);
                const TInt us = PerfCounterUs(start, User::FastCounter());
                const TBool stored = err == 0 && frame.width > 0 && frame.height > 0 && StoreFrame(s, frame);
                aShared.iLock.Wait();
                s.iState = EItemFree;
                if (err == N8M_EOF)
                    videoEof = ETrue;
                else if (err != 0 || !stored)
                    {
                    aShared.iDecodeError = err ? err : KErrNoMemory;
                    videoEof = ETrue;
                    }
                else if (gen == aShared.iGen && !aShared.iSeekPending)
                    {
                    s.iGen = gen;
                    s.iState = EItemFull;
                    aShared.iStatFrames++;
                    aShared.iStatFrameUs += us;
                    aShared.iStatFrameMaxUs = Max(aShared.iStatFrameMaxUs, us);
                    }
                if (gen == aShared.iGen && !aShared.iSeekPending)
                    aShared.iVideoEof = videoEof;
                aShared.Notify();
                aShared.iLock.Signal();
                }
            }
        n8m_close(file);
        }

    TInt DecodeThread(TAny* aShared)
        {
        CTrapCleanup* cleanup = CTrapCleanup::New();
        if (!cleanup)
            return KErrNoMemory;
        CSoftShared& shared = *static_cast<CSoftShared*>(aShared);
        DecodeLoop(shared);
        shared.iLock.Wait();
        shared.iFinished = ETrue;
        const TBool abandoned = shared.iAbandoned;
        shared.iLock.Signal();
        if (abandoned)
            {
            shared.Release();
            delete &shared;
            }
        delete cleanup;
        return KErrNone;
        }
    }

class CSoftVideo::CDeathWatch : public CActive
    {
public:
    CDeathWatch(CSoftVideo& aOwner, RThread& aThread) : CActive(EPriorityHigh), iOwner(aOwner), iThread(aThread)
        {
        CActiveScheduler::Add(this);
        iThread.Logon(iStatus);
        SetActive();
        }
    ~CDeathWatch() { Cancel(); }

private:
    void RunL()
        {
        TExitCategoryName category = iThread.ExitCategory();
        iOwner.ThreadDied(iThread.ExitType(), iThread.ExitReason(), category);
        }
    void DoCancel() { iThread.LogonCancel(iStatus); }
    CSoftVideo& iOwner;
    RThread& iThread;
    };

CSoftVideo* CSoftVideo::NewL(MSoftVideoObserver& aObserver, const TDesC& aPath, TInt aVolume)
    {
    CSoftVideo* self = new (ELeave) CSoftVideo(aObserver, aVolume);
    CleanupStack::PushL(self);
    self->ConstructL(aPath);
    CleanupStack::Pop(self);
    return self;
    }

CSoftVideo::CSoftVideo(MSoftVideoObserver& aObserver, TInt aVolume)
    : CActive(EPriorityStandard), iObserver(aObserver), iState(EOpening), iVolume(aVolume), iShownGen(-1)
    {
    CActiveScheduler::Add(this);
    }

void CSoftVideo::ConstructL(const TDesC& aPath)
    {
    iShared = new (ELeave) CSoftShared;
    User::LeaveIfError(iShared->iLock.CreateLocal());
    User::LeaveIfError(iShared->iWake.CreateLocal(0));
    iShared->iUi = RThread().Id();
    User::LeaveIfError(CnvUtfConverter::ConvertFromUnicodeToUtf8(iShared->iPath, aPath));
    for (TInt i = 0; i < KChunks; i++)
        {
        iShared->iChunks[i].iPcm = static_cast<TInt16*>(User::AllocL(KChunkSamples * 2 * sizeof(TInt16)));
        iShared->iChunks[i].iState = EItemFree;
        }
    iRestart = new (ELeave) CAsyncCallBack(TCallBack(Restart, this), CActive::EPriorityStandard);
    iStatStartMs = NowMs();
    iPlayUs = PerfNowUs();
    TBuf<32> name(_L("softvideo"));
    name.AppendNum((TUint)User::NTickCount());
    User::LeaveIfError(iThread.Create(name, DecodeThread, KThreadStack, &User::Allocator(), iShared));
    iThreadMade = ETrue;
    iThread.SetPriority(EPriorityLess);
    iDeath = new (ELeave) CDeathWatch(*this, iThread);
    Arm();
    iThread.Resume();
    TraceNow(_L("softvideo: decode thread started for %S"), &aPath);
    }

CSoftVideo::~CSoftVideo()
    {
    Cancel();
    delete iRestart;
    DropStream();
    delete iDeath;
    TBool letGo = EFalse;
    if (iThreadMade)
        {
        if (iThread.ExitType() == EExitPending)
            {
            TRequestStatus exited;
            iThread.Logon(exited);
            iShared->iQuit = ETrue;
            iShared->iWake.Signal();
            RTimer timer;
            if (timer.CreateLocal() == KErrNone)
                {
                TRequestStatus timeout;
                timer.After(timeout, KQuitWaitUs);
                User::WaitForRequest(exited, timeout);
                if (exited == KRequestPending)
                    {
                    iThread.LogonCancel(exited);
                    User::WaitForRequest(exited);

                    iShared->iLock.Wait();
                    letGo = !iShared->iFinished;
                    iShared->iAbandoned = letGo;
                    iShared->iLock.Signal();
                    if (!letGo)
                        {
                        iThread.Logon(exited);
                        User::WaitForRequest(exited);
                        }
                    }
                else
                    {
                    timer.Cancel();
                    User::WaitForRequest(timeout);
                    }
                timer.Close();
                }
            else
                User::WaitForRequest(exited);
            if (letGo)
                {
                iThread.SetPriority(EPriorityMuchLess);
                TraceNow(_L("softvideo: decode thread busy at close, let go to finish on its own"));
                }
            }
        iThread.Close();
        }
    if (iShared && !letGo)
        {
        iShared->Release();
        delete iShared;
        }
    }

void CSoftVideo::Arm()
    {
    iStatus = KRequestPending;
    SetActive();
    iShared->iLock.Wait();
    if (iShared->iNews)
        {
        iShared->iNews = EFalse;
        TRequestStatus* status = &iStatus;
        User::RequestComplete(status, KErrNone);
        }
    else
        {
        iShared->iNotify = &iStatus;
        iShared->iArmed = ETrue;
        }
    iShared->iLock.Signal();
    }

void CSoftVideo::DoCancel()
    {
    iShared->iLock.Wait();
    const TBool armed = iShared->iArmed;
    iShared->iArmed = EFalse;
    iShared->iLock.Signal();
    if (armed)
        {
        TRequestStatus* status = &iStatus;
        User::RequestComplete(status, KErrCancel);
        }
    }

void CSoftVideo::RunL()
    {
    Arm();
    if (!iOpenSeen)
        {
        iShared->iLock.Wait();
        const TBool opened = iShared->iOpened;
        iShared->iLock.Signal();
        if (opened)
            Opened();
        }
    if (iState == EFailed)
        return;
    WriteChunks();
    MaybeStart();
    CheckEnd();
    iObserver.SoftVideoFrame();
    }

void CSoftVideo::Opened()
    {
    iOpenSeen = ETrue;
    const n8m_info& info = iShared->iInfo;
    if (iShared->iOpenResult == KOpenTooLarge)
        {
        TBuf<16> codec;
        CopyText(codec, info.video_codec);
        TraceNow(_L("softvideo: %S %dx%d is past what plays here in software"), &codec, info.width, info.height);
        iState = EFailed;
        iError = KErrTooBig;
        iObserver.SoftVideoChanged();
        return;
        }
    if (iShared->iOpenResult != 0)
        {
        TBuf<60> why;
        CopyText(why, n8m_error(iShared->iOpenResult));
        TraceNow(_L("softvideo: n8m_open failed %d (%S)"), iShared->iOpenResult, &why);
        iState = EFailed;
        iError = KErrNotSupported;
        iObserver.SoftVideoChanged();
        return;
        }
    iDurationMs = (TInt)info.duration_ms;
    iRotation = info.rotation;
    iCodedW = info.width;
    iDisplayW = info.display_width > 0 ? info.display_width : info.width;
    iDisplayH = info.display_height > 0 ? info.display_height : info.height;
    iRate = info.audio_rate;
    iRateFlag = RateFlag(info.audio_rate);
    iChannelFlag = info.audio_channels == 1 ? TMdaAudioDataSettings::EChannelsMono :
                   info.audio_channels == 2 ? TMdaAudioDataSettings::EChannelsStereo : 0;
    iAudio = info.has_audio && iRateFlag && iChannelFlag;
    TBuf<16> container, video, audio;
    CopyText(container, info.container);
    CopyText(video, info.video_codec);
    CopyText(audio, info.audio_codec);
    const TPtrC silent(info.has_audio && !iAudio ? _L(" (not playable here: silent)") : KNullDesC());
    TraceNow(_L("softvideo: opened %S: video %S %dx%d (shown %dx%d, turned %d), %d/%d fps; audio %S %d Hz x%d%S; "
             "%d ms"), &container, &video, info.width, info.height, iDisplayW, iDisplayH, iRotation, info.fps_num,
             info.fps_den, &audio, info.audio_rate, info.audio_channels, &silent, iDurationMs);
    if (iState == EOpening)
        iState = EPlaying;
    iObserver.SoftVideoChanged();
    }

void CSoftVideo::ThreadDied(TExitType aType, TInt aReason, const TDesC& aCategory)
    {
    TraceNow(_L("softvideo: decode thread ended, type %d reason %d %S"), aType, aReason, &aCategory);
    if (aType == EExitKill && aReason == KErrNone)
        return;
    DropStream();
    iState = EFailed;
    iError = KErrDied;
    iObserver.SoftVideoChanged();
    }

void CSoftVideo::Play()
    {
    if (iState == EEnded)
        {
        iState = EPaused;
        SeekTo(0);
        }
    if (iState != EPaused)
        return;
    iState = EPlaying;
    iPlayUs = PerfNowUs();
    if (iStream && iStreamPaused)
        {
        iStreamPaused = EFalse;
        if (iStream->Resume() != KErrNone)
            {
            DropStream();
            iClockRunning = EFalse;
            }
        }
    else if (iClockRunning && !iClockAudio)
        iSysStartUs = PerfNowUs();
    MaybeStart();
    }

void CSoftVideo::Pause()
    {
    if (iState != EPlaying && iState != EOpening)
        return;
    Clock();
    iState = EPaused;
    if (!iClockRunning)
        return;
    if (iClockAudio && iStream)
        {
        if (iStreamOpen && iStream->Pause() == KErrNone)
            iStreamPaused = ETrue;
        else
            {

            DropStream();
            iClockRunning = EFalse;
            }
        }
    else if (!iClockAudio)
        iSysPlayedUs += PerfNowUs() - iSysStartUs;
    }

void CSoftVideo::SeekTo(TInt aMs)
    {
    if (!iOpenSeen || iState == EFailed)
        return;
    aMs = Max(0, iDurationMs > 0 ? Min(aMs, iDurationMs) : aMs);
    DropStream();
    iClockRunning = EFalse;
    iShared->iLock.Wait();
    iShared->iGen++;
    for (TInt i = 0; i < KFrames; i++)
        if (iShared->iFrames[i].iState == EItemFull)
            iShared->iFrames[i].iState = EItemFree;
    for (TInt i = 0; i < KChunks; i++)
        if (iShared->iChunks[i].iState == EItemFull)
            iShared->iChunks[i].iState = EItemFree;
    iShared->iSeekPending = ETrue;
    iShared->iSeekMs = aMs;
    iShared->iVideoEof = EFalse;
    iShared->iAudioEof = EFalse;
    iShared->iLock.Signal();
    iShared->iWake.Signal();
    iSeekMs = aMs;
    iClock.Start(aMs);
    iAudioDone = EFalse;
    if (iState == EEnded)
        iState = EPaused;
    }

void CSoftVideo::SetVolume(TInt aPercent)
    {
    iVolume = aPercent;
    if (iStream && iStreamOpen)
        iStream->SetVolume(iStream->MaxVolume() * iVolume / 100);
    }

TInt CSoftVideo::PositionMs() const
    {
    if (!iClockRunning && iShownGen != iShared->iGen)
        return iSeekMs;
    return (TInt)iClock.Ms();
    }

TInt64 CSoftVideo::Clock()
    {
    if (!iClockRunning)
        return iClock.Ms();
    const TInt64 now = PerfNowUs();
    if (iClockAudio)
        return iClock.Update(iStream && iStreamOpen ? iStream->Position().Int64() : 0, now);
    TInt64 played = iSysPlayedUs;
    if (iState == EPlaying)
        played += now - iSysStartUs;
    return iClock.Update(played, now);
    }

void CSoftVideo::StartSystemClock(TInt64 aBaseMs)
    {
    iClock.Start(aBaseMs);
    iClockRunning = ETrue;
    iClockAudio = EFalse;
    iSysPlayedUs = 0;
    iSysStartUs = PerfNowUs();
    }

void CSoftVideo::MaybeStart()
    {
    if (iState != EPlaying || !iOpenSeen || iStream || iDropPending)
        return;
    if (iClockRunning && (iClockAudio || !iAudio || iAudioDone))
        return;
    const TInt gen = iShared->iGen;
    TInt chunks = 0, frames = 0;
    TInt64 firstChunk = -1, firstFrame = -1;
    iShared->iLock.Wait();
    for (TInt i = 0; i < KChunks; i++)
        {
        const TChunk& c = iShared->iChunks[i];
        if (c.iState == EItemFull && c.iGen == gen)
            {
            chunks++;
            if (firstChunk < 0 || c.iPts < firstChunk)
                firstChunk = c.iPts;
            }
        }
    for (TInt i = 0; i < KFrames; i++)
        {
        const TFrameSlot& s = iShared->iFrames[i];
        if (s.iState == EItemFull && s.iGen == gen)
            {
            frames++;
            if (firstFrame < 0 || s.iPts < firstFrame)
                firstFrame = s.iPts;
            }
        }
    const TBool audioEof = iShared->iAudioEof, videoEof = iShared->iVideoEof;
    const TBool hasVideo = iShared->iInfo.has_video;
    iShared->iLock.Signal();
    const TBool soundReady = !iAudio || iAudioDone || chunks >= KStartChunks || (audioEof && chunks > 0);
    if (iClockRunning)
        {
        if (!soundReady || chunks == 0)
            return;
        const TInt64 now = Clock();
        iShared->iLock.Wait();
        for (TInt i = 0; i < KChunks; i++)
            {
            TChunk& c = iShared->iChunks[i];
            if (c.iState == EItemFull && c.iGen == gen && c.iPts + (TInt64)c.iSamples * 1000 / Max(1, iRate) < now)
                c.iState = EItemFree;
            }
        iShared->iLock.Signal();
        iShared->iWake.Signal();
        StartStream();
        return;
        }
    const TBool pictureReady = !hasVideo || frames > 0 || iShownGen == gen || videoEof;
    if (!pictureReady || (!soundReady && PerfNowUs() - iPlayUs < KSoundWaitUs))
        return;
    if (!soundReady)
        TraceNow(_L("softvideo: sound not ready %d ms after play: pictures first"), KSoundWaitUs / 1000);
    if (iAudio && !iAudioDone && soundReady && chunks > 0)
        StartStream();
    else
        {
        if (iAudio && audioEof && chunks == 0)
            iAudioDone = ETrue;
        StartSystemClock(iShownGen == gen ? iShownPts : firstFrame >= 0 ? firstFrame : iSeekMs);
        }
    }

void CSoftVideo::StartStream()
    {
    TRAPD(err, iStream = CMdaAudioOutputStream::NewL(*this, EMdaPriorityNormal,
                                                       EMdaPriorityPreferenceTimeAndQuality));
    if (err != KErrNone)
        {
        TraceNow(_L("softvideo: no audio output, err %d: silent"), err);
        iStream = NULL;
        iAudio = EFalse;
        MaybeStart();
        return;
        }
    iSettings.Query();
    iSettings.iSampleRate = iRateFlag;
    iSettings.iChannels = iChannelFlag;
    iSettings.iFlags = TMdaAudioDataSettings::ENoNetworkRouting;
    iSettings.iVolume = 0;
    iStreamOpen = EFalse;
    iStreamPaused = EFalse;
    iInFlight = 0;
    TraceNow(_L("softvideo: audio output opening"));
    iStream->Open(&iSettings);
    }

void CSoftVideo::DropStream()
    {
    if (iStream)
        {

        iDropping = ETrue;
        delete iStream;
        iStream = NULL;
        iDropping = EFalse;
        }
    iStreamOpen = EFalse;
    iStreamPaused = EFalse;
    iInFlight = 0;
    if (!iShared || !iShared->iLock.Handle() || !iShared->iWake.Handle())
        return;
    iShared->iLock.Wait();
    for (TInt i = 0; i < KChunks; i++)
        if (iShared->iChunks[i].iState == EItemInUse)
            iShared->iChunks[i].iState = EItemFree;
    iShared->iLock.Signal();
    iShared->iWake.Signal();
    }

TInt CSoftVideo::Restart(TAny* aSelf)
    {
    CSoftVideo* self = static_cast<CSoftVideo*>(aSelf);
    self->iDropPending = EFalse;
    self->DropStream();
    self->MaybeStart();
    self->CheckEnd();
    self->iObserver.SoftVideoFrame();
    return EFalse;
    }

void CSoftVideo::WriteChunks()
    {
    if (!iStream || !iStreamOpen || iDropPending)
        return;
    const TInt gen = iShared->iGen;
    while (iInFlight < KInFlightMax)
        {
        iShared->iLock.Wait();
        TInt next = -1;
        for (TInt i = 0; i < KChunks; i++)
            {
            const TChunk& c = iShared->iChunks[i];
            if (c.iState == EItemFull && c.iGen == gen && (next < 0 || c.iSeq < iShared->iChunks[next].iSeq))
                next = i;
            }
        if (next >= 0)
            iShared->iChunks[next].iState = EItemInUse;
        iShared->iLock.Signal();
        if (next < 0)
            return;
        TChunk& c = iShared->iChunks[next];
        const TInt channels = iChannelFlag == TMdaAudioDataSettings::EChannelsStereo ? 2 : 1;
        TRAPD(err, iStream->WriteL(TPtrC8(reinterpret_cast<const TUint8*>(c.iPcm),
                                          c.iSamples * channels * sizeof(TInt16))));
        if (err != KErrNone)
            {
            iShared->iLock.Wait();
            c.iState = EItemFree;
            iShared->iLock.Signal();
            TraceNow(_L("softvideo: audio write err %d"), err);
            return;
            }
        iInFlight++;
        if (!iWroteOnce)
            {
            iWroteOnce = ETrue;
            TraceNow(_L("softvideo: first sound written"));
            }
        }
    }

void CSoftVideo::MaoscOpenComplete(TInt aError)
    {
    TraceNow(_L("softvideo: audio output open, err %d"), aError);
    if (iDropping)
        return;
    if (aError == KErrNone)
        TRAP(aError, iStream->SetAudioPropertiesL(iRateFlag, iChannelFlag));
    if (aError != KErrNone)
        {

        TraceNow(_L("softvideo: audio output open err %d: silent"), aError);
        iAudio = EFalse;
        iDropPending = ETrue;
        iRestart->CallBack();
        return;
        }
    iStreamOpen = ETrue;
    iStream->SetVolume(iStream->MaxVolume() * iVolume / 100);
    TInt64 first = -1;
    const TInt gen = iShared->iGen;
    iShared->iLock.Wait();
    for (TInt i = 0; i < KChunks; i++)
        {
        const TChunk& c = iShared->iChunks[i];
        if (c.iState == EItemFull && c.iGen == gen && (first < 0 || c.iPts < first))
            first = c.iPts;
        }
    iShared->iLock.Signal();
    iClock.Start(first >= 0 ? first : iClock.Ms());
    iClockRunning = ETrue;
    iClockAudio = ETrue;
    WriteChunks();
    if (iState == EPaused)
        {
        if (iStream->Pause() == KErrNone)
            iStreamPaused = ETrue;
        else
            {
            iClockRunning = EFalse;
            iDropPending = ETrue;
            iRestart->CallBack();
            }
        }
    }

void CSoftVideo::MaoscBufferCopied(TInt  , const TDesC8& aBuffer)
    {
    if (iDropping)
        return;
    iShared->iLock.Wait();
    for (TInt i = 0; i < KChunks; i++)
        if (reinterpret_cast<const TUint8*>(iShared->iChunks[i].iPcm) == aBuffer.Ptr() &&
            iShared->iChunks[i].iState == EItemInUse)
            {
            iShared->iChunks[i].iState = EItemFree;
            break;
            }
    iShared->iLock.Signal();
    iShared->iWake.Signal();
    iInFlight = Max(0, iInFlight - 1);
    WriteChunks();
    }

void CSoftVideo::MaoscPlayComplete(TInt aError)
    {
    if (iDropPending || iDropping)
        return;
    Clock();
    iShared->iLock.Wait();
    const TBool audioEof = iShared->iAudioEof;
    TInt waiting = 0;
    for (TInt i = 0; i < KChunks; i++)
        if (iShared->iChunks[i].iState == EItemFull && iShared->iChunks[i].iGen == iShared->iGen)
            waiting++;
    iShared->iLock.Signal();
    if (audioEof && waiting == 0)
        {
        iAudioDone = ETrue;
        const TInt64 at = iClock.Ms();
        iClockRunning = EFalse;
        iDropPending = ETrue;
        iRestart->CallBack();
        if (iState == EPlaying)
            StartSystemClock(at);
        return;
        }
    if (aError == KErrUnderflow)
        iStatUnderruns++;
    else
        {
        TraceNow(_L("softvideo: audio output ended err %d: silent"), aError);
        if (aError == KErrInUse || aError == KErrAccessDenied || aError == KErrDied)
            iAudio = EFalse;
        }
    iClockRunning = EFalse;
    iDropPending = ETrue;
    iRestart->CallBack();
    }

void CSoftVideo::CheckEnd()
    {
    if (iState != EPlaying || !iOpenSeen)
        return;
    const TInt gen = iShared->iGen;
    iShared->iLock.Wait();
    TBool frames = EFalse;
    for (TInt i = 0; i < KFrames; i++)
        if (iShared->iFrames[i].iState == EItemFull && iShared->iFrames[i].iGen == gen)
            frames = ETrue;
    const TBool videoDone = !iShared->iInfo.has_video || (iShared->iVideoEof && !frames);
    const TInt decodeError = iShared->iDecodeError;
    iShared->iLock.Signal();
    if (!videoDone || (iAudio && !iAudioDone))
        return;
    TraceNow(_L("softvideo: ended at %d ms, decoder error %d; shown %d, dropped %d"), (TInt)iClock.Ms(), decodeError,
             iTotalShown, iTotalDropped);
    DropStream();
    iClockRunning = EFalse;
    iState = EEnded;
    iObserver.SoftVideoChanged();
    }

TBool CSoftVideo::Update(CGlRenderer& aGl)
    {
    if (!iOpenSeen || iState == EFailed)
        return EFalse;
    const TInt64 clock = Clock();
    const TInt gen = iShared->iGen;
    TInt index[KFrames];
    TInt64 pts[KFrames];
    TInt count = 0;
    iShared->iLock.Wait();
    for (TInt i = 0; i < KFrames; i++)
        if (iShared->iFrames[i].iState == EItemFull && iShared->iFrames[i].iGen == gen)
            {

            TInt at = count++;
            while (at > 0 && pts[at - 1] > iShared->iFrames[i].iPts)
                {
                pts[at] = pts[at - 1];
                index[at] = index[at - 1];
                at--;
                }
            pts[at] = iShared->iFrames[i].iPts;
            index[at] = i;
            }
    TInt drop = 0, pick;
    if (iClockRunning)
        pick = PickFrame(pts, count, clock, KEarlyMs, drop);
    else
        pick = iShownGen != gen && count > 0 ? 0 : -1;
    for (TInt i = 0; i < drop; i++)
        iShared->iFrames[index[i]].iState = EItemFree;
    if (pick >= 0)
        iShared->iFrames[index[pick]].iState = EItemInUse;
    iShared->iLock.Signal();
    iStatDropped += drop;
    iTotalDropped += drop;
    if (drop)
        iShared->iWake.Signal();
    if (pick < 0)
        return EFalse;
    TFrameSlot& slot = iShared->iFrames[index[pick]];
    if (!iPlanes[0].Valid() || iPlanes[0].iWidth != slot.iLumaW || iPlanes[0].iHeight != slot.iLumaH)
        {
        TBool ok = ETrue;
        for (TInt p = 0; p < 3; p++)
            ok = ok && aGl.CreateTexture(iPlanes[p], p ? slot.iLumaW / 2 : slot.iLumaW,
                                         p ? slot.iLumaH / 2 : slot.iLumaH, TGlTexture::ELuminance, ETrue, ETrue);
        TraceNow(_L("softvideo: textures %dx%d for %dx%d frames: %d; GPU textures %d KB"), slot.iLumaW, slot.iLumaH,
                 slot.iWidth, slot.iHeight, ok, aGl.TextureBytes() / 1024);
        if (!ok)
            for (TInt p = 0; p < 3; p++)
                aGl.DeleteTexture(iPlanes[p]);
        }
    const TInt64 startUs = PerfNowUs();
    if (iPlanes[0].Valid())
        {
        const TInt lumaBytes = slot.iLumaW * slot.iLumaH;
        aGl.Upload(iPlanes[0], 0, 0, slot.iLumaW, slot.iLumaH, slot.iPixels);
        aGl.Upload(iPlanes[1], 0, 0, slot.iLumaW / 2, slot.iLumaH / 2, slot.iPixels + lumaBytes);
        aGl.Upload(iPlanes[2], 0, 0, slot.iLumaW / 2, slot.iLumaH / 2, slot.iPixels + lumaBytes + lumaBytes / 4);
        }
    const TInt uploadUs = (TInt)(PerfNowUs() - startUs);
    iFrameW = slot.iWidth;
    iFrameH = slot.iHeight;
    iShownGen = gen;
    iShownPts = slot.iPts;
    iShared->iLock.Wait();
    slot.iState = EItemFree;
    iShared->iLock.Signal();
    iShared->iWake.Signal();
    iStatShown++;
    iTotalShown++;
    if (iTotalShown == 2 || iTotalShown == 10 || iTotalShown == 60)
        TraceNow(_L("softvideo: %d frames shown"), iTotalShown);
    iStatUploadUs += uploadUs;
    iStatUploadMaxUs = Max(iStatUploadMaxUs, uploadUs);
    if (iClockRunning)
        {
        const TInt drift = (TInt)(clock - iShownPts);
        iStatDriftSum += Abs(drift);
        iStatDriftMax = Max(iStatDriftMax, Abs(drift));
        }
    return ETrue;
    }

void CSoftVideo::Draw(CGlRenderer& aGl, TInt aWidth, TInt aHeight, TReal aAlpha, TReal aZoom, TReal aPanX,
                      TReal aPanY)
    {
    if (!HasPicture() || iFrameW <= 0 || iFrameH <= 0)
        return;

    TInt dw = iFrameW, dh = iFrameH;
    if (iCodedW > 0 && iDisplayW > 0 && iDisplayH > 0)
        {
        dw = iFrameW * iDisplayW / iCodedW;
        dh = (TInt)((TInt64)dw * iDisplayH / iDisplayW);
        }
    TReal x0, y0, x1, y1, uv[8];
    FitVideo(aWidth, aHeight, dw, dh, iRotation, (TReal)iFrameW / iPlanes[0].iWidth,
             (TReal)iFrameH / iPlanes[0].iHeight, x0, y0, x1, y1, uv);
    if (aZoom != 1 || aPanX != 0 || aPanY != 0)
        {
        const TReal cx = aWidth / 2.0, cy = aHeight / 2.0;
        x0 = cx + aPanX + (x0 - cx) * aZoom;
        x1 = cx + aPanX + (x1 - cx) * aZoom;
        y0 = cy + aPanY + (y0 - cy) * aZoom;
        y1 = cy + aPanY + (y1 - cy) * aZoom;
        }
    const TBool drawn = aGl.DrawVideo(iPlanes, x0, y0, x1, y1, uv, aAlpha);
    if (drawn && !iDrawnOnce)
        {
        iDrawnOnce = ETrue;
        TraceNow(_L("softvideo: first frame drawn"));
        }
    if (!drawn)
        {
        TraceNow(_L("softvideo: the video program failed to build"));
        iState = EFailed;
        iError = KErrNotSupported;
        iObserver.SoftVideoChanged();
        }
    }

TSize CSoftVideo::ShownSize() const
    {
    if (iFrameW <= 0 || iFrameH <= 0)
        return TSize();
    TInt dw = iFrameW, dh = iFrameH;
    if (iCodedW > 0 && iDisplayW > 0 && iDisplayH > 0)
        {
        dw = iFrameW * iDisplayW / iCodedW;
        dh = (TInt)((TInt64)dw * iDisplayH / iDisplayW);
        }
    const TInt turn = ((iRotation % 360 + 360) % 360 + 45) / 90 % 4;
    return turn & 1 ? TSize(dh, dw) : TSize(dw, dh);
    }

void CSoftVideo::ReleaseGl(CGlRenderer* aGl)
    {
    for (TInt p = 0; p < 3; p++)
        {
        if (aGl)
            aGl->DeleteTexture(iPlanes[p]);
        iPlanes[p] = TGlTexture();
        }
    iShownGen = -1;
    }

TInt CSoftVideo::WaitUs()
    {
    if (iState != EPlaying || !iClockRunning)
        return -1;
    const TInt64 clock = Clock();
    const TInt gen = iShared->iGen;
    TInt64 next = -1;
    iShared->iLock.Wait();
    for (TInt i = 0; i < KFrames; i++)
        {
        const TFrameSlot& s = iShared->iFrames[i];
        if (s.iState == EItemFull && s.iGen == gen && (next < 0 || s.iPts < next))
            next = s.iPts;
        }
    iShared->iLock.Signal();
    if (next < 0)
        return -1;
    return (TInt)Max((TInt64)0, (next - KEarlyMs - clock) * 1000);
    }

void CSoftVideo::LogStats()
    {
    const TUint32 now = NowMs();
    const TInt span = Max(1, (TInt)(now - iStatStartMs));
    iShared->iLock.Wait();
    const TInt frames = iShared->iStatFrames, frameUs = iShared->iStatFrameUs, frameMax = iShared->iStatFrameMaxUs;
    const TInt chunks = iShared->iStatChunks, chunkUs = iShared->iStatChunkUs, waits = iShared->iStatWaits;
    iShared->iStatFrames = iShared->iStatFrameUs = iShared->iStatFrameMaxUs = 0;
    iShared->iStatChunks = iShared->iStatChunkUs = iShared->iStatWaits = 0;
    iShared->iLock.Signal();
    const TPtrC clockName(iClockAudio ? _L("audio clock") : _L("system clock"));
    if (iState == EPlaying || frames || iStatShown)
        Trace(_L("softvideo: %d ms: decoded %d frames avg %d ms max %d, %d sound chunks avg %d ms, %d waits; shown %d "
                 "(%d.%d fps) dropped %d; upload avg %d us max %d; A/V drift avg %d ms max %d; underruns %d; "
                 "clock %d ms, %S"),
              span, frames, frames ? frameUs / frames / 1000 : 0, frameMax / 1000, chunks,
              chunks ? chunkUs / chunks / 1000 : 0, waits, iStatShown, iStatShown * 1000 / span,
              iStatShown * 10000 / span % 10, iStatDropped, iStatShown ? iStatUploadUs / iStatShown : 0,
              iStatUploadMaxUs, iStatShown ? iStatDriftSum / iStatShown : 0, iStatDriftMax, iStatUnderruns,
              (TInt)iClock.Ms(), &clockName);
    iStatStartMs = now;
    iStatShown = iStatDropped = iStatUploadUs = iStatUploadMaxUs = 0;
    iStatDriftSum = iStatDriftMax = iStatUnderruns = 0;
    }
