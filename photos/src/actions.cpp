#include "photosview.h"

#include <aknquerydialog.h>
#include <badesca.h>
#include <CMessageData.h>
#include <eikenv.h>
#include <e32math.h>
#include <n8photos.rsg>
#include <sendui.h>
#include <TSendingCapabilities.h>
#include <touchfeedback.h>

#include "gridlayout.h"
#include "icons.h"
#include "labels.h"
#include "library.h"
#include "moves.h"
#include "trace.h"

namespace
    {
    const TReal KPopupOmega = 28;
    const TInt KLiftPad = 4;
    const TInt KPopupRow = 48;
    const TInt KPopupWidth = 250;
    const TInt KPopupGap = 14;
    const TUint32 KPopupCard = 0x2C2C2E;
    const TUint32 KPopupPressed = 0x48484A;
    const TUint32 KRed = 0xFF453A;
    const TUint32 KWhite = 0xFFFFFF;
    const TInt KSelectEdge = 56;
    const TReal KSelectScrollSpeed = 900;
    const TInt KTopBar = 48;
    const TInt KNoFinger = -2;
    }

TIconId CPhotosView::ActionIcon(TAction aAction)
    {
    switch (aAction)
        {
        case EActFavourite: return EIconHeart;
        case EActUnfavourite: return EIconHeartFull;
        case EActShare: return EIconShare;
        case EActHide: return EIconHidden;
        case EActUnhide: return EIconEye;
        case EActTrash:
        case EActDeleteForever: return EIconTrash;
        case EActRestore: return EIconRestore;
        default: return EIconInfo;
        }
    }

TPtrC CPhotosView::ActionName(TAction aAction)
    {
    switch (aAction)
        {
        case EActFavourite: return TPtrC(_S("Favourite"));
        case EActUnfavourite: return TPtrC(_S("Unfavourite"));
        case EActShare: return TPtrC(_S("Share"));
        case EActHide: return TPtrC(_S("Hide"));
        case EActUnhide: return TPtrC(_S("Unhide"));
        case EActTrash: return TPtrC(_S("Delete"));
        case EActRestore: return TPtrC(_S("Restore"));
        case EActDeleteForever: return TPtrC(_S("Delete Permanently"));
        default: return TPtrC(_S("Info"));
        }
    }

void CPhotosView::BeginSelect()
    {
    iSelecting = ETrue;
    iSelected.Reset();
    iFrostKey = 0;
    RequestFrame();
    }

void CPhotosView::EndSelect()
    {
    if (!iSelecting)
        return;
    iSelecting = EFalse;
    iSelected.Reset();
    iFrostKey = 0;
    RequestFrame();
    }

void CPhotosView::ToggleSelected(TInt aPhoto)
    {
    TInt at;
    if (iSelected.FindInOrder(aPhoto, at) == KErrNone)
        iSelected.Remove(at);
    else
        iSelected.InsertInOrder(aPhoto);
    MTouchFeedback* feedback = MTouchFeedback::Instance();
    if (feedback)
        feedback->InstantFeedback(ETouchFeedbackCheckbox);
    }

void CPhotosView::SelectRangeTo(const TPoint& aPos)
    {
    const CGridLayout* grid = ActiveGrid();
    if (!grid || iSelectAnchor < 0 || iSelectAnchor >= grid->Count())
        return;
    const TInt scroll = (TInt)GridScroll();
    const TInt item = grid->NearestItem(TPoint(aPos.iX, aPos.iY + scroll));
    if (item < 0 || item == iSelectLast)
        return;
    iSelectLast = item;
    iSelected.Reset();
    for (TInt i = 0; i < iSelectBase.Count(); i++)
        iSelected.Append(iSelectBase[i]);
    const TInt from = Min(iSelectAnchor, item), to = Max(iSelectAnchor, item);
    for (TInt i = from; i <= to; i++)
        {
        const TInt photo = grid->PhotoAt(i);
        TInt at;
        const TBool chosen = iSelected.FindInOrder(photo, at) == KErrNone;
        if (iSelectTarget && !chosen)
            iSelected.InsertInOrder(photo);
        else if (!iSelectTarget && chosen)
            iSelected.Remove(at);
        }
    MTouchFeedback* feedback = MTouchFeedback::Instance();
    if (feedback)
        feedback->InstantFeedback(ETouchFeedbackSensitive);
    RequestFrame();
    }

