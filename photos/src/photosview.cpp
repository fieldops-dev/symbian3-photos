#include "photosview.h"

#include <AknDef.h>
#include <aknstyluspopupmenu.h>
#include <badesca.h>
#include <documenthandler.h>
#include <e32math.h>
#include <eikappui.h>
#include <eikenv.h>
#include <eikon.hrh>
#include <f32file.h>
#include <s32file.h>
#include <sendui.h>
#include <touchfeedback.h>
#include <w32std.h>

#include "gridlayout.h"
#include "icons.h"
#include "labels.h"
#include "library.h"
#include "moves.h"
#include "n8mlog.h"
#include "trace.h"

namespace
    {

    const TInt KHeading = 64;
    const TInt KTitleBaseline = 44;
    const TInt KTopBar = 48;
    const TInt KTopShade = 96;
    const TReal KTopShadeAlpha = 0.6;
    const TInt KButtonPad = 12;
    const TInt KLongPressMs = 450;
    const TInt KLiftBuzzUs = 80000;
    const TInt KTabBar = 56;
    const TInt KGap = 2;
    const TInt KHeader = 48;
    const TInt KHeaderBaseline = 33;
    const TInt KFooter = 56;
    const TInt KCellTarget = 90;

    const TInt KLevels = 3;
    const TInt KLevelCell[KLevels] = { 45, 90, 120 };
    const CGridLayout::TGrouping KLevelGrouping[KLevels] = {
        CGridLayout::EByYear, CGridLayout::EByMonth, CGridLayout::EByDay };
    const TInt KDefaultLevel = 1;
    const TInt KZoomTapColumns = 6;
    const TReal KLevelOmega = 20.5;
    const TReal KPinchIn = 1.25;
    const TReal KPinchOut = 0.8;
    const TInt KAlbumMargin = 16;
    const TInt KAlbumLabel = 62;

    const TInt KCoverSide = 150;
    const TInt KCoverGap = 12;
    const TInt KScrubWidth = 32;

    const TInt KSlop = 14;
    const TInt KTapMs = 250;
    const TInt KEdge = 24;
    const TInt KBarSlot = 32;
    const TInt KTabs = 3;
    const TInt KTabSlots = KTabs + 1;

    const TInt KListRow = 56;
    const TInt KListGroupGap = 44;
    const TUint32 KCard = 0x1C1C1E;
    const TInt KScrubTickMs = 60;

    const TUint32 KBackground = 0x000000;
    const TUint32 KPlaceholder = 0x1C1C1E;
    const TUint32 KText = 0xFFFFFF;
    const TUint32 KGrey = 0x8E8E93;
    const TUint32 KBlue = 0x0A84FF;
    const TUint32 KBarTint = 0x161618;
    const TReal KBarFrost = 0.72;
    const TUint32 KHairline = 0xFFFFFF;
    const TReal KHairlineAlpha = 0.14;

    const TReal KPushOmega = 16;
    const TReal KFling = 500;

    const TInt KFrameUs = 16667;
    const TInt KMinGapUs = 2000;
    const TInt KFrostEvery = 2;
    const TInt KUploadMoving = 4;
    const TInt KUploadIdle = 12;
    const TInt KRescanMs = 10000;
    const TInt KHideDelayUs = 600000000;

    const TInt KStatsMs = 5000;
    const TInt KMaxIntervals = 400;
    TUint16 gIntervals[KMaxIntervals];
    TInt gIntervalCount = 0;
    TInt gFrames = 0;
    TInt gFrosts = 0;
    TInt gDrawCalls = 0;
    TInt64 gCpuUs = 0;
    TInt gCpuMaxUs = 0;
    TUint32 gStatsStart = 0;
    TUint32 gLastFrameMs = 0;

    TInt Percentile(TInt aPercent)
        {
        if (gIntervalCount == 0)
            return 0;
        for (TInt i = 1; i < gIntervalCount; i++)
            for (TInt j = i; j > 0 && gIntervals[j - 1] > gIntervals[j]; j--)
                {
                TUint16 t = gIntervals[j];
                gIntervals[j] = gIntervals[j - 1];
                gIntervals[j - 1] = t;
                }
        return gIntervals[Min(gIntervalCount - 1, gIntervalCount * aPercent / 100)];
        }

    void ResetStats()
        {
        gFrames = gFrosts = gDrawCalls = gIntervalCount = 0;
        gCpuUs = 0;
        gCpuMaxUs = 0;
        gStatsStart = 0;
        gLastFrameMs = 0;
        }

    _LIT(KEtc1Name, "ETC1");
    _LIT(KRgb565Name, "RGB 565");

    const TText* const KMonths[12] = {
        _S("January"), _S("February"), _S("March"), _S("April"), _S("May"), _S("June"), _S("July"), _S("August"),
        _S("September"), _S("October"), _S("November"), _S("December") };

    class TGridQuad
        {
    public:
        TReal iX0, iY0, iX1, iY1;
        TReal iUv[4];
        TInt iAtlas;
        };
    const TInt KMaxGridQuads = 160;
    TGridQuad gQuads[KMaxGridQuads];
    TInt gQuadCount = 0;

    void AddGridQuad(TReal aX0, TReal aY0, TReal aX1, TReal aY1, TInt aAtlas, const TReal* aUv)
        {
        if (gQuadCount >= KMaxGridQuads)
            return;
        TGridQuad& q = gQuads[gQuadCount++];
        q.iX0 = aX0;
        q.iY0 = aY0;
        q.iX1 = aX1;
        q.iY1 = aY1;
        q.iAtlas = aAtlas;
        for (TInt i = 0; i < 4; i++)
            q.iUv[i] = aUv ? aUv[i] : 0;
        }

    class TCellMark
        {
    public:
        TReal iX0, iY0, iX1, iY1;
        TInt iPhoto;
        TBool iReady;
        };
    TCellMark gCells[KMaxGridQuads];
    TInt gCellCount = 0;

    void FlushGridQuads(CGlRenderer& aGl, CThumbCache& aThumbs, TReal aAlpha)
        {
        for (TInt i = 0; i < gQuadCount; i++)
            if (gQuads[i].iAtlas < 0)
                aGl.AddQuad(gQuads[i].iX0, gQuads[i].iY0, gQuads[i].iX1, gQuads[i].iY1, 0, 0, 1, 1);
        if (aGl.BatchCount())
            aGl.DrawBatchFlat(KPlaceholder, aAlpha);
        for (TInt a = 0; a < aThumbs.AtlasCount(); a++)
            {
            for (TInt i = 0; i < gQuadCount; i++)
                {
                const TGridQuad& q = gQuads[i];
                if (q.iAtlas == a)
                    aGl.AddQuad(q.iX0, q.iY0, q.iX1, q.iY1, q.iUv[0], q.iUv[1], q.iUv[2], q.iUv[3]);
                }
            if (aGl.BatchCount())
                aGl.DrawBatch(aThumbs.Atlas(a), 0xFFFFFF, aAlpha);
            }
        gQuadCount = 0;
        }

    TReal Distance(const TPoint& aA, const TPoint& aB)
        {
        TReal r;
        const TReal dx = aA.iX - aB.iX, dy = aA.iY - aB.iY;
        Math::Sqrt(r, dx * dx + dy * dy);
        return r;
        }

    void FormatCount(TDes& aText, TInt aCount, const TDesC& aOne, const TDesC& aMany)
        {
        aText.Zero();
        if (aCount >= 1000)
            {
            aText.AppendNum(aCount / 1000);
            aText.Append(',');
            aText.AppendNumFixedWidth(aCount % 1000, EDecimal, 3);
            }
        else
            aText.AppendNum(aCount);
        aText.Append(' ');
        aText.Append(aCount == 1 ? aOne : aMany);
        }
    }

CPhotosView* CPhotosView::NewL(const TRect& aRect, TInt aTopInset, MStatusBar& aStatusBar)
    {
    CPhotosView* self = new (ELeave) CPhotosView(aStatusBar);
    CleanupStack::PushL(self);
    self->ConstructL(aRect, aTopInset);
    CleanupStack::Pop(self);
    return self;
    }

CPhotosView::CPhotosView(MStatusBar& aStatusBar)
    : iForeground(ETrue), iVisible(ETrue), iStatusBar(aStatusBar), iAlbum(-1), iFirstLayout(ETrue),
      iLevel(KDefaultLevel),
      iPopupItem(-1), iPopupPressed(-2), iZoom(1), iInfoPhoto(-1)
    {
    for (TInt i = 0; i < KViewImages; i++)
        iViewImages[i].iPhoto = -1;
    for (TInt i = 0; i < KCovers; i++)
        iCovers[i].iPhoto = -1;
    for (TInt i = 0; i < KMaxTiles; i++)
        iTiles[i].iWantedFrame = -2;
    }

void CPhotosView::ConstructL(const TRect& aRect, TInt aTopInset)
    {
    Trace(_L("view: construct, status bar %d px over the top"), aTopInset);
    iTopInset = aTopInset;
    iFrameTimer = CFrameTimer::NewL(TCallBack(FrameTick, this));
    CreateWindowL();

    Window().SetOrdinalPosition(-1);

    Window().EnableAdvancedPointers();
    EnableDragEvents();
    iConstructing = ETrue;
    SetRect(aRect);
    ActivateL();
    iScale = Min(Rect().Width(), Rect().Height()) * 256 / 360;
    iLibraryGrid = CGridLayout::NewL();
    iFavouritesGrid = CGridLayout::NewL();
    iAlbumGrid = CGridLayout::NewL();
    StartGl();

    const TInt64 startUs = PerfNowUs();
    LoadIndex();
    TBuf<KMaxTypefaceNameLength> face;
    face.Copy(CEikonEnv::Static()->NormalFont()->FontSpecInTwips().iTypeface.Name().Left(KMaxTypefaceNameLength));
    iLabels = CLabels::NewL(CEikonEnv::Static()->FsSession(), face, iScale, *this);
    iThumbs = CThumbCache::NewL(*this);
    iTileScratch = static_cast<TUint16*>(User::AllocL(KTile * KTile * 2));
    iThumbs->SetLibrary(iLibrary);
    iImages = CImageWorker::NewL(*this);
    iGifWorker = CGifWorker::NewL(*this);
    iGifPhoto = -1;
    iGifShown = -1;
    Relayout();
    Trace(_L("view: cpu start work %d ms"), (TInt)((PerfNowUs() - startUs) / 1000));
    iConstructing = EFalse;
    CompleteGl();

    iScanner = CScanner::NewL(*this, iIndexFile);
    iHideTimer = CPeriodic::NewL(CActive::EPriorityStandard);
    RequestFrame();
    Trace(_L("view: constructed, gl %d"), iGl != NULL);
    }

void CPhotosView::StartDeferred()
    {
    iDeferredDone = ETrue;
    Trace(_L("start: first frame with text, deferred start-up now"));
    iLabels->StartPreload();
    TRAPD(err, iPlayer = CVideoPlayer::NewL(*this, *this, iScale));
    if (err != KErrNone)
        Trace(_L("view: no video player, err %d"), err);
    else
        {
        iPlayer->SetGl(iGl);
        iPlayer->SetTopInset(iTopInset);
        }
    StartScan();
    TraceMemory(_L("start-up done"), iGl ? iGl->TextureBytes() : 0);
    }

CPhotosView::~CPhotosView()
    {
    delete iScanner;
    delete iImages;
    StopGif();
    delete iGifWorker;
    iGifFrames.Close();
    delete iPlayer;
    delete iDocHandler;
    iSelected.Close();
    iSelectBase.Close();
    iNoItems.Close();
    StopGl();
    delete iThumbs;
    for (TInt i = 0; i < KViewImages; i++)
        User::Free(iViewImages[i].iPixels);
    User::Free(iTileScratch);
    delete iLabels;
    delete iFrameTimer;
    delete iHideTimer;
    delete iLibraryGrid;
    delete iOldGrid;
    delete iFavouritesGrid;
    delete iAlbumGrid;
    delete iLibrary;
    iRetired.ResetAndDestroy();
    delete iFavourites;
    delete iMoves;
    delete iMenu;
    delete iSendUi;
    }

