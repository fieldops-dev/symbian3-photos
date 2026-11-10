#ifndef LIBRARY_H
#define LIBRARY_H

#include <e32base.h>

class CDesC16Array;
class RFs;

class TPhoto
    {
public:

    enum { EParsed = 1, EFavourite = 2, EVideo = 4, EHidden = 8, ETrashed = 16 };

    HBufC* iPath;
    TInt64 iTaken;
    TInt64 iModified;
    TInt iSize;
    TInt iWidth;
    TInt iHeight;
    TInt iOrientation;
    TInt iThumb;
    TInt iAlbum;
    TInt iFlags;
    TInt iDuration;
    };

class CAlbum : public CBase
    {
public:
    enum TKind { EFavourites, ECamera, EScreenshots, EFolder, EVideos, EHidden, ERecentlyDeleted };
    ~CAlbum();
    HBufC* iName;
    TKind iKind;
    RArray<TInt> iPhotos;
    };

class CLibrary : public CBase
    {
public:
    static CLibrary* NewL();
    ~CLibrary();

    TInt Count() const { return iPhotos.Count(); }
    const TPhoto& Photo(TInt aIndex) const { return iPhotos[aIndex]; }

    TPhoto& PhotoForChange(TInt aIndex) { return iPhotos[aIndex]; }
    TInt AlbumCount() const { return iAlbums.Count(); }
    const CAlbum& Album(TInt aIndex) const { return *iAlbums[aIndex]; }

    TInt CoverAlbumCount() const { return iCoverAlbums; }

    TInt AlbumOfKind(CAlbum::TKind aKind) const;

    const RArray<TInt>& Visible() const { return iVisible; }

    static TBool IsHiddenPath(const TDesC& aPath);
    static TBool IsTrashPath(const TDesC& aPath);
    static TBool IsVideoPath(const TDesC& aPath);

    void AppendL(const TPhoto& aPhoto);

    void FinishL();
    void LoadL(RFs& aFs, const TDesC& aFile);
    void SaveL(RFs& aFs, const TDesC& aFile) const;

    TInt Find(const TDesC& aPath) const;

    void SetFavouritesL(const CDesC16Array& aPaths);

    CLibrary* CopyL(TInt aSkip, TInt aRenamed, const TDesC& aNewPath) const;

    CLibrary* CopyL(const RArray<TInt>& aPhotos, const CDesC16Array& aNewPaths) const;

    TInt iNextThumb;
    TInt iThumbGeneration;

private:
    void GroupAlbumsL();
    static void AlbumName(const TDesC& aPath, TDes& aName, CAlbum::TKind& aKind);

    RArray<TPhoto> iPhotos;
    RPointerArray<CAlbum> iAlbums;
    RArray<TInt> iByPath;
    RArray<TInt> iVisible;
    TInt iCoverAlbums;
    TBool iFavourites;
    };

#endif