TBool CPhotosView::StepSelectScroll(TInt aMs)
    {
    const CGridLayout* grid = ActiveGrid();
    if (iAxis != EAxisSelect || !grid)
        return EFalse;
    const TInt y = iPointer[0].iY, h = Rect().Height();
    const TInt top = iTopInset + Scale(KTopBar) + Scale(KSelectEdge), bottom = h - Scale(56) - Scale(KSelectEdge);
    TReal speed = 0;
    if (y < top)
        speed = -KSelectScrollSpeed * Min(1.0, (TReal)(top - y) / Scale(KSelectEdge));
    else if (y > bottom)
        speed = KSelectScrollSpeed * Min(1.0, (TReal)(y - bottom) / Scale(KSelectEdge));
    if (speed == 0)
        return EFalse;
    TReal& scroll = ActiveScroll();
    const TReal before = scroll;
    scroll = Clamp(scroll + speed * aMs / 1000, 0, MaxScroll(*grid));
    if (scroll != before)
        SelectRangeTo(iPointer[0]);
    return ETrue;
    }

TBool CPhotosView::IsSelected(TInt aPhoto) const
    {
    return iSelected.FindInOrder(aPhoto) >= 0;
    }

TInt CPhotosView::SelectActions(TAction* aActions, TIconId* aIcons) const
    {
    TInt n = 0;
    const TInt kind = ShownKind();
    if (kind == CAlbum::ERecentlyDeleted)
        {
        aActions[n++] = EActRestore;
        aActions[n++] = EActDeleteForever;
        }
    else
        {
        aActions[n++] = EActShare;
        if (kind != CAlbum::EHidden)
            {

            TBool all = iSelected.Count() > 0;
            for (TInt i = 0; i < iSelected.Count() && all; i++)
                all = (iLibrary->Photo(iSelected[i]).iFlags & TPhoto::EFavourite) != 0;
            aActions[n++] = all ? EActUnfavourite : EActFavourite;
            aActions[n++] = EActHide;
            }
        else
            aActions[n++] = EActUnhide;
        aActions[n++] = EActTrash;
        }
    for (TInt i = 0; i < n; i++)
        aIcons[i] = ActionIcon(aActions[i]);
    return n;
    }

void CPhotosView::SelectActionL(TInt aSlot)
    {
    TAction actions[KMaxActions];
    TIconId icons[KMaxActions];
    const TInt n = SelectActions(actions, icons);
    if (aSlot < 0 || aSlot >= n || iSelected.Count() == 0)
        return;
    RArray<TInt> photos;
    CleanupClosePushL(photos);
    for (TInt i = 0; i < iSelected.Count(); i++)
        photos.AppendL(iSelected[i]);
    const TAction action = actions[aSlot];
    TBuf<64> prompt;
    if (action == EActTrash || action == EActDeleteForever)
        {
        prompt.Format(action == EActTrash ? _L("Delete %d items?") : _L("Delete %d items permanently?"),
                      photos.Count());
        if (photos.Count() == 1)
            prompt.Copy(action == EActTrash ? _L("Delete this item?") : _L("Delete this item permanently?"));
        if (!ConfirmL(prompt))
            {
            CleanupStack::PopAndDestroy(&photos);
            return;
            }
        }
    if (action == EActShare)
        SharePhotosL(photos);
    else if (action == EActFavourite || action == EActUnfavourite)
        ChangeFavouritesL(photos, action == EActFavourite);
    else
        ActL(action, photos, KNullDesC);
    CleanupStack::PopAndDestroy(&photos);
    if (action != EActShare)
        EndSelect();
    }