void CPhotosView::LoadIndex()
    {
    RFs& fs = CEikonEnv::Static()->FsSession();
    fs.CreatePrivatePath(EDriveC);
    TFileName dir;
    fs.PrivatePath(dir);
    iIndexFile.Copy(_L("C:"));
    iIndexFile.Append(dir);
    iIndexFile.Append(_L("index.dat"));
    const TInt64 startUs = PerfNowUs();
    iFavourites = new CDesC16ArraySeg(16);
    if (!iFavourites)
        User::Panic(_L("photos"), KErrNoMemory);
    TRAPD(err, iLibrary = CLibrary::NewL());
    if (err != KErrNone)
        User::Panic(_L("photos"), err);
    TRAP(err, iLibrary->LoadL(fs, iIndexFile));
    if (err != KErrNone)
        {
        delete iLibrary;
        iLibrary = NULL;
        TRAP_IGNORE(iLibrary = CLibrary::NewL());
        if (!iLibrary)
            User::Panic(_L("photos"), KErrNoMemory);
        }
    LoadFavourites();
    TParsePtrC indexParse(iIndexFile);
    TFileName moves(indexParse.DriveAndPath());
    moves.Append(_L("moves.dat"));
    TRAP(err, iMoves = CMoves::NewL(moves));
    if (err != KErrNone)
        User::Panic(_L("photos"), err);
    iMoves->Load(fs);
    TRAP_IGNORE(iLibrary->SetFavouritesL(*iFavourites));
    Trace(_L("library: index err %d, %d pictures, %d albums in %d ms"), err, iLibrary->Count(),
          iLibrary->AlbumCount(), (TInt)((PerfNowUs() - startUs) / 1000));
    }

void CPhotosView::LibraryScanned(CLibrary* aLibrary, TBool aFinal)
    {
    iLastScanMs = NowMs();
    if (aLibrary)
        {
        TRAP_IGNORE(aLibrary->SetFavouritesL(*iFavourites));
        TFileName viewerPath;
        const TInt viewed = iViewerOpen ? ViewerPhoto(0) : -1;
        if (viewed >= 0)
            viewerPath.Copy(*iLibrary->Photo(viewed).iPath);
        delete Adopt(aLibrary, viewerPath);
        }
    if (!aFinal)
        {
        iScanSource = iLibrary;
        iScanner->Continue();
        }
    else
        {
        iScanSource = NULL;
        iRetired.ResetAndDestroy();
        iScanning = EFalse;
        if (iRescan)
            {

            iRescan = EFalse;
            StartScan();
            }
        }
    RequestFrame();
    }

void CPhotosView::StartScan()
    {
    PurgeTrash();
    iScanning = ETrue;
    iScanSource = iLibrary;
    iScanner->Start(iLibrary);
    }

CLibrary* CPhotosView::Adopt(CLibrary* aLibrary, const TDesC& aViewerPath)
    {
    CLibrary* old = iLibrary;
    const TBool atBottom = iLibraryScroll >= MaxScroll(*iLibraryGrid) - 2;
    TFileName anchorPath;
    TInt anchorOffset = 0;
    if (!atBottom && iLibraryGrid->Count() > 0)
        {
        const TInt item = iLibraryGrid->ItemAt(TPoint(1, (TInt)iLibraryScroll + 1));
        if (item >= 0)
            {
            anchorPath.Copy(*old->Photo(iLibraryGrid->PhotoAt(item)).iPath);
            anchorOffset = iLibraryGrid->ItemRect(item).iTl.iY - (TInt)iLibraryScroll;
            }
        }
    TBuf<40> albumName;
    CAlbum::TKind albumKind = CAlbum::EFolder;
    if (iAlbum >= 0)
        {
        albumName.Copy(old->Album(iAlbum).iName->Left(40));
        albumKind = old->Album(iAlbum).iKind;
        }
    TInt photoMap[KViewImages];
    for (TInt i = 0; i < KViewImages; i++)
        photoMap[i] = iViewImages[i].iPhoto >= 0 ? aLibrary->Find(*old->Photo(iViewImages[i].iPhoto).iPath) : -1;

    for (TInt i = iSelected.Count() - 1; i >= 0; i--)
        {
        const TInt found = aLibrary->Find(*old->Photo(iSelected[i]).iPath);
        if (found >= 0)
            iSelected[i] = found;
        else
            iSelected.Remove(i);
        }
    iSelected.Sort();
    iPopupOpen = EFalse;
    iPopupP = 0;
    iPopupItem = -1;
    iLibrary = aLibrary;
    FinishLevelChange();
    if (iAxis == EAxisGridPinch)
        iAxis = EAxisIgnore;
    iThumbs->SetLibrary(iLibrary);
    for (TInt i = 0; i < KViewImages; i++)
        {
        if (iViewImages[i].iPhoto >= 0 && photoMap[i] < 0)
            FreeImage(iViewImages[i]);
        else
            iViewImages[i].iPhoto = photoMap[i];
        }
    iAlbum = FindAlbum(albumName, albumKind);
    if (iAlbum < 0)
        {
        iPush = iPushTarget = 0;
        iPushVel = 0;
        }
    iAlbumsDirty = EFalse;
    Relayout();
    if (atBottom)
        iLibraryScroll = MaxScroll(*iLibraryGrid);
    else if (anchorPath.Length())
        {
        const TInt photo = iLibrary->Find(anchorPath);
        const TInt item = photo >= 0 ? iLibraryGrid->ItemOf(photo) : -1;
        if (item >= 0)
            iLibraryScroll = Clamp(iLibraryGrid->ItemRect(item).iTl.iY - anchorOffset, 0, MaxScroll(*iLibraryGrid));
        }
    if (iViewerOpen)
        {
        const TInt photo = aViewerPath.Length() ? iLibrary->Find(aViewerPath) : -1;
        const TInt item = photo >= 0 && ActiveGrid() ? ActiveGrid()->ItemOf(photo) : -1;
        if (item >= 0)
            {
            iCurrent = item;
            UpdateViewerImages();
            }
        else
            {
            iViewerOpen = EFalse;
            StopGif();
            for (TInt i = 0; i < KViewImages; i++)
                FreeImage(iViewImages[i]);
            }
        }
    iThumbEpoch++;
    iFrostKey = 0;
    return old;
    }

TInt CPhotosView::FindAlbum(const TDesC& aName, TInt aKind) const
    {
    for (TInt a = 0; a < iLibrary->AlbumCount() && aName.Length(); a++)
        if (iLibrary->Album(a).iKind == aKind && iLibrary->Album(a).iName->Compare(aName) == 0)
            return a;
    return -1;
    }

void CPhotosView::ReplaceLibrary(CLibrary* aLibrary, const TDesC& aViewerPath)
    {
    CLibrary* old = Adopt(aLibrary, aViewerPath);
    if (iScanning && old == iScanSource)
        {
        if (iRetired.Append(old) != KErrNone)
            iRetired.Close();
        }
    else
        delete old;
    if (iScanning)
        iRescan = ETrue;
    const TInt64 startUs = PerfNowUs();
    TRAPD(err, iLibrary->SaveL(CEikonEnv::Static()->FsSession(), iIndexFile));
    Trace(_L("library: saved after a change, err %d in %d ms"), err, (TInt)((PerfNowUs() - startUs) / 1000));
    RequestFrame();
    }

void CPhotosView::RegroupAlbums()
    {
    iAlbumsDirty = EFalse;
    TBuf<40> albumName;
    CAlbum::TKind albumKind = CAlbum::EFolder;
    if (iAlbum >= 0)
        {
        albumName.Copy(iLibrary->Album(iAlbum).iName->Left(40));
        albumKind = iLibrary->Album(iAlbum).iKind;
        }
    TRAPD(err, iLibrary->SetFavouritesL(*iFavourites));
    if (err != KErrNone)
        Trace(_L("library: favourites err %d"), err);
    iAlbum = FindAlbum(albumName, albumKind);
    if (iAlbum < 0)
        iPush = iPushTarget = iPushVel = 0;
    Relayout();
    iFrostKey = 0;
    }

void CPhotosView::LoadFavourites()
    {
    TParsePtrC parse(iIndexFile);
    iFavouritesFile.Copy(parse.DriveAndPath());
    iFavouritesFile.Append(_L("favourites.dat"));
    TRAPD(err, LoadFavouritesL(iFavouritesFile));
    Trace(_L("library: %d favourites, err %d"), iFavourites->Count(), err);
    }

void CPhotosView::LoadFavouritesL(const TDesC& aFile)
    {
    RFileReadStream in;
    if (in.Open(CEikonEnv::Static()->FsSession(), aFile, EFileRead | EFileShareReadersOnly) != KErrNone)
        return;
    CleanupClosePushL(in);
    const TInt count = in.ReadInt32L();
    for (TInt i = 0; i < count && i < 100000; i++)
        {
        HBufC* path = HBufC::NewLC(in, KMaxFileName);
        iFavourites->InsertIsqAllowDuplicatesL(*path, ECmpFolded);
        CleanupStack::PopAndDestroy(path);
        }
    CleanupStack::PopAndDestroy(&in);
    }

void CPhotosView::SaveFavouritesL()
    {
    RFileWriteStream out;
    User::LeaveIfError(out.Replace(CEikonEnv::Static()->FsSession(), iFavouritesFile, EFileWrite));
    CleanupClosePushL(out);
    out.WriteInt32L(iFavourites->Count());
    for (TInt i = 0; i < iFavourites->Count(); i++)
        out << (*iFavourites)[i];
    out.CommitL();
    CleanupStack::PopAndDestroy(&out);
    }

void CPhotosView::PurgeTrash()
    {
    TTime now;
    now.UniversalTime();
    RFs& fs = CEikonEnv::Static()->FsSession();
    TInt purged = 0;
    for (TInt i = iMoves->Count() - 1; i >= 0; i--)
        {
        if (!CLibrary::IsTrashPath(iMoves->Path(i)))
            continue;
        TTimeIntervalDays age = now.DaysFrom(iMoves->When(i));
        if (age.Int() < CMoves::KTrashDays)
            continue;
        const TFileName path(iMoves->Path(i));
        const TInt err = fs.Delete(path);
        if (err == KErrNone || err == KErrNotFound || err == KErrPathNotFound)
            {
            iMoves->Remove(path);
            purged++;
            }
        }
    if (purged)
        {
        TRAPD(err, iMoves->SaveL(fs));
        Trace(_L("trash: %d purged, save err %d"), purged, err);
        }
    }

void CPhotosView::BuildAlbumGrid(CGridLayout& aGrid, TInt aAlbum, TInt aTopInset)
    {
    const TInt columns = Max(3, Rect().Width() / Scale(KCellTarget));
    const RArray<TInt>* items = aAlbum >= 0 && aAlbum < iLibrary->AlbumCount() ? &iLibrary->Album(aAlbum).iPhotos :
                                                                                  &iNoItems;
    TRAP_IGNORE(aGrid.BuildL(*iLibrary, items, Rect().Width(), columns, Scale(KGap), CGridLayout::ENoHeaders, 0,
                             Scale(KFooter), aTopInset, Scale(KTabBar)));
    }

void CPhotosView::Relayout()
    {
    const TSize size = Rect().Size();
    if (size.iWidth <= 0 || size.iHeight <= 0)
        return;
    FinishLevelChange();
    TRAP_IGNORE(BuildLibraryGridL(*iLibraryGrid, iLevel));
    BuildAlbumGrid(*iFavouritesGrid, iLibrary->AlbumOfKind(CAlbum::EFavourites), iTopInset + Scale(KHeading));
    if (iAlbum >= 0 && iAlbum < iLibrary->AlbumCount())
        BuildAlbumGrid(*iAlbumGrid, iAlbum, iTopInset + Scale(KTopBar + KHeading));
    if (iFirstLayout && iLibrary->Count() > 0)
        {
        iFirstLayout = EFalse;
        iLibraryScroll = MaxScroll(*iLibraryGrid);
        }
    iLibraryScroll = Clamp(iLibraryScroll, 0, MaxScroll(*iLibraryGrid));
    iFavouritesScroll = Clamp(iFavouritesScroll, 0, MaxScroll(*iFavouritesGrid));
    iAlbumScroll = Clamp(iAlbumScroll, 0, MaxScroll(*iAlbumGrid));
    iAlbumsScroll = Clamp(iAlbumsScroll, 0, Max(0, AlbumsHeight() - size.iHeight));
    iCoversScroll = Clamp(iCoversScroll, 0, MaxCoversScroll());
    iLaidOut = size;
    iLaidOutInset = iTopInset;
    if (iPlayer)
        iPlayer->SetTopInset(iTopInset);
    }

void CPhotosView::BuildLibraryGridL(CGridLayout& aGrid, TInt aLevel)
    {
    const TInt w = Rect().Width();
    aGrid.BuildL(*iLibrary, &iLibrary->Visible(), w, Max(3, w / Scale(KLevelCell[aLevel])), Scale(KGap),
                 KLevelGrouping[aLevel],
                 Scale(KHeader), Scale(KFooter), iTopInset, Scale(KTabBar));
    }

CGridLayout* CPhotosView::ActiveGrid() const
    {
    if (iTab == ETabAll)
        return iLibraryGrid;
    if (iTab == ETabFavourites)
        return iFavouritesGrid;
    return InAlbum() ? iAlbumGrid : NULL;
    }

TReal& CPhotosView::ActiveScroll()
    {
    if (iTab == ETabAll)
        return iLibraryScroll;
    if (iTab == ETabFavourites)
        return iFavouritesScroll;
    return InAlbum() ? iAlbumScroll : iAlbumsScroll;
    }

