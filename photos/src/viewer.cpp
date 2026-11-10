#include "photosview.h"

#include <aknquerydialog.h>
#include <aknstyluspopupmenu.h>
#include <apmstd.h>
#include <badesca.h>
#include <documenthandler.h>
#include <eikenv.h>
#include <n8photos.rsg>
#include <e32math.h>

#include "exif.h"
#include "gridlayout.h"
#include "icons.h"
#include "labels.h"
#include "library.h"
#include "moves.h"
#include "n8mlog.h"
#include "spinner.h"
#include "trace.h"

namespace
    {
    const TReal KTransOmega = 22;
    const TReal KPageOmega = 17;
    const TReal KZoomOmega = 16;
    const TReal KDismissOmega = 15;
    const TReal KInfoOmega = 18;
    const TReal KMaxZoom = 6;
    const TReal KDoubleTapZoom = 2.5;
    const TReal KDoubleTapRate = 15;
    const TReal KFling = 500;
    const TInt KPageGap = 20;
    const TInt KDoubleTapMs = 350;
    const TInt KDoubleTapSlop = 60;
    const TInt KSlop = 14;
    const TInt KTapMs = 250;
    const TInt KInfoPadTop = 24;
    const TReal KInfoFling = 400;
    const TInt KBar = 56;
    const TInt KShade = 112;
    const TReal KShadeAlpha = 0.62;
    const TInt KButton = 70;
    const TInt KTabBar = 56;
    const TInt KInfoLine = 27;
    const TInt KInfoPad = 20;
    const TUint32 KWhite = 0xFFFFFF;
    const TUint32 KDim = 0xA0A0A8;
    const TUint32 KBlue = 0x0A84FF;

    const TInt KImagePixels = 1200 * 1000;
    const TInt KZoomPixels = 3100 * 1000;
    const TInt KMaxImageSide = 2048;
    const TInt KIncomingBytes = 512 * 1024;

    void StoredToOriented(TInt aOrientation, TReal aS, TReal aT, TReal& aU, TReal& aV)
        {
        TReal s0, t0, su, tu, sv, tv;
        OrientedToStored(aOrientation, 0, 0, s0, t0);
        OrientedToStored(aOrientation, 1, 0, su, tu);
        OrientedToStored(aOrientation, 0, 1, sv, tv);
        aU = (su - s0) * (aS - s0) + (tu - t0) * (aT - t0);
        aV = (sv - s0) * (aS - s0) + (tv - t0) * (aT - t0);
        }

    TReal Distance(const TPoint& aA, const TPoint& aB)
        {
        TReal r;
        const TReal dx = aA.iX - aB.iX, dy = aA.iY - aB.iY;
        Math::Sqrt(r, dx * dx + dy * dy);
        return r;
        }

    TReal Power(TReal aBase, TReal aExponent)
        {
        TReal r;
        Math::Pow(r, aBase, aExponent);
        return r;
        }

    TBox LerpBox(const TBox& aA, const TBox& aB, TReal aT)
        {
        return TBox(Lerp(aA.iX0, aB.iX0, aT), Lerp(aA.iY0, aB.iY0, aT), Lerp(aA.iX1, aB.iX1, aT),
                    Lerp(aA.iY1, aB.iY1, aT));
        }

    TBox SquareCrop(TReal aWidth, TReal aHeight)
        {
        if (aWidth >= aHeight)
            {
            const TReal m = (1 - aHeight / aWidth) / 2;
            return TBox(m, 0, 1 - m, 1);
            }
        const TReal m = (1 - aWidth / aHeight) / 2;
        return TBox(0, m, 1, 1 - m);
        }
    }

TInt CPhotosView::ViewerPhoto(TInt aOffset) const
    {
    const CGridLayout* grid = ActiveGrid();
    const TInt item = iCurrent + aOffset;
    return grid && item >= 0 && item < grid->Count() ? grid->PhotoAt(item) : -1;
    }

void CPhotosView::DisplaySize(TInt aPhoto, TReal& aWidth, TReal& aHeight) const
    {
    aWidth = aHeight = 1;
    if (aPhoto < 0)
        return;
    const TPhoto& photo = iLibrary->Photo(aPhoto);
    TInt w = photo.iWidth, h = photo.iHeight;
    if (w <= 0 || h <= 0)
        {

        for (TInt i = 0; i < KViewImages; i++)
            if (iViewImages[i].iPhoto == aPhoto && iViewImages[i].iFullSize.iWidth > 0)
                {
                w = iViewImages[i].iFullSize.iWidth;
                h = iViewImages[i].iFullSize.iHeight;
                }
        }
    if (w <= 0 || h <= 0)
        return;
    aWidth = SwapsAxes(photo.iOrientation) ? h : w;
    aHeight = SwapsAxes(photo.iOrientation) ? w : h;
    }

TBox CPhotosView::FitBox(TInt aPhoto) const
    {
    const TReal w = Rect().Width(), h = Rect().Height();
    TReal dw, dh;
    DisplaySize(aPhoto, dw, dh);
    const TReal scale = Min(w / dw, h / dh);
    const TReal fw = dw * scale, fh = dh * scale;
    return TBox((w - fw) / 2, (h - fh) / 2, (w + fw) / 2, (h + fh) / 2);
    }

TBox CPhotosView::ImageBox() const
    {
    const TBox fit = FitBox(ViewerPhoto(0));
    const TReal h = Rect().Height();
    const TReal shrink = 1 - 0.25 * Min(1.0, Abs(iDismissY) / h);
    const TReal k = iZoom * shrink;
    const TReal cx = Rect().Width() / 2.0 + iPanX + iPageOffset + iDismissX;
    const TReal cy = h / 2 + iPanY + iDismissY - InfoShift(ViewerPhoto(0)) * iInfo;
    return TBox(cx - fit.Width() * k / 2, cy - fit.Height() * k / 2, cx + fit.Width() * k / 2,
                cy + fit.Height() * k / 2);
    }

TBox CPhotosView::CellBox(TInt aItem) const
    {
    const CGridLayout* grid = ActiveGrid();
    if (!grid)
        return TBox();
    TRect cell = grid->ItemRect(aItem);
    cell.Move(0, -(TInt)GridScroll());
    return TBox(cell);
    }

TReal CPhotosView::MaxPan(TBool aX, TReal aZoom) const
    {
    const TBox fit = FitBox(ViewerPhoto(0));
    const TReal size = (aX ? fit.Width() : fit.Height()) * aZoom;
    const TReal screen = aX ? Rect().Width() : Rect().Height();
    return Max(0.0, (size - screen) / 2);
    }

void CPhotosView::OpenViewer(TInt aItem, const TRect& aCell)
    {
    iViewerOpen = ETrue;
    iClosing = EFalse;
    iCurrent = aItem;
    iTrans = 0;
    iTransVel = 0;
    iTransTarget = 1;
    iTransCell = TBox(aCell);
    iZoom = 1;
    iPanX = iPanY = iPanXVel = iPanYVel = 0;
    iZoomSettling = EFalse;
    iPageOffset = iPageVel = 0;
    iSnapping = EFalse;
    iDismissX = iDismissY = iDismissXVel = iDismissYVel = 0;
    iChrome = ETrue;
    iChromeAlpha = 0;
    iInfoOpen = EFalse;
    iInfo = iInfoVel = 0;
    iInfoPhoto = -1;
    iTapPending = EFalse;
    iAxis = EAxisIgnore;
    UpdateViewerImages();
    Trace(_L("viewer: open item %d photo %d"), aItem, ViewerPhoto(0));
    RequestFrame();
    }

void CPhotosView::CloseViewer()
    {
    if (iClosing)
        return;
    StopVideo();
    iTransFull = ImageBox();

    CGridLayout* grid = ActiveGrid();
    if (grid)
        {
        const TRect cell = grid->ItemRect(iCurrent);
        TReal& scroll = ActiveScroll();
        const TInt h = Rect().Height();
        if (cell.iTl.iY - scroll < iTopInset)
            scroll = cell.iTl.iY - iTopInset - Scale(8);
        else if (cell.iBr.iY - scroll > h - Scale(KTabBar))
            scroll = cell.iBr.iY - (h - Scale(KTabBar)) + Scale(8);
        scroll = Clamp(scroll, 0, MaxScroll(*grid));
        ActiveVelocity() = 0;
        }
    iTransCell = CellBox(iCurrent);
    iClosing = ETrue;
    iStatusAskMs = NowMs();
    UpdateStatusBar();
    iTransTarget = 0;
    iTrans = 1;
    iTransVel = 0;
    iDismissX = iDismissY = iDismissXVel = iDismissYVel = 0;
    iPageOffset = 0;
    iSnapping = EFalse;
    iInfoOpen = EFalse;
    iInfo = iInfoVel = 0;
    iTapPending = EFalse;
    iAxis = EAxisIgnore;
    iFrostKey = 0;
    RequestFrame();
    }

void CPhotosView::BeginPinch()
    {
    iAxis = EAxisPinch;
    iTapPending = EFalse;
    iPinchDistance = Max(20.0, Distance(iPointer[0], iPointer[1]));
    iPinchZoom = iZoom;
    iPinchMidX = (iPointer[0].iX + iPointer[1].iX) / 2.0;
    iPinchMidY = (iPointer[0].iY + iPointer[1].iY) / 2.0;
    iPinchPanX = iPanX;
    iPinchPanY = iPanY;
    iZoomSettling = EFalse;
    iPanXVel = iPanYVel = 0;

    if (iPageOffset != 0)
        {
        iSnapping = ETrue;
        iSnapTarget = 0;
        }
    }

void CPhotosView::Pinch()
    {
    const TReal raw = iPinchZoom * Distance(iPointer[0], iPointer[1]) / iPinchDistance;
    TReal zoom = raw;
    if (raw < 1)
        zoom = Power(raw, 0.4);
    else if (raw > KMaxZoom)
        zoom = KMaxZoom * Power(raw / KMaxZoom, 0.4);
    const TReal cx = Rect().Width() / 2.0, cy = Rect().Height() / 2.0;
    const TReal midX = (iPointer[0].iX + iPointer[1].iX) / 2.0, midY = (iPointer[0].iY + iPointer[1].iY) / 2.0;
    const TReal relX = iPinchMidX - (cx + iPinchPanX), relY = iPinchMidY - (cy + iPinchPanY);
    iZoom = zoom;
    iPanX = midX - relX * (zoom / iPinchZoom) - cx;
    iPanY = midY - relY * (zoom / iPinchZoom) - cy;
    }

void CPhotosView::SettleZoom()
    {
    const TReal target = Clamp(iZoom, 1, KMaxZoom);
    const TReal cx = Rect().Width() / 2.0, cy = Rect().Height() / 2.0;
    const TReal midX = (iPointer[0].iX + iPointer[1].iX) / 2.0, midY = (iPointer[0].iY + iPointer[1].iY) / 2.0;
    const TReal centreX = midX - (midX - (cx + iPanX)) * (target / iZoom);
    const TReal centreY = midY - (midY - (cy + iPanY)) * (target / iZoom);
    iZoomTarget = target;
    iZoomEase = EFalse;
    iPanXTarget = Clamp(centreX - cx, -MaxPan(ETrue, target), MaxPan(ETrue, target));
    iPanYTarget = Clamp(centreY - cy, -MaxPan(EFalse, target), MaxPan(EFalse, target));
    iZoomSettling = ETrue;
    iZoomVel = iPanXVel = iPanYVel = 0;
    UpdateViewerImages();
    }

void CPhotosView::ZoomTo(TReal aZoom, const TPoint& aAround)
    {
    const TReal cx = Rect().Width() / 2.0, cy = Rect().Height() / 2.0;
    const TReal relX = aAround.iX - (cx + iPanX), relY = aAround.iY - (cy + iPanY);
    iZoomTarget = aZoom;
    iZoomEase = ETrue;
    iPanXTarget = Clamp(aAround.iX - cx - relX * (aZoom / iZoom), -MaxPan(ETrue, aZoom), MaxPan(ETrue, aZoom));
    iPanYTarget = Clamp(aAround.iY - cy - relY * (aZoom / iZoom), -MaxPan(EFalse, aZoom), MaxPan(EFalse, aZoom));
    iZoomSettling = ETrue;
    iZoomVel = iPanXVel = iPanYVel = 0;
    UpdateViewerImages();
    }

