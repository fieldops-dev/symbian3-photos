#ifndef SCANNER_H
#define SCANNER_H

#include "worker.h"

class CLibrary;

class MScanObserver
    {
public:

    virtual void LibraryScanned(CLibrary* aLibrary, TBool aFinal) = 0;
    };

class CScanner : public CWorker
    {
public:

    static CScanner* NewL(MScanObserver& aObserver, const TDesC& aIndexFile);
    ~CScanner();
    void Start(const CLibrary* aOld);
    void Continue();

    class TShared;

private:
    CScanner(MScanObserver& aObserver, TShared* aShared);
    void RunL();

    MScanObserver& iObserver;
    TShared* iShared;
    };

#endif