TReal& CPhotosView::ActiveVelocity()
    {
    if (iTab == ETabAll)
        return iLibraryVel;
    if (iTab == ETabFavourites)
        return iFavouritesVel;
    return InAlbum() ? iAlbumVel : iAlbumsVel;
    }

TReal CPhotosView::GridScroll() const
    {
    if (iTab == ETabAll)
        return iLibraryScroll;
    return iTab == ETabFavourites ? iFavouritesScroll : iAlbumScroll;
    }

TInt CPhotosView::ShownKind() const
    {
    if (iTab == ETabFavourites)
        return CAlbum::EFavourites;
    return InAlbum() && iAlbum < iLibrary->AlbumCount() ? iLibrary->Album(iAlbum).iKind : -1;
    }

TInt CPhotosView::MaxScroll(const CGridLayout& aGrid) const
    {
    return Max(0, aGrid.ContentHeight() - Rect().Height());
    }

TInt CPhotosView::CoverRows() const
    {
    return iLibrary->CoverAlbumCount() > 1 ? 2 : 1;
    }

TInt CPhotosView::CoversHeight() const
    {
    return iTopInset + Scale(KHeading) + CoverRows() * (Scale(KCoverSide) + Scale(KAlbumLabel));
    }

TInt CPhotosView::MaxCoversScroll() const
    {
    const TInt rows = CoverRows(), columns = (iLibrary->CoverAlbumCount() + rows - 1) / rows;
    const TInt content = 2 * Scale(KAlbumMargin) + columns * (Scale(KCoverSide) + Scale(KCoverGap)) - Scale(KCoverGap);
    return Max(0, content - Rect().Width());
    }

TInt CPhotosView::AlbumsHeight() const
    {
    TListRow rows[KListRows];
    const TInt count = ListRows(rows);
    return count ? ListRowRect(count - 1).iBr.iY + Scale(24) + Scale(KTabBar) : CoversHeight() + Scale(KTabBar);
    }

TRect CPhotosView::AlbumCoverRect(TInt aAlbum) const
    {
    const TInt rows = CoverRows(), side = Scale(KCoverSide);
    const TInt x = Scale(KAlbumMargin) + (aAlbum / rows) * (side + Scale(KCoverGap)) - (TInt)iCoversScroll;
    const TInt y = iTopInset + Scale(KHeading) + (aAlbum % rows) * (side + Scale(KAlbumLabel));
    return TRect(x, y, x + side, y + side);
    }

void CPhotosView::OpenAlbum(TInt aAlbum)
    {
    EndSelect();
    iAlbum = aAlbum;
    BuildAlbumGrid(*iAlbumGrid, aAlbum, iTopInset + Scale(KTopBar + KHeading));

    iAlbumScroll = aAlbum < iLibrary->CoverAlbumCount() ? MaxScroll(*iAlbumGrid) : 0;
    iAlbumVel = 0;
    iPushTarget = 1;
    RequestFrame();
    }

void CPhotosView::GlFailed(TInt aErr)
    {
    StopGl();
    iGlFailed = ETrue;
    iGlNote.Copy(_L("The graphics chip is unavailable: "));
    TPtrC why = CGlRenderer::LastError();
    if (why.Length())
        iGlNote.Append(why.Left(iGlNote.MaxLength() - iGlNote.Length()));
    else
        iGlNote.AppendNum(aErr);
    Trace(_L("view: %S"), &iGlNote);
    DrawDeferred();
    }

void CPhotosView::StartGl()
    {
    if (iGl || iGlFailed || !iVisible)
        return;
    const TInt64 startUs = PerfNowUs();
    TRAPD(err, iGl = CGlRenderer::NewL(static_cast<RWindow&>(Window()), Rect().Size()));
    TPtrC why(CGlRenderer::LastError());
    Trace(_L("view: gl start err %d %S in %d ms"), err, &why, (TInt)((PerfNowUs() - startUs) / 1000));
    if (err != KErrNone)
        {
        iGl = NULL;
        GlFailed(err);
        return;
        }
    if (!iConstructing)
        CompleteGl();
    }

void CPhotosView::CompleteGl()
    {
    if (!iGl)
        return;
    TRAPD(err, iGl->CompleteL());
    const TInt64 iconsUs = PerfNowUs();
    if (err == KErrNone)
        TRAP(err, iIcons = CIcons::NewL(*iGl, iScale));
    Trace(_L("view: icons in %d ms, %d rendered here (the rest baked)"), (TInt)((PerfNowUs() - iconsUs) / 1000),
          iIcons ? iIcons->Rendered() : 0);
    if (err != KErrNone)
        {
        GlFailed(err);
        return;
        }
    iThumbs->SetGl(iGl);
    if (iPlayer)
        iPlayer->SetGl(iGl);
    iFrostKey = 0;
    if (iViewerOpen)
        UpdateViewerImages();
    RequestFrame();
    }

void CPhotosView::StopGl()
    {
    if (!iGl)
        return;

    if (iPlayer)
        iPlayer->SetGl(NULL);
    if (iThumbs)
        iThumbs->SetGl(NULL);
    ForgetCovers(ETrue);
    if (iIcons)
        iIcons->Release(*iGl);
    delete iIcons;
    iIcons = NULL;
    if (iLabels)
        iLabels->ForgetTextures();
    FreeTiles();
    for (TInt i = 0; i < KViewImages; i++)
        {
        iGl->DeleteTexture(iViewImages[i].iTexture);
        iViewImages[i].iRowsUploaded = 0;
        if (!iViewImages[i].iPixels)
            iViewImages[i].iPhoto = -1;
        }
    iGifTexture = TGlTexture();
    iSpinnerTexture = TGlTexture();
    iGifShown = -1;
    delete iGl;
    iGl = NULL;
    }

void CPhotosView::Resume()
    {
    iGlFailed = EFalse;
    StartGl();
    RequestFrame();
    }

void CPhotosView::SetForeground(TBool aForeground)
    {
    if (aForeground == iForeground)
        return;
    iForeground = aForeground;
    if (!aForeground)
        {
        N8mLogDrain();
        TraceFlush();
        if (iPlayer)
            iPlayer->Pause();
        iFrameTimer->Cancel();
        iMoving = EFalse;
        iAxis = EAxisNone;
        iPointerDown[0] = iPointerDown[1] = EFalse;
        return;
        }

    iVisible = ETrue;
    iHideTimer->Cancel();
    ResetStats();
    Resume();

    if (iDeferredDone && !iScanning && NowMs() - iLastScanMs > (TUint32)KRescanMs)
        StartScan();
    }

void CPhotosView::SetVisible(TBool aVisible)
    {

    if (!aVisible && iForeground)
        return;
    if (aVisible == iVisible)
        return;
    iVisible = aVisible;
    if (!aVisible)
        {
        StopVideo();
        iFrameTimer->Cancel();
        iMoving = EFalse;

        iHideTimer->Cancel();
        iHideTimer->Start(KHideDelayUs, KHideDelayUs, TCallBack(HideTimeout, this));
        return;
        }
    iHideTimer->Cancel();
    Resume();
    }

TInt CPhotosView::HideTimeout(TAny* aSelf)
    {
    CPhotosView* self = static_cast<CPhotosView*>(aSelf);
    self->iHideTimer->Cancel();
    if (!self->iVisible && !self->iForeground)
        {
        Trace(_L("view: hidden 10 min, GPU let go"));
        self->StopGl();
        }
    return EFalse;
    }

void CPhotosView::GpuMemoryLow()
    {
    if (iGl)
        {
        TInt total, used, ours;
        if (iGl->GpuMemory(total, used, ours))
            Trace(_L("goom: GPU memory %d of %d KB used, %d KB ours (textures %d KB)"), used / 1024, total / 1024,
                  ours / 1024, iGl->TextureBytes() / 1024);
        }
    if (iForeground && iVisible)
        {

        const TInt before = iGl ? iGl->TextureBytes() : 0;
        TInt freed = 0;
        if (iGl)
            {

            freed = iThumbs->Trim(60);
            ForgetCovers(ETrue);
            }
        Trace(_L("goom: graphics memory low, app in front: GPU kept, textures %d KB -> %d KB (thumbnail pages %d KB)"),
              before / 1024, iGl ? iGl->TextureBytes() / 1024 : 0, freed / 1024);
        return;
        }
    Trace(_L("goom: graphics memory low: GPU let go"));
    iHideTimer->Cancel();
    StopVideo();
    StopGl();
    }

void CPhotosView::SizeChanged()
    {
    const TSize size = Rect().Size();
    if (iConstructing || size == iLaidOut || size.iWidth <= 0)
        return;
    iSizeEvents++;
    const TInt64 sizeUs = PerfNowUs();
    if (iPlayer)
        iPlayer->Relayout(Rect());

    const TBool atBottom = iLibraryScroll >= MaxScroll(*iLibraryGrid) - 2;
    const TInt item = iLibraryGrid->Count() ?
        iLibraryGrid->ItemAt(TPoint(1, (TInt)iLibraryScroll + iLaidOutInset + 1)) : -1;
    const TInt albumAtBottom = iAlbumScroll >= MaxScroll(*iAlbumGrid) - 2;
    Relayout();
    if (atBottom)
        iLibraryScroll = MaxScroll(*iLibraryGrid);
    else if (item >= 0)
        iLibraryScroll = Clamp(iLibraryGrid->ItemRect(item).iTl.iY - iTopInset, 0, MaxScroll(*iLibraryGrid));
    if (albumAtBottom)
        iAlbumScroll = MaxScroll(*iAlbumGrid);
    Trace(_L("turn: size event %dx%d, status bar %d px, laid out in %d ms"), size.iWidth, size.iHeight, iTopInset,
          (TInt)((PerfNowUs() - sizeUs) / 1000));
    iRotateUs = sizeUs;
    iFrostKey = 0;
    if (iViewerOpen)
        {
        iZoom = 1;
        iPanX = iPanY = 0;
        iZoomSettling = EFalse;
        }
    if (iGl && !iGl->Resize(size))
        {
        StopGl();
        StartGl();
        }

    FrameNow();
    Trace(_L("turn: first frame drawn %d ms after the size event"), (TInt)((PerfNowUs() - sizeUs) / 1000));
    }

void CPhotosView::SetLayout(const TRect& aRect, TInt aTopInset)
    {
    const TBool insetChanged = aTopInset != iTopInset;
    iTopInset = aTopInset;
    const TBool resize = aRect.Size() != Rect().Size();
    if (aRect != Rect())
        SetRect(aRect);
    if (resize || !insetChanged || iConstructing)
        return;
    Relayout();
    iFrostKey = 0;
    Trace(_L("view: status bar now %d px, laid out again at %dx%d (no new surface)"), iTopInset, Rect().Width(),
          Rect().Height());
    RequestFrame();
    }

void CPhotosView::Draw(const TRect&  ) const
    {
    if (iGl)
        return;
    CWindowGc& gc = SystemGc();
    gc.SetBrushColor(KRgbWhite);
    gc.Clear(Rect());
    if (iGlNote.Length())
        {
        const CFont* font = CEikonEnv::Static()->NormalFont();
        gc.UseFont(font);
        gc.SetPenColor(KRgbBlack);
        gc.DrawText(iGlNote, TRect(0, Rect().Height() / 2 - 20, Rect().Width(), Rect().Height() / 2 + 20),
                    font->AscentInPixels() + 10, CGraphicsContext::ECenter);
        gc.DiscardFont();
        }
    }

void CPhotosView::RequestFrame()
    {
    if (!iForeground || !iVisible || !iGl || iFrameTimer->IsActive())
        return;
    iLastTick = NowMs();
    iFrameTimer->Schedule(0);
    }

void CPhotosView::FrameNow()
    {
    if (!iForeground || !iVisible || !iGl)
        return;
    iFrameTimer->Cancel();
    iLastTick = NowMs();
    FrameTick(this);
    }

void CPhotosView::ThumbsChanged()
    {
    iThumbEpoch++;
    RequestFrame();
    }

void CPhotosView::LabelsChanged()
    {
    iThumbEpoch++;
    RequestFrame();
    }

