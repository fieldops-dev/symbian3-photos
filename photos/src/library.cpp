#include "library.h"

#include <badesca.h>
#include <f32file.h>
#include <s32file.h>

namespace
    {
    const TInt32 KIndexMagic = 0x31494850;
    const TInt KIndexVersion = 2;
    const TInt KMaxNameLength = 40;

    TInt CompareTaken(const TPhoto& aA, const TPhoto& aB)
        {
        if (aA.iTaken != aB.iTaken)
            return aA.iTaken < aB.iTaken ? -1 : 1;
        return aA.iPath->CompareF(*aB.iPath);
        }

    TBool IsDigits(const TDesC& aText, TInt aCount)
        {
        if (aText.Length() < aCount)
            return EFalse;
        for (TInt i = 0; i < aCount; i++)
            if (aText[i] < '0' || aText[i] > '9')
                return EFalse;
        return ETrue;
        }

    const TText* const KHiddenPatterns[] = {
        _S("?:\\dcim\\hidden\\*"), _S("?:\\dcim\\*\\hidden\\*"), _S("?:\\images\\hidden\\*"),
        _S("?:\\images\\*\\hidden\\*"), _S("?:\\data\\dcim\\hidden\\*"), _S("?:\\data\\dcim\\*\\hidden\\*"),
        _S("?:\\data\\images\\hidden\\*"), _S("?:\\data\\images\\*\\hidden\\*") };
    const TText* const KVideoExtensions[] = {
        _S(".mp4"), _S(".m4v"), _S(".3gp"), _S(".3g2"), _S(".mov"), _S(".avi"), _S(".wmv"), _S(".flv"), _S(".mkv") };
    }

TBool CLibrary::IsHiddenPath(const TDesC& aPath)
    {
    for (TUint i = 0; i < sizeof(KHiddenPatterns) / sizeof(KHiddenPatterns[0]); i++)
        if (aPath.MatchF(TPtrC(KHiddenPatterns[i])) >= 0)
            return ETrue;
    return EFalse;
    }

TBool CLibrary::IsTrashPath(const TDesC& aPath)
    {

    _LIT(KTrash, "?:\\private\\e4e47003\\trash\\*");
    return aPath.MatchF(KTrash) >= 0;
    }

TBool CLibrary::IsVideoPath(const TDesC& aPath)
    {
    TParsePtrC parse(aPath);
    for (TUint i = 0; i < sizeof(KVideoExtensions) / sizeof(KVideoExtensions[0]); i++)
        if (parse.Ext().CompareF(TPtrC(KVideoExtensions[i])) == 0)
            return ETrue;
    return EFalse;
    }

TInt CLibrary::AlbumOfKind(CAlbum::TKind aKind) const
    {
    for (TInt a = 0; a < iAlbums.Count(); a++)
        if (iAlbums[a]->iKind == aKind)
            return a;
    return -1;
    }

CAlbum::~CAlbum()
    {
    delete iName;
    iPhotos.Close();
    }

CLibrary* CLibrary::NewL()
    {
    return new (ELeave) CLibrary;
    }

CLibrary::~CLibrary()
    {
    for (TInt i = 0; i < iPhotos.Count(); i++)
        delete iPhotos[i].iPath;
    iPhotos.Close();
    iAlbums.ResetAndDestroy();
    iByPath.Close();
    iVisible.Close();
    }

void CLibrary::AppendL(const TPhoto& aPhoto)
    {
    TInt err = iPhotos.Append(aPhoto);
    if (err != KErrNone)
        {
        delete aPhoto.iPath;
        User::Leave(err);
        }
    }

