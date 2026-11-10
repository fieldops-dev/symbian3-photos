#ifndef WORKER_H
#define WORKER_H

#include <e32base.h>

class TWorkerLink
    {
public:

    TBool WaitForJob();
    void JobDone(TInt aResult);

    RSemaphore iWork;
    TThreadId iUiThread;
    TRequestStatus* iDone;
    volatile TBool iQuit;

    TBuf<1024> iLog;
    void Log(TRefByValue<const TDesC> aFormat, ...);
    };

class CWorker : public CActive
    {
public:
    ~CWorker();
    TBool Busy() const { return IsActive(); }

protected:
    CWorker(TWorkerLink& aLink, TThreadFunction aFunction, TAny* aShared, const TDesC& aName);

    TInt Submit();

    void WriteLog();

    void Shutdown();

private:
    class CDeathWatch;
    TInt StartThread();
    void ThreadDied();
    void DoCancel();
    TBool Alive() const;

    TWorkerLink& iLink;
    TThreadFunction iFunction;
    TAny* iShared;
    TBuf<16> iName;
    RThread iThread;
    TBool iThreadMade;
    CDeathWatch* iDeathWatch;
    TBool iShutDown;
    };

TInt RunWorkerThread(void (*aBody)(TAny*), TAny* aShared);

#endif