void CPhotosView::ViewerPointer(const TPointerEvent& aEvent, TInt aPointer, TUint32 aNow)
    {
    const TPoint pos = aEvent.iPosition;
    const TBool settled = iTrans >= 1 && !iClosing;
    if (aPointer == 1)
        {
        iPointer[1] = pos;
        if (aEvent.iType == TPointerEvent::EButton1Down)
            {
            iPointerDown[1] = ETrue;
            if (iPointerDown[0] && settled)
                BeginPinch();
            }
        else if (aEvent.iType == TPointerEvent::EDrag && iAxis == EAxisPinch)
            Pinch();
        else if (aEvent.iType == TPointerEvent::EButton1Up)
            {
            iPointerDown[1] = EFalse;
            if (iAxis == EAxisPinch)
                {
                SettleZoom();
                iAxis = EAxisIgnore;
                }
            }
        return;
        }

    const TInt w = Rect().Width(), h = Rect().Height();
    const TInt pageWidth = w + Scale(KPageGap);
    const CGridLayout* grid = ActiveGrid();
    const TInt count = grid ? grid->Count() : 0;
    const TReal pageLo = iCurrent < count - 1 ? -pageWidth : 0, pageHi = iCurrent > 0 ? pageWidth : 0;
    switch (aEvent.iType)
        {
        case TPointerEvent::EButton1Down:
            iPointer[0] = pos;
            iPointerDown[0] = ETrue;
            if (!settled)
                {
                iAxis = EAxisIgnore;
                break;
                }
            iDownPos = pos;
            iTouchPos = pos;
            iTouchMs = aNow;
            iTouchStray = 0;
            iAxis = EAxisUndecided;

            if (iTapPending && aNow - iTapMs <= (TUint32)KDoubleTapMs &&
                Distance(pos, iTapPos) < Scale(KDoubleTapSlop) && iInfo <= 0.001)
                {
                iTapPending = EFalse;
                ZoomTo(iZoom > 1.05 ? 1 : KDoubleTapZoom, iTapPos);
                iAxis = EAxisIgnore;
                break;
                }
            iTapCancelled = iSnapping || Abs(iPanXVel) > 80 || Abs(iPanYVel) > 80;
            iPanXVel = iPanYVel = 0;
            iFilter.Reset(pos.iX, pos.iY, aNow);
            iZoomSettling = EFalse;
            iTracker.Reset();
            iTrackerY.Reset();
            iTracker.Add(pos.iX, aNow);
            iTrackerY.Add(pos.iY, aNow);
            if (iSnapping)
                {

                iSnapping = EFalse;
                iAxis = EAxisPage;
                iDragOrigin = pos.iX;
                iDragStart = Unrubber(iPageOffset, pageLo, pageHi, w);
                }
            if (iPointerDown[1])
                BeginPinch();
            break;

        case TPointerEvent::EDrag:
            iPointer[0] = pos;
            if (iAxis == EAxisPinch)
                {
                Pinch();
                break;
                }
            if (iAxis == EAxisNone || iAxis == EAxisIgnore)
                break;
            iTouchStray = Max(iTouchStray, Max(Abs(pos.iX - iTouchPos.iX), Abs(pos.iY - iTouchPos.iY)));
            iTracker.Add(pos.iX, aNow);
            iTrackerY.Add(pos.iY, aNow);
            if (iAxis == EAxisUndecided)
                {
                const TPoint d = pos - iDownPos;
                if (Abs(d.iX) < Scale(KSlop) && Abs(d.iY) < Scale(KSlop))
                    break;
                const TBool across = Abs(d.iX) > Abs(d.iY);
                if (iZoom > 1.01)
                    {

                    const TReal mx = MaxPan(ETrue, iZoom);
                    const TBool atEdge = (d.iX > 0 && iPanX >= mx - 1) || (d.iX < 0 && iPanX <= -mx + 1);
                    iAxis = across && atEdge ? EAxisPage : EAxisPan;
                    }
                else if (across)
                    iAxis = EAxisPage;
                else

                    iAxis = d.iY < 0 || iInfo > 0.001 ? EAxisInfo : EAxisDismiss;
                if (!iVideoHeld || iAxis != EAxisPage)
                    StopVideo();
                iInfoDragStart = iInfo;
                iDownPos = pos;
                iFilter.Reset(pos.iX, pos.iY, aNow);
                iDragOrigin = pos.iX;
                if (iAxis == EAxisPage)
                    iDragStart = Unrubber(iPageOffset, pageLo, pageHi, w);
                else if (iAxis == EAxisPan)
                    {
                    iDragStart = Unrubber(iPanX, -MaxPan(ETrue, iZoom), MaxPan(ETrue, iZoom), w);
                    iDragStartY = Unrubber(iPanY, -MaxPan(EFalse, iZoom), MaxPan(EFalse, iZoom), h);
                    }
                }
            {

            iFilter.Add(pos.iX, pos.iY, aNow);
            const TReal dx = iFilter.X() - iDownPos.iX, dy = iFilter.Y() - iDownPos.iY;
            if (iAxis == EAxisPage)
                iPageOffset = Rubber(iDragStart + dx, pageLo, pageHi, w);
            else if (iAxis == EAxisPan)
                {
                const TReal mx = MaxPan(ETrue, iZoom), my = MaxPan(EFalse, iZoom);
                iPanX = Rubber(iDragStart + dx, -mx, mx, w);
                iPanY = Rubber(iDragStartY + dy, -my, my, h);
                }
            else if (iAxis == EAxisDismiss)
                {
                iDismissX = dx;
                iDismissY = dy;
                iTapPending = EFalse;
                }
            else if (iAxis == EAxisInfo)
                {
                iInfo = Clamp(iInfoDragStart - dy / Max(1, InfoTravel()), 0, 1.05);
                iTapPending = EFalse;
                }
            }
            break;

        case TPointerEvent::EButton1Up:
            {
            iPointerDown[0] = EFalse;
            const TReal vx = iTracker.Velocity(aNow), vy = iTrackerY.Velocity(aNow);

            if ((iAxis == EAxisPage || iAxis == EAxisDismiss || iAxis == EAxisPan || iAxis == EAxisInfo) &&
                !iTapCancelled && aNow - iTouchMs < (TUint32)KTapMs && iTouchStray < 2 * Scale(KSlop) &&
                Abs(pos.iX - iTouchPos.iX) < Scale(KSlop) && Abs(pos.iY - iTouchPos.iY) < Scale(KSlop))
                {
                if (iAxis == EAxisPage)
                    {
                    iSnapTarget = 0;
                    iPageVel = 0;
                    iSnapping = ETrue;
                    }
                iDismissXVel = iDismissYVel = iPanXVel = iPanYVel = 0;
                iAxis = EAxisNone;
                ViewerTap(iTouchPos, aNow);
                break;
                }
            switch (iAxis)
                {
                case EAxisUndecided:
                    if (!iTapCancelled)
                        ViewerTap(iTouchPos, aNow);
                    break;
                case EAxisInfo:

                    iInfoOpen = vy < -KInfoFling ? ETrue : (vy > KInfoFling ? EFalse : iInfo > 0.5);
                    iInfoVel = -vy / Max(1, InfoTravel());
                    if (iInfoOpen && iInfoPhoto != ViewerPhoto(0))
                        ReadInfoHeader();
                    break;
                case EAxisPage:
                    iSnapTarget = 0;
                    if (iPageOffset < 0 && iCurrent < count - 1 && (iPageOffset < -w / 3 || vx < -KFling))
                        iSnapTarget = -pageWidth;
                    else if (iPageOffset > 0 && iCurrent > 0 && (iPageOffset > w / 3 || vx > KFling))
                        iSnapTarget = pageWidth;
                    iPageVel = vx;
                    iSnapping = ETrue;
                    break;
                case EAxisPan:
                    iPanXVel = vx;
                    iPanYVel = vy;
                    break;
                case EAxisDismiss:
                    if ((iDismissY > h * 0.12 && vy > -100) || vy > 600)
                        CloseViewer();
                    else
                        {
                        iDismissXVel = vx;
                        iDismissYVel = vy;
                        }
                    break;
                case EAxisPinch:
                    SettleZoom();
                    break;
                default:
                    break;
                }
            iAxis = EAxisNone;
            break;
            }

        default:
            break;
        }
    }

void CPhotosView::ViewerTap(const TPoint& aPos, TUint32 aNow)
    {
    const TInt h = Rect().Height();
    const TInt bar = Scale(KBar);
    const TBool video = IsVideo(ViewerPhoto(0));
    if (iChromeAlpha > 0.5)
        {
        if (aPos.iY < iTopInset + bar)
            {
            if (aPos.iX < Scale(KButton))
                {
                Feedback();
                CloseViewer();
                }
            return;
            }
        if (aPos.iY >= h - bar)
            {
            if (aPos.iX < BarSlotX(0, 3) + Scale(KButton) / 2)
                {
                Feedback();
                ToggleFavourite();
                }
            else if (aPos.iX >= BarSlotX(2, 3) - Scale(KButton) / 2)
                {
                Feedback();
                TRAPD(err, ShowMenuL());
                if (err != KErrNone)
                    Trace(_L("viewer: menu err %d"), err);
                }
            else if (video && Abs(aPos.iX - BarSlotX(1, 3)) < Scale(KButton) / 2)
                {
                Feedback();
                if (VideoPlaying())
                    iPlayer->Pause();
                else
                    PlayVideo();
                }
            return;
            }
        }

    if (video && !VideoPlaying() && iZoom < 1.05 && PlayButtonRect().Contains(aPos))
        {
        Feedback();
        PlayVideo();
        return;
        }
    if (iInfoOpen)
        {
        if (aPos.iY < h - bar - InfoHeight())
            iInfoOpen = EFalse;
        return;
        }

    iTapPending = ETrue;
    iTapMs = aNow;
    iTapPos = aPos;
    }

TBool CPhotosView::StepViewer(TInt aMs, TUint32 aNow)
    {
    TBool moving = EFalse;
    StepGif(aNow);
    const TReal dt = aMs / 1000.0;
    const TReal controls = iPlayer && iPlayer->Soft() && iPlayer->ControlsShown() ? 1 : 0;
    if (iVideoControls != controls)
        {
        iVideoControls = controls > iVideoControls ? Min(1.0, iVideoControls + aMs / 150.0) :
                                                     Max(0.0, iVideoControls - aMs / 150.0);
        moving = ETrue;
        }
    if (iTrans != iTransTarget || iTransVel != 0)
        {
        moving |= SpringTo(iTrans, iTransVel, iTransTarget, KTransOmega, aMs, 0.002);
        if (iClosing && iTrans <= 0.002)
            {
            iTrans = 0;
            iViewerOpen = EFalse;
            iClosing = EFalse;
            StopGif();
            for (TInt i = 0; i < KViewImages; i++)
                FreeImage(iViewImages[i]);
            iFrostKey = 0;
            if (iAlbumsDirty)
                RegroupAlbums();
            Trace(_L("viewer: closed"));
            return ETrue;
            }
        }
    if (iSnapping)
        {
        const TReal before = iPageOffset;
        Spring(iPageOffset, iPageVel, iSnapTarget, KPageOmega, dt);

        if ((before - iSnapTarget) * (iPageOffset - iSnapTarget) < 0)
            {
            iPageOffset = iSnapTarget;
            iPageVel = 0;
            }
        if (Abs(iPageOffset - iSnapTarget) < 0.5 && Abs(iPageVel) < 20)
            {
            iSnapping = EFalse;
            const TInt step = iSnapTarget < 0 ? 1 : (iSnapTarget > 0 ? -1 : 0);
            iPageOffset = iPageVel = 0;
            if (step)
                PageChanged(step);
            else if (iVideoHeld && iPlayer)
                {

                iVideoHeld = EFalse;
                iPlayer->SetShift(0);
                iPlayer->ShowControls(ETrue);
                }
            }
        else
            moving = ETrue;
        }
    if (iZoomSettling)
        {
        TBool busy;
        if (iZoomEase)
            {
            busy = EaseTo(iZoom, iZoomTarget, KDoubleTapRate, aMs, 0.001);
            busy |= EaseTo(iPanX, iPanXTarget, KDoubleTapRate, aMs);
            busy |= EaseTo(iPanY, iPanYTarget, KDoubleTapRate, aMs);
            }
        else
            {
            busy = SpringTo(iZoom, iZoomVel, iZoomTarget, KZoomOmega, aMs, 0.001);
            busy |= SpringTo(iPanX, iPanXVel, iPanXTarget, KZoomOmega, aMs);
            busy |= SpringTo(iPanY, iPanYVel, iPanYTarget, KZoomOmega, aMs);
            }
        iZoomSettling = busy;
        moving |= busy;
        }
    else if (iAxis != EAxisPan && iAxis != EAxisPinch && iZoom > 1)
        {
        moving |= Coast(iPanX, iPanXVel, -MaxPan(ETrue, iZoom), MaxPan(ETrue, iZoom), aMs);
        moving |= Coast(iPanY, iPanYVel, -MaxPan(EFalse, iZoom), MaxPan(EFalse, iZoom), aMs);
        }
    if (iAxis != EAxisDismiss && !iClosing && (iDismissX != 0 || iDismissY != 0))
        {
        moving |= SpringTo(iDismissX, iDismissXVel, 0, KDismissOmega, aMs);
        moving |= SpringTo(iDismissY, iDismissYVel, 0, KDismissOmega, aMs);
        }
    if (iTapPending && aNow - iTapMs > (TUint32)KDoubleTapMs)
        {
        iTapPending = EFalse;
        iChrome = !iChrome;
        iStatusAskMs = iTapMs;
        }
    moving |= iTapPending;
    const TReal chrome = iChrome && iTrans >= 1 && !iClosing ? 1 : 0;
    if (iChromeAlpha != chrome)
        {
        iChromeAlpha = chrome > iChromeAlpha ? Min(chrome, iChromeAlpha + aMs / 180.0)
                                             : Max(chrome, iChromeAlpha - aMs / 180.0);
        moving = ETrue;
        }
    if (iAxis != EAxisInfo)
        moving |= SpringTo(iInfo, iInfoVel, iInfoOpen ? 1 : 0, KInfoOmega, aMs, 0.002);
    if (iVideoHeld && iPlayer)
        iPlayer->SetShift((TInt)iPageOffset);
    PrepareShownVideo();
    return moving;
    }