void CLibrary::FinishL()
    {
    iPhotos.Sort(TLinearOrder<TPhoto>(CompareTaken));
    for (TInt i = 0; i < iPhotos.Count(); i++)
        {
        TPhoto& photo = iPhotos[i];
        const TDesC& path = *photo.iPath;
        photo.iFlags &= ~(TPhoto::EVideo | TPhoto::EHidden | TPhoto::ETrashed);
        if (IsVideoPath(path))
            photo.iFlags |= TPhoto::EVideo;
        if (IsTrashPath(path))
            photo.iFlags |= TPhoto::ETrashed;
        else if (IsHiddenPath(path))
            photo.iFlags |= TPhoto::EHidden;
        }
    iByPath.Reset();
    iByPath.ReserveL(iPhotos.Count());
    for (TInt i = 0; i < iPhotos.Count(); i++)
        iByPath.AppendL(i);

    for (TInt i = 1; i < iByPath.Count(); i++)
        {
        const TInt item = iByPath[i];
        const TDesC& path = *iPhotos[item].iPath;
        TInt lo = 0, hi = i;
        while (lo < hi)
            {
            const TInt mid = (lo + hi) / 2;
            if (iPhotos[iByPath[mid]].iPath->CompareF(path) <= 0)
                lo = mid + 1;
            else
                hi = mid;
            }
        for (TInt k = i; k > lo; k--)
            iByPath[k] = iByPath[k - 1];
        iByPath[lo] = item;
        }
    GroupAlbumsL();
    }

TInt CLibrary::Find(const TDesC& aPath) const
    {
    TInt lo = 0, hi = iByPath.Count();
    while (lo < hi)
        {
        const TInt mid = (lo + hi) / 2;
        const TInt order = iPhotos[iByPath[mid]].iPath->CompareF(aPath);
        if (order == 0)
            return iByPath[mid];
        if (order < 0)
            lo = mid + 1;
        else
            hi = mid;
        }
    return KErrNotFound;
    }

void CLibrary::AlbumName(const TDesC& aPath, TDes& aName, CAlbum::TKind& aKind)
    {
    TParsePtrC parse(aPath);
    TPtrC dir = parse.Path();
    TPtrC folder = dir.Left(Max(0, dir.Length() - 1));
    folder.Set(folder.Mid(folder.LocateReverse('\\') + 1));

    _LIT(KDcim, "*\\DCIM\\*");
    _LIT(KCamera, "camera");
    if (dir.MatchF(KDcim) >= 0 || IsDigits(folder, 6) || folder.CompareF(KCamera) == 0)
        {
        aKind = CAlbum::ECamera;
        aName.Copy(_L("Camera"));
        return;
        }
    _LIT(KScreenshot, "screen*shot*");
    if (folder.MatchF(KScreenshot) >= 0)
        {
        aKind = CAlbum::EScreenshots;
        aName.Copy(_L("Screenshots"));
        return;
        }
    aKind = CAlbum::EFolder;
    if (folder.Length() == 0)
        aName.Copy(parse.Drive());
    else
        aName.Copy(folder.Left(aName.MaxLength()));
    }

