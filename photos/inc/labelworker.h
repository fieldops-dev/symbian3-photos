#ifndef LABELWORKER_H
#define LABELWORKER_H

#include <gdi.h>

#include "worker.h"

class TLabelJob
    {
public:
    enum { KMaxChars = 120 };

    TInt iTag;
    TInt iFont;
    TFontSpec iSpec;
    TBuf<KMaxChars> iText;
    TInt16 iPen[KMaxChars];
    TInt iPad;
    TInt iBaseline;
    TSize iSize;

    TInt iResult;
    TUint8* iPixels;
    TInt iStride;
    TInt iMicros;
    };

class MLabelObserver
    {
public:
    virtual void LabelReady(TLabelJob& aJob) = 0;
    };

class CLabelWorker : public CWorker
    {
public:
    enum { KMaxFiles = 8, KMaxFonts = 16 };
    static CLabelWorker* NewL(MLabelObserver& aObserver, const TDesC* const* aFiles, TInt aFileCount);
    ~CLabelWorker();
    TInt Request(const TLabelJob& aJob);

    class TShared;

private:
    CLabelWorker(MLabelObserver& aObserver, TShared* aShared);
    void RunL();

    MLabelObserver& iObserver;
    TShared* iShared;
    };

#endif