TInt CPhotosView::FrameTick(TAny* aSelf)
    {
    CPhotosView* self = static_cast<CPhotosView*>(aSelf);
    if (!self->iForeground || !self->iVisible || !self->iGl)
        return EFalse;
    self->iFrameStartUs = PerfNowUs();
    const TUint32 now = NowMs();
    const TInt ms = Max(1, Min(50, (TInt)(now - self->iLastTick)));
    self->iLastTick = now;
    const TBool moving = self->Step(ms) || (self->iAxis != EAxisNone && self->iAxis != EAxisIgnore);
    self->iMoving = moving;

    if (gLastFrameMs && gIntervalCount < KMaxIntervals && now - gLastFrameMs < 200)
        gIntervals[gIntervalCount++] = (TUint16)(now - gLastFrameMs);
    gLastFrameMs = now;
    gFrames++;
    if (gStatsStart == 0)
        gStatsStart = now;
    if (now - gStatsStart >= (TUint32)KStatsMs)
        self->LogStats(now);

    const TInt64 cpuStart = PerfNowUs();
    self->RenderFrame();
    if (!self->iGl)
        return EFalse;
    TBool loading = self->PumpUploads(moving);
    if (!self->iDeferredDone)
        {

        self->StartDeferred();
        loading = ETrue;
        }
    const TInt cpuUs = (TInt)(PerfNowUs() - cpuStart);
    gCpuUs += cpuUs;
    gCpuMaxUs = Max(gCpuMaxUs, cpuUs);

    TInt videoWaitUs = self->iViewerOpen && self->iPlayer ? self->iPlayer->SoftWaitUs() : -1;
    const TInt gifWaitUs = self->GifWaitUs();
    if (gifWaitUs >= 0 && (videoWaitUs < 0 || gifWaitUs < videoWaitUs))
        videoWaitUs = gifWaitUs;
    if (moving || loading)
        {
        const TInt used = (TInt)(PerfNowUs() - self->iFrameStartUs);
        self->iFrameTimer->Schedule(Max(KMinGapUs, KFrameUs - used));
        }
    else if (videoWaitUs >= 0)
        self->iFrameTimer->Schedule(Max(KMinGapUs, videoWaitUs));
    else
        {
        gLastFrameMs = 0;
        self->Idle();
        if (self->iRotateUs)
            {
            Trace(_L("turn: all content at +%d ms"),
                  (TInt)((PerfNowUs() - self->iRotateUs) / 1000));
            self->iRotateUs = 0;
            }
        }
    return ETrue;
    }

void CPhotosView::LogStats(TUint32 aNow)
    {
    const TInt span = Max(1, (TInt)(aNow - gStatsStart));
    const TInt p50 = Percentile(50), p95 = Percentile(95), worst = Percentile(100);
    Trace(_L("perf: %d frames in %d ms (%d timed while moving: median %d ms, 95%% %d, worst %d); cpu per frame avg %d "
             "max %d us; draw calls avg %d; frost redraws %d"),
          gFrames, span, gIntervalCount, p50, p95, worst, gFrames ? (TInt)(gCpuUs / gFrames) : 0, gCpuMaxUs,
          gFrames ? gDrawCalls / gFrames : 0, gFrosts);
    TInt labels = 0, labelUs = 0;
    iLabels->TakeStats(labels, labelUs);
    if (labels)
        Trace(_L("text: %d glyphs added in %d us, %d in the atlas"), labels, labelUs, iLabels->Count());
    iThumbs->TraceStats();
    if (iImagesDecoded)
        Trace(_L("images: %d decoded avg %d ms; uploads %d KB in %d ms"), iImagesDecoded,
              iImageDecodeUs / iImagesDecoded / 1000, iImageUploadBytes / 1024, iImageUploadUs / 1000);
    iImagesDecoded = iImageDecodeUs = iImageUploadUs = iImageUploadBytes = 0;
    if (iPlayer)
        iPlayer->LogStats();
    N8mLogDrain();
    TraceMemory(_L("periodic"), iGl ? iGl->TextureBytes() : 0);
    TraceFlush();
    gFrames = gFrosts = gDrawCalls = gIntervalCount = 0;
    gCpuUs = 0;
    gCpuMaxUs = 0;
    gStatsStart = aNow;
    }

void CPhotosView::Idle()
    {
    if (!iGl)
        return;
    iThumbs->Idle();
    TGpuState state = EGpuGrid;
    if (iViewerOpen)
        {
        state = EGpuViewer;
        for (TInt i = 0; i < KMaxTiles && state == EGpuViewer; i++)
            if (iTiles[i].iTexture.Valid())
                state = EGpuZoomed;
        }
    else if (iGpuState == EGpuNone)
        state = EGpuStart;

    const TInt thumbs = iThumbs->Bytes(CThumbCache::ELarge) + 3 * iThumbs->Bytes(CThumbCache::ESmall);
    const TUint32 now = NowMs();
    if ((state == iGpuState && (state != EGpuGrid || thumbs == iGpuLogThumbs)) ||
        (iGpuState != EGpuNone && now - iGpuLogMs < 2000))
        return;
    iGpuLogThumbs = thumbs;
    const TText* const names[5] = { _S(""), _S("start"), _S("grid"), _S("viewer"), _S("zoomed") };
    iGpuState = state;
    iGpuLogMs = now;
    TraceGpu(TPtrC(names[state]));
    }

void CPhotosView::TraceGpu(const TDesC& aWhere)
    {
    TInt tiles = 0, covers = 0;
    TInt viewer = iGifTexture.Bytes();
    for (TInt i = 0; i < KViewImages; i++)
        viewer += iViewImages[i].iTexture.Bytes();
    for (TInt i = 0; i < KMaxTiles; i++)
        tiles += iTiles[i].iTexture.Bytes();
    for (TInt i = 0; i < KCovers; i++)
        covers += iCovers[i].iTexture.Bytes();
    const TInt large = iThumbs->Bytes(CThumbCache::ELarge), small = iThumbs->Bytes(CThumbCache::ESmall);
    const TInt text = iLabels->TextureBytes(), all = iGl->TextureBytes();
    Trace(_L("gpu memory (%S): textures %d KB = thumbnails %d KB in %d pages + small thumbnails %d KB in %d pages "
             "(%S) + text %d KB + viewer %d KB + zoom tiles %d KB + covers %d KB + other (frost, icons, video) %d KB"),
          &aWhere, all / 1024, large / 1024, iThumbs->Pages(CThumbCache::ELarge), small / 1024,
          iThumbs->Pages(CThumbCache::ESmall), iThumbs->Etc1() ? &KEtc1Name() : &KRgb565Name(), text / 1024,
          viewer / 1024, tiles / 1024, covers / 1024, (all - large - small - text - viewer - tiles - covers) / 1024);
    TInt total, used, ours;
    if (iGl->GpuMemory(total, used, ours))
        Trace(_L("gpu memory (%S): phone %d of %d KB used, %d KB ours"), &aWhere, used / 1024, total / 1024,
              ours / 1024);
    }

TBool CPhotosView::Step(TInt aMs)
    {
    TBool moving = EFalse;
    const TUint32 now = NowMs();
    const TInt h = Rect().Height();

    if (iAxis == EAxisUndecided && !iTapCancelled && !iViewerOpen && !iPopupOpen && !iSelecting &&
        now - iDownMs >= (TUint32)KLongPressMs)
        {
        TRect cell;
        const TInt item = GridItemAt(iDownPos, cell);
        if (item >= 0)
            {
            iAxis = EAxisIgnore;

            MTouchFeedback* feedback = MTouchFeedback::Instance();
            if (feedback)
                {
                feedback->InstantFeedback(ETouchFeedbackLongTap);
                feedback->StartFeedback(this, ETouchContinuousSmooth, NULL, 100, KLiftBuzzUs);
                }
            OpenPopup(item, cell);
            }
        }
    moving |= StepPopup(aMs);
    moving |= StepSelectScroll(aMs);
    if (iTab == ETabAll)
        {
        if (iAxis != EAxisScroll && iAxis != EAxisScrub && iAxis != EAxisGridPinch && !iLevelTrans)
            moving |= Coast(iLibraryScroll, iLibraryVel, 0, MaxScroll(*iLibraryGrid), aMs);
        if (iLevelTrans)
            {
            moving = ETrue;
            if (!SpringTo(iLevelP, iLevelPVel, 1, KLevelOmega, aMs, 0.002))
                FinishLevelChange();
            }
        }
    else if (iTab == ETabFavourites)
        {
        if (iAxis != EAxisScroll && iAxis != EAxisScrub)
            moving |= Coast(iFavouritesScroll, iFavouritesVel, 0, MaxScroll(*iFavouritesGrid), aMs);
        }
    else
        {
        const TBool inAlbum = InAlbum();
        if (!(inAlbum && (iAxis == EAxisScroll || iAxis == EAxisScrub)))
            moving |= Coast(iAlbumScroll, iAlbumVel, 0, MaxScroll(*iAlbumGrid), aMs);
        if (!(!inAlbum && iAxis == EAxisScroll))
            moving |= Coast(iAlbumsScroll, iAlbumsVel, 0, Max(0, AlbumsHeight() - h), aMs);
        if (iAxis != EAxisCovers)
            moving |= Coast(iCoversScroll, iCoversVel, 0, MaxCoversScroll(), aMs);
        if (iAxis != EAxisBack)
            {
            TReal before = iPush;
            moving |= SpringTo(iPush, iPushVel, iPushTarget, KPushOmega, aMs, 0.002);

            if ((before - iPushTarget) * (iPush - iPushTarget) < 0)
                {
                iPush = iPushTarget;
                iPushVel = 0;
                }
            if (iPush == 0 && iPushTarget == 0 && iAlbum >= 0 && !iViewerOpen)
                {
                iAlbum = -1;
                EndSelect();
                }
            }
        }

    if (ActiveVelocity() != 0 || iAxis == EAxisScroll || iAxis == EAxisScrub)
        {
        iIndicatorMs = now;
        iIndicator = 1;
        }
    else if (iIndicator > 0)
        {
        if (now - iIndicatorMs > 500)
            iIndicator = Max(0.0, iIndicator - aMs / 250.0);
        moving = ETrue;
        }
    if (iViewerOpen)
        moving |= StepViewer(aMs, now);
    UpdateStatusBar();
    return moving;
    }

TBool CPhotosView::PumpUploads(TBool aMoving)
    {
    TInt budget = aMoving ? KUploadMoving : KUploadIdle;
    TBool more = iViewerOpen && UploadImages(budget);
    more |= iThumbs->Pump(Max(1, budget));
    return more;
    }

TInt CPhotosView::FrostKey() const
    {
    TInt key = iThumbEpoch * 31 + iTab * 7 + iAlbum * 131 + iSelected.Count() * 4099 + iSelecting * 8191 +
               iPopupItem * 37;
    return key * 17 + (TInt)iLibraryScroll * 3 + (TInt)iAlbumScroll * 5 + (TInt)iAlbumsScroll * 7 +
           (TInt)iFavouritesScroll * 11 + (TInt)iCoversScroll * 13 +
           (TInt)(iPush * 512) + (TInt)Rect().Width() + iLevel * 7919 + (TInt)(iGridScale * 997) +
           (TInt)(iGridMidY * 13) + (TInt)(iLevelP * 4096) + (iAxis == EAxisGridPinch ? 77 : 0);
    }

void CPhotosView::RenderFrame()
    {
    static TInt frames = 0;
    if (frames < 3)
        Trace(_L("view: frame %d begin"), frames);
    iFrame++;
    iThumbs->BeginFrame();
    iThumbs->SetHold(iViewerOpen);
    iLabels->NextFrame(*iGl);
    if (iViewerOpen && iPlayer)
        iPlayer->UpdateSoft();
    const TBool viewerOpaque = iViewerOpen && iTrans >= 1 && !iClosing && iDismissY == 0 && iDismissX == 0;

    const TInt key = FrostKey();
    if (iGl->HasFrost() && key != iFrostKey && (!iMoving || iFrame - iFrostFrame >= KFrostEvery) && !viewerOpaque)
        {
        iGl->BeginFrost(KBackground);
        DrawScreen(ETrue);
        iGl->EndFrost();
        iFrostKey = key;
        iFrostFrame = iFrame;
        gFrosts++;
        }
    if (iViewerOpen)
        DrawViewer();
    else
        {
        iGl->BeginFrame(KBackground);

        iGl->SetDim(iPopupP * 0.6);
        DrawScreen(EFalse);
        DrawBars();
        iGl->SetDim(0);
        if (iPopupP > 0)
            DrawPopup();
        }
    gDrawCalls += iGl->DrawCalls();
    if (frames < 3)
        Trace(_L("view: frame %d drawn"), frames);
    if (frames == 2)
        TraceBuffered();
    frames++;
    DrawScreenCorners();
    const TBool ok = iGl->EndFrame();
    if (!ok)
        {
        Trace(_L("view: gl context lost"));
        StopGl();
        StartGl();
        }
    }

void CPhotosView::DrawScreenCorners()
    {
    if (!iIcons)
        return;
    const TInt r = iIcons->Size(EIconScreenCorner).iWidth;
    const TInt w = Rect().Width(), h = Rect().Height(), top = VisibleTop();
    iGl->SetDim(0);
    iGl->NoClip();

    if (top > 0)
        iGl->FillRect(TRect(0, 0, w, top), 0x000000, 1);
    iIcons->AddCorner(*iGl, TRect(0, top, r, top + r), 0);
    iIcons->AddCorner(*iGl, TRect(w - r, top, w, top + r), 1);
    iIcons->AddCorner(*iGl, TRect(0, h - r, r, h), 2);
    iIcons->AddCorner(*iGl, TRect(w - r, h - r, w, h), 3);
    iGl->DrawBatch(iIcons->Texture(EIconScreenCorner), 0x000000, 1);
    }

