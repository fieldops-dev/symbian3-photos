#ifndef PHOTOSVIEW_H
#define PHOTOSVIEW_H

#include <coecntrl.h>

#include "decoder.h"
#include "exif.h"
#include "icons.h"
#include "glrenderer.h"
#include "labels.h"
#include "physics.h"
#include "scanner.h"
#include "thumbs.h"
#include "video.h"

class CAknStylusPopUpMenu;
class CDesC16Array;
class CDocumentHandler;
class CGridLayout;
class CIcons;
class CLabels;
class CLibrary;
class CMoves;
class CSendUi;

class MStatusBar
    {
public:
    virtual TBool StatusBarShown() = 0;

    virtual TInt ShowStatusBar(TBool aShow) = 0;
    };

class TBox
    {
public:
    TBox() : iX0(0), iY0(0), iX1(0), iY1(0) {}
    TBox(TReal aX0, TReal aY0, TReal aX1, TReal aY1) : iX0(aX0), iY0(aY0), iX1(aX1), iY1(aY1) {}
    explicit TBox(const TRect& aRect) : iX0(aRect.iTl.iX), iY0(aRect.iTl.iY), iX1(aRect.iBr.iX), iY1(aRect.iBr.iY) {}
    TReal Width() const { return iX1 - iX0; }
    TReal Height() const { return iY1 - iY0; }
    TReal iX0;
    TReal iY0;
    TReal iX1;
    TReal iY1;
    };

class TGridView
    {
public:
    static TGridView Plain(TReal aScroll, TInt aOffsetX);
    static TGridView Between(const TGridView& aFrom, const TGridView& aTo, TReal aT);
    TReal iScale;
    TReal iAx;
    TReal iAy;
    TReal iSx;
    TReal iSy;
    TReal iAlpha;
    };

