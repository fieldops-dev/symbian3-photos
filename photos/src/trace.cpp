#include "trace.h"

#include <f32file.h>
#include <hal.h>
#include <utf.h>

namespace
    {
    _LIT(KCardLog, "E:\\photos-log.txt");
    _LIT(KPhoneLog, "C:\\Data\\photos-log.txt");
    TInt64 gStartUs = 0;

    struct TLog
        {
        RFs iFs;
        RFile iFile;
        TBool iOpen;
        TBool iBuffered;
        TBuf8<4096> iPending;
        };
    TLog* gLog = NULL;

    void FlushPending()
        {
        if (gLog && gLog->iOpen && gLog->iPending.Length())
            {
            gLog->iFile.Write(gLog->iPending);
            gLog->iPending.Zero();
            }
        }

    void Write(const TDesC& aLine)
        {
        if (!gLog || !gLog->iOpen)
            return;
        TBuf8<480> utf8;
        CnvUtfConverter::ConvertFromUnicodeToUtf8(utf8, aLine.Left(460));
        utf8.Append(_L8("\r\n"));
        if (!gLog->iBuffered)
            {
            gLog->iFile.Write(utf8);
            return;
            }
        if (gLog->iPending.Length() + utf8.Length() > gLog->iPending.MaxLength())
            FlushPending();
        gLog->iPending.Append(utf8);
        }
    }

TInt64 PerfNowUs()
    {
    static TInt freq = 0;
    if (freq == 0 && (HAL::Get(HALData::EFastCounterFrequency, freq) != KErrNone || freq <= 0))
        freq = 1000000;

    static TUint32 last = 0;
    static TInt64 high = 0;
    TUint32 now = User::FastCounter();
    if (now < last)
        high += (TInt64)1 << 32;
    last = now;
    return (high + now) * 1000000 / freq;
    }

TInt PerfCounterUs(TUint32 aFrom, TUint32 aTo)
    {
    TInt freq = 0;
    if (HAL::Get(HALData::EFastCounterFrequency, freq) != KErrNone || freq <= 0)
        freq = 1000000;
    return (TInt)((TInt64)(TUint32)(aTo - aFrom) * 1000000 / freq);
    }

void TraceReset()
    {
    gStartUs = PerfNowUs();
    if (!gLog)
        {

        gLog = new TLog;
        if (gLog)
            gLog->iOpen = EFalse;
        }
    if (!gLog)
        return;
    gLog->iBuffered = EFalse;
    gLog->iPending.Zero();
    if (!gLog->iOpen && gLog->iFs.Connect() == KErrNone)
        {
        gLog->iOpen = gLog->iFile.Replace(gLog->iFs, KCardLog, EFileWrite | EFileShareAny) == KErrNone ||
                      gLog->iFile.Replace(gLog->iFs, KPhoneLog, EFileWrite | EFileShareAny) == KErrNone;
        if (!gLog->iOpen)
            gLog->iFs.Close();
        }
    TTime now;
    now.HomeTime();
    TDateTime dt = now.DateTime();
    TBuf<64> line;
    line.Format(_L("photos log %04d-%02d-%02d %02d:%02d:%02d"), dt.Year(), dt.Month() + 1, dt.Day() + 1, dt.Hour(),
                dt.Minute(), dt.Second());
    Write(line);
    }

void TraceBuffered()
    {
    if (gLog)
        gLog->iBuffered = ETrue;
    }

void TraceFlush()
    {
    FlushPending();
    }

void TraceClose()
    {
    if (!gLog)
        return;
    FlushPending();
    if (gLog->iOpen)
        {
        gLog->iFile.Close();
        gLog->iFs.Close();
        }
    delete gLog;
    gLog = NULL;
    }

namespace
    {
    void TraceList(TRefByValue<const TDesC> aFormat, VA_LIST aList)
        {
        TBuf<460> line;
        TInt64 us = PerfNowUs() - gStartUs;
        line.Format(_L("%6d.%d "), (TInt)(us / 1000), (TInt)(us / 100 % 10));
        TBuf<440> text;
        text.FormatList(aFormat, aList);
        line.Append(text.Left(line.MaxLength() - line.Length()));
        Write(line);
        }
    }

void Trace(TRefByValue<const TDesC> aFormat, ...)
    {
    VA_LIST list;
    VA_START(list, aFormat);
    TraceList(aFormat, list);
    VA_END(list);
    }

void TraceNow(TRefByValue<const TDesC> aFormat, ...)
    {
    VA_LIST list;
    VA_START(list, aFormat);
    TraceList(aFormat, list);
    VA_END(list);
    FlushPending();
    }

void TraceMemory(const TDesC& aWhere, TInt aGpuBytes)
    {
    TInt used = 0;
    TInt cells = User::AllocSize(used);
    TInt heap = User::Heap().Size();
    TInt ramFree = 0, ramTotal = 0;
    HAL::Get(HALData::EMemoryRAMFree, ramFree);
    HAL::Get(HALData::EMemoryRAM, ramTotal);
    Trace(_L("mem: %S heap %d KB, used %d KB in %d cells, gpu ~%d KB, phone RAM free %d of %d MB"), &aWhere,
          heap / 1024, used / 1024, cells, aGpuBytes / 1024, ramFree / (1024 * 1024), ramTotal / (1024 * 1024));
    }