TGridView TGridView::Plain(TReal aScroll, TInt aOffsetX)
    {
    TGridView view;
    view.iScale = 1;
    view.iAx = 0;
    view.iAy = aScroll;
    view.iSx = aOffsetX;
    view.iSy = 0;
    view.iAlpha = 1;
    return view;
    }

TGridView TGridView::Between(const TGridView& aFrom, const TGridView& aTo, TReal aT)
    {
    TGridView view = aTo;
    TReal ratio;
    Math::Pow(ratio, aTo.iScale / aFrom.iScale, aT);
    view.iScale = aFrom.iScale * ratio;
    view.iSx = Lerp(aFrom.iSx, aTo.iSx, aT);
    view.iSy = Lerp(aFrom.iSy, aTo.iSy, aT);
    return view;
    }

void CPhotosView::DrawGrid(const CGridLayout& aGrid, const TGridView& aView, TBool aFrost, TInt aHidden)
    {
    const TInt h = Rect().Height(), w = Rect().Width();
    const TReal k = aView.iScale;
    const TBool plain = k == 1 && aView.iAlpha >= 1;

    const TInt y0 = (TInt)(aView.iAy - aView.iSy / k) - 1, y1 = (TInt)(aView.iAy + (h - aView.iSy) / k) + 1;
    const TInt columns = aGrid.Columns(), rowHeight = aGrid.RowHeight();

    const TInt side = (TInt)((aGrid.ColumnX1(0) - aGrid.ColumnX0(0)) * k + 0.5);
    TInt firstItem = -1, lastItem = -1;
    TInt headers[8];
    TInt headerCount = 0;
    gCellCount = 0;
    for (TInt s = aGrid.SectionCount() ? aGrid.SectionAt(y0) : 0; s < aGrid.SectionCount(); s++)
        {
        const CGridLayout::TSection& section = aGrid.Section(s);
        if (section.iTop >= y1)
            break;
        if (aGrid.HeaderHeight() > 0 && section.iRowsTop > y0 && headerCount < 8)
            headers[headerCount++] = s;
        const TInt rows = (section.iCount + columns - 1) / columns;
        const TInt r0 = Max(0, (y0 - section.iRowsTop) / rowHeight);
        const TInt r1 = Min(rows - 1, (y1 - section.iRowsTop) / rowHeight);
        for (TInt r = r0; r <= r1; r++)
            for (TInt c = 0; c < columns; c++)
                {
                const TInt item = section.iFirst + r * columns + c;
                if (r * columns + c >= section.iCount)
                    break;
                if (firstItem < 0)
                    firstItem = item;
                lastItem = item;
                if (item == aHidden)
                    continue;
                const TReal top = section.iRowsTop + r * rowHeight;
                const TReal x0 = aView.iSx + (aGrid.ColumnX0(c) - aView.iAx) * k;
                const TReal x1 = aView.iSx + (aGrid.ColumnX1(c) - aView.iAx) * k;
                const TReal ty0 = aView.iSy + (top - aView.iAy) * k;
                const TReal ty1 = aView.iSy + (top + aGrid.CellHeight() - aView.iAy) * k;
                if (x1 <= 0 || x0 >= w)
                    continue;
                TInt atlas = -1;
                TReal uv[4];
                const TBool ready = iThumbs->Get(aGrid.PhotoAt(item), CThumbCache::EVisible, atlas, uv, side);
                AddGridQuad(x0, ty0, x1, ty1, ready ? atlas : -1, ready ? uv : NULL);
                if (gCellCount < KMaxGridQuads)
                    {
                    TCellMark& mark = gCells[gCellCount++];
                    mark.iX0 = x0;
                    mark.iY0 = ty0;
                    mark.iX1 = x1;
                    mark.iY1 = ty1;
                    mark.iPhoto = aGrid.PhotoAt(item);
                    mark.iReady = ready;
                    }
                }
        }
    FlushGridQuads(*iGl, *iThumbs, aView.iAlpha);
    if (aFrost)
        return;
    DrawCellMarks(plain, aView.iAlpha);

    if (firstItem >= 0 && plain)
        {
        TInt atlas;
        TReal uv[4];
        const TInt perScreen = (h / Max(1, rowHeight) + 1) * columns;
        const TBool down = ActiveVelocity() >= 0;
        const TInt ahead = perScreen * 3 / 2, behind = perScreen / 2;
        for (TInt i = 1; i <= Max(ahead, behind); i++)
            {
            const TInt next = lastItem + i, previous = firstItem - i;
            const CThumbCache::TPriority near = i <= columns * 2 ? CThumbCache::ENear : CThumbCache::EAhead;
            if (next < aGrid.Count() && i <= (down ? ahead : behind))
                iThumbs->Get(aGrid.PhotoAt(next), near, atlas, uv, side);
            if (previous >= 0 && i <= (down ? behind : ahead))
                iThumbs->Get(aGrid.PhotoAt(previous), near, atlas, uv, side);
            }
        }

    TReal textAlpha = aView.iAlpha;
    if (k != 1)
        textAlpha *= Max(0.0, 1 - Abs(k > 1 ? k - 1 : 1 / k - 1) * 3);
    if (textAlpha <= 0.01)
        return;
    TBuf<32> text;
    for (TInt i = 0; i < headerCount; i++)
        {
        const CGridLayout::TSection& section = aGrid.Section(headers[i]);
        text.Zero();
        if (aGrid.Grouping() == CGridLayout::EByDay)
            {
            text.AppendNum(section.iDay);
            text.Append(' ');
            }
        if (aGrid.Grouping() != CGridLayout::EByYear)
            {
            text.Append(TPtrC(KMonths[section.iMonth - 1]));
            text.Append(' ');
            }
        text.AppendNum(section.iYear);
        const TInt x = (TInt)(aView.iSx + (Scale(10) - aView.iAx) * k);
        const TInt y = (TInt)(aView.iSy + (section.iTop + Scale(KHeaderBaseline) - aView.iAy) * k);
        iLabels->Draw(*iGl, text, aGrid.Grouping() == CGridLayout::EByYear ? EFontYear : EFontHeader, x, y,
                      ELabelLeft, KText, textAlpha);
        }
    const TInt footer = (TInt)(aView.iSy + (aGrid.FooterTop() + Scale(30) - aView.iAy) * k);
    if (footer < h + Scale(30))
        {
        const TInt kind = &aGrid == iLibraryGrid ? -1 : ShownKind();
        if (kind == CAlbum::ERecentlyDeleted)
            text.Copy(_L("Deleted for good after 30 days"));
        else if (kind == CAlbum::EVideos)
            FormatCount(text, aGrid.Count(), _L("Video"), _L("Videos"));
        else
            FormatCount(text, aGrid.Count(), _L("Item"), _L("Items"));
        if (aGrid.Count() > 0 || kind == CAlbum::ERecentlyDeleted)
            iLabels->Draw(*iGl, text, EFontSmall, (TInt)(aView.iSx + (w / 2 - aView.iAx) * k), footer,
                          ELabelCentre, KGrey, textAlpha);
        }
    }

void CPhotosView::DrawCellMarks(TBool aPlain, TReal aAlpha)
    {
    const TInt count = gCellCount;
    gCellCount = 0;
    if (!iIcons || count == 0)
        return;
    const TBool trash = ShownKind() == CAlbum::ERecentlyDeleted;
    const TInt band = Scale(26);
    const TReal gh = iIcons->Size(EIconGradient).iHeight;
    TInt shaded = 0;
    for (TInt i = 0; i < count && aPlain; i++)
        {
        const TCellMark& m = gCells[i];
        if (!trash && !IsVideo(m.iPhoto))
            continue;
        iGl->AddQuad(m.iX0, m.iY1 - band, m.iX1, m.iY1, 0, 1 - 0.5 / gh, 1, 0.5 / gh);
        shaded++;
        }
    if (shaded)
        iGl->DrawBatch(iIcons->Texture(EIconGradient), 0x000000, 0.6 * aAlpha);

    TInt glyphs = 0;
    for (TInt i = 0; i < count; i++)
        {
        const TCellMark& m = gCells[i];
        if (m.iReady || !IsVideo(m.iPhoto) || m.iX1 - m.iX0 < iIcons->Size(EIconVideo).iWidth + Scale(8))
            continue;
        iIcons->Add(*iGl, EIconVideo, (TInt)((m.iX0 + m.iX1) / 2), (TInt)((m.iY0 + m.iY1) / 2) - Scale(4));
        glyphs++;
        }
    if (glyphs)
        iGl->DrawBatch(iIcons->Texture(EIconVideo), KGrey, aAlpha);
    TBuf<16> text;
    TTime now;
    now.UniversalTime();
    TInt plays = 0;
    for (TInt i = 0; i < count && aPlain; i++)
        {
        const TCellMark& m = gCells[i];
        const TInt baseline = (TInt)m.iY1 - Scale(7);
        if (trash)
            {
            const TInt at = iMoves->Find(*iLibrary->Photo(m.iPhoto).iPath);
            const TInt left = at >= 0 ? Max(0, (TInt)CMoves::KTrashDays - now.DaysFrom(iMoves->When(at)).Int()) :
                                        (TInt)CMoves::KTrashDays;
            text.Zero();
            text.AppendNum(left);
            text.Append(left == 1 ? _L(" day") : _L(" days"));
            iLabels->Draw(*iGl, text, EFontCaptionDigits, (TInt)m.iX0 + Scale(6), baseline, ELabelLeft, KText, aAlpha,
                          ETrue);
            }
        if (IsGif(m.iPhoto))
            iLabels->Draw(*iGl, _L("GIF"), EFontCaption, (TInt)m.iX1 - Scale(6), baseline, ELabelRight, KText, aAlpha);
        if (!IsVideo(m.iPhoto))
            continue;
        const TInt seconds = iLibrary->Photo(m.iPhoto).iDuration;
        if (seconds > 0)
            {
            FormatDuration(text, seconds);
            iLabels->Draw(*iGl, text, EFontCaptionDigits, (TInt)m.iX1 - Scale(6), baseline, ELabelRight, KText, aAlpha,
                          ETrue);
            }
        else
            {
            iIcons->Add(*iGl, EIconPlay, (TInt)m.iX1 - Scale(14), (TInt)m.iY1 - Scale(13));
            plays++;
            }
        }
    if (plays)
        iGl->DrawBatch(iIcons->Texture(EIconPlay), KText, aAlpha);
    if (!iSelecting || iSelected.Count() == 0)
        return;

    const TInt r = Scale(10);
    TInt ticks = 0;
    for (TInt i = 0; i < count; i++)
        if (IsSelected(gCells[i].iPhoto))
            {
            iGl->AddQuad(gCells[i].iX0, gCells[i].iY0, gCells[i].iX1, gCells[i].iY1, 0, 0, 1, 1);
            ticks++;
            }
    if (ticks == 0)
        return;
    iGl->DrawBatchFlat(0x000000, 0.3 * aAlpha);
    for (TInt pass = 0; pass < 3; pass++)
        {
        const TIconId icon = pass == 0 ? EIconDisk : (pass == 1 ? EIconRing : EIconCheck);
        for (TInt i = 0; i < count; i++)
            {
            if (!IsSelected(gCells[i].iPhoto))
                continue;
            const TInt cx = (TInt)gCells[i].iX1 - Scale(15), cy = (TInt)gCells[i].iY1 - Scale(15);
            if (pass == 0)
                iGl->AddQuad(cx - r, cy - r, cx + r, cy + r, 0, 0, 1, 1);
            else
                iIcons->Add(*iGl, icon, cx, cy);
            }
        iGl->DrawBatch(iIcons->Texture(icon), pass == 0 ? KBlue : KText, aAlpha);
        }
    }