void CPhotosView::OpenPopup(TInt aItem, const TRect& aCell)
    {
    const CGridLayout* grid = ActiveGrid();
    if (!grid)
        return;
    iPopupOpen = ETrue;
    iPopupItem = aItem;
    iPopupPhoto = grid->PhotoAt(aItem);
    iPopupCell = TBox(aCell);

    iPopupPressed = -1;
    iPopupHeld = ETrue;
    iAxis = EAxisNone;
    iFrostKey = 0;
    RequestFrame();
    }

void CPhotosView::ClosePopup()
    {
    iPopupOpen = EFalse;
    iPopupPressed = KNoFinger;
    iFrostKey = 0;
    RequestFrame();
    }

TInt CPhotosView::PopupActions(TAction* aActions) const
    {
    TInt n = 0;
    const TInt flags = iLibrary->Photo(iPopupPhoto).iFlags;
    if (flags & TPhoto::ETrashed)
        {
        aActions[n++] = EActRestore;
        aActions[n++] = EActInfo;
        aActions[n++] = EActDeleteForever;
        return n;
        }
    if (!(flags & TPhoto::EHidden))
        aActions[n++] = flags & TPhoto::EFavourite ? EActUnfavourite : EActFavourite;
    aActions[n++] = EActShare;
    aActions[n++] = flags & TPhoto::EHidden ? EActUnhide : EActHide;
    aActions[n++] = EActInfo;
    aActions[n++] = EActTrash;
    return n;
    }

void CPhotosView::PopupLayout(TBox& aPreview, TRect& aCard) const
    {
    TAction actions[KMaxActions];
    const TInt rows = PopupActions(actions);
    const TInt w = Rect().Width(), h = Rect().Height();
    const TInt margin = Scale(12), gap = Scale(KPopupGap);
    const TInt cardW = Min(Scale(KPopupWidth), w - 2 * margin), cardH = rows * Scale(KPopupRow);
    const TInt top = iTopInset + margin, bottom = h - margin;
    const TInt side = Min(Scale(200), Min(w * 55 / 100, (bottom - top - gap - cardH) * 9 / 10));
    const TInt cellX = (TInt)((iPopupCell.iX0 + iPopupCell.iX1) / 2), cellY = (TInt)((iPopupCell.iY0 + iPopupCell.iY1) / 2);
    const TInt x = Max(margin, Min(w - margin - side, cellX - side / 2));
    TInt y = Max(top, Min(bottom - side, cellY - side / 2));
    TInt cardY;
    if (y + side + gap + cardH <= bottom)
        cardY = y + side + gap;
    else if (y - gap - cardH >= top)
        cardY = y - gap - cardH;
    else
        {

        y = Max(top, bottom - cardH - gap - side);
        cardY = y + side + gap;
        }
    aPreview = TBox(x, y, x + side, y + side);
    TInt cardX = x + side / 2 < w / 2 ? x : x + side - cardW;
    cardX = Max(margin, Min(w - margin - cardW, cardX));
    aCard = TRect(cardX, cardY, cardX + cardW, cardY + cardH);
    }

TBool CPhotosView::StepPopup(TInt aMs)
    {
    if (!iPopupOpen && iPopupP <= 0)
        return EFalse;
    const TBool moving = SpringTo(iPopupP, iPopupPVel, iPopupOpen ? 1 : 0, KPopupOmega, aMs, 0.002);
    if (!iPopupOpen && !moving)
        {
        iPopupP = 0;
        iPopupItem = -1;
        iFrostKey = 0;
        }
    return moving || !iPopupOpen;
    }

