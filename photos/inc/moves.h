#ifndef MOVES_H
#define MOVES_H

#include <e32base.h>

class RFs;

class CMoves : public CBase
    {
public:
    enum { KTrashDays = 30 };
    static CMoves* NewL(const TDesC& aFile);
    ~CMoves();
    void Load(RFs& aFs);
    void SaveL(RFs& aFs) const;

    TInt Count() const { return iEntries.Count(); }

    TInt Find(const TDesC& aPath) const;
    const TDesC& Path(TInt aIndex) const { return *iEntries[aIndex].iPath; }
    const TDesC& Origin(TInt aIndex) const { return *iEntries[aIndex].iOrigin; }
    TTime When(TInt aIndex) const { return TTime(iEntries[aIndex].iWhen); }

    void SetL(const TDesC& aPath, const TDesC& aOrigin, const TTime& aWhen);
    void Remove(const TDesC& aPath);

    static void HiddenFolder(const TDesC& aPath, TDes& aFolder);
    static void ImagesFolder(const TDesC& aPath, TDes& aFolder);
    static void TrashFolder(const TDesC& aPath, TDes& aFolder);

    static TInt MoveInto(RFs& aFs, const TDesC& aFrom, const TDesC& aFolder, TDes& aTo);

private:
    class TMove
        {
    public:
        HBufC* iPath;
        HBufC* iOrigin;
        TInt64 iWhen;
        };
    void LoadL(RFs& aFs);
    void Reset();

    TFileName iFile;
    RArray<TMove> iEntries;
    };

#endif
