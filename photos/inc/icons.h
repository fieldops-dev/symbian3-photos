#ifndef ICONS_H
#define ICONS_H

#include <e32base.h>

#include "glrenderer.h"

#include "iconshapes.h"

class CIcons : public CBase
    {
public:

    static CIcons* NewL(CGlRenderer& aGl, TInt aScale);
    ~CIcons();
    TSize Size(TIconId aIcon) const { return iSizes[aIcon]; }
    void Draw(CGlRenderer& aGl, TIconId aIcon, TInt aCx, TInt aCy, TUint32 aRgb, TReal aAlpha);

    void Add(CGlRenderer& aGl, TIconId aIcon, TInt aCx, TInt aCy);

    void DrawStretched(CGlRenderer& aGl, TIconId aIcon, const TRect& aRect, TUint32 aRgb, TReal aAlpha,
                       TBool aFlip = EFalse);

    void DrawThreeSlice(CGlRenderer& aGl, TIconId aIcon, const TRect& aRect, TReal aAlpha);

    TInt ShadowMargin() const { return (iSizes[EIconKeyShadow].iHeight - iSizes[EIconKey].iHeight) / 2; }

    void AddCorner(CGlRenderer& aGl, const TRect& aRect, TInt aCorner);
    const TGlTexture& Texture(TIconId aIcon) const { return iTextures[aIcon]; }
    void Release(CGlRenderer& aGl);

    TInt Rendered() const { return iRendered; }

private:
    void ConstructL(CGlRenderer& aGl, TInt aScale);
    TGlTexture iTextures[EIconCount];
    TSize iSizes[EIconCount];
    TInt iRendered;
    };

#endif