void CPhotosView::PageChanged(TInt aStep)
    {
    StopVideo();
    iCurrent += aStep;
    if (iInfoOpen)
        ReadInfoHeader();
    iZoom = 1;
    iPanX = iPanY = iPanXVel = iPanYVel = 0;
    iZoomSettling = EFalse;
    UpdateViewerImages();
    }

CPhotosView::TViewImage* CPhotosView::ImageFor(TInt aPhoto, TImageKind aKind)
    {
    for (TInt i = 0; i < KViewImages; i++)
        if (iViewImages[i].iPhoto == aPhoto && iViewImages[i].iKind == aKind)
            return &iViewImages[i];
    return NULL;
    }

TBool CPhotosView::ImageReadyToDraw(const TViewImage* aImage) const
    {
    return aImage && aImage->iTexture.Valid() && aImage->iRowsUploaded > 0 &&
        aImage->iRowsUploaded >= aImage->iSize.iHeight;
    }

TBool CPhotosView::WantsTexture(const TViewImage& aImage) const
    {
    if (aImage.iKind == EImageZoom || !aImage.iPixels || aImage.iPhoto < 0)
        return EFalse;
    if (aImage.iPhoto == iGifPhoto && iGifShown >= 0)
        return EFalse;
    if (aImage.iKind == EImagePreview)
        for (TInt i = 0; i < KViewImages; i++)
            if (iViewImages[i].iPhoto == aImage.iPhoto && iViewImages[i].iKind == EImageBase &&
                ImageReadyToDraw(&iViewImages[i]))
                return EFalse;
    return aImage.iPhoto == ViewerPhoto(0) || (iPageOffset < 0 && aImage.iPhoto == ViewerPhoto(1)) ||
        (iPageOffset > 0 && aImage.iPhoto == ViewerPhoto(-1));
    }

void CPhotosView::FreeTiles()
    {
    for (TInt i = 0; i < KMaxTiles; i++)
        {
        if (iGl)
            iGl->DeleteTexture(iTiles[i].iTexture);
        iTiles[i].iTexture = TGlTexture();
        iTiles[i].iWantedFrame = -2;
        }
    }

void CPhotosView::FreeImage(TViewImage& aImage)
    {
    if (aImage.iKind == EImageZoom)
        FreeTiles();
    if (iGl)
        iGl->DeleteTexture(aImage.iTexture);
    User::Free(aImage.iPixels);
    aImage.iPixels = NULL;
    aImage.iPhoto = -1;
    aImage.iKind = EImageBase;
    aImage.iRequested = EFalse;
    aImage.iRowsUploaded = 0;
    aImage.iSize = TSize();
    aImage.iFullSize = TSize();
    }

void CPhotosView::UpdateViewerImages()
    {
    const TInt current = ViewerPhoto(0), next = ViewerPhoto(1), previous = ViewerPhoto(-1);
    const TInt next2 = ViewerPhoto(2), previous2 = ViewerPhoto(-2);
    const TBool zoomed = iZoom > 1.3 || (iZoomSettling && iZoomTarget > 1.3);
    const TUint32 now = NowMs();
    for (TInt i = 0; i < KViewImages; i++)
        {
        TViewImage& image = iViewImages[i];
        if (image.iPhoto < 0)
            continue;
        const TBool near = image.iPhoto == current || image.iPhoto == next || image.iPhoto == previous;
        if (image.iKind == EImageBase)
            {
            if (near || image.iPhoto == next2 || image.iPhoto == previous2)
                image.iUsedMs = now;
            continue;
            }
        const TBool keep = image.iKind == EImageZoom ? (zoomed && image.iPhoto == current) : near;
        if (!keep && !image.iRequested)
            FreeImage(image);
        }
    if (current != iGifPhoto && (iGifPhoto >= 0 || IsGif(current)))
        StartGif(current);
    RequestImages();
    }

void CPhotosView::RequestImages()
    {
    if (!iImages || iImages->Busy() || !iViewerOpen || iClosing)
        return;
    const TBool zoomed = iZoom > 1.3 || (iZoomSettling && iZoomTarget > 1.3);
    const TInt current = ViewerPhoto(0), next = ViewerPhoto(1), previous = ViewerPhoto(-1);

    const TInt wanted[9] = { current, current, next, previous, next, previous, zoomed && !IsGif(current) ? current : -1,
                             ViewerPhoto(2), ViewerPhoto(-2) };
    const TImageKind kinds[9] = { EImagePreview, EImageBase, EImagePreview, EImagePreview, EImageBase, EImageBase,
                                  EImageZoom, EImageBase, EImageBase };
    const TUint32 now = NowMs();
    for (TInt k = 0; k < 9; k++)
        {
        const TInt photo = wanted[k];
        const TImageKind kind = kinds[k];

        if (photo < 0 || ImageFor(photo, kind) || IsVideo(photo))
            continue;

        const TViewImage* decoded = ImageFor(photo, EImageBase);
        if (kind == EImagePreview && decoded && decoded->iPixels)
            continue;
        TViewImage* slot = NULL;
        for (TInt i = 0; i < KViewImages && !slot; i++)
            if (iViewImages[i].iPhoto < 0)
                slot = &iViewImages[i];
        if (!slot)
            {

            for (TInt i = 0; i < KViewImages; i++)
                {
                TViewImage& image = iViewImages[i];
                const TBool inWindow = image.iPhoto == current || image.iPhoto == next ||
                    image.iPhoto == previous || image.iPhoto == wanted[7] || image.iPhoto == wanted[8];
                if (image.iKind == EImageBase && !image.iRequested && !inWindow &&
                    (!slot || image.iUsedMs < slot->iUsedMs))
                    slot = &image;
                }
            if (!slot)
                return;
            FreeImage(*slot);
            }

        TReal dw, dh;
        DisplaySize(photo, dw, dh);
        const TPhoto& p = iLibrary->Photo(photo);
        TSize min(640, 640);
        if (p.iWidth > 0 && p.iHeight > 0)
            {
            const TReal a = Rect().Width(), b = Rect().Height();
            const TReal scale = Max(Min(a / dw, b / dh), Min(b / dw, a / dh)) * (kind == EImageZoom ? KMaxZoom : 1);
            min = SwapsAxes(p.iOrientation) ? TSize((TInt)(dh * scale), (TInt)(dw * scale)) :
                                              TSize((TInt)(dw * scale), (TInt)(dh * scale));
            }
        const TInt maxSide = Min(KMaxImageSide, iGl ? iGl->MaxTexture() : KMaxImageSide);
        slot->iPhoto = photo;
        slot->iKind = kind;
        slot->iRequested = ETrue;
        slot->iUsedMs = now;
        const TInt err = iImages->Request(*p.iPath, slot - iViewImages, kind == EImagePreview, min, maxSide,
                                          kind == EImageZoom ? KZoomPixels : KImagePixels);
        if (err != KErrNone)
            slot->iRequested = EFalse;
        return;
        }
    }

void CPhotosView::ImageReady(TImageJob& aJob)
    {
    if (aJob.iTag >= KCoverTag)
        {
        CoverReady(aJob);
        if (iViewerOpen)
            UpdateViewerImages();
        return;
        }
    iImagesDecoded++;
    iImageDecodeUs += aJob.iMicros;
    Trace(_L("images: %S %dx%d -> %dx%d err %d in %d ms"), &aJob.iPath, aJob.iFullSize.iWidth,
          aJob.iFullSize.iHeight, aJob.iSize.iWidth, aJob.iSize.iHeight, aJob.iResult, aJob.iMicros / 1000);
    if (aJob.iTag >= 0 && aJob.iTag < KViewImages)
        {
        TViewImage& slot = iViewImages[aJob.iTag];

        if (slot.iRequested && slot.iPhoto >= 0 && iLibrary->Photo(slot.iPhoto).iPath->Compare(aJob.iPath) == 0)
            {
            slot.iRequested = EFalse;
            slot.iFullSize = aJob.iFullSize;
            if (aJob.iResult == KErrNone && aJob.iPixels)
                {
                slot.iPixels = aJob.iPixels;
                aJob.iPixels = NULL;
                slot.iSize = aJob.iSize;
                slot.iStride = aJob.iStride;
                slot.iRowsUploaded = 0;
                }
            }
        }
    UpdateViewerImages();
    RequestFrame();
    }

TBool CPhotosView::UploadImages(TInt& aBudget)
    {
    if (!iGl)
        return EFalse;
    TBool more = iImages->Busy();
    TInt bytes = aBudget * KThumbBytes * 2;
    for (TInt i = 0; i < KViewImages; i++)
        if (iViewImages[i].iTexture.Valid() && !WantsTexture(iViewImages[i]))
            {
            iGl->DeleteTexture(iViewImages[i].iTexture);
            iViewImages[i].iRowsUploaded = 0;
            }
    const TInt current = ViewerPhoto(0), incoming = iPageOffset < 0 ? ViewerPhoto(1) :
                                                    (iPageOffset > 0 ? ViewerPhoto(-1) : -1);

    if (incoming >= 0)
        bytes = Max(bytes, KIncomingBytes);
    const TInt order[4] = { current, current, incoming, incoming };
    const TImageKind kinds[4] = { EImagePreview, EImageBase, EImagePreview, EImageBase };
    for (TInt k = 0; k < 4; k++)
        {
        TViewImage* image = order[k] >= 0 ? ImageFor(order[k], kinds[k]) : NULL;
        if (!image || !image->iPixels || image->iRowsUploaded >= image->iSize.iHeight)
            continue;
        if (bytes <= 0)
            return ETrue;
        if (!image->iTexture.Valid() &&
            !iGl->CreateTexture(image->iTexture, image->iSize.iWidth, image->iSize.iHeight, TGlTexture::ERgb565,
                                ETrue, ETrue))
            {
            Trace(_L("images: no texture for %dx%d (graphics memory)"), image->iSize.iWidth, image->iSize.iHeight);
            continue;
            }
        const TInt rows = Min(image->iSize.iHeight - image->iRowsUploaded, Max(8, bytes / image->iStride));
        const TInt64 startUs = PerfNowUs();
        iGl->Upload(image->iTexture, 0, image->iRowsUploaded, image->iSize.iWidth, rows,
                    reinterpret_cast<const TUint8*>(image->iPixels) + image->iRowsUploaded * image->iStride);
        iImageUploadUs += (TInt)(PerfNowUs() - startUs);
        iImageUploadBytes += rows * image->iStride;
        bytes -= rows * image->iStride;
        image->iRowsUploaded += rows;
        if (image->iRowsUploaded < image->iSize.iHeight)
            more = ETrue;
        }
    more |= UploadTiles(bytes);
    aBudget = Max(0, bytes / KThumbBytes);
    return more;
    }

