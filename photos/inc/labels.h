#ifndef LABELS_H
#define LABELS_H

#include <e32base.h>
#include <gdi.h>

#include "fontmetrics.h"
#include "glrenderer.h"
#include "labelworker.h"

class RFs;
class CFbsBitmap;
class CFbsBitmapDevice;
class CFont;

enum TFontId
    {
    EFontTitle,
    EFontYear,
    EFontHeader,
    EFontHeaderDigits,
    EFontBody,
    EFontSmall,
    EFontCaption,
    EFontCaptionDigits,
    EFontButton,
    EFontDate,
    EFontTime,
    EFontMenu,
    EFontScrub,
    EFontCount
    };

enum TLabelAlign { ELabelLeft, ELabelCentre, ELabelRight };

class MLabelsObserver
    {
public:

    virtual void LabelsChanged() = 0;
    };

class CLabels : public CBase, public MLabelObserver
    {
public:
    static CLabels* NewL(RFs& aFs, const TDesC& aFallbackTypeface, TInt aScale, MLabelsObserver& aObserver);
    ~CLabels();

    void Draw(CGlRenderer& aGl, const TDesC& aText, TFontId aFont, TInt aX, TInt aBaseline, TLabelAlign aAlign,
              TUint32 aRgb, TReal aAlpha, TBool aLive = EFalse);
    TInt TextWidth(const TDesC& aText, TFontId aFont) const;

    void Fit(TDes& aText, TFontId aFont, TInt aWidth) const;

    void StartPreload();

    void NextFrame(CGlRenderer& aGl);

    void ForgetTextures();

    void TakeStats(TInt& aCount, TInt& aMicros);
    TInt Count() const { return iGlyphs.Count(); }

    TInt TextureBytes() const;

private:
    class TGlyph
        {
    public:
        TUint16 iX;
        TUint16 iY;
        TUint16 iWidth;
        TUint16 iHeight;
        };
    class TOther
        {
    public:
        TInt iFont;
        TUint iChar;
        TInt iGlyph;
        };

    class TLabel
        {
    public:
        TInt iId;
        TFontId iFont;
        HBufC* iText;
        TGlTexture iTexture;
        TInt iUsedFrame;
        TBool iPending;
        };
    enum TFace { EFaceHeadline, EFaceBold, EFaceTabularBold, EFaceTabular, EFaceRegular, EFaceCount };
    enum { KFirstDirect = 32, KDirect = 95 };

    CLabels(RFs& aFs, const TDesC& aFallbackTypeface, TInt aScale, MLabelsObserver& aObserver);
    void ConstructL();
    void AddFontFiles();
    TBool CopyFontL(const TDesC& aSource, const TDesC& aTarget);
    void ResolveFaces();
    void ReadMetrics(TInt aFace);
    void MakeFontL(TFontId aFont, TFace aFace, TInt aPixels);
    TInt Find(TFontId aFont, TUint aChar) const;

    TInt Layout(const TDesC& aText, TFontId aFont, TInt16* aPen) const;

    void AddL(TFontId aFont, const TDesC& aChars);
    void EnsureScratchL(TSize aSize);
    void Upload(CGlRenderer& aGl);
    void DrawFromAtlas(CGlRenderer& aGl, const TDesC& aText, TFontId aFont, TInt aLeft, TInt aBaseline,
                       TUint32 aRgb, TReal aAlpha);
    TLabel* FindLabel(const TDesC& aText, TFontId aFont);
    void Dispatch();
    void LabelReady(TLabelJob& aJob);
    void RemoveLabel(TInt aIndex);
    static TInt PreloadStep(TAny* aSelf);

    RFs& iFs;
    MLabelsObserver& iObserver;
    TBuf<KMaxTypefaceNameLength> iFallbackTypeface;
    TInt iScale;
    CFbsBitmap* iBitmap;
    CFbsBitmapDevice* iDevice;
    enum { KMaxFontFiles = 6 };
    TInt iFontIds[KMaxFontFiles];
    TInt iFontIdCount;
    TBool iFontsShared;
    TFileName* iFiles[KMaxFontFiles];
    TInt iFileCount;
    TBuf<KMaxTypefaceNameLength> iFaceName[EFaceCount];
    TInt iFaceFile[EFaceCount];
    TFontMetrics* iFaceMetrics[EFaceCount];
    CFont* iFonts[EFontCount];
    TFontSpec iSpecs[EFontCount];
    TFace iFontFace[EFontCount];
    TReal iScaleK[EFontCount];
    TInt iPad[EFontCount];
    TInt16 iDirect[EFontCount][KDirect];
    RArray<TOther> iOthers;
    RArray<TGlyph> iGlyphs;
    TUint8* iPixels;
    TInt iShelfX;
    TInt iShelfY;
    TInt iShelfHeight;
    TBool iFull;
    TInt iDirtyTop;
    TInt iDirtyBottom;
    TGlTexture iTexture;
    CGlRenderer* iGl;
    CLabelWorker* iWorker;
    RArray<TLabel> iLabels;
    TInt iNextId;
    TInt iWorkerId;
    TInt iFrame;
    TInt64 iStartUs;
    TBool iAllLogged;
    CIdle* iPreload;
    TInt iPreloadFont;
    TInt iAdded;
    TInt iAddUs;
    TInt iLabelsMade;
    TInt iLabelUs;
    };

#endif