void CLibrary::GroupAlbumsL()
    {
    iAlbums.ResetAndDestroy();
    iVisible.Reset();
    TBuf<KMaxNameLength> name;
    const TInt KAway = TPhoto::EHidden | TPhoto::ETrashed;
    for (TInt i = 0; i < iPhotos.Count(); i++)
        {

        if (iPhotos[i].iFlags & KAway)
            continue;
        iVisible.AppendL(i);
        CAlbum::TKind kind;
        AlbumName(*iPhotos[i].iPath, name, kind);
        TInt album = KErrNotFound;
        for (TInt k = 0; k < iAlbums.Count() && album < 0; k++)
            if (iAlbums[k]->iKind == kind && iAlbums[k]->iName->CompareF(name) == 0)
                album = k;
        if (album < 0)
            {
            CAlbum* made = new (ELeave) CAlbum;
            CleanupStack::PushL(made);
            made->iName = name.AllocL();
            made->iKind = kind;
            iAlbums.AppendL(made);
            CleanupStack::Pop(made);
            album = iAlbums.Count() - 1;
            }
        iAlbums[album]->iPhotos.AppendL(i);
        }

    for (TInt i = 1; i < iAlbums.Count(); i++)
        for (TInt j = i; j > 0; j--)
            {
            CAlbum* a = iAlbums[j - 1];
            CAlbum* b = iAlbums[j];
            const TBool after = a->iKind != b->iKind ? a->iKind > b->iKind : a->iName->CompareC(*b->iName) > 0;
            if (!after)
                break;
            iAlbums[j - 1] = b;
            iAlbums[j] = a;
            }
    for (TInt k = 0; k < iAlbums.Count(); k++)
        for (TInt j = 0; j < iAlbums[k]->iPhotos.Count(); j++)
            iPhotos[iAlbums[k]->iPhotos[j]].iAlbum = k;
    if (iFavourites)
        {

        CAlbum* favourites = new (ELeave) CAlbum;
        CleanupStack::PushL(favourites);
        favourites->iName = _L("Favourites").AllocL();
        favourites->iKind = CAlbum::EFavourites;
        for (TInt i = 0; i < iPhotos.Count(); i++)
            if ((iPhotos[i].iFlags & TPhoto::EFavourite) && !(iPhotos[i].iFlags & KAway))
                favourites->iPhotos.AppendL(i);
        iAlbums.InsertL(favourites, 0);
        CleanupStack::Pop(favourites);
        for (TInt i = 0; i < iPhotos.Count(); i++)
            iPhotos[i].iAlbum++;
        }
    iCoverAlbums = iAlbums.Count();

    const CAlbum::TKind kinds[3] = { CAlbum::EVideos, CAlbum::EHidden, CAlbum::ERecentlyDeleted };
    const TText* const names[3] = { _S("Videos"), _S("Hidden"), _S("Recently Deleted") };
    for (TInt k = 0; k < 3; k++)
        {
        CAlbum* list = new (ELeave) CAlbum;
        CleanupStack::PushL(list);
        list->iName = TPtrC(names[k]).AllocL();
        list->iKind = kinds[k];
        for (TInt i = 0; i < iPhotos.Count(); i++)
            {
            const TInt flags = iPhotos[i].iFlags;
            const TBool in = kinds[k] == CAlbum::EVideos ? (flags & TPhoto::EVideo) && !(flags & KAway) :
                             (kinds[k] == CAlbum::EHidden ? (flags & TPhoto::EHidden) && !(flags & TPhoto::ETrashed) :
                                                            (flags & TPhoto::ETrashed) != 0);
            if (in)
                list->iPhotos.AppendL(i);
            }
        iAlbums.AppendL(list);
        CleanupStack::Pop(list);
        }
    }

void CLibrary::SetFavouritesL(const CDesC16Array& aPaths)
    {
    for (TInt i = 0; i < iPhotos.Count(); i++)
        {
        TInt at;
        if (aPaths.FindIsq(*iPhotos[i].iPath, at, ECmpFolded) == 0)
            iPhotos[i].iFlags |= TPhoto::EFavourite;
        else
            iPhotos[i].iFlags &= ~TPhoto::EFavourite;
        }
    iFavourites = ETrue;
    GroupAlbumsL();
    }

CLibrary* CLibrary::CopyL(const RArray<TInt>& aPhotos, const CDesC16Array& aNewPaths) const
    {
    CLibrary* copy = CLibrary::NewL();
    CleanupStack::PushL(copy);
    copy->iPhotos.ReserveL(iPhotos.Count());
    for (TInt i = 0; i < iPhotos.Count(); i++)
        {
        const TInt change = aPhotos.Find(i);
        TPhoto photo = iPhotos[i];
        if (change < 0)
            photo.iPath = iPhotos[i].iPath->AllocL();
        else if (aNewPaths[change].Length() == 0)
            continue;
        else
            photo.iPath = aNewPaths[change].AllocL();
        copy->AppendL(photo);
        }
    copy->iNextThumb = iNextThumb;
    copy->iThumbGeneration = iThumbGeneration;
    copy->iFavourites = iFavourites;
    copy->FinishL();
    CleanupStack::Pop(copy);
    return copy;
    }