TBool CPhotosView::UploadTiles(TInt& aBytes)
    {
    const TViewImage* zoom = ImageFor(ViewerPhoto(0), EImageZoom);
    TBool more = EFalse;
    for (TInt i = 0; i < KMaxTiles; i++)
        {
        TTile& tile = iTiles[i];
        const TBool wanted = zoom && zoom->iPixels && tile.iWantedFrame >= iFrame - 1;
        if (!wanted)
            {
            if (tile.iTexture.Valid())
                iGl->DeleteTexture(tile.iTexture);
            continue;
            }
        if (tile.iTexture.Valid() || tile.iWantedFrame != iFrame)
            continue;
        if (aBytes <= 0 || !iTileScratch)
            {
            more = ETrue;
            continue;
            }
        const TInt columns = (zoom->iSize.iWidth + KTileStep - 1) / KTileStep;
        const TInt tx = i % columns, ty = i / columns;
        const TInt x0 = Max(0, tx * KTileStep - 1), x1 = Min(zoom->iSize.iWidth, (tx + 1) * KTileStep + 1);
        const TInt y0 = Max(0, ty * KTileStep - 1), y1 = Min(zoom->iSize.iHeight, (ty + 1) * KTileStep + 1);
        const TInt w = x1 - x0, h = y1 - y0, stride = (w * 2 + 3) & ~3;
        if (w <= 0 || h <= 0 || !iGl->CreateTexture(tile.iTexture, w, h, TGlTexture::ERgb565, ETrue))
            continue;
        const TInt64 startUs = PerfNowUs();
        const TUint8* source = reinterpret_cast<const TUint8*>(zoom->iPixels) + y0 * zoom->iStride + x0 * 2;
        TUint8* target = reinterpret_cast<TUint8*>(iTileScratch);
        for (TInt y = 0; y < h; y++)
            Mem::Copy(target + y * stride, source + y * zoom->iStride, w * 2);
        iGl->Upload(tile.iTexture, 0, 0, w, h, iTileScratch);
        iImageUploadUs += (TInt)(PerfNowUs() - startUs);
        iImageUploadBytes += stride * h;
        aBytes -= stride * h;
        }
    return more;
    }

void CPhotosView::DrawPhoto(TInt aPhoto, const TBox& aBox, const TBox& aWindow, TReal aAlpha)
    {
    if (aPhoto < 0 || aBox.Width() <= 0 || aBox.Height() <= 0)
        return;
    const TInt orientation = iLibrary->Photo(aPhoto).iOrientation;
    const TViewImage* image = ImageFor(aPhoto, EImageBase);
    const TViewImage* zoom = ImageFor(aPhoto, EImageZoom);
    const TReal shown = aBox.Width() * (aWindow.iX1 - aWindow.iX0);
    const TBool useZoom = zoom && zoom->iPixels && (!ImageReadyToDraw(image) ||
        (shown > image->iSize.iWidth * 1.2 && shown * 2 >= zoom->iSize.iWidth));
    if (!useZoom || !DrawTiles(*zoom, orientation, aBox, aWindow, aAlpha, EFalse))
        DrawDecode(aPhoto, aBox, aWindow, aAlpha);
    if (useZoom)
        DrawTiles(*zoom, orientation, aBox, aWindow, aAlpha, ETrue);
    }

void CPhotosView::DrawDecode(TInt aPhoto, const TBox& aBox, const TBox& aWindow, TReal aAlpha)
    {
    const TPhoto& photo = iLibrary->Photo(aPhoto);
    TViewImage* image = ImageFor(aPhoto, EImageBase);
    if (!ImageReadyToDraw(image))
        {

        image = ImageFor(aPhoto, EImagePreview);
        if (!ImageReadyToDraw(image))
            image = NULL;
        }
    const TGlTexture* texture = image ? &image->iTexture : NULL;
    if (aPhoto == iGifPhoto && iGifShown >= 0 && iGifTexture.Valid())
        texture = &iGifTexture;
    if (texture)
        {
        TReal uv[8];
        const TReal us[2] = { aWindow.iX0, aWindow.iX1 }, vs[2] = { aWindow.iY0, aWindow.iY1 };
        for (TInt c = 0; c < 4; c++)
            OrientedToStored(photo.iOrientation, us[c & 1], vs[c >> 1], uv[2 * c], uv[2 * c + 1]);
        iGl->AddQuad(aBox.iX0, aBox.iY0, aBox.iX1, aBox.iY1, uv);
        iGl->DrawBatch(*texture, KWhite, aAlpha);
        return;
        }

    TReal dw, dh;
    DisplaySize(aPhoto, dw, dh);
    const TBox square = SquareCrop(dw, dh);
    const TBox part(Max(square.iX0, aWindow.iX0), Max(square.iY0, aWindow.iY0), Min(square.iX1, aWindow.iX1),
                    Min(square.iY1, aWindow.iY1));
    if (part.Width() <= 0 || part.Height() <= 0)
        return;
    TInt atlas;
    TReal cell[4];
    if (!iThumbs->Get(aPhoto, CThumbCache::EVisible, atlas, cell))
        {

        if (IsVideo(aPhoto))
            iGl->FillRect(TRect((TInt)aBox.iX0, (TInt)aBox.iY0, (TInt)aBox.iX1, (TInt)aBox.iY1), 0x1C1C1E, aAlpha);
        return;
        }
    const TReal sx = aBox.Width() / aWindow.Width(), sy = aBox.Height() / aWindow.Height();
    const TReal cu = (cell[2] - cell[0]) / square.Width(), cv = (cell[3] - cell[1]) / square.Height();
    iGl->AddQuad(aBox.iX0 + (part.iX0 - aWindow.iX0) * sx, aBox.iY0 + (part.iY0 - aWindow.iY0) * sy,
                 aBox.iX0 + (part.iX1 - aWindow.iX0) * sx, aBox.iY0 + (part.iY1 - aWindow.iY0) * sy,
                 cell[0] + (part.iX0 - square.iX0) * cu, cell[1] + (part.iY0 - square.iY0) * cv,
                 cell[0] + (part.iX1 - square.iX0) * cu, cell[1] + (part.iY1 - square.iY0) * cv);
    iGl->DrawBatch(iThumbs->Atlas(atlas), KWhite, aAlpha);
    }

TBool CPhotosView::TileRange(const TViewImage& aZoom, TInt aOrientation, const TBox& aWindow, TInt aMargin,
                             TRect& aRange, TReal* aStored) const
    {
    const TReal us[2] = { aWindow.iX0, aWindow.iX1 }, vs[2] = { aWindow.iY0, aWindow.iY1 };
    TReal s0 = 1, t0 = 1, s1 = 0, t1 = 0;
    for (TInt c = 0; c < 4; c++)
        {
        TReal s, t;
        OrientedToStored(aOrientation, us[c & 1], vs[c >> 1], s, t);
        s0 = Min(s0, s);
        t0 = Min(t0, t);
        s1 = Max(s1, s);
        t1 = Max(t1, t);
        }
    const TInt w = aZoom.iSize.iWidth, h = aZoom.iSize.iHeight;
    aStored[0] = Max(0.0, s0) * w;
    aStored[1] = Max(0.0, t0) * h;
    aStored[2] = Min(1.0, s1) * w;
    aStored[3] = Min(1.0, t1) * h;
    if (aStored[2] <= aStored[0] || aStored[3] <= aStored[1])
        return EFalse;
    const TInt columns = (w + KTileStep - 1) / KTileStep, rows = (h + KTileStep - 1) / KTileStep;
    aRange.iTl.iX = Max(0, (TInt)((aStored[0] - aMargin) / KTileStep));
    aRange.iTl.iY = Max(0, (TInt)((aStored[1] - aMargin) / KTileStep));
    aRange.iBr.iX = Min(columns - 1, (TInt)((aStored[2] + aMargin - 0.001) / KTileStep));
    aRange.iBr.iY = Min(rows - 1, (TInt)((aStored[3] + aMargin - 0.001) / KTileStep));
    return ETrue;
    }

TBool CPhotosView::DrawTiles(const TViewImage& aZoom, TInt aOrientation, const TBox& aBox, const TBox& aWindow,
                             TReal aAlpha, TBool aDraw)
    {

    const TReal sx = aBox.Width() / aWindow.Width(), sy = aBox.Height() / aWindow.Height();
    const TBox shown(aWindow.iX0 + (Max(aBox.iX0, 0.0) - aBox.iX0) / sx,
                     aWindow.iY0 + (Max(aBox.iY0, 0.0) - aBox.iY0) / sy,
                     aWindow.iX0 + (Min(aBox.iX1, (TReal)Rect().Width()) - aBox.iX0) / sx,
                     aWindow.iY0 + (Min(aBox.iY1, (TReal)Rect().Height()) - aBox.iY0) / sy);
    TRect range;
    TReal stored[4];
    if (shown.Width() <= 0 || shown.Height() <= 0 || !TileRange(aZoom, aOrientation, shown, 0, range, stored))
        return ETrue;
    const TInt w = aZoom.iSize.iWidth, h = aZoom.iSize.iHeight, columns = (w + KTileStep - 1) / KTileStep;
    if (!aDraw)
        {
        TRect near;
        TReal around[4];
        TileRange(aZoom, aOrientation, shown, KTileStep / 2, near, around);
        for (TInt ty = near.iTl.iY; ty <= near.iBr.iY; ty++)
            for (TInt tx = near.iTl.iX; tx <= near.iBr.iX; tx++)
                if (ty * columns + tx < KMaxTiles)
                    iTiles[ty * columns + tx].iWantedFrame = iFrame;
        for (TInt ty = range.iTl.iY; ty <= range.iBr.iY; ty++)
            for (TInt tx = range.iTl.iX; tx <= range.iBr.iX; tx++)
                if (ty * columns + tx >= KMaxTiles || !iTiles[ty * columns + tx].iTexture.Valid())
                    return EFalse;
        return ETrue;
        }
    for (TInt ty = range.iTl.iY; ty <= range.iBr.iY; ty++)
        for (TInt tx = range.iTl.iX; tx <= range.iBr.iX; tx++)
            {
            if (ty * columns + tx >= KMaxTiles)
                continue;
            const TGlTexture& texture = iTiles[ty * columns + tx].iTexture;
            if (!texture.Valid())
                continue;

            const TReal x0 = Max(stored[0], (TReal)(tx * KTileStep));
            const TReal y0 = Max(stored[1], (TReal)(ty * KTileStep));
            const TReal x1 = Min(stored[2], (TReal)Min(w, (tx + 1) * KTileStep));
            const TReal y1 = Min(stored[3], (TReal)Min(h, (ty + 1) * KTileStep));
            if (x1 <= x0 || y1 <= y0)
                continue;
            TReal ua, va, ub, vb;
            StoredToOriented(aOrientation, x0 / w, y0 / h, ua, va);
            StoredToOriented(aOrientation, x1 / w, y1 / h, ub, vb);
            const TBox part(Min(ua, ub), Min(va, vb), Max(ua, ub), Max(va, vb));

            const TReal left = Max(0, tx * KTileStep - 1), top = Max(0, ty * KTileStep - 1);
            TReal uv[8];
            const TReal us[2] = { part.iX0, part.iX1 }, vs[2] = { part.iY0, part.iY1 };
            for (TInt c = 0; c < 4; c++)
                {
                TReal s, t;
                OrientedToStored(aOrientation, us[c & 1], vs[c >> 1], s, t);
                uv[2 * c] = (s * w - left) / texture.iWidth;
                uv[2 * c + 1] = (t * h - top) / texture.iHeight;
                }
            iGl->AddQuad(aBox.iX0 + (part.iX0 - aWindow.iX0) * sx, aBox.iY0 + (part.iY0 - aWindow.iY0) * sy,
                         aBox.iX0 + (part.iX1 - aWindow.iX0) * sx, aBox.iY0 + (part.iY1 - aWindow.iY0) * sy, uv);
            iGl->DrawBatch(texture, KWhite, aAlpha);
            }
    return ETrue;
    }

