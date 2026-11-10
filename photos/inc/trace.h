#ifndef TRACE_H
#define TRACE_H

#include <e32std.h>

void TraceReset();
void Trace(TRefByValue<const TDesC> aFormat, ...);

void TraceNow(TRefByValue<const TDesC> aFormat, ...);

void TraceBuffered();

void TraceFlush();
void TraceClose();

TInt64 PerfNowUs();

TInt PerfCounterUs(TUint32 aFrom, TUint32 aTo);

void TraceMemory(const TDesC& aWhere, TInt aGpuBytes = 0);

#endif
