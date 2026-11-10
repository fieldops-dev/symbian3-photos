#include "worker.h"

#include "trace.h"

namespace
    {

    const TInt KWorkerStack = 0x40000;
    const TInt KQuitWaitUs = 3000000;
    }

TBool TWorkerLink::WaitForJob()
    {
    iWork.Wait();
    return !iQuit;
    }

void TWorkerLink::JobDone(TInt aResult)
    {
    RThread ui;
    if (ui.Open(iUiThread) == KErrNone)
        {
        ui.RequestComplete(iDone, aResult);
        ui.Close();
        }
    }

void TWorkerLink::Log(TRefByValue<const TDesC> aFormat, ...)
    {
    VA_LIST list;
    VA_START(list, aFormat);
    TBuf<200> line;
    line.FormatList(aFormat, list);
    VA_END(list);
    const TInt room = iLog.MaxLength() - iLog.Length();
    if (room > 2)
        {
        iLog.Append(line.Left(room - 1));
        iLog.Append('\n');
        }
    }

TInt RunWorkerThread(void (*aBody)(TAny*), TAny* aShared)
    {
    CTrapCleanup* cleanup = CTrapCleanup::New();
    if (!cleanup)
        return KErrNoMemory;
    CActiveScheduler* scheduler = new CActiveScheduler;
    if (!scheduler)
        {
        delete cleanup;
        return KErrNoMemory;
        }
    CActiveScheduler::Install(scheduler);
    aBody(aShared);
    delete scheduler;
    delete cleanup;
    return KErrNone;
    }

class CWorker::CDeathWatch : public CActive
    {
public:
    CDeathWatch(CWorker& aOwner) : CActive(EPriorityHigh), iOwner(aOwner) { CActiveScheduler::Add(this); }
    ~CDeathWatch() { Cancel(); }
    void Watch(RThread& aThread)
        {
        iThread = &aThread;
        aThread.Logon(iStatus);
        SetActive();
        }

private:
    void RunL() { iOwner.ThreadDied(); }
    void DoCancel() { iThread->LogonCancel(iStatus); }
    CWorker& iOwner;
    RThread* iThread;
    };

CWorker::CWorker(TWorkerLink& aLink, TThreadFunction aFunction, TAny* aShared, const TDesC& aName)
    : CActive(EPriorityStandard), iLink(aLink), iFunction(aFunction), iShared(aShared), iName(aName.Left(16))
    {
    CActiveScheduler::Add(this);
    }

CWorker::~CWorker()
    {
    }

void CWorker::Shutdown()
    {
    if (iShutDown)
        return;
    iShutDown = ETrue;
    iLink.iQuit = ETrue;
    Cancel();
    delete iDeathWatch;
    iDeathWatch = NULL;
    if (iThreadMade)
        {
        if (Alive())
            {
            TRequestStatus exited;
            iThread.Logon(exited);
            iLink.iWork.Signal();
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
                    iThread.Kill(KErrCancel);
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
            }
        iThread.Close();
        iThreadMade = EFalse;
        }
    iLink.iWork.Close();
    }

TBool CWorker::Alive() const
    {
    return iThreadMade && const_cast<RThread&>(iThread).ExitType() == EExitPending;
    }

TInt CWorker::StartThread()
    {
    if (iDeathWatch)
        iDeathWatch->Cancel();
    if (iThreadMade)
        {
        iThread.Close();
        iThreadMade = EFalse;
        }
    if (iLink.iWork.Handle() == 0)
        {
        TInt err = iLink.iWork.CreateLocal(0);
        if (err != KErrNone)
            return err;
        }
    if (!iDeathWatch)
        {
        iDeathWatch = new CDeathWatch(*this);
        if (!iDeathWatch)
            return KErrNoMemory;
        }
    iLink.iUiThread = RThread().Id();
    iLink.iQuit = EFalse;
    TBuf<32> name(iName);
    name.AppendNum((TUint)User::NTickCount());

    TInt err = iThread.Create(name, iFunction, KWorkerStack, &User::Allocator(), iShared);
    if (err != KErrNone)
        return err;
    iThreadMade = ETrue;
    iThread.SetPriority(EPriorityLess);
    iDeathWatch->Watch(iThread);
    iThread.Resume();
    return KErrNone;
    }

TInt CWorker::Submit()
    {
    if (IsActive())
        return KErrInUse;
    if (!Alive())
        {
        TInt err = StartThread();
        if (err != KErrNone)
            return err;
        }
    iLink.iLog.Zero();
    iLink.iDone = &iStatus;
    iStatus = KRequestPending;
    SetActive();
    iLink.iWork.Signal();
    return KErrNone;
    }

void CWorker::WriteLog()
    {
    TPtrC lines(iLink.iLog);
    while (lines.Length())
        {
        const TInt end = lines.Locate('\n');
        TPtrC line(end < 0 ? lines : lines.Left(end));
        Trace(_L("%S: %S"), &iName, &line);
        lines.Set(end < 0 ? TPtrC() : lines.Mid(end + 1));
        }
    iLink.iLog.Zero();
    }

void CWorker::ThreadDied()
    {
    TExitCategoryName category = iThread.ExitCategory();
    Trace(_L("%S: thread ended, type %d reason %d %S"), &iName, iThread.ExitType(), iThread.ExitReason(), &category);
    if (IsActive() && iStatus == KRequestPending)
        {
        TRequestStatus* status = &iStatus;
        User::RequestComplete(status, KErrDied);
        }
    }

void CWorker::DoCancel()
    {
    RTimer timer;
    if (iShutDown && Alive() && timer.CreateLocal() == KErrNone)
        {
        TRequestStatus timeout;
        timer.After(timeout, KQuitWaitUs);
        User::WaitForRequest(iStatus, timeout);
        if (iStatus == KRequestPending)
            {
            iThread.Kill(KErrCancel);
            TRequestStatus* status = &iStatus;
            User::RequestComplete(status, KErrCancel);
            }
        else
            {
            timer.Cancel();
            User::WaitForRequest(timeout);
            }
        timer.Close();
        User::WaitForRequest(iStatus);
        return;
        }
    if (Alive())
        {
        TRequestStatus exited;
        iThread.Logon(exited);
        User::WaitForRequest(iStatus, exited);
        if (iStatus != KRequestPending)
            {
            iThread.LogonCancel(exited);
            User::WaitForRequest(exited);
            return;
            }
        }
    if (iStatus == KRequestPending)
        {
        TRequestStatus* status = &iStatus;
        User::RequestComplete(status, KErrCancel);
        }
    User::WaitForRequest(iStatus);
    }