void CPhotosView::DrawViewer()
    {
    const TInt w = Rect().Width(), h = Rect().Height();
    const TBool moving = iTrans < 1 || iClosing;
    TReal background = 1;
    if (moving)
        background = Clamp(iTrans, 0, 1);
    else if (iDismissY != 0 || iDismissX != 0)
        background = 1 - Min(1.0, Abs(iDismissY) / (h * 0.5)) * 0.85;
    if (background < 1)
        {

        iGl->BeginFrame(0x000000);
        iGl->SetDim(background);
        DrawScreen(EFalse);
        DrawBars();
        iGl->SetDim(0);
        }
    else
        iGl->BeginFrame(0x000000);

    const TInt current = ViewerPhoto(0);
    if (!moving && iPlayer && iPlayer->Soft() && !iVideoHeld)
        {
        DrawSoftVideo();
        return;
        }
    if (moving)
        {
        TReal dw, dh;
        DisplaySize(current, dw, dh);
        const TBox full = iClosing ? iTransFull : FitBox(current);
        const TReal t = Clamp(iTrans, 0, 1.05);
        DrawPhoto(current, LerpBox(iTransCell, full, t), LerpBox(SquareCrop(dw, dh), TBox(0, 0, 1, 1), Min(t, 1.0)),
                  1);
        return;
        }
    const TInt pageWidth = w + Scale(KPageGap);
    for (TInt k = -1; k <= 1; k++)
        {
        const TInt photo = ViewerPhoto(k);
        if (photo < 0)
            continue;
        TBox box;
        if (k == 0)
            box = ImageBox();
        else
            {
            box = FitBox(photo);
            const TReal raised = InfoShift(photo) * iInfo;
            box.iY0 -= raised;
            box.iY1 -= raised;
            const TReal dx = k * pageWidth + iPageOffset;
            box.iX0 += dx;
            box.iX1 += dx;
            }
        if (box.iX1 <= 0 || box.iX0 >= w)
            continue;

        if (k == 0 && iVideoHeld && iPlayer->Soft() && iPlayer->SoftHasPicture())
            {
            iPlayer->DrawSoft(w, h, 1, iPageOffset);
            continue;
            }
        DrawPhoto(photo, box, TBox(0, 0, 1, 1), 1);
        }

    if (iIcons && IsVideo(current) && !VideoPlaying() && iZoom < 1.05 && background >= 1)
        {
        const TRect button = PlayButtonRect();
        const TPoint c = button.Center() + TPoint((TInt)iPageOffset, 0);
        TRect disk(button);
        disk.Move((TInt)iPageOffset, 0);
        iIcons->DrawStretched(*iGl, EIconDisk, disk, 0x000000, 0.45);
        iIcons->Draw(*iGl, EIconPlayLarge, c.iX + Scale(2), c.iY, KWhite, 1);
        }
    const TReal chrome = iChromeAlpha * background;
    if (chrome > 0)
        DrawViewerChrome(chrome);
    if (iInfo > 0.001)
        DrawInfo();
    }

void CPhotosView::FormatDate(TDes& aText, const TTime& aTime, TBool aWeekday) const
    {
    _LIT(KDays, "MonTueWedThuFriSatSun");
    const TText* const months[12] = {
        _S("January"), _S("February"), _S("March"), _S("April"), _S("May"), _S("June"), _S("July"), _S("August"),
        _S("September"), _S("October"), _S("November"), _S("December") };
    const TDateTime when = aTime.DateTime();
    aText.Zero();
    if (aWeekday)
        {
        aText.Append(KDays().Mid(aTime.DayNoInWeek() * 3, 3));
        aText.Append(' ');
        }
    aText.AppendNum(when.Day() + 1);
    aText.Append(' ');
    aText.Append(TPtrC(months[when.Month()]));
    aText.Append(' ');
    aText.AppendNum(when.Year());
    }

void CPhotosView::DrawViewerChrome(TReal aAlpha)
    {
    const TInt w = Rect().Width(), h = Rect().Height();
    const TInt bar = Scale(KBar);
    const TInt current = ViewerPhoto(0);
    if (current < 0 || !iIcons)
        return;

    const TInt top = iTopInset;
    const TInt shade = Scale(KShade);
    iIcons->DrawStretched(*iGl, EIconGradient, TRect(0, 0, w, top + shade), 0x000000, KShadeAlpha * aAlpha);
    iIcons->DrawStretched(*iGl, EIconGradient, TRect(0, h - shade, w, h), 0x000000, KShadeAlpha * aAlpha, ETrue);
    const TTime taken(iLibrary->Photo(current).iTaken);
    TBuf<40> text;
    FormatDate(text, taken, EFalse);
    iLabels->Draw(*iGl, text, EFontDate, w / 2, top + Scale(25), ELabelCentre, KWhite, aAlpha);
    const TBool video = IsVideo(current);
    const TBool position = video && iPlayer && iPlayer->Path().CompareF(*iLibrary->Photo(current).iPath) == 0 &&
        (iPlayer->State() == CVideoPlayer::EPlaying || iPlayer->State() == CVideoPlayer::EPaused);
    if (position)
        {

        FormatDuration(text, iPlayer->PositionMs() / 1000);
        text.Append(_L(" / "));
        TBuf<16> length;
        FormatDuration(length, (iPlayer->DurationMs() + 500) / 1000);
        text.Append(length);
        }
    else
        {
        const TDateTime when = taken.DateTime();
        text.Format(_L("%02d:%02d"), when.Hour(), when.Minute());
        }
    iLabels->Draw(*iGl, text, EFontTime, w / 2, top + Scale(43), ELabelCentre, KDim, aAlpha, position);
    iIcons->Draw(*iGl, EIconBack, Scale(18), top + bar / 2, KWhite, aAlpha);

    const TBool favourite = iLibrary->Photo(current).iFlags & TPhoto::EFavourite;
    iIcons->Draw(*iGl, favourite ? EIconHeartFull : EIconHeart, BarSlotX(0, 3), h - bar / 2, KWhite, aAlpha);
    if (video)
        iIcons->Draw(*iGl, VideoPlaying() ? EIconPause : EIconPlay, BarSlotX(1, 3), h - bar / 2, KWhite, aAlpha);
    iIcons->Draw(*iGl, EIconMore, BarSlotX(2, 3), h - bar / 2, KWhite, aAlpha);
    }

TInt CPhotosView::InfoLines(TInfoLine* aLines) const
    {
    const TInt current = ViewerPhoto(0);
    if (current < 0)
        return 0;
    const TPhoto& photo = iLibrary->Photo(current);
    const TImageHeader& exif = iInfoHeader;
    const TBool known = iInfoPhoto == current;
    TParsePtrC parse(*photo.iPath);
    TInt n = 0;
    aLines[n].iFont = EFontHeader;
    aLines[n++].iText.Copy(parse.NameAndExt().Left(80));

    TTime taken(photo.iTaken);
    if (known && exif.iHasDate)
        {
        TDateTime when;
        if (when.Set(exif.iYear, (TMonth)(exif.iMonth - 1), exif.iDay - 1, exif.iHour, exif.iMinute, exif.iSecond,
                     0) == KErrNone)
            taken = when;
        }
    FormatDate(aLines[n].iText, taken, ETrue);
    const TDateTime when = taken.DateTime();
    aLines[n].iText.AppendFormat(_L(", %02d:%02d:%02d"), when.Hour(), when.Minute(), when.Second());
    aLines[n++].iFont = EFontBody;

    TDes& size = aLines[n].iText;
    size.Zero();
    TReal dw, dh;
    DisplaySize(current, dw, dh);
    if (dw > 1 && dh > 1)
        size.AppendFormat(_L("%d x %d, %d.%d MP   "), (TInt)dw, (TInt)dh, (TInt)(dw * dh / 1000000),
                          (TInt)(dw * dh / 100000) % 10);
    if (photo.iSize >= 1024 * 1024)
        {
        const TInt tenths = (TInt)((TInt64)photo.iSize * 10 / (1024 * 1024));
        size.AppendFormat(_L("%d.%d MB"), tenths / 10, tenths % 10);
        }
    else
        size.AppendFormat(_L("%d KB"), Max(1, photo.iSize / 1024));
    aLines[n++].iFont = EFontBody;

    if (known && (exif.iMakeLength || exif.iModelLength))
        {
        TBuf<TImageHeader::KTextLength> make, model;
        make.Copy(TPtrC8(exif.iMake, exif.iMakeLength));
        model.Copy(TPtrC8(exif.iModel, exif.iModelLength));
        TDes& camera = aLines[n].iText;
        camera.Zero();

        if (make.Length() && model.Left(make.Length()).CompareF(make) != 0)
            {
            camera.Append(make);
            camera.Append(' ');
            }
        camera.Append(model);
        aLines[n++].iFont = EFontBody;
        }

    if (known)
        {
        TDes& settings = aLines[n].iText;
        settings.Zero();
        if (exif.iExposure[0] && exif.iExposure[1])
            {
            if (exif.iExposure[0] < exif.iExposure[1])
                settings.AppendFormat(_L("1/%d s   "), (TInt)((exif.iExposure[1] + exif.iExposure[0] / 2) /
                                                              exif.iExposure[0]));
            else
                settings.AppendFormat(_L("%d s   "), (TInt)(exif.iExposure[0] / exif.iExposure[1]));
            }
        if (exif.iFNumber[1])
            {
            const TInt tenths = (TInt)((exif.iFNumber[0] * 10 + exif.iFNumber[1] / 2) / exif.iFNumber[1]);
            settings.AppendFormat(_L("f/%d.%d   "), tenths / 10, tenths % 10);
            }
        if (exif.iIso > 0)
            settings.AppendFormat(_L("ISO %d   "), exif.iIso);
        if (exif.iFocal[1])
            {
            const TInt tenths = (TInt)((exif.iFocal[0] * 10 + exif.iFocal[1] / 2) / exif.iFocal[1]);
            settings.AppendFormat(_L("%d.%d mm   "), tenths / 10, tenths % 10);
            }
        if (exif.iFlash >= 0)
            settings.Append(exif.iFlash & 1 ? _L("Flash") : _L("No flash"));
        settings.TrimRight();
        if (settings.Length())
            aLines[n++].iFont = EFontBody;
        }

    if (known && exif.iHasPosition)
        {
        TDes& place = aLines[n].iText;
        place.Zero();
        const TReal values[2] = { exif.iLatitude, exif.iLongitude };
        const TText* const hemispheres[2] = { _S("NS"), _S("EW") };
        for (TInt i = 0; i < 2; i++)
            {
            const TReal v = values[i] < 0 ? -values[i] : values[i];
            const TInt degrees = (TInt)v;
            const TInt fraction = (TInt)((v - degrees) * 100000 + 0.5);
            place.AppendFormat(_L("%d.%05d"), degrees, Min(fraction, 99999));
            place.Append(TChar(0x00B0));
            place.Append(' ');
            place.Append(hemispheres[i][values[i] < 0 ? 1 : 0]);
            if (i == 0)
                place.Append(_L(",  "));
            }
        aLines[n++].iFont = EFontBody;
        }

    aLines[n].iFont = EFontSmall;
    aLines[n++].iText.Copy(parse.DriveAndPath().Left(80));
    return n;
    }

TReal CPhotosView::InfoShift(TInt aPhoto) const
    {
    if (aPhoto < 0)
        return 0;
    const TBox fit = FitBox(aPhoto);
    const TReal room = Rect().Height() - Scale(KBar) - InfoHeight() - Scale(KInfoPadTop);
    return Max(0.0, fit.iY1 - room);
    }

TInt CPhotosView::InfoTravel() const
    {
    return InfoHeight() + Scale(KBar);
    }

TInt CPhotosView::InfoHeight() const
    {
    TInfoLine lines[KInfoLines];
    return Scale(KInfoPad) + InfoLines(lines) * Scale(KInfoLine);
    }

void CPhotosView::DrawInfo()
    {
    const TInt w = Rect().Width(), h = Rect().Height();
    const TInt bar = Scale(KBar);
    TInfoLine lines[KInfoLines];
    const TInt count = InfoLines(lines);
    if (count == 0)
        return;

    const TInt height = Scale(KInfoPad) + count * Scale(KInfoLine);
    const TInt top = h - bar - height + (TInt)((1 - Min(1.0, iInfo)) * (height + bar));
    const TReal alpha = Clamp(iInfo, 0, 1);
    const TInt x = Scale(16), width = w - 2 * x;
    for (TInt i = 0; i < count; i++)
        {
        iLabels->Fit(lines[i].iText, lines[i].iFont, width);
        iLabels->Draw(*iGl, lines[i].iText, lines[i].iFont, x, top + Scale(30) + i * Scale(KInfoLine), ELabelLeft,
                      lines[i].iFont == EFontSmall ? KDim : KWhite, alpha);
        }
    }

