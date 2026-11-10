#include "n8mlog.h"

#include "n8media.h"
#include "trace.h"

namespace
    {
    const TInt KLines = 24;
    const TInt KLineLength = 120;
    const TInt KMaxLevel = 1;

    struct TN8mLog
        {
        RMutex iLock;
        TBuf8<KLineLength> iLines[KLines];
        TInt iLevels[KLines];
        TInt iCount;
        TInt iDropped;
        };
    TN8mLog* gN8mLog = NULL;

    void* N8mMalloc(size_t aSize)
        {
        return User::Alloc(aSize);
        }

    void* N8mRealloc(void* aPtr, size_t aSize)
        {
        return User::ReAlloc(aPtr, aSize);
        }

    void N8mFree(void* aPtr)
        {
        User::Free(aPtr);
        }

    void N8mLogLine(int aLevel, const char* aMessage)
        {
        TN8mLog* log = gN8mLog;
        if (!log || aLevel > KMaxLevel || !aMessage)
            return;
        TPtrC8 text(reinterpret_cast<const TUint8*>(aMessage), User::StringLength(
            reinterpret_cast<const TUint8*>(aMessage)));
        while (text.Length() && (text[text.Length() - 1] == '\n' || text[text.Length() - 1] == '\r'))
            text.Set(text.Left(text.Length() - 1));
        log->iLock.Wait();
        if (log->iCount < KLines)
            {
            log->iLines[log->iCount].Copy(text.Left(KLineLength));
            log->iLevels[log->iCount++] = aLevel;
            }
        else
            log->iDropped++;
        log->iLock.Signal();
        }
    }

void N8mInstallL()
    {
    TN8mLog* log = new (ELeave) TN8mLog;
    log->iCount = log->iDropped = 0;
    TInt err = log->iLock.CreateLocal();
    if (err != KErrNone)
        {
        delete log;
        User::Leave(err);
        }
    gN8mLog = log;
    n8m_set_allocator(N8mMalloc, N8mRealloc, N8mFree);
    n8m_set_log(N8mLogLine);
    }

void N8mUninstall()
    {
    n8m_set_log(NULL);
    N8mLogDrain();
    if (gN8mLog)
        {
        gN8mLog->iLock.Close();
        delete gN8mLog;
        gN8mLog = NULL;
        }
    }

void N8mLogDrain()
    {
    TN8mLog* log = gN8mLog;
    if (!log || (log->iCount == 0 && log->iDropped == 0))
        return;
    TBuf<KLineLength> line;
    log->iLock.Wait();
    for (TInt i = 0; i < log->iCount; i++)
        {
        line.Copy(log->iLines[i]);
        Trace(_L("n8media: %s %S"), log->iLevels[i] == 0 ? _S("error") : _S("warning"), &line);
        }
    if (log->iDropped)
        Trace(_L("n8media: %d more lines dropped"), log->iDropped);
    log->iCount = log->iDropped = 0;
    log->iLock.Signal();
    }