void CPhotosView::DrawAlbums(TReal aScroll, TInt aOffsetX, TBool aFrost)
    {
    const TInt h = Rect().Height(), w = Rect().Width();
    const TInt scroll = (TInt)aScroll;
    const TInt corner = iIcons ? iIcons->Size(EIconCorner).iWidth : 0;
    TInt first = -1, last = -1;
    TInt sharpPhotos[KCovers];
    const TGlTexture* sharpTextures[KCovers];
    TRect sharpRects[KCovers];
    TInt sharpCount = 0;
    for (TInt a = 0; a < iLibrary->CoverAlbumCount(); a++)
        {
        TRect cover = AlbumCoverRect(a);
        cover.Move(aOffsetX, -scroll);
        if (cover.iBr.iY + Scale(KAlbumLabel) < 0 || cover.iTl.iY >= h || cover.iBr.iX <= aOffsetX ||
            cover.iTl.iX >= aOffsetX + w)
            continue;
        if (first < 0)
            first = a;
        last = a;
        const CAlbum& album = iLibrary->Album(a);
        const TInt photo = album.iPhotos.Count() ? album.iPhotos[album.iPhotos.Count() - 1] : -1;

        const TGlTexture* sharp = photo >= 0 && iPush == 0 ? CoverTexture(photo, cover.Width()) : NULL;
        if (sharp && sharpCount < KCovers)
            {
            sharpPhotos[sharpCount] = photo;
            sharpTextures[sharpCount] = sharp;
            sharpRects[sharpCount++] = cover;
            continue;
            }
        TInt atlas = -1;
        TReal uv[4];
        const TBool ready = photo >= 0 && iThumbs->Get(photo, CThumbCache::EVisible, atlas, uv);
        AddGridQuad(cover.iTl.iX, cover.iTl.iY, cover.iBr.iX, cover.iBr.iY, ready ? atlas : -1, ready ? uv : NULL);
        }
    FlushGridQuads(*iGl, *iThumbs, 1);
    for (TInt i = 0; i < sharpCount; i++)
        DrawCover(sharpPhotos[i], *sharpTextures[i], sharpRects[i]);
    DrawLists(scroll, aOffsetX, aFrost);
    if (first < 0)
        return;

    if (iIcons && corner > 0)
        {
        for (TInt a = first; a <= last; a++)
            {
            TRect c = AlbumCoverRect(a);
            c.Move(aOffsetX, -scroll);
            iIcons->AddCorner(*iGl, TRect(c.iTl.iX, c.iTl.iY, c.iTl.iX + corner, c.iTl.iY + corner), 0);
            iIcons->AddCorner(*iGl, TRect(c.iBr.iX - corner, c.iTl.iY, c.iBr.iX, c.iTl.iY + corner), 1);
            iIcons->AddCorner(*iGl, TRect(c.iTl.iX, c.iBr.iY - corner, c.iTl.iX + corner, c.iBr.iY), 2);
            iIcons->AddCorner(*iGl, TRect(c.iBr.iX - corner, c.iBr.iY - corner, c.iBr.iX, c.iBr.iY), 3);
            }
        iGl->DrawBatch(iIcons->Texture(EIconCorner), KBackground, 1);
        }
    if (aFrost)
        return;
    iLabels->Draw(*iGl, _L("Albums"), EFontTitle, aOffsetX + Scale(KAlbumMargin),
                  iTopInset + Scale(KTitleBaseline) - scroll, ELabelLeft, KText, 1);
    TBuf<48> text;
    for (TInt a = first; a <= last; a++)
        {
        TRect c = AlbumCoverRect(a);
        c.Move(aOffsetX, -scroll);
        const CAlbum& album = iLibrary->Album(a);
        text.Copy(album.iName->Left(40));
        iLabels->Fit(text, EFontBody, c.Width());
        iLabels->Draw(*iGl, text, EFontBody, c.iTl.iX, c.iBr.iY + Scale(27), ELabelLeft, KText, 1);
        text.Zero();
        text.AppendNum(album.iPhotos.Count());
        iLabels->Draw(*iGl, text, EFontSmall, c.iTl.iX, c.iBr.iY + Scale(49), ELabelLeft, KGrey, 1);
        }

    TInt atlas;
    TReal uv[4];
    for (TInt a = last + 1; a < Min(iLibrary->CoverAlbumCount(), last + 2 * CoverRows() + 1); a++)
        if (iLibrary->Album(a).iPhotos.Count())
            iThumbs->Get(iLibrary->Album(a).iPhotos[iLibrary->Album(a).iPhotos.Count() - 1], CThumbCache::ENear,
                         atlas, uv);
    }

void CPhotosView::DrawScreen(TBool aFrost)
    {
    const TInt w = Rect().Width(), h = Rect().Height();
    const TInt hidden = iViewerOpen ? iCurrent : (iPopupOpen || iPopupP > 0 ? iPopupItem : -1);
    if (iTab == ETabAll)
        {
        if (iAxis == EAxisGridPinch)
            {
            TGridView view = TGridView::Plain(iLibraryScroll, 0);
            view.iScale = iGridScale;
            view.iAx = iGridAx;
            view.iAy = iGridAy;
            view.iSx = iGridMidX;
            view.iSy = iGridMidY;
            DrawGrid(*iLibraryGrid, view, aFrost, -1);
            }
        else if (iLevelTrans)
            {
            if (iOldGrid)
                {
                TGridView old = TGridView::Between(iOldFrom, iOldTo, iLevelP);
                old.iAlpha = 1 - iLevelP;
                DrawGrid(*iOldGrid, old, aFrost, -1);
                }
            TGridView view = TGridView::Between(iNewFrom, iNewTo, iLevelP);
            view.iAlpha = iOldGrid ? iLevelP : 1;
            DrawGrid(*iLibraryGrid, view, aFrost, -1);
            }
        else
            DrawGrid(*iLibraryGrid, TGridView::Plain(iLibraryScroll, 0), aFrost, hidden);
        if (!aFrost && iLibrary->Visible().Count() == 0)
            iLabels->Draw(*iGl, iScanning ? _L("Looking for photos...") : _L("No Photos"),
                          iScanning ? EFontBody : EFontTitle, w / 2, h / 2, ELabelCentre, KGrey, 1);
        return;
        }
    if (iTab == ETabFavourites)
        {
        DrawGrid(*iFavouritesGrid, TGridView::Plain(iFavouritesScroll, 0), aFrost, hidden);
        if (!aFrost)
            {
            iLabels->Draw(*iGl, _L("Favourites"), EFontTitle, Scale(KAlbumMargin),
                          iTopInset + Scale(KTitleBaseline) - (TInt)iFavouritesScroll, ELabelLeft, KText, 1);
            if (iFavouritesGrid->Count() == 0)
                iLabels->Draw(*iGl, _L("No Favourites"), EFontTitle, w / 2, h / 2, ELabelCentre, KGrey, 1);
            }
        return;
        }
    const TInt gridX = (TInt)(w * (1 - iPush));
    if (iPush < 1)
        {

        const TReal dim = iGl->Dim();
        iGl->SetDim(1 - (1 - dim) * (1 - iPush * 0.1));
        if (!aFrost)
            iGl->Clip(TRect(0, 0, gridX, h));
        DrawAlbums(iAlbumsScroll, -(TInt)(w * 0.3 * iPush), aFrost);
        iGl->NoClip();
        iGl->SetDim(dim);
        }
    if (iPush > 0 && iAlbum >= 0)
        {
        iGl->FillRect(TRect(gridX, 0, w, h), KBackground, 1);
        DrawGrid(*iAlbumGrid, TGridView::Plain(iAlbumScroll, gridX), aFrost, hidden);
        if (!aFrost)
            {
            const CAlbum& album = iLibrary->Album(iAlbum);
            TBuf<40> title(album.iName->Left(40));
            iLabels->Fit(title, EFontTitle, w - Scale(24));
            const TInt top = iTopInset + Scale(KTopBar) - (TInt)iAlbumScroll;
            iLabels->Draw(*iGl, title, EFontTitle, gridX + Scale(KAlbumMargin), top + Scale(KTitleBaseline),
                          ELabelLeft, KText, 1);
            if (iAlbumGrid->Count() == 0)
                iLabels->Draw(*iGl, album.iKind == CAlbum::EVideos ? _L("No Videos") : _L("No Photos"), EFontTitle,
                              gridX + w / 2, h / 2, ELabelCentre, KGrey, 1);
            }
        }
    }

TInt CPhotosView::ListRows(TListRow* aRows) const
    {
    const CAlbum::TKind kinds[KListRows] = { CAlbum::EVideos, CAlbum::EScreenshots, CAlbum::EHidden,
                                             CAlbum::ERecentlyDeleted };
    const TIconId icons[KListRows] = { EIconVideo, EIconScreenshot, EIconHidden, EIconTrash };
    TInt n = 0;
    for (TInt r = 0; r < KListRows; r++)
        {
        const TInt album = iLibrary->AlbumOfKind(kinds[r]);
        if (album < 0)
            continue;
        aRows[n].iIcon = icons[r];
        aRows[n].iName.Set(*iLibrary->Album(album).iName);
        aRows[n].iCount = iLibrary->Album(album).iPhotos.Count();
        aRows[n].iAlbum = album;
        n++;
        }
    return n;
    }

TRect CPhotosView::ListRowRect(TInt aRow) const
    {
    TListRow rows[KListRows];
    const TInt count = ListRows(rows);
    const TInt utilities = count - 2;
    const TInt group = aRow >= utilities ? 1 : 0;
    const TInt margin = Scale(KAlbumMargin);
    TInt top = CoversHeight() + Scale(KListGroupGap) + aRow * Scale(KListRow);
    if (group == 1)
        top += Scale(KListGroupGap);
    return TRect(margin, top, Rect().Width() - margin, top + Scale(KListRow));
    }

void CPhotosView::FillRoundRect(const TRect& aRect, TInt aRadius, TUint32 aRgb, TReal aAlpha)
    {
    if (!iIcons)
        return;
    const TInt r = Min(aRadius, Min(aRect.Width(), aRect.Height()) / 2);
    const TInt x0 = aRect.iTl.iX, y0 = aRect.iTl.iY, x1 = aRect.iBr.iX, y1 = aRect.iBr.iY;
    iGl->AddQuad(x0, y0, x0 + r, y0 + r, 0, 0, 0.5, 0.5);
    iGl->AddQuad(x1 - r, y0, x1, y0 + r, 0.5, 0, 1, 0.5);
    iGl->AddQuad(x0, y1 - r, x0 + r, y1, 0, 0.5, 0.5, 1);
    iGl->AddQuad(x1 - r, y1 - r, x1, y1, 0.5, 0.5, 1, 1);
    iGl->DrawBatch(iIcons->Texture(EIconDisk), aRgb, aAlpha);
    iGl->AddQuad(x0 + r, y0, x1 - r, y0 + r, 0, 0, 1, 1);
    iGl->AddQuad(x0, y0 + r, x1, y1 - r, 0, 0, 1, 1);
    iGl->AddQuad(x0 + r, y1 - r, x1 - r, y1, 0, 0, 1, 1);
    iGl->DrawBatchFlat(aRgb, aAlpha);
    }

void CPhotosView::DrawLists(TInt aScroll, TInt aOffsetX, TBool aFrost)
    {
    TListRow rows[KListRows];
    const TInt count = ListRows(rows);
    if (count == 0)
        return;
    const TInt h = Rect().Height();
    TRect first = ListRowRect(0), last = ListRowRect(count - 1);
    if (last.iBr.iY - aScroll < 0 || first.iTl.iY - Scale(KListGroupGap) - aScroll >= h)
        return;
    const TInt utilities = count - 2;
    for (TInt g = 0; g < 2; g++)
        {
        const TInt r0 = g == 0 ? 0 : utilities, r1 = g == 0 ? utilities - 1 : count - 1;
        if (r1 < r0)
            continue;
        TRect card(ListRowRect(r0).iTl, ListRowRect(r1).iBr);
        card.Move(aOffsetX, -aScroll);
        FillRoundRect(card, Scale(7), KCard, 1);

        for (TInt r = r0; r < r1; r++)
            {
            const TInt divider = ListRowRect(r).iBr.iY - aScroll;
            iGl->FillRect(TRect(card.iTl.iX + Scale(52), divider, card.iBr.iX, divider + 1), KHairline,
                          KHairlineAlpha);
            }
        }
    if (aFrost)
        return;
    TBuf<16> number;
    for (TInt r = 0; r < count; r++)
        {
        TRect row = ListRowRect(r);
        row.Move(aOffsetX, -aScroll);
        if (r == 0 || r == utilities)
            iLabels->Draw(*iGl, r == 0 ? _L("MEDIA TYPES") : _L("UTILITIES"), EFontCaption, row.iTl.iX + Scale(12),
                          row.iTl.iY - Scale(10), ELabelLeft, KGrey, 1);
        const TInt cy = row.Center().iY;
        if (iIcons)
            {
            iIcons->Draw(*iGl, rows[r].iIcon, row.iTl.iX + Scale(27), cy, KText, 1);
            iIcons->Draw(*iGl, EIconChevron, row.iBr.iX - Scale(16), cy, KGrey, 1);
            }
        iLabels->Draw(*iGl, rows[r].iName, EFontBody, row.iTl.iX + Scale(52), cy + Scale(7), ELabelLeft, KText, 1);
        number.Zero();
        number.AppendNum(rows[r].iCount);
        iLabels->Draw(*iGl, number, EFontSmall, row.iBr.iX - Scale(30), cy + Scale(6), ELabelRight, KGrey, 1);
        }
    }