void CPhotosView::ReadInfoHeader()
    {
    const TInt current = ViewerPhoto(0);
    iInfoPhoto = -1;
    iInfoHeader.Reset();
    if (current < 0)
        return;
    HBufC8* bytes = HBufC8::New(64 * 1024);
    if (!bytes)
        return;
    TPtr8 data = bytes->Des();
    RFile file;
    if (file.Open(CEikonEnv::Static()->FsSession(), *iLibrary->Photo(current).iPath,
                  EFileRead | EFileShareReadersOnly) == KErrNone)
        {
        if (file.Read(data) == KErrNone)
            {
            ParseImageHeader(data.Ptr(), data.Length(), iInfoHeader);
            iInfoPhoto = current;
            }
        file.Close();
        }
    delete bytes;
    }

void CPhotosView::ToggleInfo()
    {
    iInfoOpen = !iInfoOpen;
    if (iInfoOpen)
        ReadInfoHeader();
    RequestFrame();
    }

void CPhotosView::ToggleFavourite()
    {
    const TInt current = ViewerPhoto(0);
    if (current < 0)
        return;
    TPhoto& photo = iLibrary->PhotoForChange(current);
    const TDesC& path = *photo.iPath;
    TInt at;
    const TBool was = iFavourites->FindIsq(path, at, ECmpFolded) == 0;
    TRAPD(err, ChangeFavouriteL(path, !was));
    if (err != KErrNone)
        {
        Trace(_L("viewer: favourite err %d"), err);
        return;
        }
    if (was)
        photo.iFlags &= ~TPhoto::EFavourite;
    else
        photo.iFlags |= TPhoto::EFavourite;
    iAlbumsDirty = ETrue;
    RequestFrame();
    }

void CPhotosView::ChangeFavouriteL(const TDesC& aPath, TBool aAdd)
    {
    TInt at;
    if (iFavourites->FindIsq(aPath, at, ECmpFolded) == 0)
        {
        if (!aAdd)
            iFavourites->Delete(at);
        }
    else if (aAdd)
        iFavourites->InsertIsqL(aPath, ECmpFolded);
    SaveFavouritesL();
    }

void CPhotosView::ShowMenuL()
    {
    const TInt current = ViewerPhoto(0);
    if (current < 0)
        return;
    if (VideoPlaying())
        iPlayer->Pause();
    delete iMenu;
    iMenu = NULL;
    const TPoint anchor = PositionRelativeToScreen() +
        TPoint(Rect().Width() - Scale(6), Rect().Height() - Scale(KBar) - Scale(4));
    iMenu = CAknStylusPopUpMenu::NewL(CEikonEnv::Static()->EikAppUi(), anchor, NULL);
    const TInt flags = iLibrary->Photo(current).iFlags;
    if (flags & TPhoto::ETrashed)
        {
        iMenu->AddMenuItemL(_L("Restore"), ECmdRestore);
        iMenu->AddMenuItemL(_L("Delete Permanently"), ECmdDeleteForever);
        }
    else
        {
        iMenu->AddMenuItemL(_L("Share"), ECmdShare);
        if (flags & TPhoto::EVideo)
            iMenu->AddMenuItemL(_L("Open in Videos"), ECmdOpenExternally);
        iMenu->AddMenuItemL(_L("Rename"), ECmdRename);
        iMenu->AddMenuItemL(flags & TPhoto::EHidden ? _L("Unhide") : _L("Hide"),
                            flags & TPhoto::EHidden ? ECmdUnhide : ECmdHide);
        iMenu->AddMenuItemL(_L("Delete"), ECmdDelete);
        }
    iMenu->AddMenuItemL(iInfoOpen ? _L("Hide info") : _L("Info"), ECmdInfo);
    iMenu->SetPosition(anchor, CAknStylusPopUpMenu::EPositionTypeRightBottom);
    iMenu->ShowMenu();
    }

void CPhotosView::HandleCommandL(TInt aCommand)
    {
    if (!iViewerOpen || iClosing)
        return;
    switch (aCommand)
        {
        case ECmdShare:
            Share();
            break;
        case ECmdRename:
            RenameL();
            break;
        case ECmdDelete:
            ViewerActL(EActTrash);
            break;
        case ECmdHide:
            ViewerActL(EActHide);
            break;
        case ECmdUnhide:
            ViewerActL(EActUnhide);
            break;
        case ECmdRestore:
            ViewerActL(EActRestore);
            break;
        case ECmdDeleteForever:
            ViewerActL(EActDeleteForever);
            break;
        case ECmdOpenExternally:
            if (ViewerPhoto(0) >= 0)
                OpenExternally(*iLibrary->Photo(ViewerPhoto(0)).iPath);
            break;
        case ECmdInfo:
            ToggleInfo();
            break;
        default:
            break;
        }
    }

void CPhotosView::RenameL()
    {
    const TInt current = ViewerPhoto(0);
    if (current < 0)
        return;
    const TFileName oldPath(*iLibrary->Photo(current).iPath);
    TParsePtrC parse(oldPath);
    TFileName name(parse.Name());
    CAknTextQueryDialog* ask = CAknTextQueryDialog::NewL(name);
    if (!ask->ExecuteLD(R_PHOTOS_RENAME_QUERY) || name.Length() == 0 || name == parse.Name())
        return;
    TFileName newPath(parse.DriveAndPath());
    newPath.Append(name.Left(KMaxFileName - newPath.Length() - parse.Ext().Length()));
    newPath.Append(parse.Ext());
    RFs& fs = CEikonEnv::Static()->FsSession();
    TInt err = fs.IsValidName(newPath) ? fs.Rename(oldPath, newPath) : KErrBadName;
    Trace(_L("viewer: rename %S -> %S err %d"), &oldPath, &newPath, err);
    if (err != KErrNone)
        {
        CEikonEnv::Static()->InfoMsg(err == KErrAlreadyExists ? _L("A file with that name exists") :
                                                                _L("Can't rename the photo"));
        return;
        }
    TInt at;
    if (iFavourites->FindIsq(oldPath, at, ECmpFolded) == 0)
        {
        iFavourites->Delete(at);
        iFavourites->InsertIsqAllowDuplicatesL(newPath, ECmpFolded);
        TRAP_IGNORE(SaveFavouritesL());
        }

    const TInt moved = iMoves->Find(oldPath);
    if (moved >= 0)
        {
        const TFileName origin(iMoves->Origin(moved));
        iMoves->SetL(newPath, origin, iMoves->When(moved));
        iMoves->Remove(oldPath);
        TRAP_IGNORE(iMoves->SaveL(fs));
        }
    CLibrary* renamed = iLibrary->CopyL(-1, current, newPath);
    CleanupStack::PushL(renamed);
    renamed->SetFavouritesL(*iFavourites);
    CleanupStack::Pop(renamed);
    ReplaceLibrary(renamed, newPath);
    }

void CPhotosView::ViewerActL(TAction aAction)
    {
    const TInt current = ViewerPhoto(0);
    if (current < 0)
        return;
    StopVideo();
    if ((aAction == EActTrash || aAction == EActDeleteForever) &&
        !ConfirmL(aAction == EActTrash ? _L("Delete this item?") : _L("Delete this item permanently?")))
        return;
    const TInt next = ViewerPhoto(1) >= 0 ? ViewerPhoto(1) : ViewerPhoto(-1);
    TFileName nextPath;
    if (next >= 0)
        nextPath.Copy(*iLibrary->Photo(next).iPath);
    RArray<TInt> photos;
    CleanupClosePushL(photos);
    photos.AppendL(current);
    iZoom = 1;
    iPanX = iPanY = iPanXVel = iPanYVel = 0;
    iZoomSettling = EFalse;
    ActL(aAction, photos, nextPath);
    CleanupStack::PopAndDestroy(&photos);
    }

void CPhotosView::Share()
    {
    const TInt current = ViewerPhoto(0);
    if (current < 0)
        return;
    RArray<TInt> photos;
    TInt err = photos.Append(current);
    if (err == KErrNone)
        TRAP(err, SharePhotosL(photos));
    photos.Close();
    Trace(_L("viewer: share err %d"), err);
    }

TBool CPhotosView::IsVideo(TInt aPhoto) const
    {
    return aPhoto >= 0 && aPhoto < iLibrary->Count() && (iLibrary->Photo(aPhoto).iFlags & TPhoto::EVideo);
    }

void CPhotosView::FormatDuration(TDes& aText, TInt aSeconds)
    {
    aText.Zero();
    if (aSeconds >= 3600)
        {
        aText.AppendNum(aSeconds / 3600);
        aText.Append(':');
        aText.AppendNumFixedWidth(aSeconds / 60 % 60, EDecimal, 2);
        }
    else
        aText.AppendNum(aSeconds / 60);
    aText.Append(':');
    aText.AppendNumFixedWidth(aSeconds % 60, EDecimal, 2);
    }

TRect CPhotosView::PlayButtonRect() const
    {
    const TInt r = Scale(32);
    const TPoint c(Rect().Width() / 2, Rect().Height() / 2);
    return TRect(c.iX - r, c.iY - r, c.iX + r, c.iY + r);
    }

TBool CPhotosView::VideoPlaying() const
    {
    return iPlayer && (iPlayer->State() == CVideoPlayer::EPlaying || iPlayer->State() == CVideoPlayer::EOpening);
    }

void CPhotosView::PlayVideo()
    {
    const TInt current = ViewerPhoto(0);
    if (!IsVideo(current) || iClosing || iTrans < 1)
        return;
    const TDesC& path = *iLibrary->Photo(current).iPath;
    if (!iPlayer)
        {
        OpenExternally(path);
        return;
        }
    TBuf<40> title;
    FormatDate(title, TTime(iLibrary->Photo(current).iTaken), EFalse);
    TRAPD(err, iPlayer->PlayL(path, Rect(), title));
    if (err != KErrNone)
        {
        TraceNow(_L("video: can't play %S: %d"), &path, err);
        iPlayer->Stop();
        CEikonEnv::Static()->InfoMsg(_L("Can't play this video here"));
        OpenExternally(path);
        return;
        }
    iTapPending = EFalse;
    RequestFrame();
    }

void CPhotosView::StopVideo()
    {
    iVideoHeld = EFalse;
    if (iPlayer && iPlayer->State() != CVideoPlayer::EIdle)
        {
        iPlayer->Stop();
        RequestFrame();
        }
    }

void CPhotosView::VideoChanged()
    {
    if (!iPlayer)
        return;
    switch (iPlayer->State())
        {
        case CVideoPlayer::EFailed:
            {

            const TFileName path(iPlayer->Path());
            N8mLogDrain();
            TraceNow(_L("video: playing failed %d, opening it in the phone's player"), iPlayer->Error());
            CEikonEnv::Static()->InfoMsg(_L("Can't play this video here"));
            OpenExternally(path);
            iChrome = ETrue;
            break;
            }
        case CVideoPlayer::EEnded:
        case CVideoPlayer::EPaused:
            iChrome = ETrue;
            break;
        default:
            break;
        }
    RequestFrame();
    }

