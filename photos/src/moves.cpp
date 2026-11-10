#include "moves.h"

#include <f32file.h>
#include <s32file.h>

namespace
    {
    const TInt32 KMovesMagic = 0x31564D50;

    void DriveRoot(const TDesC& aPath, TDes& aRoot)
        {
        aRoot.Copy(aPath.Left(2));
        if (aRoot.CompareF(_L("C:")) == 0)
            aRoot.Append(_L("\\Data"));
        }
    }

CMoves* CMoves::NewL(const TDesC& aFile)
    {
    CMoves* self = new (ELeave) CMoves;
    self->iFile.Copy(aFile.Left(KMaxFileName));
    return self;
    }

CMoves::~CMoves()
    {
    Reset();
    }

void CMoves::Reset()
    {
    for (TInt i = 0; i < iEntries.Count(); i++)
        {
        delete iEntries[i].iPath;
        delete iEntries[i].iOrigin;
        }
    iEntries.Close();
    }

void CMoves::Load(RFs& aFs)
    {
    TRAPD(err, LoadL(aFs));
    if (err != KErrNone)
        Reset();
    }

void CMoves::LoadL(RFs& aFs)
    {
    RFileReadStream in;
    if (in.Open(aFs, iFile, EFileRead | EFileShareReadersOnly) != KErrNone)
        return;
    CleanupClosePushL(in);
    if (in.ReadInt32L() != KMovesMagic)
        User::Leave(KErrCorrupt);
    const TInt count = in.ReadInt32L();
    for (TInt i = 0; i < count && i < 100000; i++)
        {
        HBufC* path = HBufC::NewLC(in, KMaxFileName);
        HBufC* origin = HBufC::NewLC(in, KMaxFileName);
        const TInt high = in.ReadInt32L();
        const TInt64 when = MAKE_TINT64(high, in.ReadUint32L());
        TMove entry;
        entry.iPath = path;
        entry.iOrigin = origin;
        entry.iWhen = when;
        iEntries.AppendL(entry);
        CleanupStack::Pop(2, path);
        }
    CleanupStack::PopAndDestroy(&in);
    }

void CMoves::SaveL(RFs& aFs) const
    {
    RFileWriteStream out;
    User::LeaveIfError(out.Replace(aFs, iFile, EFileWrite));
    CleanupClosePushL(out);
    out.WriteInt32L(KMovesMagic);
    out.WriteInt32L(iEntries.Count());
    for (TInt i = 0; i < iEntries.Count(); i++)
        {
        out << *iEntries[i].iPath;
        out << *iEntries[i].iOrigin;
        out.WriteInt32L(I64HIGH(iEntries[i].iWhen));
        out.WriteUint32L(I64LOW(iEntries[i].iWhen));
        }
    out.CommitL();
    CleanupStack::PopAndDestroy(&out);
    }

TInt CMoves::Find(const TDesC& aPath) const
    {
    for (TInt i = 0; i < iEntries.Count(); i++)
        if (iEntries[i].iPath->CompareF(aPath) == 0)
            return i;
    return KErrNotFound;
    }

void CMoves::SetL(const TDesC& aPath, const TDesC& aOrigin, const TTime& aWhen)
    {
    HBufC* path = aPath.AllocLC();
    HBufC* origin = aOrigin.AllocLC();
    Remove(aPath);
    TMove entry;
    entry.iPath = path;
    entry.iOrigin = origin;
    entry.iWhen = aWhen.Int64();
    iEntries.AppendL(entry);
    CleanupStack::Pop(2, path);
    }

void CMoves::Remove(const TDesC& aPath)
    {
    const TInt at = Find(aPath);
    if (at < 0)
        return;
    delete iEntries[at].iPath;
    delete iEntries[at].iOrigin;
    iEntries.Remove(at);
    }

void CMoves::HiddenFolder(const TDesC& aPath, TDes& aFolder)
    {
    DriveRoot(aPath, aFolder);
    aFolder.Append(_L("\\Images\\Hidden\\"));
    }

void CMoves::ImagesFolder(const TDesC& aPath, TDes& aFolder)
    {
    DriveRoot(aPath, aFolder);
    aFolder.Append(_L("\\Images\\"));
    }

void CMoves::TrashFolder(const TDesC& aPath, TDes& aFolder)
    {

    aFolder.Copy(aPath.Left(2));
    aFolder.Append(_L("\\private\\E4E47003\\trash\\"));
    }

TInt CMoves::MoveInto(RFs& aFs, const TDesC& aFrom, const TDesC& aFolder, TDes& aTo)
    {
    TInt err = aFs.MkDirAll(aFolder);
    if (err != KErrNone && err != KErrAlreadyExists)
        return err;
    TParsePtrC parse(aFrom);
    for (TInt n = 1; n < 100; n++)
        {
        aTo.Copy(aFolder);
        const TInt room = aTo.MaxLength() - aTo.Length() - parse.Ext().Length() - 6;
        if (room <= 0)
            return KErrBadName;
        aTo.Append(parse.Name().Left(room));
        if (n > 1)
            {
            aTo.Append(_L(" ("));
            aTo.AppendNum(n);
            aTo.Append(')');
            }
        aTo.Append(parse.Ext());
        err = aFs.Rename(aFrom, aTo);
        if (err != KErrAlreadyExists)
            return err;
        }
    return KErrAlreadyExists;
    }