TInt CPhotosView::BarSlotX(TInt aSlot, TInt aSlots) const
    {
    const TInt w = Rect().Width();
    const TInt columnW = Min(w, Scale(360)), columnX = (w - columnW) / 2;
    const TInt first = columnX + Scale(KBarSlot), last = columnX + columnW - Scale(KBarSlot);
    return aSlots > 1 ? first + (last - first) * aSlot / (aSlots - 1) : (first + last) / 2;
    }

void CPhotosView::DrawBarBand(TInt aTop, TInt aBottom, TInt aHairlineY, TReal aAlpha)
    {
    const TInt w = Rect().Width();
    iGl->DrawFrost(TRect(0, aTop, w, aBottom), KBarTint, KBarFrost, aAlpha);
    iGl->FillRect(TRect(0, aHairlineY, w, aHairlineY + 1), KHairline, KHairlineAlpha * aAlpha);
    }

void CPhotosView::DrawTabBar()
    {
    const TInt h = Rect().Height();
    const TInt top = h - Scale(KTabBar);
    DrawBarBand(top, h, top, 1);
    if (!iIcons)
        return;
    const TInt cy = top + Scale(KTabBar) / 2;
    if (iSelecting)
        {

        TAction actions[KMaxActions];
        TIconId icons[KMaxActions];
        const TInt n = SelectActions(actions, icons);
        for (TInt i = 0; i < n; i++)
            iIcons->Draw(*iGl, icons[i], BarSlotX(i, n), cy, iSelected.Count() ? KText : KGrey, 1);
        return;
        }

    iIcons->Draw(*iGl, EIconExit, BarSlotX(0, KTabSlots), cy, KGrey, 1);
    const TIconId outlines[KTabs] = { EIconLibrary, EIconHeart, EIconAlbums };
    const TIconId filled[KTabs] = { EIconLibraryFull, EIconHeartFull, EIconAlbumsFull };
    for (TInt t = 0; t < KTabs; t++)
        iIcons->Draw(*iGl, (TInt)iTab == t ? filled[t] : outlines[t], BarSlotX(t + 1, KTabSlots), cy,
                     (TInt)iTab == t ? KText : KGrey, 1);
    }

TBool CPhotosView::HasSelectButton() const
    {
    if (iSelecting)
        return ETrue;
    const CGridLayout* grid = ActiveGrid();
    return grid && grid->Count() > 0 && (iTab != ETabAlbums || iPush >= 1);
    }

TRect CPhotosView::TopButtonRect() const
    {
    const TInt w = Rect().Width();
    const TInt height = iIcons ? iIcons->Size(EIconButton).iHeight : Scale(32);
    const TInt width = iLabels->TextWidth(iSelecting ? _L("Cancel") : _L("Select"), EFontButton) +
                       2 * Scale(KButtonPad);
    const TInt cy = iTopInset + Scale(KTopBar) / 2 + Scale(2);
    return TRect(w - Scale(10) - width, cy - height / 2, w - Scale(10), cy - height / 2 + height);
    }

void CPhotosView::DrawTopBar()
    {
    const TInt w = Rect().Width();
    const TBool grid = iTab != ETabAlbums || (iAlbum >= 0 && iPush > 0);
    if (!grid || !iIcons)
        return;
    const TReal alpha = iTab == ETabAlbums ? Min(1.0, iPush) : 1;
    const TInt offsetX = iTab == ETabAlbums ? (TInt)(w * (1 - iPush)) : 0;
    iIcons->DrawStretched(*iGl, EIconGradient, TRect(offsetX, iTopInset, w + offsetX, iTopInset + Scale(KTopShade)),
                          0x000000, KTopShadeAlpha * alpha);
    const TInt cy = iTopInset + Scale(KTopBar) / 2 + Scale(2);
    if (iTab == ETabAlbums && !iSelecting)
        {
        iIcons->Draw(*iGl, EIconBack, offsetX + Scale(18), cy, KText, alpha);
        iLabels->Draw(*iGl, _L("Albums"), EFontBody, offsetX + Scale(32), cy + Scale(7), ELabelLeft, KText, alpha);
        }
    if (iSelecting)
        {
        TBuf<32> title;
        if (iSelected.Count() == 0)
            title.Copy(_L("Select Items"));
        else
            {
            title.AppendNum(iSelected.Count());
            title.Append(_L(" Selected"));
            }
        iLabels->Draw(*iGl, title, iSelected.Count() ? EFontHeaderDigits : EFontHeader, w / 2, cy + Scale(7),
                      ELabelCentre, KText, 1);
        }
    if (HasSelectButton())
        {
        TRect button = TopButtonRect();
        button.Move(offsetX, 0);
        iIcons->DrawThreeSlice(*iGl, EIconButton, button, alpha);
        iLabels->Draw(*iGl, iSelecting ? _L("Cancel") : _L("Select"), EFontButton, button.Center().iX,
                      button.Center().iY + Scale(6), ELabelCentre, KText, alpha);
        }
    }

void CPhotosView::DrawScrollIndicator()
    {
    if (iIndicator <= 0)
        return;
    const CGridLayout* grid = ActiveGrid();
    const TBool albumsList = iTab == ETabAlbums && !InAlbum();
    const TInt content = albumsList ? AlbumsHeight() : (grid ? grid->ContentHeight() : 0);
    const TInt h = Rect().Height(), w = Rect().Width();
    if (content <= h)
        return;
    const TInt trackTop = iTopInset + Scale(3), trackBottom = h - Scale(KTabBar) - Scale(3);
    const TInt track = trackBottom - trackTop;
    const TReal scroll = albumsList ? iAlbumsScroll : ActiveScroll();
    const TInt thumb = Max(Scale(36), track * h / content);
    const TReal at = Clamp(scroll / (content - h), 0, 1);
    const TInt y = trackTop + (TInt)((track - thumb) * at);
    const TBool scrub = iAxis == EAxisScrub;
    const TInt width = scrub ? Scale(6) : Scale(3);
    iGl->FillRect(TRect(w - width - Scale(3), y, w - Scale(3), y + thumb), 0xFFFFFF, 0.5 * iIndicator);
    if (scrub && grid)
        {

        const CGridLayout::TSection& section = grid->Section(grid->SectionAt((TInt)scroll + iTopInset));
        TBuf<32> text(TPtrC(KMonths[section.iMonth - 1]));
        text.Append(' ');
        text.AppendNum(section.iYear);
        const TInt tw = iLabels->TextWidth(text, EFontHeader);

        if (iIcons)
            {
            const TInt keyHeight = iIcons->Size(EIconKey).iHeight, pad = Scale(12);
            const TRect key(w - Scale(24) - tw - 2 * pad, y + thumb / 2 - keyHeight / 2, w - Scale(24),
                            y + thumb / 2 - keyHeight / 2 + keyHeight);
            TRect shadow(key);
            shadow.Grow(iIcons->ShadowMargin(), iIcons->ShadowMargin());
            shadow.Move(0, Scale(2));
            iIcons->DrawThreeSlice(*iGl, EIconKeyShadow, shadow, 0.6);
            iIcons->DrawThreeSlice(*iGl, EIconKey, key, 1);
            iLabels->Draw(*iGl, text, EFontHeader, key.iTl.iX + pad, key.iTl.iY + keyHeight / 2 + Scale(7), ELabelLeft,
                          KText, 1, ETrue);
            }
        }
    }

void CPhotosView::DrawBars()
    {
    DrawTopBar();
    DrawTabBar();
    DrawScrollIndicator();
    }

void CPhotosView::Feedback() const
    {
    MTouchFeedback* feedback = MTouchFeedback::Instance();
    if (feedback)
        feedback->InstantFeedback(ETouchFeedbackBasic);
    }

void CPhotosView::HandlePointerEventL(const TPointerEvent& aEvent)
    {
    if (!iGl)
        return;
    const TAdvancedPointerEvent* advanced = aEvent.AdvancedPointerEvent();
    const TInt pointer = advanced ? advanced->PointerNumber() : 0;
    if (pointer > 1)
        return;
    const TUint32 now = NowMs();
    if (iViewerOpen && !iClosing && iPlayer && iPlayer->Soft())
        {

        VideoPointerL(aEvent, pointer);
        }
    else if (iViewerOpen)
        ViewerPointer(aEvent, pointer, now);
    else if (iPopupOpen)
        {
        if (pointer == 0)
            PopupPointerL(aEvent);
        }
    else
        GridPointerL(aEvent, pointer, now);
    RequestFrame();
    }

void CPhotosView::BeginGridPinch()
    {
    iAxis = EAxisGridPinch;
    iLibraryVel = 0;
    iLibraryScroll = Clamp(iLibraryScroll, 0, MaxScroll(*iLibraryGrid));
    iGridPinchDistance = Max(20.0, Distance(iPointer[0], iPointer[1]));
    iGridMidX = (iPointer[0].iX + iPointer[1].iX) / 2.0;
    iGridMidY = (iPointer[0].iY + iPointer[1].iY) / 2.0;

    iGridAx = iGridMidX;
    iGridAy = iGridMidY + iLibraryScroll;
    iGridScale = 1;
    }

void CPhotosView::MoveGridPinch()
    {
    iGridScale = Clamp(Distance(iPointer[0], iPointer[1]) / iGridPinchDistance, 0.35, 3.0);
    iGridMidX = (iPointer[0].iX + iPointer[1].iX) / 2.0;
    iGridMidY = (iPointer[0].iY + iPointer[1].iY) / 2.0;
    }

void CPhotosView::EndGridPinch()
    {
    TInt level = iLevel;
    if (iGridScale > KPinchIn && iLevel < KLevels - 1)
        level++;
    else if (iGridScale < KPinchOut && iLevel > 0)
        level--;
    TGridView pinched = TGridView::Plain(iLibraryScroll, 0);
    pinched.iScale = iGridScale;
    pinched.iAx = iGridAx;
    pinched.iAy = iGridAy;
    pinched.iSx = iGridMidX;
    pinched.iSy = iGridMidY;
    ChangeLevel(level, pinched);
    }

void CPhotosView::ZoomInAt(const TPoint& aPos)
    {
    TGridView view = TGridView::Plain(iLibraryScroll, 0);
    view.iAx = aPos.iX;
    view.iAy = aPos.iY + iLibraryScroll;
    view.iSx = aPos.iX;
    view.iSy = aPos.iY;
    iLibraryVel = 0;
    ChangeLevel(iLevel + 1, view);
    }

void CPhotosView::ChangeLevel(TInt aLevel, const TGridView& aFrom)
    {
    const TInt level = aLevel;
    const TGridView& pinched = aFrom;
    CGridLayout* grid = NULL;
    if (level != iLevel)
        {
        grid = new CGridLayout;
        if (grid)
            {
            TRAPD(err, BuildLibraryGridL(*grid, level));
            if (err != KErrNone)
                {
                delete grid;
                grid = NULL;
                }
            }
        }
    const TInt item = iLibraryGrid->NearestItem(TPoint((TInt)pinched.iAx, (TInt)pinched.iAy));
    if (!grid || item < 0)
        {

        iNewFrom = pinched;
        iNewTo = pinched;
        iNewTo.iScale = 1;
        iNewTo.iSx = pinched.iAx;
        iNewTo.iSy = pinched.iAy - iLibraryScroll;
        }
    else
        {
        const TInt photo = iLibraryGrid->PhotoAt(item);
        const TRect oldCell = iLibraryGrid->ItemRect(item);
        const TRect newCell = grid->ItemRect(grid->ItemOf(photo));
        const TReal oldX = (oldCell.iTl.iX + oldCell.iBr.iX) / 2.0, oldY = (oldCell.iTl.iY + oldCell.iBr.iY) / 2.0;
        const TReal newX = (newCell.iTl.iX + newCell.iBr.iX) / 2.0, newY = (newCell.iTl.iY + newCell.iBr.iY) / 2.0;

        const TReal fromX = pinched.iSx + (oldX - pinched.iAx) * pinched.iScale;
        const TReal fromY = pinched.iSy + (oldY - pinched.iAy) * pinched.iScale;
        const TReal scroll = Clamp(newY - fromY, 0, Max(0, grid->ContentHeight() - Rect().Height()));
        const TReal ratio = (TReal)grid->RowHeight() / iLibraryGrid->RowHeight();
        iOldFrom = pinched;
        iOldFrom.iAx = oldX;
        iOldFrom.iAy = oldY;
        iOldFrom.iSx = fromX;
        iOldFrom.iSy = fromY;
        iOldTo = iOldFrom;
        iOldTo.iScale = ratio;
        iOldTo.iSx = newX;
        iOldTo.iSy = newY - scroll;
        iNewFrom = iOldFrom;
        iNewFrom.iAx = newX;
        iNewFrom.iAy = newY;
        iNewFrom.iScale = pinched.iScale / ratio;
        iNewTo = iOldTo;
        iNewTo.iAx = newX;
        iNewTo.iAy = newY;
        iNewTo.iScale = 1;
        iOldGrid = iLibraryGrid;
        iLibraryGrid = grid;
        iLibraryScroll = scroll;
        iLevel = level;
        Trace(_L("grid: level %d, %d columns"), level, grid->Columns());
        }
    iLevelTrans = ETrue;
    iLevelP = 0;
    iLevelPVel = 0;
    iFrostKey = 0;
    }