void CPhotosView::DrawPopup()
    {
    if (!iIcons)
        return;
    const TReal p = Clamp(iPopupP, 0, 1.1);
    TBox target;
    TRect card;
    PopupLayout(target, card);
    const TBox box(Lerp(iPopupCell.iX0, target.iX0, p), Lerp(iPopupCell.iY0, target.iY0, p),
                   Lerp(iPopupCell.iX1, target.iX1, p), Lerp(iPopupCell.iY1, target.iY1, p));

    const TRect pic((TInt)box.iX0, (TInt)box.iY0, (TInt)box.iX1, (TInt)box.iY1);
    const TInt corner = iIcons->Size(EIconCorner).iWidth, pad = Scale(KLiftPad);
    TRect frame(pic);
    frame.Grow(pad, pad);
    TRect shadow(frame);
    shadow.Grow(Scale(5), Scale(5));
    shadow.Move(0, Scale(3));
    FillRoundRect(shadow, corner + pad + Scale(5), 0x000000, 0.35 * Min(1.0, p));
    FillRoundRect(frame, corner + pad, KPopupCard, 1);
    TInt atlas;
    TReal uv[4];
    if (iThumbs->Get(iPopupPhoto, CThumbCache::EVisible, atlas, uv))
        {
        iGl->AddQuad(box.iX0, box.iY0, box.iX1, box.iY1, uv[0], uv[1], uv[2], uv[3]);
        iGl->DrawBatch(iThumbs->Atlas(atlas), KWhite, 1);
        }
    else
        iGl->FillRect(pic, 0x1C1C1E, 1);
    iIcons->AddCorner(*iGl, TRect(pic.iTl.iX, pic.iTl.iY, pic.iTl.iX + corner, pic.iTl.iY + corner), 0);
    iIcons->AddCorner(*iGl, TRect(pic.iBr.iX - corner, pic.iTl.iY, pic.iBr.iX, pic.iTl.iY + corner), 1);
    iIcons->AddCorner(*iGl, TRect(pic.iTl.iX, pic.iBr.iY - corner, pic.iTl.iX + corner, pic.iBr.iY), 2);
    iIcons->AddCorner(*iGl, TRect(pic.iBr.iX - corner, pic.iBr.iY - corner, pic.iBr.iX, pic.iBr.iY), 3);
    iGl->DrawBatch(iIcons->Texture(EIconCorner), KPopupCard, 1);

    const TReal alpha = Clamp((p - 0.3) / 0.7, 0, 1);
    if (alpha <= 0)
        return;
    TAction actions[KMaxActions];
    const TInt rows = PopupActions(actions);
    FillRoundRect(card, Scale(12), KPopupCard, alpha);
    const TInt rowH = Scale(KPopupRow);
    if (iPopupPressed >= 0 && iPopupPressed < rows)
        {

        TRect row(card.iTl.iX, card.iTl.iY + iPopupPressed * rowH, card.iBr.iX, card.iTl.iY + (iPopupPressed + 1) * rowH);
        FillRoundRect(row, iPopupPressed == 0 || iPopupPressed == rows - 1 ? Scale(12) : 0, KPopupPressed, alpha);
        }
    for (TInt r = 1; r < rows; r++)
        iGl->FillRect(TRect(card.iTl.iX, card.iTl.iY + r * rowH, card.iBr.iX, card.iTl.iY + r * rowH + 1), KWhite,
                      0.12 * alpha);
    for (TInt r = 0; r < rows; r++)
        {
        const TBool danger = actions[r] == EActTrash || actions[r] == EActDeleteForever;
        const TUint32 colour = danger ? KRed : KWhite;
        const TInt cy = card.iTl.iY + r * rowH + rowH / 2;
        iLabels->Draw(*iGl, ActionName(actions[r]), EFontMenu, card.iTl.iX + Scale(16), cy + Scale(7),
                      ELabelLeft, colour, alpha);
        iIcons->Draw(*iGl, ActionIcon(actions[r]), card.iBr.iX - Scale(26), cy, colour, alpha);
        }
    }