class CPhotosView : public CCoeControl, public MThumbCacheObserver, public MLabelsObserver, public MImageObserver,
                    public MGifObserver, public MScanObserver, public MVideoPlayerObserver
    {
public:

    static CPhotosView* NewL(const TRect& aRect, TInt aTopInset, MStatusBar& aStatusBar);
    ~CPhotosView();

    void SetLayout(const TRect& aRect, TInt aTopInset);

    TInt SizeEvents() const { return iSizeEvents; }

    void SetForeground(TBool aForeground);

    void SetVisible(TBool aVisible);

    void GpuMemoryLow();

    void HandleCommandL(TInt aCommand);

private:
    enum TTab { ETabAll, ETabFavourites, ETabAlbums };
    enum { KListRows = 4 };

    class TListRow
        {
    public:
        TIconId iIcon;
        TPtrC iName;
        TInt iCount;
        TInt iAlbum;
        };
    enum TCommand
        {
        ECmdShare = 0x7000, ECmdRename, ECmdDelete, ECmdInfo, ECmdHide, ECmdUnhide, ECmdRestore, ECmdDeleteForever,
        ECmdOpenExternally
        };

    enum TAction
        {
        EActFavourite, EActUnfavourite, EActShare, EActHide, EActUnhide, EActTrash, EActRestore, EActDeleteForever,
        EActInfo
        };
    enum { KMaxActions = 5 };
    enum TAxis
        {
        EAxisNone, EAxisUndecided, EAxisScroll, EAxisScrub, EAxisBack, EAxisPage, EAxisPan, EAxisDismiss,
        EAxisPinch, EAxisGridPinch, EAxisSelect, EAxisInfo, EAxisCovers, EAxisIgnore
        };
    enum TImageKind { EImageBase, EImageZoom, EImagePreview };

    class TViewImage
        {
    public:
        TInt iPhoto;
        TImageKind iKind;
        TBool iRequested;
        TGlTexture iTexture;
        TUint16* iPixels;
        TSize iSize;
        TInt iStride;
        TInt iRowsUploaded;
        TSize iFullSize;
        TUint32 iUsedMs;
        };

    enum { KViewImages = 11 };

    enum { KTile = 256, KTileStep = KTile - 2, KMaxTiles = 96 };
    class TTile
        {
    public:
        TGlTexture iTexture;
        TInt iWantedFrame;
        };

    enum TGpuState { EGpuNone, EGpuStart, EGpuGrid, EGpuViewer, EGpuZoomed };

    class TCover
        {
    public:
        TInt iPhoto;
        TBool iRequested;
        TGlTexture iTexture;
        TInt iUsedFrame;
        };
    enum { KCovers = 8, KCoverTag = 100 };

    CPhotosView(MStatusBar& aStatusBar);
    void ConstructL(const TRect& aRect, TInt aTopInset);
    void SizeChanged();
    void Draw(const TRect& aRect) const;
    void HandlePointerEventL(const TPointerEvent& aEvent);

    void LoadIndex();
    void LibraryScanned(CLibrary* aLibrary, TBool aFinal);
    void StartScan();
    void StartDeferred();
    static TInt HideTimeout(TAny* aSelf);
    CLibrary* Adopt(CLibrary* aLibrary, const TDesC& aViewerPath);
    TInt FindAlbum(const TDesC& aName, TInt aKind) const;
    void ReplaceLibrary(CLibrary* aLibrary, const TDesC& aViewerPath);
    void RegroupAlbums();
    void LoadFavourites();
    void LoadFavouritesL(const TDesC& aFile);
    void SaveFavouritesL();
    void PurgeTrash();
    void Relayout();
    void BuildAlbumGrid(CGridLayout& aGrid, TInt aAlbum, TInt aTopInset);
    TInt Scale(TInt aDesign) const { return aDesign * iScale / 256; }
    CGridLayout* ActiveGrid() const;
    TReal& ActiveScroll();
    TReal& ActiveVelocity();
    TReal GridScroll() const;
    TBool InAlbum() const { return iTab == ETabAlbums && iAlbum >= 0 && iPushTarget > 0; }

    TInt ShownKind() const;
    TInt MaxScroll(const CGridLayout& aGrid) const;
    TInt BarSlotX(TInt aSlot, TInt aSlots) const;
    void DrawBarBand(TInt aTop, TInt aBottom, TInt aHairlineY, TReal aAlpha);
    TInt ListRows(TListRow* aRows) const;
    TRect ListRowRect(TInt aRow) const;
    void DrawLists(TInt aScroll, TInt aOffsetX, TBool aFrost);
    void FillRoundRect(const TRect& aRect, TInt aRadius, TUint32 aRgb, TReal aAlpha);
    TInt AlbumsHeight() const;
    TInt CoverRows() const;
    TInt CoversHeight() const;
    TInt MaxCoversScroll() const;
    TRect AlbumCoverRect(TInt aAlbum) const;
    void OpenAlbum(TInt aAlbum);

    void StartGl();
    void CompleteGl();
    void StopGl();
    void GlFailed(TInt aErr);
    void Resume();

    void RequestFrame();
    void FrameNow();
    static TInt FrameTick(TAny* aSelf);
    TBool Step(TInt aMs);
    void RenderFrame();
    void LogStats(TUint32 aNow);

    void Idle();
    void TraceGpu(const TDesC& aWhere);
    void DrawGrid(const CGridLayout& aGrid, const TGridView& aView, TBool aFrost, TInt aHidden);
    void DrawCellMarks(TBool aPlain, TReal aAlpha);
    void BuildLibraryGridL(CGridLayout& aGrid, TInt aLevel);
    void BeginGridPinch();
    void MoveGridPinch();
    void EndGridPinch();
    void ChangeLevel(TInt aLevel, const TGridView& aFrom);
    void ZoomInAt(const TPoint& aPos);
    void FinishLevelChange();
    void DrawAlbums(TReal aScroll, TInt aOffsetX, TBool aFrost);
    void DrawScreen(TBool aFrost);
    void DrawBars();
    void DrawTabBar();
    void DrawTopBar();
    TBool HasSelectButton() const;
    TRect TopButtonRect() const;
    TBool TopBarTapL(const TPoint& aPos);
    void DrawScrollIndicator();
    void DrawScreenCorners();
    TInt FrostKey() const;
    void ThumbsChanged();
    void LabelsChanged();
    TBool PumpUploads(TBool aMoving);

    void GridPointerL(const TPointerEvent& aEvent, TInt aPointer, TUint32 aNow);
    void GridTapL(const TPoint& aPos);
    void TabBarTapL(const TPoint& aPos);
    TInt GridItemAt(const TPoint& aPos, TRect& aCell) const;
    void Feedback() const;

    void BeginSelect();
    void EndSelect();
    void ToggleSelected(TInt aPhoto);
    void SelectRangeTo(const TPoint& aPos);
    TBool StepSelectScroll(TInt aMs);
    TBool IsSelected(TInt aPhoto) const;
    static TIconId ActionIcon(TAction aAction);
    static TPtrC ActionName(TAction aAction);
    TInt SelectActions(TAction* aActions, TIconId* aIcons) const;
    void SelectActionL(TInt aSlot);
    void OpenPopup(TInt aItem, const TRect& aCell);
    void ClosePopup();
    TInt PopupActions(TAction* aActions) const;
    void PopupLayout(TBox& aPreview, TRect& aCard) const;
    TBool StepPopup(TInt aMs);
    void DrawPopup();
    void PopupPointerL(const TPointerEvent& aEvent);
    void PopupActionL(TAction aAction);
    TBool ConfirmL(const TDesC& aPrompt);
    void ActL(TAction aAction, const RArray<TInt>& aPhotos, const TDesC& aViewerPath);
    void ChangeFavouritesL(const RArray<TInt>& aPhotos, TBool aAdd);
    void SharePhotosL(const RArray<TInt>& aPhotos);

    TBool ViewerShown() const { return iViewerOpen; }
    void OpenViewer(TInt aItem, const TRect& aCell);
    void CloseViewer();
    TInt ViewerPhoto(TInt aOffset) const;
    TBox FitBox(TInt aPhoto) const;
    TBox ImageBox() const;
    TBox CellBox(TInt aItem) const;
    void DisplaySize(TInt aPhoto, TReal& aWidth, TReal& aHeight) const;
    TReal MaxPan(TBool aX, TReal aZoom) const;
    void ViewerPointer(const TPointerEvent& aEvent, TInt aPointer, TUint32 aNow);
    void ViewerTap(const TPoint& aPos, TUint32 aNow);
    void ZoomTo(TReal aZoom, const TPoint& aAround);
    void SettleZoom();
    void BeginPinch();
    void Pinch();
    TBool StepViewer(TInt aMs, TUint32 aNow);
    void DrawViewer();
    void DrawPhoto(TInt aPhoto, const TBox& aBox, const TBox& aWindow, TReal aAlpha);
    void DrawViewerChrome(TReal aAlpha);
    void DrawInfo();
    void PageChanged(TInt aStep);
    void UpdateViewerImages();
    TViewImage* ImageFor(TInt aPhoto, TImageKind aKind);
    TBool ImageReadyToDraw(const TViewImage* aImage) const;
    TBool WantsTexture(const TViewImage& aImage) const;
    void DrawDecode(TInt aPhoto, const TBox& aBox, const TBox& aWindow, TReal aAlpha);
    TBool TileRange(const TViewImage& aZoom, TInt aOrientation, const TBox& aWindow, TInt aMargin, TRect& aRange,
                    TReal* aStored) const;
    TBool DrawTiles(const TViewImage& aZoom, TInt aOrientation, const TBox& aBox, const TBox& aWindow, TReal aAlpha,
                    TBool aDraw);
    TBool UploadTiles(TInt& aBytes);
    void FreeTiles();
    void FreeImage(TViewImage& aImage);
    void RequestImages();
    void ImageReady(TImageJob& aJob);

    TBool IsGif(TInt aPhoto) const;
    void StartGif(TInt aPhoto);
    void StopGif();
    void RequestGif();
    void GifReady(TGifJob& aJob);
    TBool GifNextReady() const;
    void ShowGifFrame();
    void StepGif(TUint32 aNow);
    TInt GifWaitUs() const;
    TBool UploadImages(TInt& aBudget);
    void Share();
    void ToggleFavourite();
    void ChangeFavouriteL(const TDesC& aPath, TBool aAdd);
    void ShowMenuL();
    void RenameL();
    void ViewerActL(TAction aAction);
    void ToggleInfo();
    void ReadInfoHeader();
    enum { KInfoLines = 8 };
    class TInfoLine
        {
    public:
        TBuf<100> iText;
        TFontId iFont;
        };
    TInt InfoLines(TInfoLine* aLines) const;
    TInt InfoHeight() const;
    TReal InfoShift(TInt aPhoto) const;
    TInt InfoTravel() const;
    void FormatDate(TDes& aText, const TTime& aTime, TBool aWeekday) const;

    TBool IsVideo(TInt aPhoto) const;
    static void FormatDuration(TDes& aText, TInt aSeconds);
    void VideoChanged();
    void VideoPointerL(const TPointerEvent& aEvent, TInt aPointer);
    void VideoPinch();
    void DrawVideoSpinner();
    void VideoFrameReady();
    void PrepareShownVideo();
    void DrawSoftVideo();
    void DrawVideoControls(TReal aAlpha);
    void PlayVideo();
    void StopVideo();
    TBool VideoPlaying() const;
    TRect PlayButtonRect() const;
    void OpenExternally(const TDesC& aPath);

    const TGlTexture* CoverTexture(TInt aPhoto, TInt aSide);
    void DrawCover(TInt aPhoto, const TGlTexture& aTexture, const TRect& aRect);
    void CoverReady(TImageJob& aJob);
    void ForgetCovers(TBool aDelete);
    void VideoControlsChanged();

    TBool StatusBarWanted() const;
    void UpdateStatusBar();

    TInt VisibleTop() const;

    CLibrary* iLibrary;
    CScanner* iScanner;
    TBool iScanning;
    const CLibrary* iScanSource;
    RPointerArray<CLibrary> iRetired;
    TBool iRescan;
    CDesC16Array* iFavourites;
    TFileName iFavouritesFile;
    CMoves* iMoves;
    TBool iAlbumsDirty;
    CAknStylusPopUpMenu* iMenu;
    TUint32 iLastScanMs;
    TFileName iIndexFile;
    CGlRenderer* iGl;
    TBool iGlFailed;
    TBuf<80> iGlNote;
    CLabels* iLabels;
    CIcons* iIcons;
    CThumbCache* iThumbs;
    CImageWorker* iImages;

    class TGifFrame
        {
    public:
        TInt iIndex;
        TUint16* iPixels;
        TInt iDelayMs;
        };
    CGifWorker* iGifWorker;
    TInt iGifPhoto;
    TInt iGifTag;
    TBool iGifRestart;
    TBool iGifDone;
    TInt iGifCount;
    TSize iGifSize;
    TInt iGifStride;
    TBool iGifAll;
    RArray<TGifFrame> iGifFrames;
    TInt iGifShown;
    TInt iGifShownDelay;
    TBool iGifPlaying;
    TUint32 iGifDueMs;
    TGlTexture iGifTexture;
    TInt iGifDecoded;
    TInt iGifDecodeUs;
    TBool iGifLogged;
    TBool iGifGpuLogged;
    CFrameTimer* iFrameTimer;
    CPeriodic* iHideTimer;
    TBool iDeferredDone;
    CSendUi* iSendUi;
    CDocumentHandler* iDocHandler;
    CVideoPlayer* iPlayer;
    TBool iForeground;
    TBool iVisible;
    TBool iConstructing;
    TInt iScale;
    TSize iLaidOut;
    MStatusBar& iStatusBar;
    TInt iTopInset;
    TInt iLaidOutInset;
    TInt iSizeEvents;
    TUint32 iStatusAskMs;

    TTab iTab;
    CGridLayout* iLibraryGrid;
    CGridLayout* iFavouritesGrid;
    RArray<TInt> iNoItems;
    CGridLayout* iAlbumGrid;
    TInt iAlbum;
    TReal iLibraryScroll;
    TReal iLibraryVel;
    TReal iFavouritesScroll;
    TReal iFavouritesVel;
    TReal iAlbumScroll;
    TReal iAlbumVel;
    TReal iAlbumsScroll;
    TReal iAlbumsVel;
    TReal iCoversScroll;
    TReal iCoversVel;
    TReal iPush;
    TReal iPushVel;
    TReal iPushTarget;
    TReal iIndicator;
    TUint32 iIndicatorMs;
    TBool iFirstLayout;
    TInt iScrubSection;
    TUint32 iScrubTickMs;

    TInt iLevel;
    TReal iGridPinchDistance;
    TReal iGridScale;
    TReal iGridAx;
    TReal iGridAy;
    TReal iGridMidX;
    TReal iGridMidY;
    TBool iLevelTrans;
    TReal iLevelP;
    TReal iLevelPVel;
    CGridLayout* iOldGrid;
    TGridView iOldFrom;
    TGridView iOldTo;
    TGridView iNewFrom;
    TGridView iNewTo;

    TBool iSelecting;
    RArray<TInt> iSelected;

    TInt iSelectAnchor;
    TInt iSelectLast;
    TBool iSelectTarget;
    RArray<TInt> iSelectBase;

    TBool iPopupOpen;
    TInt iPopupPhoto;
    TInt iPopupItem;
    TBox iPopupCell;
    TReal iPopupP;
    TReal iPopupPVel;
    TInt iPopupPressed;
    TBool iPopupHeld;
    TUint32 iDownMs;

    TAxis iAxis;
    TPoint iTouchPos;
    TUint32 iTouchMs;
    TInt iTouchStray;
    TPoint iDownPos;
    TBool iTapCancelled;
    TInt iDragOrigin;
    TReal iDragStart;
    TVelocityTracker iTracker;
    TVelocityTracker iTrackerY;
    TPoint iPointer[2];
    TBool iPointerDown[2];

    TBool iMoving;
    TInt64 iRotateUs;
    TUint32 iLastTick;
    TInt64 iFrameStartUs;
    TInt iFrostKey;
    TInt iFrostFrame;
    TInt iFrame;
    TInt iThumbEpoch;

    TBool iViewerOpen;
    TBool iClosing;
    TReal iTrans;
    TReal iTransVel;
    TReal iTransTarget;
    TBox iTransCell;
    TBox iTransFull;
    TInt iCurrent;
    TReal iPageOffset;
    TReal iPageVel;
    TBool iSnapping;
    TReal iSnapTarget;
    TReal iZoom;
    TReal iPanX;
    TReal iPanY;
    TReal iZoomVel;
    TReal iPanXVel;
    TReal iPanYVel;
    TBool iZoomSettling;
    TBool iZoomEase;
    TReal iZoomTarget;
    TReal iPanXTarget;
    TReal iPanYTarget;
    TReal iDismissX;
    TReal iDismissY;
    TReal iDismissXVel;
    TReal iDismissYVel;
    TBool iChrome;
    TReal iChromeAlpha;
    TBool iInfoOpen;
    TReal iInfo;
    TReal iInfoVel;
    TReal iInfoDragStart;
    TBool iTapPending;
    TUint32 iTapMs;
    TPoint iTapPos;
    TUint32 iLastTapMs;
    TPoint iLastTapPos;
    TReal iPinchDistance;
    TReal iPinchZoom;
    TReal iPinchMidX;
    TReal iPinchMidY;
    TReal iPinchPanX;
    TReal iPinchPanY;
    TReal iDragStartY;
    TPointFilter iFilter;
    TViewImage iViewImages[KViewImages];
    TTile iTiles[KMaxTiles];
    TUint16* iTileScratch;
    TGpuState iGpuState;
    TUint32 iGpuLogMs;
    TInt iGpuLogThumbs;
    TCover iCovers[KCovers];
    TPoint iVideoDown;
    TBool iVideoDragged;
    TBool iVideoScrub;
    TPoint iVideoTouch[2];
    TBool iVideoTouchDown[2];
    TBool iVideoPinching;
    TBool iVideoIgnore;
    TBool iVideoPanned;
    TBool iVideoHeld;
    TUint32 iVideoTapMs;
    TPoint iVideoTapPos;
    TBool iVideoTapHadControls;
    TReal iVideoPinchDistance;
    TReal iVideoPinchZoom;
    TReal iVideoPinchMidX;
    TReal iVideoPinchMidY;
    TReal iVideoPinchPanX;
    TReal iVideoPinchPanY;
    TGlTexture iSpinnerTexture;
    TReal iVideoControls;
    TImageHeader iInfoHeader;
    TInt iInfoPhoto;
    TInt iImagesDecoded;
    TInt iImageDecodeUs;
    TInt iImageUploadUs;
    TInt iImageUploadBytes;
    };

#endif