void CPhotosView::FinishLevelChange()
    {
    delete iOldGrid;
    iOldGrid = NULL;
    iLevelTrans = EFalse;
    iLevelP = 1;
    }

void CPhotosView::GridPointerL(const TPointerEvent& aEvent, TInt aPointer, TUint32 aNow)
    {
    const TPoint pos = aEvent.iPosition;
    iPointer[aPointer] = pos;
    if (aEvent.iType == TPointerEvent::EButton1Down)
        iPointerDown[aPointer] = ETrue;
    else if (aEvent.iType == TPointerEvent::EButton1Up)
        iPointerDown[aPointer] = EFalse;

    if (iAxis == EAxisGridPinch)
        {
        if (aEvent.iType == TPointerEvent::EDrag)
            MoveGridPinch();
        else if (aEvent.iType == TPointerEvent::EButton1Up)
            {
            EndGridPinch();
            iAxis = iPointerDown[0] || iPointerDown[1] ? EAxisIgnore : EAxisNone;
            }
        return;
        }
    if (aPointer == 1)
        {
        if (aEvent.iType == TPointerEvent::EButton1Down && iPointerDown[0] && iTab == ETabAll && !iLevelTrans &&
            iLibraryGrid->Count() > 0)
            BeginGridPinch();
        return;
        }
    if (iAxis == EAxisIgnore)
        {
        if (aEvent.iType == TPointerEvent::EButton1Up)
            iAxis = EAxisNone;
        return;
        }
    if (iLevelTrans && aEvent.iType == TPointerEvent::EButton1Down)
        {
        iAxis = EAxisIgnore;
        return;
        }
    const TInt w = Rect().Width(), h = Rect().Height();
    TReal& scroll = ActiveScroll();
    TReal& velocity = ActiveVelocity();
    const TBool albumsList = iTab == ETabAlbums && !InAlbum();
    const TInt max = albumsList ? Max(0, AlbumsHeight() - h) : (ActiveGrid() ? MaxScroll(*ActiveGrid()) : 0);
    switch (aEvent.iType)
        {
        case TPointerEvent::EButton1Down:
            {
            iDownPos = pos;
            iDownMs = aNow;
            iTouchPos = pos;
            iTouchMs = aNow;
            iTouchStray = 0;

            iTapCancelled = Abs(velocity) > 80 || scroll < 0 || scroll > max || (iPush > 0 && iPush < 1) ||
                            Abs(iCoversVel) > 80;
            iCoversVel = 0;
            iSelectAnchor = -1;
            if (iSelecting)
                {
                TRect cell;
                iSelectAnchor = GridItemAt(pos, cell);
                }
            velocity = 0;
            iTracker.Reset();
            iTracker.Add(pos.iY, aNow);
            iAxis = EAxisUndecided;
            const TBool inContent = pos.iY < h - Scale(KTabBar) && pos.iY >= iTopInset + Scale(KTopBar);
            if (inContent && pos.iX >= w - Scale(KScrubWidth) && iIndicator > 0.3 && max > 3 * h && !albumsList)
                {

                iAxis = EAxisScrub;
                iTapCancelled = ETrue;
                Feedback();
                iScrubSection = -1;
                }
            break;
            }
        case TPointerEvent::EDrag:
            iTouchStray = Max(iTouchStray, Max(Abs(pos.iX - iTouchPos.iX), Abs(pos.iY - iTouchPos.iY)));
            if (iAxis == EAxisUndecided)
                {
                const TPoint d = pos - iDownPos;
                if (Abs(d.iX) < Scale(KSlop) && Abs(d.iY) < Scale(KSlop))
                    break;
                if (iTab == ETabAlbums && iAlbum >= 0 && iPush == 1 && iDownPos.iX < Scale(KEdge) &&
                    Abs(d.iX) > Abs(d.iY))
                    {
                    iAxis = EAxisBack;
                    iTracker.Reset();
                    iDragOrigin = pos.iX;
                    }
                else if (albumsList && iPush == 0 && Abs(d.iX) > Abs(d.iY) && MaxCoversScroll() > 0 &&
                         iDownPos.iY + scroll >= iTopInset + Scale(KHeading) &&
                         iDownPos.iY + scroll < CoversHeight())
                    {

                    iAxis = EAxisCovers;
                    iTracker.Reset();
                    iDragOrigin = pos.iX;
                    iDragStart = Unrubber(iCoversScroll, 0, MaxCoversScroll(), w);
                    }
                else if (iSelecting && iSelectAnchor >= 0 && Abs(d.iX) > Abs(d.iY))
                    {

                    iAxis = EAxisSelect;
                    const CGridLayout* grid = ActiveGrid();
                    iSelectTarget = !IsSelected(grid->PhotoAt(iSelectAnchor));
                    iSelectBase.Reset();
                    for (TInt i = 0; i < iSelected.Count(); i++)
                        iSelectBase.Append(iSelected[i]);
                    iSelectLast = -1;
                    SelectRangeTo(iDownPos);
                    }
                else
                    {
                    iAxis = EAxisScroll;
                    iDragOrigin = pos.iY;
                    iDragStart = Unrubber(scroll, 0, max, h);
                    iTracker.Reset();
                    }
                }
            if (iAxis == EAxisScroll)
                {
                iTracker.Add(pos.iY, aNow);
                scroll = Rubber(iDragStart - (pos.iY - iDragOrigin), 0, max, h);
                }
            else if (iAxis == EAxisSelect)
                SelectRangeTo(pos);
            else if (iAxis == EAxisCovers)
                {
                iTracker.Add(pos.iX, aNow);
                iCoversScroll = Rubber(iDragStart - (pos.iX - iDragOrigin), 0, MaxCoversScroll(), w);
                }
            else if (iAxis == EAxisScrub)
                {
                const TInt track = h - Scale(KTabBar) - iTopInset;
                scroll = Clamp((TReal)(pos.iY - iTopInset) / Max(1, track), 0, 1) * max;

                const CGridLayout* grid = ActiveGrid();
                const TInt section = grid && grid->SectionCount() ? grid->SectionAt((TInt)scroll + iTopInset) : -1;
                if (section != iScrubSection && aNow - iScrubTickMs >= (TUint32)KScrubTickMs)
                    {
                    if (iScrubSection >= 0)
                        {
                        MTouchFeedback* feedback = MTouchFeedback::Instance();
                        if (feedback)
                            feedback->InstantFeedback(ETouchFeedbackSensitive);
                        }
                    iScrubSection = section;
                    iScrubTickMs = aNow;
                    }
                }
            else if (iAxis == EAxisBack)
                {
                iTracker.Add(pos.iX, aNow);
                iPush = Clamp(1 - (TReal)(pos.iX - iDragOrigin) / w, 0, 1);
                }
            break;
        case TPointerEvent::EButton1Up:
            {
            const TReal v = iTracker.Velocity(aNow);
            if (iAxis == EAxisScroll)
                velocity = -v;
            else if (iAxis == EAxisCovers)
                iCoversVel = -v;
            else if (iAxis == EAxisBack)
                {
                iPushTarget = iPush < 0.5 || v > KFling ? 0 : 1;
                iPushVel = -v / w;
                }
            else if (iAxis == EAxisUndecided && !iTapCancelled)
                GridTapL(iDownPos);
            else if (iAxis == EAxisScroll && !iTapCancelled && aNow - iTouchMs < (TUint32)KTapMs &&
                     iTouchStray < 2 * Scale(KSlop) && Abs(pos.iX - iTouchPos.iX) < Scale(KSlop) &&
                     Abs(pos.iY - iTouchPos.iY) < Scale(KSlop))
                {

                scroll = Clamp(iDragStart, 0, max);
                velocity = 0;
                GridTapL(iTouchPos);
                }
            iAxis = EAxisNone;
            break;
            }
        default:
            break;
        }
    }

TInt CPhotosView::GridItemAt(const TPoint& aPos, TRect& aCell) const
    {
    const CGridLayout* grid = ActiveGrid();
    if (!grid || iLevelTrans || aPos.iY >= Rect().Height() - Scale(KTabBar))
        return -1;
    if (iTab == ETabAlbums && iPush < 1)
        return -1;
    const TInt scroll = (TInt)GridScroll();
    const TInt item = grid->ItemAt(TPoint(aPos.iX, aPos.iY + scroll));
    if (item >= 0)
        {
        aCell = grid->ItemRect(item);
        aCell.Move(0, -scroll);
        }
    return item;
    }

void CPhotosView::TabBarTapL(const TPoint& aPos)
    {
    Feedback();
    if (iSelecting)
        {
        TAction actions[KMaxActions];
        TIconId icons[KMaxActions];
        const TInt n = SelectActions(actions, icons);
        for (TInt i = 0; i < n; i++)
            {

            const TInt left = i == 0 ? 0 : (BarSlotX(i - 1, n) + BarSlotX(i, n)) / 2;
            const TInt right = i == n - 1 ? Rect().Width() : (BarSlotX(i, n) + BarSlotX(i + 1, n)) / 2;
            if (aPos.iX >= left && aPos.iX < right)
                {
                SelectActionL(i);
                return;
                }
            }
        return;
        }
    if (aPos.iX < (BarSlotX(0, KTabSlots) + BarSlotX(1, KTabSlots)) / 2)
        {
        CEikonEnv::Static()->EikAppUi()->HandleCommandL(EEikCmdExit);
        return;
        }
    const TTab tab = aPos.iX < (BarSlotX(1, KTabSlots) + BarSlotX(2, KTabSlots)) / 2 ? ETabAll :
                     (aPos.iX < (BarSlotX(2, KTabSlots) + BarSlotX(3, KTabSlots)) / 2 ? ETabFavourites : ETabAlbums);
    if (tab == iTab)
        {

        if (tab == ETabAll)
            {
            iLibraryVel = 0;
            iLibraryScroll = MaxScroll(*iLibraryGrid);
            }
        else if (tab == ETabFavourites)
            {
            iFavouritesVel = 0;
            iFavouritesScroll = MaxScroll(*iFavouritesGrid);
            }
        else
            iPushTarget = 0;
        }
    iTab = tab;
    iIndicator = 0;
    }

TBool CPhotosView::TopBarTapL(const TPoint& aPos)
    {
    if (aPos.iY >= iTopInset + Scale(KTopBar + 8))
        return EFalse;
    if (HasSelectButton())
        {
        TRect button = TopButtonRect();
        button.Grow(Scale(10), Scale(8));
        if (button.Contains(aPos))
            {
            Feedback();
            if (iSelecting)
                EndSelect();
            else
                BeginSelect();
            return ETrue;
            }
        }
    if (InAlbum() && iPush >= 1 && !iSelecting && aPos.iX < Scale(110))
        {
        Feedback();
        iPushTarget = 0;
        return ETrue;
        }
    return EFalse;
    }

void CPhotosView::GridTapL(const TPoint& aPos)
    {
    const TInt h = Rect().Height();
    if (aPos.iY >= h - Scale(KTabBar))
        {
        TabBarTapL(aPos);
        return;
        }
    if (TopBarTapL(aPos))
        return;
    if (iTab == ETabAlbums && !InAlbum())
        {
        const TInt scroll = (TInt)iAlbumsScroll;
        for (TInt a = 0; a < iLibrary->CoverAlbumCount(); a++)
            {
            TRect hit = AlbumCoverRect(a);
            hit.iBr.iY += Scale(KAlbumLabel);
            hit.Move(0, -scroll);
            if (hit.Contains(aPos))
                {
                Feedback();
                OpenAlbum(a);
                return;
                }
            }
        TListRow rows[KListRows];
        const TInt count = ListRows(rows);
        for (TInt r = 0; r < count; r++)
            {
            TRect hit = ListRowRect(r);
            hit.Move(0, -scroll);
            if (hit.Contains(aPos))
                {
                Feedback();
                OpenAlbum(rows[r].iAlbum);
                return;
                }
            }
        return;
        }
    TRect cell;
    const TInt item = GridItemAt(aPos, cell);
    if (item < 0)
        return;
    if (iSelecting)
        {
        ToggleSelected(ActiveGrid()->PhotoAt(item));
        return;
        }

    if (iTab == ETabAll && iLevel < KLevels - 1 && iLibraryGrid->Columns() >= KZoomTapColumns)
        {
        ZoomInAt(aPos);
        return;
        }
    OpenViewer(item, cell);
    }