void CPhotosView::PopupPointerL(const TPointerEvent& aEvent)
    {
    TBox preview;
    TRect card;
    PopupLayout(preview, card);
    TAction actions[KMaxActions];
    const TInt rows = PopupActions(actions);
    const TPoint pos = aEvent.iPosition;
    const TInt row = card.Contains(pos) ? (pos.iY - card.iTl.iY) / Scale(KPopupRow) : -1;
    switch (aEvent.iType)
        {
        case TPointerEvent::EButton1Down:
            iPopupPressed = row >= 0 && row < rows ? row : -1;
            if (iPopupPressed >= 0)
                Feedback();
            break;
        case TPointerEvent::EDrag:
            if (iPopupPressed != KNoFinger)
                {
                const TInt now = row >= 0 && row < rows ? row : -1;
                if (now != iPopupPressed && now >= 0)
                    {
                    MTouchFeedback* feedback = MTouchFeedback::Instance();
                    if (feedback)
                        feedback->InstantFeedback(ETouchFeedbackSensitive);
                    }
                iPopupPressed = now;
                }
            break;
        case TPointerEvent::EButton1Up:
            {
            const TInt pressed = iPopupPressed;
            const TBool held = iPopupHeld;
            iPopupHeld = EFalse;
            if (pressed == KNoFinger)
                break;
            iPopupPressed = KNoFinger;
            if (pressed >= 0 && pressed == row)
                {
                ClosePopup();
                PopupActionL(actions[row]);
                }
            else if (!held && !card.Contains(pos))
                ClosePopup();
            break;
            }
        default:
            break;
        }
    }

void CPhotosView::PopupActionL(TAction aAction)
    {
    RArray<TInt> photos;
    CleanupClosePushL(photos);
    photos.AppendL(iPopupPhoto);
    switch (aAction)
        {
        case EActFavourite:
        case EActUnfavourite:
            ChangeFavouritesL(photos, aAction == EActFavourite);
            break;
        case EActShare:
            SharePhotosL(photos);
            break;
        case EActInfo:
            {
            TRect cell;
            const CGridLayout* grid = ActiveGrid();
            if (grid && iPopupItem >= 0)
                {
                cell = grid->ItemRect(iPopupItem);
                cell.Move(0, -(TInt)GridScroll());
                const TInt item = iPopupItem;
                iPopupP = 0;
                iPopupItem = -1;
                OpenViewer(item, cell);
                ToggleInfo();
                }
            break;
            }
        case EActTrash:
        case EActDeleteForever:
            if (ConfirmL(aAction == EActTrash ? _L("Delete this item?") : _L("Delete this item permanently?")))
                ActL(aAction, photos, KNullDesC);
            break;
        default:
            ActL(aAction, photos, KNullDesC);
            break;
        }
    CleanupStack::PopAndDestroy(&photos);
    }

TBool CPhotosView::ConfirmL(const TDesC& aPrompt)
    {
    CAknQueryDialog* ask = CAknQueryDialog::NewL();
    ask->PrepareLC(R_PHOTOS_DELETE_QUERY);
    ask->SetPromptL(aPrompt);
    return ask->RunLD() != 0;
    }

