#ifndef GLRENDERER_H
#define GLRENDERER_H

#include <e32base.h>

class RWindow;

class TGlTexture
    {
public:

    enum TFormat { ERgb565, ELumAlpha, ELuminance, EEtc1 };
    TGlTexture() : iId(0), iWidth(0), iHeight(0), iFormat(ERgb565) {}
    TBool Valid() const { return iId != 0; }
    TInt Bytes() const
        {
        return iFormat == EEtc1 ? iWidth * iHeight / 2 : iWidth * iHeight * (iFormat == ELuminance ? 1 : 2);
        }
    TUint iId;
    TInt iWidth;
    TInt iHeight;
    TFormat iFormat;
    };

class CGlRenderer : public CBase
    {
public:

    static CGlRenderer* NewL(RWindow& aWindow, TSize aSize);
    void CompleteL();
    ~CGlRenderer();
    static TPtrC LastError();

    TBool Resize(TSize aSize);
    void RecreateSurfaceL();
    TSize Size() const { return iSize; }
    TInt MaxTexture() const { return iMaxTexture; }

    TBool HasEtc1() const { return iEtc1; }
    TInt TextureBytes() const { return iTextureBytes; }

    TBool GpuMemory(TInt& aTotal, TInt& aUsed, TInt& aOurs);

    TBool CreateTexture(TGlTexture& aTexture, TInt aWidth, TInt aHeight, TGlTexture::TFormat aFormat,
                        TBool aSmooth, TBool aChecked = EFalse);
    void Upload(const TGlTexture& aTexture, TInt aX, TInt aY, TInt aWidth, TInt aHeight, const TAny* aPixels);

    TBool CreateEtc1(TGlTexture& aTexture, TInt aWidth, TInt aHeight, const TAny* aBlocks);
    void UploadEtc1(const TGlTexture& aTexture, TInt aX, TInt aY, TInt aWidth, TInt aHeight, const TAny* aBlocks);
    void ReplaceEtc1(const TGlTexture& aTexture, const TAny* aBlocks);

    TBool TakeError();
    void DeleteTexture(TGlTexture& aTexture);

    TBool HasFrost() const { return iFrost; }
    void BeginFrost(TUint32 aClearRgb);
    void EndFrost();

    void BeginFrame(TUint32 aClearRgb);

    void SetDim(TReal aDim) { iDim = aDim; }
    TReal Dim() const { return iDim; }

    void Clip(const TRect& aRect);
    void NoClip();

    void AddQuad(TReal aX0, TReal aY0, TReal aX1, TReal aY1, TReal aU0, TReal aV0, TReal aU1, TReal aV1);
    void AddQuad(TReal aX0, TReal aY0, TReal aX1, TReal aY1, const TReal* aUv);

    void DrawBatch(const TGlTexture& aTexture, TUint32 aRgb, TReal aAlpha);

    void PrepareVideo();
    TBool DrawVideo(const TGlTexture* aPlanes, TReal aX0, TReal aY0, TReal aX1, TReal aY1, const TReal* aUv,
                    TReal aAlpha);

    void DrawBatchFlat(TUint32 aRgb, TReal aAlpha);
    TInt BatchCount() const { return iQuads; }

    void FillRect(const TRect& aRect, TUint32 aRgb, TReal aAlpha);

    void DrawFrost(const TRect& aBand, TUint32 aTint, TReal aAmount, TReal aAlpha = 1);

    TBool EndFrame();

    TInt DrawCalls() const { return iDrawCalls; }

private:
    CGlRenderer(TSize aSize);
    void ConstructL(RWindow& aWindow);
    TUint StartProgram(const char* aVertex, const char* aFragment);
    void MakeTarget(TSize aSize, TUint& aTexture, TUint& aFbo);
    void CreateFrostTargets();
    void WarmUp();
    void LogExtensions();
    void UseQuad(TUint aTexture, const float* aMul, const float* aAdd, TUint aProgram = 0);
    void Flush();
    void BlurPass(TUint aSource, TUint aTargetFbo, TReal aDx, TReal aDy);

    TSize iSize;
    RWindow* iWindow;
    TInt iDisplay;
    TInt iSurface;
    TInt iContext;
    TInt iConfig;
    TInt iMaxTexture;
    TUint iQuadProgram;
    TUint iBlurProgram;
    TUint iTextProgram;
    TBool iEtc1;
    TUint iWhiteTexture;

    TInt iVideoState;
    TUint iVideoProgram;
    TUint iVideoShader[2];

    enum { KMaxPrograms = 3 };
    TUint iPendingProgram[KMaxPrograms];
    TUint iPendingShader[KMaxPrograms * 2];
    TInt iPendingCount;

    TBool iFrost;
    TSize iFrostSize;
    TUint iFrostTex;
    TUint iFrostFbo;
    TUint iTempTex;
    TUint iTempFbo;
    TBool iInFrost;
    TReal iDim;
    TInt iTextureBytes;
    TInt iDrawCalls;

    float* iVertices;
    TInt iQuads;
    };

#endif