CLibrary* CLibrary::CopyL(TInt aSkip, TInt aRenamed, const TDesC& aNewPath) const
    {
    CLibrary* copy = CLibrary::NewL();
    CleanupStack::PushL(copy);
    copy->iPhotos.ReserveL(iPhotos.Count());
    for (TInt i = 0; i < iPhotos.Count(); i++)
        {
        if (i == aSkip)
            continue;

        const TDesC& oldPath = *iPhotos[i].iPath;
        const TDesC& path = i == aRenamed ? aNewPath : oldPath;
        TPhoto photo = iPhotos[i];
        photo.iPath = path.AllocL();
        copy->AppendL(photo);
        }
    copy->iNextThumb = iNextThumb;
    copy->iThumbGeneration = iThumbGeneration;
    copy->iFavourites = iFavourites;
    copy->FinishL();
    CleanupStack::Pop(copy);
    return copy;
    }

void CLibrary::SaveL(RFs& aFs, const TDesC& aFile) const
    {

    TFileName temp(aFile);
    temp.Append(_L(".new"));
    RFileWriteStream out;
    User::LeaveIfError(out.Replace(aFs, temp, EFileWrite));
    CleanupClosePushL(out);
    out.WriteInt32L(KIndexMagic);
    out.WriteInt32L(KIndexVersion);
    out.WriteInt32L(iNextThumb);
    out.WriteInt32L(iThumbGeneration);
    out.WriteInt32L(iPhotos.Count());
    for (TInt i = 0; i < iPhotos.Count(); i++)
        {
        const TPhoto& p = iPhotos[i];
        out << *p.iPath;
        out.WriteInt32L(I64HIGH(p.iTaken));
        out.WriteUint32L(I64LOW(p.iTaken));
        out.WriteInt32L(I64HIGH(p.iModified));
        out.WriteUint32L(I64LOW(p.iModified));
        out.WriteInt32L(p.iSize);
        out.WriteInt32L(p.iWidth);
        out.WriteInt32L(p.iHeight);
        out.WriteInt8L(p.iOrientation);
        out.WriteInt32L(p.iThumb);
        out.WriteInt8L(p.iFlags);
        out.WriteInt32L(p.iDuration);
        }
    out.CommitL();
    CleanupStack::PopAndDestroy(&out);
    aFs.Delete(aFile);
    User::LeaveIfError(aFs.Rename(temp, aFile));
    }

void CLibrary::LoadL(RFs& aFs, const TDesC& aFile)
    {
    RFileReadStream in;
    User::LeaveIfError(in.Open(aFs, aFile, EFileRead | EFileShareReadersOnly));
    CleanupClosePushL(in);
    if (in.ReadInt32L() != KIndexMagic)
        User::Leave(KErrCorrupt);
    const TInt version = in.ReadInt32L();
    if (version < 1 || version > KIndexVersion)
        User::Leave(KErrCorrupt);
    iNextThumb = in.ReadInt32L();
    iThumbGeneration = in.ReadInt32L();
    const TInt count = in.ReadInt32L();
    if (count < 0 || count > 1000000)
        User::Leave(KErrCorrupt);
    iPhotos.ReserveL(count);
    for (TInt i = 0; i < count; i++)
        {
        TPhoto p;
        p.iPath = HBufC::NewLC(in, KMaxFileName);
        const TInt takenHigh = in.ReadInt32L();
        p.iTaken = MAKE_TINT64(takenHigh, in.ReadUint32L());
        const TInt modifiedHigh = in.ReadInt32L();
        p.iModified = MAKE_TINT64(modifiedHigh, in.ReadUint32L());
        p.iSize = in.ReadInt32L();
        p.iWidth = in.ReadInt32L();
        p.iHeight = in.ReadInt32L();
        p.iOrientation = in.ReadInt8L();
        p.iThumb = in.ReadInt32L();
        p.iFlags = in.ReadInt8L();
        p.iDuration = version >= 2 ? in.ReadInt32L() : 0;
        p.iAlbum = 0;
        CleanupStack::Pop(p.iPath);
        AppendL(p);
        }
    CleanupStack::PopAndDestroy(&in);
    FinishL();
    }
