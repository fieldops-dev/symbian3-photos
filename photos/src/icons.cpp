#include "icons.h"

#include "iconbake.h"

CIcons* CIcons::NewL(CGlRenderer& aGl, TInt aScale)
    {
    CIcons* self = new (ELeave) CIcons;
    CleanupStack::PushL(self);
    self->ConstructL(aGl, aScale);
    CleanupStack::Pop(self);
    return self;
    }

CIcons::~CIcons()
    {
    }

void CIcons::ConstructL(CGlRenderer& aGl, TInt aScale)
    {
    for (TInt i = 0; i < EIconCount; i++)
        {
        const TIconId icon = (TIconId)i;
        const TSize size = IconShapes::PixelSize(icon, aScale);
        const TInt stride = (size.iWidth * 2 + 3) & ~3;
        const TUint8* pixels = NULL;
        TUint8* rendered = NULL;
        if (aScale == KBakedScale && KBakedSizes[i][0] == size.iWidth && KBakedSizes[i][1] == size.iHeight)
            pixels = KBakedPixels + KBakedOffsets[i];
        else
            {
            rendered = static_cast<TUint8*>(User::AllocLC(stride * size.iHeight));
            IconShapes::Render(icon, aScale, rendered, stride);
            pixels = rendered;
            iRendered++;
            }
        iSizes[i] = size;
        if (aGl.CreateTexture(iTextures[i], size.iWidth, size.iHeight, TGlTexture::ELumAlpha, ETrue))
            aGl.Upload(iTextures[i], 0, 0, size.iWidth, size.iHeight, pixels);
        if (rendered)
            CleanupStack::PopAndDestroy(rendered);
        }
    }

void CIcons::DrawThreeSlice(CGlRenderer& aGl, TIconId aIcon, const TRect& aRect, TReal aAlpha)
    {
    const TSize s = iSizes[aIcon];
    const TInt end = s.iWidth / 2;
    const TReal u0 = (TReal)end / s.iWidth, u1 = (TReal)(end + 1) / s.iWidth;
    aGl.AddQuad(aRect.iTl.iX, aRect.iTl.iY, aRect.iTl.iX + end, aRect.iBr.iY, 0, 0, u0, 1);
    aGl.AddQuad(aRect.iTl.iX + end, aRect.iTl.iY, aRect.iBr.iX - end, aRect.iBr.iY, u0, 0, u1, 1);
    aGl.AddQuad(aRect.iBr.iX - end, aRect.iTl.iY, aRect.iBr.iX, aRect.iBr.iY, u1, 0, 1, 1);

    aGl.DrawBatch(iTextures[aIcon], aIcon == EIconKeyShadow ? 0x000000 : 0xFFFFFF, aAlpha);
    }

void CIcons::Release(CGlRenderer& aGl)
    {
    for (TInt i = 0; i < EIconCount; i++)
        aGl.DeleteTexture(iTextures[i]);
    }

void CIcons::Draw(CGlRenderer& aGl, TIconId aIcon, TInt aCx, TInt aCy, TUint32 aRgb, TReal aAlpha)
    {
    const TSize s = iSizes[aIcon];
    const TInt x = aCx - s.iWidth / 2, y = aCy - s.iHeight / 2;
    aGl.AddQuad(x, y, x + s.iWidth, y + s.iHeight, 0, 0, 1, 1);
    aGl.DrawBatch(iTextures[aIcon], aRgb, aAlpha);
    }

void CIcons::Add(CGlRenderer& aGl, TIconId aIcon, TInt aCx, TInt aCy)
    {
    const TSize s = iSizes[aIcon];
    const TInt x = aCx - s.iWidth / 2, y = aCy - s.iHeight / 2;
    aGl.AddQuad(x, y, x + s.iWidth, y + s.iHeight, 0, 0, 1, 1);
    }

void CIcons::DrawStretched(CGlRenderer& aGl, TIconId aIcon, const TRect& aRect, TUint32 aRgb, TReal aAlpha,
                           TBool aFlip)
    {

    const TReal h = iSizes[aIcon].iHeight, v0 = 0.5 / h, v1 = 1 - 0.5 / h;
    aGl.AddQuad(aRect.iTl.iX, aRect.iTl.iY, aRect.iBr.iX, aRect.iBr.iY, 0, aFlip ? v1 : v0, 1, aFlip ? v0 : v1);
    aGl.DrawBatch(iTextures[aIcon], aRgb, aAlpha);
    }

void CIcons::AddCorner(CGlRenderer& aGl, const TRect& aRect, TInt aCorner)
    {

    const TBool flipU = aCorner == 1 || aCorner == 3;
    const TBool flipV = aCorner >= 2;
    const TReal u0 = flipU ? 1 : 0, u1 = flipU ? 0 : 1, v0 = flipV ? 1 : 0, v1 = flipV ? 0 : 1;
    aGl.AddQuad(aRect.iTl.iX, aRect.iTl.iY, aRect.iBr.iX, aRect.iBr.iY, u0, v0, u1, v1);
    }