void CPhotosView::ActL(TAction aAction, const RArray<TInt>& aPhotos, const TDesC& aViewerPath)
    {
    RFs& fs = CEikonEnv::Static()->FsSession();
    TTime now;
    now.UniversalTime();
    RArray<TInt> changed;
    CleanupClosePushL(changed);
    CDesC16ArrayFlat* paths = new (ELeave) CDesC16ArrayFlat(8);
    CleanupStack::PushL(paths);
    TInt failed = 0;
    TBool favouritesChanged = EFalse;
    for (TInt i = 0; i < aPhotos.Count(); i++)
        {
        const TFileName from(*iLibrary->Photo(aPhotos[i]).iPath);
        const TInt known = iMoves->Find(from);
        TFileName folder, to;
        TInt err = KErrNone;
        switch (aAction)
            {
            case EActHide:
                CMoves::HiddenFolder(from, folder);
                err = CMoves::MoveInto(fs, from, folder, to);
                if (err == KErrNone)
                    iMoves->SetL(to, from, now);
                break;
            case EActUnhide:
            case EActRestore:
                {

                TParsePtrC origin(known >= 0 ? iMoves->Origin(known) : KNullDesC());
                if (known >= 0 && origin.Drive().CompareF(TParsePtrC(from).Drive()) == 0)
                    folder.Copy(origin.DriveAndPath());
                else
                    CMoves::ImagesFolder(from, folder);
                err = CMoves::MoveInto(fs, from, folder, to);
                if (err == KErrNone)
                    iMoves->Remove(from);
                break;
                }
            case EActTrash:
                CMoves::TrashFolder(from, folder);
                err = CMoves::MoveInto(fs, from, folder, to);
                if (err == KErrNone)
                    {

                    iMoves->Remove(from);
                    iMoves->SetL(to, from, now);
                    }
                break;
            case EActDeleteForever:
                err = fs.Delete(from);
                if (err == KErrNone)
                    iMoves->Remove(from);
                break;
            default:
                err = KErrNotSupported;
                break;
            }
        Trace(_L("act %d: %S -> %S err %d"), aAction, &from, &to, err);
        if (err != KErrNone)
            {
            failed++;
            continue;
            }
        TInt at;
        if (iFavourites->FindIsq(from, at, ECmpFolded) == 0)
            {
            iFavourites->Delete(at);
            if (to.Length())
                iFavourites->InsertIsqAllowDuplicatesL(to, ECmpFolded);
            favouritesChanged = ETrue;
            }
        changed.AppendL(aPhotos[i]);
        paths->AppendL(to);
        }
    TRAPD(err, iMoves->SaveL(fs));
    if (err != KErrNone)
        Trace(_L("moves: save err %d"), err);
    if (favouritesChanged)
        TRAP_IGNORE(SaveFavouritesL());
    if (failed)
        CEikonEnv::Static()->InfoMsg(failed == 1 ? _L("One item couldn't be moved") : _L("Some items couldn't be moved"));
    if (changed.Count())
        {
        CLibrary* library = iLibrary->CopyL(changed, *paths);
        CleanupStack::PushL(library);
        library->SetFavouritesL(*iFavourites);
        CleanupStack::Pop(library);
        iInfoPhoto = -1;
        ReplaceLibrary(library, aViewerPath);
        }
    CleanupStack::PopAndDestroy(2, &changed);
    }

void CPhotosView::ChangeFavouritesL(const RArray<TInt>& aPhotos, TBool aAdd)
    {
    for (TInt i = 0; i < aPhotos.Count(); i++)
        {
        TPhoto& photo = iLibrary->PhotoForChange(aPhotos[i]);
        TInt at;
        const TBool was = iFavourites->FindIsq(*photo.iPath, at, ECmpFolded) == 0;
        if (was && !aAdd)
            iFavourites->Delete(at);
        else if (!was && aAdd)
            iFavourites->InsertIsqL(*photo.iPath, ECmpFolded);
        if (aAdd)
            photo.iFlags |= TPhoto::EFavourite;
        else
            photo.iFlags &= ~TPhoto::EFavourite;
        }
    SaveFavouritesL();
    if (iViewerOpen)
        iAlbumsDirty = ETrue;
    else
        RegroupAlbums();
    RequestFrame();
    }

void CPhotosView::SharePhotosL(const RArray<TInt>& aPhotos)
    {
    if (!iSendUi)
        iSendUi = CSendUi::NewL();
    CMessageData* message = CMessageData::NewLC();
    for (TInt i = 0; i < aPhotos.Count(); i++)
        message->AppendAttachmentL(*iLibrary->Photo(aPhotos[i]).iPath);
    iSendUi->ShowQueryAndSendL(message, TSendingCapabilities(0, 0, TSendingCapabilities::ESupportsAttachments), NULL,
                               KNullUid, EFalse);
    CleanupStack::PopAndDestroy(message);
    }
