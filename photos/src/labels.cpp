#include "labels.h"

#include <bitdev.h>
#include <bitstd.h>
#include <f32file.h>
#include <fbs.h>
#include <openfont.h>

#include "trace.h"

namespace
    {

    _LIT(KFontDir, "\\resource\\apps\\n8photos_fonts\\");
    const TText* const KFontFiles[] = { _S("NPHeadlineBold.ttf"), _S("N9TextBd.ttf"), _S("N9TextTBd.ttf"),
                                        _S("N9TextTRg.ttf"), _S("N9TextRg.ttf") };

    const TText* const KOldCopies[] = { _S("NPTextBold.ttf"), _S("NPTextMedium.ttf"), _S("NPTextRegular.ttf"),
                                        _S("N9TextMEMd.ttf") };
    _LIT(KHeadline, "Nokia Pure Headline");
    _LIT(KText, "Nokia Pure Text");
    _LIT(KTabular, "Nokia Pure Text T");
    _LIT(KInstead, " instead");
    _LIT(KShared, ", or a copy registered earlier");
    const TText* const KFaceNames[] = { _S("headline bold"), _S("text bold"), _S("tabular bold"),
                                        _S("tabular regular"), _S("text regular") };
    const TText* const KStyleNames[] = { _S("title"), _S("year"), _S("header"), _S("header digits"), _S("body"),
                                         _S("small"), _S("caption"), _S("caption digits"), _S("button"), _S("date"),
                                         _S("time"), _S("menu"), _S("scrub") };

    _LIT(KFontCopyDir, "C:\\Data\\photos\\fonts\\");

    const TInt KAtlasSize = 1024;
    const TInt KAtlasStart = 512;
    const TInt KAddChunk = 48;
    const TInt KPreloadChunk = 8;
    const TFontId KPreloadFonts[] = { EFontTime, EFontCaptionDigits, EFontHeader };

    const TInt KMaxLabels = 96;
    const TInt KLabelBytes = 384 * 1024;

    _LIT(KCalibration, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

    TInt Round(TReal aValue) { return aValue >= 0 ? (TInt)(aValue + 0.5) : -(TInt)(-aValue + 0.5); }
    }

CLabels* CLabels::NewL(RFs& aFs, const TDesC& aFallbackTypeface, TInt aScale, MLabelsObserver& aObserver)
    {
    CLabels* self = new (ELeave) CLabels(aFs, aFallbackTypeface, aScale, aObserver);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop(self);
    return self;
    }

CLabels::CLabels(RFs& aFs, const TDesC& aFallbackTypeface, TInt aScale, MLabelsObserver& aObserver)
    : iFs(aFs), iObserver(aObserver), iFallbackTypeface(aFallbackTypeface.Left(KMaxTypefaceNameLength)),
      iScale(aScale), iNextId(1)
    {
    for (TInt f = 0; f < EFaceCount; f++)
        iFaceFile[f] = -1;
    }

void CLabels::ConstructL()
    {
    iStartUs = PerfNowUs();
    Mem::Fill(iDirect, sizeof(iDirect), 0xFF);
    iPixels = static_cast<TUint8*>(User::AllocZL(KAtlasSize * KAtlasSize));
    iDirtyTop = KAtlasSize;
    iBitmap = new (ELeave) CFbsBitmap;
    User::LeaveIfError(iBitmap->Create(TSize(64, 32), EColor16MU));
    iDevice = CFbsBitmapDevice::NewL(iBitmap);
    AddFontFiles();
    ResolveFaces();
    const TInt64 metricsUs = PerfNowUs();
    for (TInt f = 0; f < EFaceCount; f++)
        ReadMetrics(f);
    Trace(_L("fonts: metrics read in %d ms"), (TInt)((PerfNowUs() - metricsUs) / 1000));
    const TInt s = iScale;

    MakeFontL(EFontTitle, EFaceHeadline, 32 * s / 256);
    MakeFontL(EFontYear, EFaceHeadline, 24 * s / 256);
    MakeFontL(EFontHeader, EFaceBold, 20 * s / 256);
    MakeFontL(EFontHeaderDigits, EFaceTabularBold, 20 * s / 256);
    MakeFontL(EFontBody, EFaceBold, 20 * s / 256);
    MakeFontL(EFontSmall, EFaceBold, 16 * s / 256);
    MakeFontL(EFontCaption, EFaceBold, 14 * s / 256);
    MakeFontL(EFontCaptionDigits, EFaceTabularBold, 14 * s / 256);
    MakeFontL(EFontButton, EFaceBold, 16 * s / 256);

    MakeFontL(EFontDate, EFaceBold, 18 * s / 256);
    MakeFontL(EFontTime, EFaceTabular, 14 * s / 256);

    MakeFontL(EFontMenu, EFaceRegular, 17 * s / 256);
    MakeFontL(EFontScrub, EFaceTabularBold, 30 * s / 256);
    for (TInt i = 0; i < EFontCount; i++)
        iPad[i] = Max(2, iFonts[i]->HeightInPixels() / 6);
    const TDesC* files[KMaxFontFiles];
    for (TInt i = 0; i < iFileCount; i++)
        files[i] = iFiles[i];
    iWorker = CLabelWorker::NewL(*this, files, iFileCount);
    Trace(_L("fonts: ready in %d ms"), (TInt)((PerfNowUs() - iStartUs) / 1000));
    }

CLabels::~CLabels()
    {
    delete iWorker;
    delete iPreload;
    for (TInt i = 0; i < iLabels.Count(); i++)
        delete iLabels[i].iText;
    iLabels.Close();
    iGlyphs.Close();
    iOthers.Close();
    User::Free(iPixels);
    if (iDevice)
        {
        for (TInt i = 0; i < EFontCount; i++)
            if (iFonts[i])
                iDevice->ReleaseFont(iFonts[i]);
        for (TInt i = 0; i < iFontIdCount; i++)
            iDevice->RemoveFile(iFontIds[i]);
        }
    for (TInt f = 0; f < EFaceCount; f++)
        delete iFaceMetrics[f];
    for (TInt i = 0; i < iFileCount; i++)
        delete iFiles[i];
    delete iDevice;
    delete iBitmap;
    }

TBool CLabels::CopyFontL(const TDesC& aSource, const TDesC& aTarget)
    {
    TEntry source, target;
    User::LeaveIfError(iFs.Entry(aSource, source));
    if (iFs.Entry(aTarget, target) == KErrNone && target.iSize == source.iSize)
        return EFalse;
    TInt err = iFs.MkDirAll(aTarget);
    if (err != KErrNone && err != KErrAlreadyExists)
        User::Leave(err);
    RFile in, out;
    User::LeaveIfError(in.Open(iFs, aSource, EFileRead | EFileShareReadersOnly));
    CleanupClosePushL(in);
    User::LeaveIfError(out.Replace(iFs, aTarget, EFileWrite));
    CleanupClosePushL(out);
    HBufC8* buffer = HBufC8::NewLC(32 * 1024);
    TPtr8 chunk = buffer->Des();
    for (;;)
        {
        User::LeaveIfError(in.Read(chunk));
        if (chunk.Length() == 0)
            break;
        User::LeaveIfError(out.Write(chunk));
        }
    CleanupStack::PopAndDestroy(3, &in);
    return ETrue;
    }

void CLabels::AddFontFiles()
    {
    for (TUint i = 0; i < sizeof(KOldCopies) / sizeof(KOldCopies[0]); i++)
        {
        TFileName old(KFontCopyDir);
        old.Append(TPtrC(KOldCopies[i]));
        iFs.Delete(old);
        }
    TFindFile finder(iFs);
    for (TUint i = 0; i < sizeof(KFontFiles) / sizeof(KFontFiles[0]) && iFileCount < KMaxFontFiles; i++)
        {
        TFileName name(KFontDir);
        name.Append(TPtrC(KFontFiles[i]));
        TInt found = finder.FindByDir(name, KNullDesC);
        if (found != KErrNone)
            {
            Trace(_L("fonts: %S not installed, err %d"), &name, found);
            continue;
            }
        TFileName file(KFontCopyDir);
        file.Append(TPtrC(KFontFiles[i]));
        TBool copied = EFalse;
        TRAPD(copyErr, copied = CopyFontL(finder.File(), file));
        TEntry existing;
        if (copyErr != KErrNone && iFs.Entry(file, existing) != KErrNone)
            file.Copy(finder.File());
        TInt id = 0;
        const TInt64 addUs = PerfNowUs();
        TInt added = iDevice->AddFile(file, id);
        Trace(_L("fonts: add %S (copied %d, copy err %d) err %d in %d us"), &file, copied, copyErr, added,
              (TInt)(PerfNowUs() - addUs));
        if (added != KErrNone && added != KErrAlreadyExists)
            continue;
        if (added == KErrNone)
            iFontIds[iFontIdCount++] = id;
        else
            iFontsShared = ETrue;
        iFiles[iFileCount] = new TFileName(file);
        if (!iFiles[iFileCount])
            continue;
        iFaceFile[i] = iFileCount++;
        }
    }

void CLabels::ResolveFaces()
    {
    TTypefaceSupport support;
    for (TInt i = 0; i < iDevice->NumTypefaces(); i++)
        {
        iDevice->TypefaceSupport(support, i);
        TPtrC name(support.iTypeface.Name());
        if (name.Left(10) != _L("Nokia Pure"))
            continue;
        Trace(_L("fonts: typeface %d %S"), i, &name);
        if (name == KHeadline)
            iFaceName[EFaceHeadline].Copy(name);
        else if (name == KText)
            {
            iFaceName[EFaceBold].Copy(name);
            iFaceName[EFaceRegular].Copy(name);
            }
        else if (name == KTabular)
            {
            iFaceName[EFaceTabularBold].Copy(name);
            iFaceName[EFaceTabular].Copy(name);
            }
        }
    }

void CLabels::ReadMetrics(TInt aFace)
    {
    if (iFaceFile[aFace] < 0 || iFaceName[aFace].Length() == 0)
        return;
    RFile file;
    if (file.Open(iFs, *iFiles[iFaceFile[aFace]], EFileRead | EFileShareReadersOnly) != KErrNone)
        return;
    TBuf8<12 + 16 * 32> directory;
    const TUint32 tags[5] = { TFontMetrics::Tag('h', 'e', 'a', 'd'), TFontMetrics::Tag('h', 'h', 'e', 'a'),
                              TFontMetrics::Tag('h', 'm', 't', 'x'), TFontMetrics::Tag('c', 'm', 'a', 'p'),
                              TFontMetrics::Tag('k', 'e', 'r', 'n') };
    HBufC8* tables[5] = { NULL, NULL, NULL, NULL, NULL };
    if (file.Read(0, directory) == KErrNone)
        for (TInt t = 0; t < 5; t++)
            {
            TInt offset = 0, size = 0;
            if (!TFontMetrics::FindTable(directory.Ptr(), directory.Length(), tags[t], offset, size) ||
                size > 256 * 1024)
                continue;
            tables[t] = HBufC8::New(size);
            if (!tables[t])
                continue;
            TPtr8 data = tables[t]->Des();
            if (file.Read(offset, data, size) != KErrNone || data.Length() != size)
                {
                delete tables[t];
                tables[t] = NULL;
                }
            }
    file.Close();
    TFontMetrics* metrics = new TFontMetrics;
    const TUint8* at[5];
    TInt sizes[5];
    for (TInt t = 0; t < 5; t++)
        {
        at[t] = tables[t] ? tables[t]->Ptr() : NULL;
        sizes[t] = tables[t] ? tables[t]->Length() : 0;
        }
    if (metrics &&
        metrics->Parse(at[0], sizes[0], at[1], sizes[1], at[2], sizes[2], at[3], sizes[3], at[4], sizes[4]))
        iFaceMetrics[aFace] = metrics;
    else
        delete metrics;
    for (TInt t = 0; t < 5; t++)
        delete tables[t];
    TPtrC face(KFaceNames[aFace]);
    Trace(_L("fonts: %S metrics %d, %d kerning pairs"), &face, iFaceMetrics[aFace] != NULL,
          iFaceMetrics[aFace] ? iFaceMetrics[aFace]->PairCount() : 0);
    }

void CLabels::MakeFontL(TFontId aFont, TFace aFace, TInt aPixels)
    {
    TFace face = aFace;
    if (iFaceName[face].Length() == 0 && face == EFaceTabularBold)
        face = EFaceBold;
    const TBool bold = face == EFaceHeadline || face == EFaceBold || face == EFaceTabularBold;

    TPtrC name = iFaceName[face].Length() ? TPtrC(iFaceName[face]) : TPtrC(iFallbackTypeface);
    name.Set(name.Left(KMaxTypefaceNameLength));
    TFontSpec spec(name, aPixels);
    spec.iFontStyle.SetBitmapType(EAntiAliasedGlyphBitmap);
    if (bold)
        spec.iFontStyle.SetStrokeWeight(EStrokeWeightBold);
    CFont* font = NULL;
    TInt err = iDevice->GetNearestFontToDesignHeightInPixels(font, spec);
    User::LeaveIfError(err);

    TFontSpec actual = font->FontSpecInTwips();
    TBuf<KMaxTypefaceNameLength> got;
    got.Copy(actual.iTypeface.Name());
    const TBool gotBold = actual.iFontStyle.StrokeWeight() == EStrokeWeightBold;
    const TBool synthetic = actual.iFontStyle.IsEffectOn(FontEffect::EAlgorithmicBold);

    TBuf<TOpenFontFaceAttrib::ENameLength> full;
    TOpenFontFaceAttrib attrib;
    if (static_cast<CFbsFont*>(font)->GetFaceAttrib(attrib))
        full.Copy(attrib.FullName());
    iFonts[aFont] = font;
    iSpecs[aFont] = spec;
    iFontFace[aFont] = face;

    const TFontMetrics* metrics = iFaceMetrics[face];
    iScaleK[aFont] = 0;
    if (metrics && got == name)
        {
        TInt pixels = 0, units = 0;
        for (TInt i = 0; i < KCalibration().Length(); i++)
            {
            const TInt advance = metrics->Advance(KCalibration()[i]);
            if (advance > 0)
                {
                units += advance;
                pixels += font->CharWidthInPixels(KCalibration()[i]);
                }
            }
        if (units > 0)
            iScaleK[aFont] = (TReal)pixels / units;
        }
    TFileName file;
    if (iFaceFile[face] >= 0)
        file.Copy(*iFiles[iFaceFile[face]]);
    TPtrC style(KStyleNames[aFont]);
    Trace(_L("fonts: %S uses %S [%S]%S (%S%S) %d px, height %d, bold %d, em %d/1000 px"), &style, &got, &full,
          face != aFace ? &KInstead() : &KNullDesC(), &file, iFontsShared ? &KShared() : &KNullDesC(), aPixels,
          font->HeightInPixels(), gotBold, (TInt)(iScaleK[aFont] * 1000 * 1000));
    if ((bold && full.Find(_L("Bold")) < 0) || synthetic)
        Trace(_L("fonts: warning: %S asked bold %d got %S [%S], algorithmic bold %d"), &style, bold, &got, &full,
              synthetic);
    }

TInt CLabels::Layout(const TDesC& aText, TFontId aFont, TInt16* aPen) const
    {
    const CFont* font = iFonts[aFont];
    const TFontMetrics* metrics = iFaceMetrics[iFontFace[aFont]];
    const TReal k = iScaleK[aFont];
    TReal pen = 0;
    for (TInt i = 0; i < aText.Length(); i++)
        {
        const TUint c = aText[i];
        if (aPen)
            aPen[i] = (TInt16)Round(pen);
        const TInt units = k > 0 ? metrics->Advance(c) : -1;
        pen += units >= 0 ? units * k : font->CharWidthInPixels(c);
        if (k > 0 && i + 1 < aText.Length())
            pen += metrics->Kern(c, aText[i + 1]) * k;
        }
    return Round(pen);
    }

TInt CLabels::TextWidth(const TDesC& aText, TFontId aFont) const
    {
    return Layout(aText, aFont, NULL);
    }

void CLabels::Fit(TDes& aText, TFontId aFont, TInt aWidth) const
    {
    if (TextWidth(aText, aFont) <= aWidth)
        return;
    _LIT(KDots, "...");
    while (aText.Length() > 1)
        {
        aText.SetLength(aText.Length() - 1);
        TBuf<160> trial(aText.Left(150));
        trial.Append(KDots);
        if (TextWidth(trial, aFont) <= aWidth)
            {
            if (aText.Length() + 3 <= aText.MaxLength())
                aText.Append(KDots);
            return;
            }
        }
    }

TInt CLabels::Find(TFontId aFont, TUint aChar) const
    {
    if (aChar >= KFirstDirect && aChar < KFirstDirect + KDirect)
        return iDirect[aFont][aChar - KFirstDirect];
    for (TInt i = 0; i < iOthers.Count(); i++)
        if (iOthers[i].iChar == aChar && iOthers[i].iFont == aFont)
            return iOthers[i].iGlyph;
    return -1;
    }

void CLabels::EnsureScratchL(TSize aSize)
    {
    const TSize have = iBitmap->SizeInPixels();
    if (have.iWidth >= aSize.iWidth && have.iHeight >= aSize.iHeight)
        return;
    const TSize size(Max(have.iWidth, aSize.iWidth), Max(have.iHeight, aSize.iHeight));
    User::LeaveIfError(iBitmap->Resize(size));
    User::LeaveIfError(iDevice->Resize(size));
    }

void CLabels::AddL(TFontId aFont, const TDesC& aChars)
    {
    if (iFull)
        return;
    TBuf<KAddChunk> todo;
    for (TInt i = 0; i < aChars.Length() && todo.Length() < KAddChunk; i++)
        {
        const TChar c = aChars[i];
        if (c > KFirstDirect && Find(aFont, c) < 0 && todo.Locate(c) == KErrNotFound)
            todo.Append(c);
        }
    if (todo.Length() == 0)
        return;
    const TInt64 startUs = PerfNowUs();
    CFont* font = iFonts[aFont];
    const TInt pad = iPad[aFont];
    const TInt ascent = font->FontMaxAscent();
    const TInt cellH = ascent + font->FontMaxDescent() + 2 * pad;
    TInt cellW[KAddChunk];
    TInt total = 0;
    for (TInt i = 0; i < todo.Length(); i++)
        {
        cellW[i] = Max(0, font->CharWidthInPixels(todo[i])) + 2 * pad;
        total += cellW[i];
        }
    EnsureScratchL(TSize(total, cellH));
    CFbsBitGc* gc = NULL;
    User::LeaveIfError(iDevice->CreateContext(gc));
    gc->SetBrushStyle(CGraphicsContext::ESolidBrush);
    gc->SetBrushColor(KRgbBlack);
    gc->Clear(TRect(0, 0, total, cellH));
    gc->SetBrushStyle(CGraphicsContext::ENullBrush);
    gc->UseFont(font);
    gc->SetPenColor(KRgbWhite);
    TInt x = 0;
    for (TInt i = 0; i < todo.Length(); i++)
        {
        gc->DrawText(todo.Mid(i, 1), TPoint(x + pad, pad + ascent));
        x += cellW[i];
        }
    gc->DiscardFont();
    delete gc;

    iBitmap->BeginDataAccess();
    const TUint32* source = iBitmap->DataAddress();
    const TInt sourceStride = CFbsBitmap::ScanLineLength(iBitmap->SizeInPixels().iWidth, EColor16MU) / 4;
    TInt err = KErrNone;
    x = 0;
    for (TInt i = 0; i < todo.Length() && err == KErrNone; i++)
        {
        if (iShelfX + cellW[i] > KAtlasSize)
            {
            iShelfY += iShelfHeight;
            iShelfX = iShelfHeight = 0;
            }
        if (iShelfY + cellH > KAtlasSize)
            {
            iFull = ETrue;
            Trace(_L("text: the glyph atlas is full at %d glyphs"), iGlyphs.Count());
            break;
            }
        for (TInt y = 0; y < cellH; y++)
            {
            const TUint32* row = source + y * sourceStride + x;
            TUint8* out = iPixels + (iShelfY + y) * KAtlasSize + iShelfX;
            for (TInt c = 0; c < cellW[i]; c++)
                out[c] = (TUint8)((row[c] >> 8) & 0xFF);
            }
        TGlyph glyph;
        glyph.iX = (TUint16)iShelfX;
        glyph.iY = (TUint16)iShelfY;
        glyph.iWidth = (TUint16)cellW[i];
        glyph.iHeight = (TUint16)cellH;
        err = iGlyphs.Append(glyph);
        if (err != KErrNone)
            break;
        const TUint c = todo[i];
        const TInt index = iGlyphs.Count() - 1;
        if (c < KFirstDirect + KDirect)
            iDirect[aFont][c - KFirstDirect] = (TInt16)index;
        else
            {
            TOther other;
            other.iFont = aFont;
            other.iChar = c;
            other.iGlyph = index;
            err = iOthers.Append(other);
            }
        iDirtyTop = Min(iDirtyTop, iShelfY);
        iDirtyBottom = Max(iDirtyBottom, iShelfY + cellH);
        iShelfX += cellW[i];
        iShelfHeight = Max(iShelfHeight, cellH);
        x += cellW[i];
        }
    iBitmap->EndDataAccess(ETrue);
    iAdded += todo.Length();
    iAddUs += (TInt)(PerfNowUs() - startUs);
    User::LeaveIfError(err);
    }

void CLabels::Upload(CGlRenderer& aGl)
    {
    const TInt used = iShelfY + iShelfHeight;
    if (!iTexture.Valid() || used > iTexture.iHeight)
        {
        const TInt height = used > KAtlasStart ? KAtlasSize : KAtlasStart;
        if (iTexture.Valid())
            Trace(_L("text: glyph atlas grows to %d rows"), height);
        if (!aGl.CreateTexture(iTexture, KAtlasSize, height, TGlTexture::ELuminance, EFalse))
            return;
        iDirtyTop = 0;
        iDirtyBottom = used;
        }
    if (iDirtyTop >= iDirtyBottom)
        return;
    aGl.Upload(iTexture, 0, iDirtyTop, KAtlasSize, iDirtyBottom - iDirtyTop, iPixels + iDirtyTop * KAtlasSize);
    iDirtyTop = KAtlasSize;
    iDirtyBottom = 0;
    }

void CLabels::Draw(CGlRenderer& aGl, const TDesC& aText, TFontId aFont, TInt aX, TInt aBaseline, TLabelAlign aAlign,
                   TUint32 aRgb, TReal aAlpha, TBool aLive)
    {
    if (aText.Length() == 0 || aAlpha <= 0)
        return;
    iGl = &aGl;
    const TInt width = Layout(aText, aFont, NULL);
    const TInt left = aAlign == ELabelLeft ? aX : (aAlign == ELabelCentre ? aX - width / 2 : aX - width);
    if (!aLive && aText.Length() <= TLabelJob::KMaxChars)
        {
        TLabel* label = FindLabel(aText, aFont);
        if (!label)
            {
            TLabel added;
            added.iId = iNextId++;
            added.iFont = aFont;
            added.iText = aText.Alloc();
            added.iUsedFrame = iFrame;
            added.iPending = ETrue;
            if (added.iText && iLabels.Append(added) == KErrNone)
                label = &iLabels[iLabels.Count() - 1];
            else
                delete added.iText;
            Dispatch();
            label = FindLabel(aText, aFont);
            }
        if (label)
            {
            label->iUsedFrame = iFrame;
            if (label->iTexture.Valid())
                {
                const TInt x0 = left - iPad[aFont], y0 = aBaseline - iFonts[aFont]->FontMaxAscent() - iPad[aFont];
                aGl.AddQuad(x0, y0, x0 + label->iTexture.iWidth, y0 + label->iTexture.iHeight, 0, 0, 1, 1);
                aGl.DrawBatch(label->iTexture, aRgb, aAlpha);
                return;
                }
            }
        }
    DrawFromAtlas(aGl, aText, aFont, left, aBaseline, aRgb, aAlpha);
    }

void CLabels::DrawFromAtlas(CGlRenderer& aGl, const TDesC& aText, TFontId aFont, TInt aLeft, TInt aBaseline,
                            TUint32 aRgb, TReal aAlpha)
    {
    TPtrC text(aText.Left(TLabelJob::KMaxChars));
    for (TInt i = 0; i < text.Length(); i += KAddChunk)
        TRAP_IGNORE(AddL(aFont, text.Mid(i, Min(KAddChunk, text.Length() - i))));
    Upload(aGl);
    if (!iTexture.Valid())
        return;
    const TInt pad = iPad[aFont];
    const TInt top = aBaseline - iFonts[aFont]->FontMaxAscent() - pad;
    const TReal k = 1.0 / KAtlasSize, kv = 1.0 / iTexture.iHeight;
    TInt16 pen[TLabelJob::KMaxChars];
    Layout(text, aFont, pen);
    for (TInt i = 0; i < text.Length(); i++)
        {
        const TInt index = text[i] == ' ' ? -1 : Find(aFont, text[i]);
        if (index < 0)
            continue;
        const TGlyph& g = iGlyphs[index];
        const TInt x = aLeft + pen[i] - pad;
        aGl.AddQuad(x, top, x + g.iWidth, top + g.iHeight, g.iX * k, g.iY * kv, (g.iX + g.iWidth) * k,
                    (g.iY + g.iHeight) * kv);
        }
    aGl.DrawBatch(iTexture, aRgb, aAlpha);
    }

CLabels::TLabel* CLabels::FindLabel(const TDesC& aText, TFontId aFont)
    {
    for (TInt i = 0; i < iLabels.Count(); i++)
        if (iLabels[i].iFont == aFont && *iLabels[i].iText == aText)
            return &iLabels[i];
    return NULL;
    }

void CLabels::Dispatch()
    {
    if (!iWorker || iWorker->Busy() || iWorkerId)
        return;
    for (TInt i = 0; i < iLabels.Count(); i++)
        {
        TLabel& label = iLabels[i];
        if (!label.iPending)
            continue;
        if (label.iUsedFrame < iFrame - 1)
            {
            RemoveLabel(i--);
            continue;
            }
        const TFontId font = label.iFont;
        TLabelJob job;
        job.iTag = label.iId;
        job.iFont = font;
        job.iSpec = iSpecs[font];
        job.iText.Copy(*label.iText);
        const TInt width = Layout(job.iText, font, job.iPen);
        job.iPad = iPad[font];
        job.iBaseline = iPad[font] + iFonts[font]->FontMaxAscent();
        job.iSize = TSize(width + 2 * iPad[font],
                          iFonts[font]->FontMaxAscent() + iFonts[font]->FontMaxDescent() + 2 * iPad[font]);
        job.iPixels = NULL;
        if (iWorker->Request(job) == KErrNone)
            iWorkerId = label.iId;
        else
            label.iPending = EFalse;
        return;
        }
    }

void CLabels::LabelReady(TLabelJob& aJob)
    {
    iWorkerId = 0;
    iLabelsMade++;
    iLabelUs += aJob.iMicros;
    for (TInt i = 0; i < iLabels.Count(); i++)
        {
        TLabel& label = iLabels[i];
        if (label.iId != aJob.iTag)
            continue;
        label.iPending = EFalse;
        if (aJob.iResult != KErrNone || !aJob.iPixels || !iGl)
            {
            if (aJob.iResult != KErrNone)
                Trace(_L("text: label failed %d"), aJob.iResult);
            if (!iGl)
                RemoveLabel(i);
            break;
            }
        if (iGl->CreateTexture(label.iTexture, aJob.iSize.iWidth, aJob.iSize.iHeight, TGlTexture::ELuminance,
                               EFalse))
            {
            iGl->Upload(label.iTexture, 0, 0, aJob.iSize.iWidth, aJob.iSize.iHeight, aJob.iPixels);
            iObserver.LabelsChanged();
            }
        break;
        }
    Dispatch();
    if (!iAllLogged && !iWorkerId)
        {
        iAllLogged = ETrue;
        Trace(_L("text: all labels on screen %d ms after start-up (%d labels, %d ms on the worker)"),
              (TInt)((PerfNowUs() - iStartUs) / 1000), iLabelsMade, iLabelUs / 1000);
        }
    }

void CLabels::RemoveLabel(TInt aIndex)
    {
    TLabel& label = iLabels[aIndex];
    if (iGl)
        iGl->DeleteTexture(label.iTexture);
    delete label.iText;
    iLabels.Remove(aIndex);
    }

TInt CLabels::TextureBytes() const
    {
    TInt bytes = iTexture.Bytes();
    for (TInt i = 0; i < iLabels.Count(); i++)
        bytes += iLabels[i].iTexture.Bytes();
    return bytes;
    }

void CLabels::StartPreload()
    {
    if (iPreload)
        return;
    iPreload = CIdle::New(CActive::EPriorityIdle);
    if (iPreload)
        iPreload->Start(TCallBack(PreloadStep, this));
    }

TInt CLabels::PreloadStep(TAny* aSelf)
    {
    CLabels* self = static_cast<CLabels*>(aSelf);
    const TInt fonts = sizeof(KPreloadFonts) / sizeof(KPreloadFonts[0]);
    while (self->iPreloadFont < fonts && !self->iFull)
        {
        const TFontId font = KPreloadFonts[self->iPreloadFont];
        TBuf<KPreloadChunk> chars;
        for (TInt c = 0; c < KDirect && chars.Length() < KPreloadChunk; c++)
            if (self->iDirect[font][c] < 0 && c + KFirstDirect != ' ')
                chars.Append(c + KFirstDirect);
        if (chars.Length())
            {
            TRAPD(err, self->AddL(font, chars));
            if (err != KErrNone)
                break;
            return ETrue;
            }
        self->iPreloadFont++;
        }
    Trace(_L("text: preload done, %d glyphs, atlas rows %d of %d"), self->iGlyphs.Count(),
          self->iShelfY + self->iShelfHeight, KAtlasSize);
    return EFalse;
    }

void CLabels::NextFrame(CGlRenderer& aGl)
    {
    iGl = &aGl;
    iFrame++;
    Upload(aGl);
    for (;;)
        {
        TInt bytes = 0, oldest = -1;
        for (TInt i = 0; i < iLabels.Count(); i++)
            {
            const TLabel& label = iLabels[i];
            bytes += label.iTexture.Bytes();
            if (!label.iPending && label.iUsedFrame < iFrame - 1 &&
                (oldest < 0 || label.iUsedFrame < iLabels[oldest].iUsedFrame))
                oldest = i;
            }
        if (oldest < 0 || (iLabels.Count() <= KMaxLabels && bytes <= KLabelBytes))
            break;
        RemoveLabel(oldest);
        }
    Dispatch();
    }

void CLabels::ForgetTextures()
    {
    iTexture = TGlTexture();
    iGl = NULL;
    while (iLabels.Count())
        RemoveLabel(iLabels.Count() - 1);
    }

void CLabels::TakeStats(TInt& aCount, TInt& aMicros)
    {
    aCount = iAdded;
    aMicros = iAddUs;
    iAdded = iAddUs = 0;
    }