void CPhotosView::VideoPointerL(const TPointerEvent& aEvent, TInt aPointer)
    {
    const TUint32 now = NowMs();
    const TPoint pos = aEvent.iPosition;
    iVideoTouch[aPointer] = pos;
    if (aPointer == 1)
        {
        if (aEvent.iType == TPointerEvent::EButton1Down)
            {
            iVideoTouchDown[1] = ETrue;
            if (iVideoTouchDown[0] && !iVideoScrub && !iVideoDragged)
                {
                iVideoPinching = ETrue;
                iVideoIgnore = ETrue;
                iVideoPinchDistance = Max(20.0, Distance(iVideoTouch[0], iVideoTouch[1]));
                iVideoPinchZoom = iPlayer->Zoom();
                iVideoPinchMidX = (iVideoTouch[0].iX + iVideoTouch[1].iX) / 2.0;
                iVideoPinchMidY = (iVideoTouch[0].iY + iVideoTouch[1].iY) / 2.0;
                iVideoPinchPanX = iPlayer->PanX();
                iVideoPinchPanY = iPlayer->PanY();
                }
            }
        else if (aEvent.iType == TPointerEvent::EDrag && iVideoPinching)
            VideoPinch();
        else if (aEvent.iType == TPointerEvent::EButton1Up)
            {
            iVideoTouchDown[1] = EFalse;
            iVideoPinching = EFalse;
            if (iPlayer->Zoom() < 1.05)
                iPlayer->SetZoom(1, 0, 0);
            }
        RequestFrame();
        return;
        }
    switch (aEvent.iType)
        {
        case TPointerEvent::EButton1Down:
            iVideoTouchDown[0] = ETrue;
            iVideoDown = pos;

            if (iVideoTapMs && now - iVideoTapMs <= (TUint32)KDoubleTapMs &&
                Distance(pos, iVideoTapPos) < Scale(KDoubleTapSlop) && !iPlayer->OnProgress(pos))
                {
                iVideoTapMs = 0;
                iVideoIgnore = ETrue;
                iVideoScrub = EFalse;
                iPlayer->ShowControls(iVideoTapHadControls);
                if (iPlayer->Zoom() > 1.05)
                    iPlayer->SetZoom(1, 0, 0);
                else
                    {
                    const TReal fill = iPlayer->FillZoom();
                    if (fill > 1.05)
                        iPlayer->SetZoom(fill, 0, 0);
                    else
                        {
                        const TReal cx = Rect().Width() / 2.0, cy = Rect().Height() / 2.0;
                        iPlayer->SetZoom(2, (cx - pos.iX), (cy - pos.iY));
                        }
                    }
                Feedback();
                break;
                }
            iVideoDragged = EFalse;
            iVideoPanned = EFalse;
            iVideoIgnore = EFalse;
            iVideoPinchPanX = iPlayer->PanX();
            iVideoPinchPanY = iPlayer->PanY();
            iVideoScrub = iPlayer->OnProgress(pos);
            if (iVideoScrub)
                iPlayer->BeginScrub(pos);
            break;
        case TPointerEvent::EDrag:
            {
            if (iVideoPinching)
                {
                VideoPinch();
                break;
                }
            if (iVideoIgnore)
                break;
            if (iVideoScrub)
                {
                iPlayer->ScrubTo(pos);
                break;
                }
            const TPoint d = pos - iVideoDown;
            if (iPlayer->Zoom() > 1.01)
                {
                iVideoPanned = iVideoPanned || Abs(d.iX) >= Scale(KSlop) || Abs(d.iY) >= Scale(KSlop);
                if (iVideoPanned)
                    iPlayer->SetZoom(iPlayer->Zoom(), iVideoPinchPanX + d.iX, iVideoPinchPanY + d.iY);
                break;
                }
            if (!iVideoDragged && (Abs(d.iX) >= Scale(KSlop) || Abs(d.iY) >= Scale(KSlop)))
                {
                iVideoDragged = ETrue;

                const CVideoPlayer::TState state = iPlayer->State();
                if (Abs(d.iX) > Abs(d.iY) && (state == CVideoPlayer::EPlaying || state == CVideoPlayer::EPaused))
                    {
                    iPlayer->Pause();
                    iPlayer->ShowControls(EFalse);
                    iVideoHeld = ETrue;
                    }
                else
                    StopVideo();
                TPointerEvent down = aEvent;
                down.iType = TPointerEvent::EButton1Down;
                down.iPosition = iVideoDown;
                ViewerPointer(down, 0, now);
                }
            if (iVideoDragged)
                ViewerPointer(aEvent, 0, now);
            break;
            }
        case TPointerEvent::EButton1Up:
            iVideoTouchDown[0] = EFalse;
            if (iVideoPinching)
                {
                iVideoPinching = EFalse;
                if (iPlayer->Zoom() < 1.05)
                    iPlayer->SetZoom(1, 0, 0);
                }
            else if (iVideoIgnore || iVideoPanned)
                ;
            else if (iVideoScrub)
                {
                iVideoScrub = EFalse;
                iPlayer->EndScrub();
                }
            else if (iVideoDragged)
                ViewerPointer(aEvent, 0, now);
            else
                {
                iStatusAskMs = now;
                const TBool hadControls = iPlayer->ControlsShown();
                const CVideoPlayer::TTap tap = iPlayer->Tap(iVideoDown);
                iVideoTapMs = tap == CVideoPlayer::ETapControls ? now : 0;
                iVideoTapPos = iVideoDown;
                iVideoTapHadControls = hadControls;
                if (tap != CVideoPlayer::ETapControls)
                    Feedback();
                if (tap == CVideoPlayer::ETapBack)
                    CloseViewer();
                }
            break;
        default:
            break;
        }
    RequestFrame();
    }

void CPhotosView::VideoPinch()
    {
    const TReal zoom = iVideoPinchZoom * Distance(iVideoTouch[0], iVideoTouch[1]) / iVideoPinchDistance;
    const TReal cx = Rect().Width() / 2.0, cy = Rect().Height() / 2.0;
    const TReal midX = (iVideoTouch[0].iX + iVideoTouch[1].iX) / 2.0;
    const TReal midY = (iVideoTouch[0].iY + iVideoTouch[1].iY) / 2.0;
    const TReal relX = iVideoPinchMidX - (cx + iVideoPinchPanX), relY = iVideoPinchMidY - (cy + iVideoPinchPanY);
    const TReal ratio = Max(1.0, zoom) / iVideoPinchZoom;
    iPlayer->SetZoom(zoom, midX - relX * ratio - cx, midY - relY * ratio - cy);
    }

void CPhotosView::VideoFrameReady()
    {
    RequestFrame();
    }

void CPhotosView::VideoControlsChanged()
    {
    UpdateStatusBar();
    }

TBool CPhotosView::StatusBarWanted() const
    {
    if (!iViewerOpen || iClosing)
        return ETrue;
    if (iPlayer)
        {
        const CVideoPlayer::TState state = iPlayer->State();
        if (state == CVideoPlayer::EOpening || state == CVideoPlayer::EPlaying || state == CVideoPlayer::EPaused)
            return iPlayer->ControlsShown();
        }
    return iChrome;
    }

void CPhotosView::UpdateStatusBar()
    {
    const TBool want = StatusBarWanted();
    if (want == iStatusBar.StatusBarShown())
        return;
    const TInt sizes = iSizeEvents;
    const TInt64 us = PerfNowUs();
    const TInt callbacks = iStatusBar.ShowStatusBar(want);
    Trace(_L("statusbar: %s at +%d ms, size events %d (status pane callbacks %d, call %d ms)"),
          want ? _S("shown") : _S("hidden"), (TInt)(NowMs() - iStatusAskMs), iSizeEvents - sizes, callbacks,
          (TInt)((PerfNowUs() - us) / 1000));
    RequestFrame();
    }

TInt CPhotosView::VisibleTop() const
    {
    return iStatusBar.StatusBarShown() ? iTopInset : 0;
    }

void CPhotosView::PrepareShownVideo()
    {
    if (!iPlayer || !iViewerOpen || iClosing || iTrans < 1 || iSnapping || iDismissY != 0 || iDismissX != 0 ||
        (iAxis != EAxisNone && iAxis != EAxisIgnore))
        return;
    const TInt current = ViewerPhoto(0);
    const CVideoPlayer::TState state = iPlayer->State();
    if (!IsVideo(current))
        {
        if (state == CVideoPlayer::EPreparing || state == CVideoPlayer::EReady)
            iPlayer->Stop();
        return;
        }
    const TDesC& path = *iLibrary->Photo(current).iPath;
    if (state != CVideoPlayer::EIdle && iPlayer->Path().CompareF(path) == 0)
        return;
    if (VideoPlaying())
        return;
    TBuf<40> title;
    FormatDate(title, TTime(iLibrary->Photo(current).iTaken), EFalse);
    TRAPD(err, iPlayer->PrepareL(path, Rect(), title));
    if (err != KErrNone)
        TraceNow(_L("video: can't prepare %S: %d"), &path, err);
    }

void CPhotosView::DrawSoftVideo()
    {
    const TInt w = Rect().Width(), h = Rect().Height();
    if (iPlayer->SoftHasPicture())
        iPlayer->DrawSoft(w, h, 1);
    else if (iPlayer->Waiting())
        DrawVideoSpinner();
    else
        DrawPhoto(ViewerPhoto(0), ImageBox(), TBox(0, 0, 1, 1), 1);
    if (iVideoControls > 0)
        DrawVideoControls(iVideoControls);
    }

void CPhotosView::DrawVideoControls(TReal aAlpha)
    {
    if (!iIcons)
        return;
    const TInt w = Rect().Width(), h = Rect().Height();
    const TInt shade = Scale(KShade), top = iTopInset;
    iIcons->DrawStretched(*iGl, EIconGradient, TRect(0, 0, w, top + shade), 0x000000, KShadeAlpha * aAlpha);
    iIcons->DrawStretched(*iGl, EIconGradient, TRect(0, h - shade - Scale(40), w, h), 0x000000,
                          KShadeAlpha * aAlpha, ETrue);
    iIcons->Draw(*iGl, EIconBack, Scale(18), top + Scale(KBar) / 2, KWhite, aAlpha);
    iLabels->Draw(*iGl, iPlayer->Title(), EFontDate, w / 2, top + Scale(34), ELabelCentre, KWhite, aAlpha);
    TInt lineY, x0, x1;
    iPlayer->ProgressLine(lineY, x0, x1);
    const TInt position = iPlayer->PositionMs(), duration = iPlayer->DurationMs();
    const TInt done = duration > 0 ? x0 + (TInt)((TInt64)(x1 - x0) * Min(position, duration) / duration) : x0;
    iGl->FillRect(TRect(x0, lineY, x1, lineY + Scale(3)), 0x8E8E93, aAlpha);
    iGl->FillRect(TRect(x0, lineY, done, lineY + Scale(3)), KWhite, aAlpha);
    const TInt knob = Scale(7);
    iIcons->DrawStretched(*iGl, EIconDisk, TRect(done - knob, lineY + 1 - knob, done + knob, lineY + 1 + knob),
                          KWhite, aAlpha);
    TBuf<16> text;
    FormatDuration(text, position / 1000);
    iLabels->Draw(*iGl, text, EFontTime, x0, lineY + Scale(24), ELabelLeft, KWhite, aAlpha, ETrue);
    FormatDuration(text, (duration + 500) / 1000);
    iLabels->Draw(*iGl, text, EFontTime, x1, lineY + Scale(24), ELabelRight, KWhite, aAlpha, ETrue);
    const TPoint c = iPlayer->PlayPauseRect().Center();
    const TBool playing = iPlayer->State() == CVideoPlayer::EPlaying || iPlayer->State() == CVideoPlayer::EOpening;
    iIcons->Draw(*iGl, playing ? EIconPause : EIconPlay, c.iX, c.iY, KWhite, aAlpha);
    const TReal scrubAlpha = iPlayer->ScrubAlpha() * aAlpha;
    if (scrubAlpha > 0)
        {

        TBuf<16> at, length;
        FormatDuration(at, iPlayer->ScrubMs() / 1000);
        FormatDuration(length, (iPlayer->DurationMs() + 500) / 1000);
        TBuf<40> time;
        time.Format(_L("%S / %S"), &at, &length);
        const TInt tw = iLabels->TextWidth(time, EFontScrub), th = Scale(30);
        const TRect pill(w / 2 - tw / 2 - Scale(18), h / 2 - th / 2 - Scale(10), w / 2 + tw / 2 + Scale(18),
                         h / 2 + th / 2 + Scale(10));
        FillRoundRect(pill, pill.Height() / 2, 0x1C1C1E, 0.9 * scrubAlpha);
        iLabels->Draw(*iGl, time, EFontScrub, w / 2, h / 2 + th * 7 / 20, ELabelCentre, KWhite, scrubAlpha);
        }
    const TPoint loop = iPlayer->LoopRect().Center();
    iIcons->Draw(*iGl, EIconLoop, loop.iX, loop.iY, iPlayer->Looping() ? KBlue : KWhite, aAlpha);
    }

void CPhotosView::DrawVideoSpinner()
    {
    TInt side = 0;
    const TUint8* frame = iPlayer->SpinnerFrame(side);
    if (!frame)
        return;
    if (!iSpinnerTexture.Valid() && !iGl->CreateTexture(iSpinnerTexture, side, side, TGlTexture::ELuminance, ETrue))
        return;
    iGl->Upload(iSpinnerTexture, 0, 0, side, side, frame);
    const TInt x = (Rect().Width() - side) / 2, y = (Rect().Height() - side) / 2;
    iGl->AddQuad(x, y, x + side, y + side, 0, 0, 1, 1);
    iGl->DrawBatch(iSpinnerTexture, KWhite, 1);
    }

void CPhotosView::OpenExternally(const TDesC& aPath)
    {
    TInt err = KErrNone;
    if (!iDocHandler)
        TRAP(err, iDocHandler = CDocumentHandler::NewL());
    TInt result = err;
    if (iDocHandler)
        {
        TDataType type;
        TRAP(err, result = iDocHandler->OpenFileL(aPath, type));
        }
    TraceNow(_L("video: %S opened outside, err %d result %d"), &aPath, err, result);
    }

const TGlTexture* CPhotosView::CoverTexture(TInt aPhoto, TInt aSide)
    {
    if (aPhoto < 0 || IsVideo(aPhoto))
        return NULL;
    TCover* slot = NULL;
    for (TInt i = 0; i < KCovers; i++)
        if (iCovers[i].iPhoto == aPhoto)
            {
            iCovers[i].iUsedFrame = iFrame;
            return iCovers[i].iTexture.Valid() ? &iCovers[i].iTexture : NULL;
            }
    if (!iImages || iImages->Busy() || iViewerOpen || !iGl)
        return NULL;
    for (TInt i = 0; i < KCovers; i++)
        {
        TCover& cover = iCovers[i];
        if (cover.iRequested || cover.iUsedFrame == iFrame)
            continue;
        if (cover.iPhoto < 0)
            {
            slot = &cover;
            break;
            }
        if (!slot || cover.iUsedFrame < slot->iUsedFrame)
            slot = &cover;
        }
    if (!slot)
        return NULL;
    iGl->DeleteTexture(slot->iTexture);
    slot->iPhoto = aPhoto;
    slot->iUsedFrame = iFrame;
    slot->iRequested = ETrue;
    const TInt maxSide = Min(KMaxImageSide, iGl->MaxTexture());
    if (iImages->Request(*iLibrary->Photo(aPhoto).iPath, KCoverTag + (slot - iCovers), EFalse, TSize(aSide, aSide),
                         maxSide, aSide * aSide * 6) != KErrNone)
        slot->iRequested = EFalse;
    return NULL;
    }

void CPhotosView::CoverReady(TImageJob& aJob)
    {
    const TInt index = aJob.iTag - KCoverTag;
    if (index < 0 || index >= KCovers)
        return;
    TCover& cover = iCovers[index];
    if (!cover.iRequested || cover.iPhoto < 0 || iLibrary->Photo(cover.iPhoto).iPath->Compare(aJob.iPath) != 0)
        return;
    cover.iRequested = EFalse;
    if (aJob.iResult != KErrNone || !aJob.iPixels || !iGl)
        return;
    if (iGl->CreateTexture(cover.iTexture, aJob.iSize.iWidth, aJob.iSize.iHeight, TGlTexture::ERgb565, ETrue))
        iGl->Upload(cover.iTexture, 0, 0, aJob.iSize.iWidth, aJob.iSize.iHeight, aJob.iPixels);
    Trace(_L("covers: %S %dx%d in %d ms"), &aJob.iPath, aJob.iSize.iWidth, aJob.iSize.iHeight, aJob.iMicros / 1000);
    iFrostKey = 0;
    RequestFrame();
    }

void CPhotosView::DrawCover(TInt aPhoto, const TGlTexture& aTexture, const TRect& aRect)
    {
    TReal dw, dh;
    DisplaySize(aPhoto, dw, dh);
    const TBox square = SquareCrop(dw, dh);
    const TInt orientation = iLibrary->Photo(aPhoto).iOrientation;
    TReal uv[8];
    const TReal us[2] = { square.iX0, square.iX1 }, vs[2] = { square.iY0, square.iY1 };
    for (TInt c = 0; c < 4; c++)
        OrientedToStored(orientation, us[c & 1], vs[c >> 1], uv[2 * c], uv[2 * c + 1]);
    iGl->AddQuad(aRect.iTl.iX, aRect.iTl.iY, aRect.iBr.iX, aRect.iBr.iY, uv);
    iGl->DrawBatch(aTexture, KWhite, 1);
    }

void CPhotosView::ForgetCovers(TBool aDelete)
    {
    for (TInt i = 0; i < KCovers; i++)
        {
        if (aDelete && iGl)
            iGl->DeleteTexture(iCovers[i].iTexture);
        iCovers[i].iTexture = TGlTexture();
        if (!iCovers[i].iRequested)
            iCovers[i].iPhoto = -1;
        }
    }

namespace
    {
    const TInt KGifRam = 8 * 1024 * 1024;
    const TInt KGifAhead = 6;
    const TInt KGifStart = 3;
    const TInt KGifMaxSide = 640;
    _LIT(KGifKeptAll, "every frame kept");
    _LIT(KGifKeptAhead, "decoded ahead");

    TInt GifDelay(TInt aMs)
        {
        return aMs <= 10 ? 100 : Max(20, aMs);
        }
    }

TBool CPhotosView::IsGif(TInt aPhoto) const
    {
    if (aPhoto < 0 || aPhoto >= iLibrary->Count())
        return EFalse;
    const TDesC& path = *iLibrary->Photo(aPhoto).iPath;
    return path.Length() > 4 && path.Right(4).CompareF(_L(".gif")) == 0;
    }

void CPhotosView::StartGif(TInt aPhoto)
    {
    StopGif();
    if (!IsGif(aPhoto) || !iViewerOpen)
        return;
    iGifPhoto = aPhoto;
    iGifTag++;
    iGifRestart = ETrue;
    RequestGif();
    }

void CPhotosView::StopGif()
    {
    for (TInt i = 0; i < iGifFrames.Count(); i++)
        User::Free(iGifFrames[i].iPixels);
    iGifFrames.Reset();
    if (iGl)
        iGl->DeleteTexture(iGifTexture);
    iGifTexture = TGlTexture();
    iGifPhoto = -1;
    iGifTag++;
    iGifDone = EFalse;
    iGifCount = 0;
    iGifAll = EFalse;
    iGifShown = -1;
    iGifPlaying = EFalse;
    iGifDecoded = iGifDecodeUs = 0;
    iGifLogged = EFalse;
    }

void CPhotosView::RequestGif()
    {
    if (iGifPhoto < 0 || iGifDone || !iGifWorker || iGifWorker->Busy())
        return;
    if (!iGifAll && iGifCount > 0 && iGifFrames.Count() >= KGifAhead)
        return;
    if (iGifWorker->Request(*iLibrary->Photo(iGifPhoto).iPath, iGifTag, iGifRestart, KGifMaxSide) == KErrNone)
        iGifRestart = EFalse;
    }

void CPhotosView::GifReady(TGifJob& aJob)
    {
    if (aJob.iTag != iGifTag || iGifPhoto < 0)
        {
        RequestGif();
        return;
        }
    const TDesC& path = *iLibrary->Photo(iGifPhoto).iPath;
    const TParsePtrC parse(path);
    const TPtrC name(parse.NameAndExt());
    if (aJob.iCount == 0)
        {
        Trace(_L("gif: %S failed %d"), &name, aJob.iResult);
        iGifDone = ETrue;
        return;
        }
    if (iGifCount == 0)
        {
        iGifCount = aJob.iFrameCount;
        iGifSize = aJob.iSize;
        iGifStride = aJob.iStride;
        if (iGifCount <= 1)
            {
            iGifDone = ETrue;
            return;
            }
        iGifAll = (TInt64)iGifCount * iGifStride * iGifSize.iHeight <= KGifRam;
        if (iGifAll)
            {
            TGifFrame empty;
            empty.iPixels = NULL;
            empty.iDelayMs = 100;
            for (TInt i = 0; i < iGifCount; i++)
                {
                empty.iIndex = i;
                if (iGifFrames.Append(empty) != KErrNone)
                    {
                    iGifDone = ETrue;
                    return;
                    }
                }
            }
        }
    iGifDecoded += aJob.iCount;
    iGifDecodeUs += aJob.iMicros;
    for (TInt i = 0; i < aJob.iCount; i++)
        {
        TGifFrame frame;
        frame.iIndex = aJob.iIndex[i];
        frame.iPixels = aJob.iPixels[i];
        frame.iDelayMs = GifDelay(aJob.iDelayMs[i]);
        if (iGifAll)
            {
            if (frame.iIndex < 0 || frame.iIndex >= iGifCount || iGifFrames[frame.iIndex].iPixels)
                continue;
            iGifFrames[frame.iIndex] = frame;
            }
        else if (iGifFrames.Append(frame) != KErrNone)
            continue;
        aJob.iPixels[i] = NULL;
        }
    TInt kept = 0;
    for (TInt i = 0; i < iGifFrames.Count(); i++)
        kept += iGifFrames[i].iPixels != NULL;
    if (iGifAll && kept == iGifCount)
        iGifDone = ETrue;
    if (!iGifLogged && iGifDecoded >= iGifCount)
        {
        iGifLogged = ETrue;
        Trace(_L("gif: %S %dx%d (shown %dx%d), %d frames, loops forever, decode %d ms/frame, RAM %d KB (%S)"),
              &name, aJob.iCanvas.iWidth, aJob.iCanvas.iHeight, iGifSize.iWidth, iGifSize.iHeight,
              iGifCount, iGifDecodeUs / Max(1, iGifDecoded) / 1000,
              (iGifAll ? iGifCount : KGifAhead) * iGifStride * iGifSize.iHeight / 1024,
              iGifAll ? &KGifKeptAll() : &KGifKeptAhead());
        }
    if (!iGifPlaying && kept >= Min((TInt)KGifStart, iGifCount) && (!iGifAll || iGifFrames[0].iPixels))
        {
        iGifPlaying = ETrue;
        iGifShown = -1;
        ShowGifFrame();
        }
    RequestGif();
    RequestFrame();
    }

TBool CPhotosView::GifNextReady() const
    {
    if (iGifAll)
        return iGifFrames.Count() == iGifCount && iGifFrames[(iGifShown + 1) % iGifCount].iPixels != NULL;
    return iGifFrames.Count() > 0;
    }

void CPhotosView::ShowGifFrame()
    {
    if (!iGl || !GifNextReady())
        return;
    const TInt at = iGifAll ? (iGifShown + 1) % iGifCount : 0;
    const TGifFrame frame = iGifFrames[at];
    if (!iGifTexture.Valid())
        {
        if (!iGl->CreateTexture(iGifTexture, iGifSize.iWidth, iGifSize.iHeight, TGlTexture::ERgb565, ETrue, ETrue))
            {
            Trace(_L("gif: no texture for %dx%d (graphics memory)"), iGifSize.iWidth, iGifSize.iHeight);
            iGifPlaying = EFalse;
            return;
            }
        }
    const TInt64 startUs = PerfNowUs();
    iGl->Upload(iGifTexture, 0, 0, iGifSize.iWidth, iGifSize.iHeight, frame.iPixels);
    iImageUploadUs += (TInt)(PerfNowUs() - startUs);
    iImageUploadBytes += iGifStride * iGifSize.iHeight;
    const TUint32 now = NowMs();
    const TBool first = iGifShown < 0;
    iGifShown = frame.iIndex;
    iGifShownDelay = frame.iDelayMs;

    iGifDueMs = first || now - iGifDueMs > (TUint32)frame.iDelayMs ? now + frame.iDelayMs : iGifDueMs + frame.iDelayMs;
    if (!iGifAll)
        {
        User::Free(frame.iPixels);
        iGifFrames.Remove(0);
        RequestGif();
        }
    if (first && !iGifGpuLogged)
        {
        iGifGpuLogged = ETrue;
        TraceGpu(_L("gif"));
        }
    }

void CPhotosView::StepGif(TUint32 aNow)
    {
    if (!iGifPlaying || iGifPhoto < 0)
        return;
    if (iGifShown < 0)
        {
        ShowGifFrame();
        return;
        }
    if ((TInt)(aNow - iGifDueMs) > 1000)
        iGifDueMs = aNow;
    if ((TInt)(aNow - iGifDueMs) >= 0)
        ShowGifFrame();
    }

TInt CPhotosView::GifWaitUs() const
    {
    if (!iGifPlaying || !iViewerOpen || !GifNextReady())
        return -1;
    const TInt ms = (TInt)(iGifDueMs - NowMs());
    return Max(0, ms) * 1000;
    }
