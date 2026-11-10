#include "glrenderer.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gdi.h>
#include <w32std.h>

#include "trace.h"

namespace
    {

    TUint16 gError[80];
    TInt gErrorLength = 0;

    const TInt KMaxQuads = 400;
    const TInt KFloatsPerQuad = 24;
    const TInt KFrostDivisor = 8;

    void SetError(const TDesC& aStep, TInt aCode)
        {
        TPtr16 text(gError, 0, sizeof(gError) / sizeof(gError[0]));
        text.Copy(aStep.Left(50));
        if (aCode)
            text.AppendFormat(_L(" 0x%x"), aCode);
        gErrorLength = text.Length();
        }

    void FailL(const TDesC& aStep, TInt aCode)
        {
        SetError(aStep, aCode);
        User::Leave(KErrNotSupported);
        }

    #define FS_PRECISION \
        "#ifdef GL_FRAGMENT_PRECISION_HIGH\n#define HP highp\n#else\n#define HP mediump\n#endif\n" \
        "precision mediump float;\n"

    const char KQuadVs[] =
        "attribute vec2 aPos;\n"
        "attribute vec2 aUv;\n"
        "uniform vec2 uScreen;\n"
        "varying vec2 vUv;\n"
        "void main() {\n"
        "  vUv = aUv;\n"
        "  gl_Position = vec4(aPos.x * 2.0 / uScreen.x - 1.0, 1.0 - aPos.y * 2.0 / uScreen.y, 0.0, 1.0);\n"
        "}\n";

    const char KQuadFs[] = FS_PRECISION
        "uniform sampler2D uTex;\n"
        "uniform vec4 uMul;\n"
        "uniform vec4 uAdd;\n"
        "varying HP vec2 vUv;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(uTex, vUv) * uMul + uAdd;\n"
        "}\n";

    const char KTextFs[] = FS_PRECISION
        "uniform sampler2D uTex;\n"
        "uniform vec4 uMul;\n"
        "varying HP vec2 vUv;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(uTex, vUv).r * uMul;\n"
        "}\n";

    const char KBlurFs[] = FS_PRECISION
        "uniform sampler2D uTex;\n"
        "uniform vec2 uTarget;\n"
        "uniform vec2 uDir;\n"
        "void main() {\n"
        "  vec2 uv = gl_FragCoord.xy / uTarget;\n"
        "  vec3 c = texture2D(uTex, uv).rgb * 0.2270270;\n"
        "  c += texture2D(uTex, uv + uDir * 1.3846154).rgb * 0.3162162;\n"
        "  c += texture2D(uTex, uv - uDir * 1.3846154).rgb * 0.3162162;\n"
        "  c += texture2D(uTex, uv + uDir * 3.2307692).rgb * 0.0702703;\n"
        "  c += texture2D(uTex, uv - uDir * 3.2307692).rgb * 0.0702703;\n"
        "  gl_FragColor = vec4(c, 1.0);\n"
        "}\n";

    const char KVideoFs[] = FS_PRECISION
        "uniform sampler2D uY;\n"
        "uniform sampler2D uU;\n"
        "uniform sampler2D uV;\n"
        "uniform float uAlpha;\n"
        "varying HP vec2 vUv;\n"
        "void main() {\n"
        "  float y = (texture2D(uY, vUv).r - 0.0627451) * 1.1643836;\n"
        "  float u = texture2D(uU, vUv).r - 0.5019608;\n"
        "  float v = texture2D(uV, vUv).r - 0.5019608;\n"
        "  vec3 rgb = vec3(y + 1.5960268 * v, y - 0.3917623 * u - 0.8129676 * v, y + 2.0172321 * u);\n"
        "  gl_FragColor = vec4(clamp(rgb, 0.0, 1.0) * uAlpha, uAlpha);\n"
        "}\n";

    const TInt KMaxUniforms = 24;
    TUint gUniformProgram[KMaxUniforms];
    const char* gUniformName[KMaxUniforms];
    GLint gUniformLocation[KMaxUniforms];
    TInt gUniformCount = 0;

    GLint U(TUint aProgram, const char* aName)
        {
        for (TInt i = 0; i < gUniformCount; i++)
            if (gUniformName[i] == aName && gUniformProgram[i] == aProgram)
                return gUniformLocation[i];
        GLint location = glGetUniformLocation(aProgram, aName);
        if (gUniformCount < KMaxUniforms)
            {
            gUniformProgram[gUniformCount] = aProgram;
            gUniformName[gUniformCount] = aName;
            gUniformLocation[gUniformCount] = location;
            gUniformCount++;
            }
        return location;
        }

    void Unpack(TUint32 aRgb, float& aR, float& aG, float& aB)
        {
        aR = ((aRgb >> 16) & 0xFF) / 255.0f;
        aG = ((aRgb >> 8) & 0xFF) / 255.0f;
        aB = (aRgb & 0xFF) / 255.0f;
        }
    }

CGlRenderer* CGlRenderer::NewL(RWindow& aWindow, TSize aSize)
    {
    gErrorLength = 0;
    CGlRenderer* self = new (ELeave) CGlRenderer(aSize);
    CleanupStack::PushL(self);
    self->ConstructL(aWindow);
    CleanupStack::Pop(self);
    return self;
    }

CGlRenderer::CGlRenderer(TSize aSize)
    : iSize(aSize)
    {
    }

TPtrC CGlRenderer::LastError()
    {
    return TPtrC(gError, gErrorLength);
    }

void CGlRenderer::ConstructL(RWindow& aWindow)
    {
    iWindow = &aWindow;
    gUniformCount = 0;
    iVertices = static_cast<float*>(User::AllocL(KMaxQuads * KFloatsPerQuad * sizeof(float)));
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY)
        FailL(_L("eglGetDisplay"), eglGetError());
    if (eglInitialize(display, NULL, NULL) == EGL_FALSE)
        FailL(_L("eglInitialize"), eglGetError());
    iDisplay = display;
    const EGLint bits = TDisplayModeUtils::NumDisplayModeBitsPerPixel(aWindow.DisplayMode());
    const EGLint configAttributes[] =
        {
        EGL_BUFFER_SIZE, bits,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
        };
    EGLConfig config;
    EGLint configs = 0;
    if (eglChooseConfig(display, configAttributes, &config, 1, &configs) == EGL_FALSE || configs == 0)
        FailL(_L("eglChooseConfig"), eglGetError());
    iConfig = config;
    EGLSurface surface = eglCreateWindowSurface(display, config, &aWindow, NULL);
    if (surface == EGL_NO_SURFACE)
        FailL(_L("eglCreateWindowSurface"), eglGetError());
    iSurface = surface;
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    if (context == EGL_NO_CONTEXT)
        FailL(_L("eglCreateContext"), eglGetError());
    iContext = context;
    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE)
        FailL(_L("eglMakeCurrent"), eglGetError());

    eglSwapInterval(display, 1);

    GLint maxTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    iMaxTexture = maxTexture;
    iQuadProgram = StartProgram(KQuadVs, KQuadFs);
    iBlurProgram = StartProgram(KQuadVs, KBlurFs);
    iTextProgram = StartProgram(KQuadVs, KTextFs);
    LogExtensions();
    const TUint32 white = 0xFFFFFFFF;
    glGenTextures(1, reinterpret_cast<GLuint*>(&iWhiteTexture));
    glBindTexture(GL_TEXTURE_2D, iWhiteTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);

    glViewport(0, 0, iSize.iWidth, iSize.iHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    CreateFrostTargets();
    Trace(_L("gpu: context made, max texture %d, %d-bit window"), iMaxTexture, bits);
    }

CGlRenderer::~CGlRenderer()
    {
    User::Free(iVertices);
    EGLDisplay display = iDisplay;
    if (display)
        {
        if (iContext)
            {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display, iContext);
            }
        if (iSurface)
            eglDestroySurface(display, iSurface);
        eglTerminate(display);
        eglReleaseThread();
        }
    }

TBool CGlRenderer::Resize(TSize aSize)
    {
    if (aSize == iSize)
        return ETrue;
    iSize = aSize;
    const TInt64 startUs = PerfNowUs();
    TRAPD(err, RecreateSurfaceL());
    Trace(_L("turn: surface made in %d ms, err %d"), (TInt)((PerfNowUs() - startUs) / 1000), err);
    if (err != KErrNone)
        return EFalse;
    glViewport(0, 0, iSize.iWidth, iSize.iHeight);
    const TUint textures[2] = { iFrostTex, iTempTex };
    const TUint fbos[2] = { iFrostFbo, iTempFbo };
    glDeleteTextures(2, textures);
    glDeleteFramebuffers(2, fbos);
    iTextureBytes -= 2 * iFrostSize.iWidth * iFrostSize.iHeight * 4;
    CreateFrostTargets();
    return ETrue;
    }

void CGlRenderer::RecreateSurfaceL()
    {
    eglMakeCurrent(iDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(iDisplay, iSurface);
    iSurface = 0;
    EGLSurface surface = eglCreateWindowSurface(iDisplay, iConfig, iWindow, NULL);
    if (surface == EGL_NO_SURFACE)
        FailL(_L("eglCreateWindowSurface again"), eglGetError());
    iSurface = surface;
    if (eglMakeCurrent(iDisplay, surface, surface, iContext) == EGL_FALSE)
        FailL(_L("eglMakeCurrent again"), eglGetError());
    eglSwapInterval(iDisplay, 1);
    glViewport(0, 0, iSize.iWidth, iSize.iHeight);
    }

TUint CGlRenderer::StartProgram(const char* aVertex, const char* aFragment)
    {
    const char* sources[2] = { aVertex, aFragment };
    const GLenum types[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    GLuint shaders[2];
    for (TInt i = 0; i < 2; i++)
        {
        shaders[i] = glCreateShader(types[i]);
        glShaderSource(shaders[i], 1, &sources[i], NULL);
        glCompileShader(shaders[i]);
        }
    GLuint program = glCreateProgram();
    glAttachShader(program, shaders[0]);
    glAttachShader(program, shaders[1]);
    glBindAttribLocation(program, 0, "aPos");
    glBindAttribLocation(program, 1, "aUv");
    glLinkProgram(program);
    if (iPendingCount < KMaxPrograms)
        {
        iPendingProgram[iPendingCount] = program;
        iPendingShader[iPendingCount * 2] = shaders[0];
        iPendingShader[iPendingCount * 2 + 1] = shaders[1];
        iPendingCount++;
        }
    return program;
    }

void CGlRenderer::CompleteL()
    {
    const TInt64 startUs = PerfNowUs();
    for (TInt p = 0; p < iPendingCount; p++)
        {
        for (TInt i = 0; i < 2; i++)
            {
            const GLuint shader = iPendingShader[p * 2 + i];
            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok)
                {
                char log[48];
                GLsizei length = 0;
                glGetShaderInfoLog(shader, sizeof(log) - 1, &length, log);
                TBuf<60> step(i == 0 ? _L("vertex shader: ") : _L("fragment shader: "));
                TBuf<48> text;
                text.Copy(TPtrC8(reinterpret_cast<const TUint8*>(log), Max(0, Min((TInt)length, 40))));
                step.Append(text.Left(step.MaxLength() - step.Length()));
                FailL(step, 0);
                }
            }
        GLint linked = 0;
        glGetProgramiv(iPendingProgram[p], GL_LINK_STATUS, &linked);
        if (!linked)
            FailL(_L("shader link"), glGetError());
        glDeleteShader(iPendingShader[p * 2]);
        glDeleteShader(iPendingShader[p * 2 + 1]);
        }
    iPendingCount = 0;

    glBindFramebuffer(GL_FRAMEBUFFER, iFrostFbo);
    iFrost = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    WarmUp();
    Trace(_L("gpu: programs ready, frost %d (%dx%d); waited %d ms after the CPU work"), iFrost, iFrostSize.iWidth,
          iFrostSize.iHeight, (TInt)((PerfNowUs() - startUs) / 1000));
    }

void CGlRenderer::MakeTarget(TSize aSize, TUint& aTexture, TUint& aFbo)
    {
    glGenTextures(1, reinterpret_cast<GLuint*>(&aTexture));
    glBindTexture(GL_TEXTURE_2D, aTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, aSize.iWidth, aSize.iHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, reinterpret_cast<GLuint*>(&aFbo));
    glBindFramebuffer(GL_FRAMEBUFFER, aFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aTexture, 0);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    iTextureBytes += aSize.iWidth * aSize.iHeight * 4;
    }

void CGlRenderer::CreateFrostTargets()
    {
    iFrostSize = TSize(Max(1, iSize.iWidth / KFrostDivisor), Max(1, iSize.iHeight / KFrostDivisor));
    MakeTarget(iFrostSize, iFrostTex, iFrostFbo);
    MakeTarget(iFrostSize, iTempTex, iTempFbo);
    }

void CGlRenderer::WarmUp()
    {
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 1, 1);
    const TUint programs[3] = { iQuadProgram, iBlurProgram, iTextProgram };
    for (TInt i = 0; i < 3; i++)
        {
        glUseProgram(programs[i]);
        glBindTexture(GL_TEXTURE_2D, iWhiteTexture);
        AddQuad(0, 0, 1, 1, 0, 0, 1, 1);
        Flush();
        }
    glDisable(GL_SCISSOR_TEST);
    }

void CGlRenderer::LogExtensions()
    {
    const char* text = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const TPtrC8 all(text ? reinterpret_cast<const TUint8*>(text) : reinterpret_cast<const TUint8*>(""),
                     text ? User::StringLength(reinterpret_cast<const TUint8*>(text)) : 0);
    _LIT8(KEtc1, "GL_OES_compressed_ETC1_RGB8_texture");
    iEtc1 = all.Find(KEtc1) >= 0;
    Trace(_L("gpu: ETC1 %d; %d characters of extensions follow"), iEtc1, all.Length());
    const TInt KPiece = 400;
    TBuf<KPiece> piece;
    for (TInt at = 0; at < all.Length(); at += KPiece)
        {
        piece.Copy(all.Mid(at, Min(KPiece, all.Length() - at)));
        Trace(_L("gpu: extensions %S"), &piece);
        }
    }

TBool CGlRenderer::GpuMemory(TInt& aTotal, TInt& aUsed, TInt& aOurs)
    {
    aTotal = aUsed = aOurs = 0;
    PFNEGLQUERYPROFILINGDATANOKPROC query =
        reinterpret_cast<PFNEGLQUERYPROFILINGDATANOKPROC>(eglGetProcAddress("eglQueryProfilingDataNOK"));
    if (!query)
        return EFalse;
    const EGLint bits = EGL_PROF_QUERY_GLOBAL_BIT_NOK | EGL_PROF_QUERY_MEMORY_USAGE_BIT_NOK;
    EGLint count = 0;
    if (!query(iDisplay, bits, NULL, 0, &count) || count <= 0)
        return EFalse;
    const TInt KMax = 256;
    EGLint data[KMax];
    if (!query(iDisplay, bits, data, Min((TInt)count, KMax), &count))
        return EFalse;

    const TUint32 self = (TUint32)RProcess().Id().Id();
    TBool ours = EFalse;
    for (TInt i = 0; i + 1 < count && i + 1 < KMax;)
        {
        const EGLint attribute = data[i];
        if (attribute == EGL_PROF_PROCESS_ID_NOK)
            {
            ours = (TUint32)data[i + 1] == self;
            i += 3;
            continue;
            }
        const EGLint value = data[i + 1];
        if (attribute == EGL_PROF_TOTAL_MEMORY_NOK)
            aTotal = value;
        else if (attribute == EGL_PROF_USED_MEMORY_NOK)
            aUsed = value;
        else if (ours && (attribute == EGL_PROF_PROCESS_USED_PRIVATE_MEMORY_NOK ||
                          attribute == EGL_PROF_PROCESS_USED_SHARED_MEMORY_NOK))
            aOurs += value;
        i += 2;
        }
    return aTotal > 0;
    }

TBool CGlRenderer::CreateTexture(TGlTexture& aTexture, TInt aWidth, TInt aHeight, TGlTexture::TFormat aFormat,
                                 TBool aSmooth, TBool aChecked)
    {
    DeleteTexture(aTexture);
    if (aWidth <= 0 || aHeight <= 0 || aWidth > iMaxTexture || aHeight > iMaxTexture)
        return EFalse;
    if (aChecked)
        while (glGetError() != GL_NO_ERROR)
            {
            }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    const GLint filter = aSmooth ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (aFormat == TGlTexture::ERgb565)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, aWidth, aHeight, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
    else if (aFormat == TGlTexture::ELuminance)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, aWidth, aHeight, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, aWidth, aHeight, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                     NULL);

    if (aChecked && glGetError() != GL_NO_ERROR)
        {
        glDeleteTextures(1, &id);
        return EFalse;
        }
    aTexture.iId = id;
    aTexture.iWidth = aWidth;
    aTexture.iHeight = aHeight;
    aTexture.iFormat = aFormat;
    iTextureBytes += aTexture.Bytes();
    return ETrue;
    }

void CGlRenderer::Upload(const TGlTexture& aTexture, TInt aX, TInt aY, TInt aWidth, TInt aHeight,
                         const TAny* aPixels)
    {
    if (!aTexture.iId)
        return;
    glBindTexture(GL_TEXTURE_2D, aTexture.iId);
    if (aTexture.iFormat == TGlTexture::ERgb565)
        glTexSubImage2D(GL_TEXTURE_2D, 0, aX, aY, aWidth, aHeight, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, aPixels);
    else if (aTexture.iFormat == TGlTexture::ELuminance)
        glTexSubImage2D(GL_TEXTURE_2D, 0, aX, aY, aWidth, aHeight, GL_LUMINANCE, GL_UNSIGNED_BYTE, aPixels);
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, aX, aY, aWidth, aHeight, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, aPixels);
    }

TBool CGlRenderer::CreateEtc1(TGlTexture& aTexture, TInt aWidth, TInt aHeight, const TAny* aBlocks)
    {
    DeleteTexture(aTexture);
    if (!iEtc1 || aWidth <= 0 || aHeight <= 0 || aWidth > iMaxTexture || aHeight > iMaxTexture)
        return EFalse;
    while (glGetError() != GL_NO_ERROR)
        {
        }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, aWidth, aHeight, 0, aWidth * aHeight / 2, aBlocks);
    if (glGetError() != GL_NO_ERROR)
        {
        glDeleteTextures(1, &id);
        return EFalse;
        }
    aTexture.iId = id;
    aTexture.iWidth = aWidth;
    aTexture.iHeight = aHeight;
    aTexture.iFormat = TGlTexture::EEtc1;
    iTextureBytes += aTexture.Bytes();
    return ETrue;
    }

void CGlRenderer::UploadEtc1(const TGlTexture& aTexture, TInt aX, TInt aY, TInt aWidth, TInt aHeight,
                             const TAny* aBlocks)
    {
    if (!aTexture.iId)
        return;
    glBindTexture(GL_TEXTURE_2D, aTexture.iId);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, aX, aY, aWidth, aHeight, GL_ETC1_RGB8_OES, aWidth * aHeight / 2,
                              aBlocks);
    }

void CGlRenderer::ReplaceEtc1(const TGlTexture& aTexture, const TAny* aBlocks)
    {
    if (!aTexture.iId)
        return;
    glBindTexture(GL_TEXTURE_2D, aTexture.iId);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, aTexture.iWidth, aTexture.iHeight, 0,
                           aTexture.Bytes(), aBlocks);
    }

TBool CGlRenderer::TakeError()
    {
    TBool failed = EFalse;
    while (glGetError() != GL_NO_ERROR)
        failed = ETrue;
    return failed;
    }

void CGlRenderer::DeleteTexture(TGlTexture& aTexture)
    {
    if (!aTexture.iId)
        return;
    GLuint id = aTexture.iId;
    glDeleteTextures(1, &id);
    iTextureBytes -= aTexture.Bytes();
    aTexture = TGlTexture();
    }

void CGlRenderer::BeginFrost(TUint32 aClearRgb)
    {
    if (!iFrost)
        return;
    iInFrost = ETrue;
    iDim = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, iFrostFbo);
    glViewport(0, 0, iFrostSize.iWidth, iFrostSize.iHeight);
    glDisable(GL_SCISSOR_TEST);
    float r, g, b;
    Unpack(aClearRgb, r, g, b);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    }

void CGlRenderer::EndFrost()
    {
    if (!iInFrost)
        return;
    iInFrost = EFalse;
    glDisable(GL_BLEND);
    BlurPass(iFrostTex, iTempFbo, 1.0 / iFrostSize.iWidth, 0);
    BlurPass(iTempTex, iFrostFbo, 0, 1.0 / iFrostSize.iHeight);
    glEnable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, iSize.iWidth, iSize.iHeight);
    }

void CGlRenderer::BlurPass(TUint aSource, TUint aTargetFbo, TReal aDx, TReal aDy)
    {
    const GLuint p = iBlurProgram;
    glBindFramebuffer(GL_FRAMEBUFFER, aTargetFbo);
    glUseProgram(p);
    glUniform2f(U(p, "uScreen"), (GLfloat)iSize.iWidth, (GLfloat)iSize.iHeight);
    glUniform2f(U(p, "uTarget"), (GLfloat)iFrostSize.iWidth, (GLfloat)iFrostSize.iHeight);
    glUniform1i(U(p, "uTex"), 0);
    glUniform2f(U(p, "uDir"), (GLfloat)aDx, (GLfloat)aDy);
    glBindTexture(GL_TEXTURE_2D, aSource);
    AddQuad(0, 0, iSize.iWidth, iSize.iHeight, 0, 0, 1, 1);
    Flush();
    }

void CGlRenderer::BeginFrame(TUint32 aClearRgb)
    {
    iDrawCalls = 0;
    iDim = 0;
    glDisable(GL_SCISSOR_TEST);
    float r, g, b;
    Unpack(aClearRgb, r, g, b);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    }

void CGlRenderer::Clip(const TRect& aRect)
    {
    if (iInFrost)
        return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(aRect.iTl.iX, iSize.iHeight - aRect.iBr.iY, Max(0, aRect.Width()), Max(0, aRect.Height()));
    }

void CGlRenderer::NoClip()
    {
    glDisable(GL_SCISSOR_TEST);
    }

void CGlRenderer::AddQuad(TReal aX0, TReal aY0, TReal aX1, TReal aY1, TReal aU0, TReal aV0, TReal aU1, TReal aV1)
    {
    const TReal uv[8] = { aU0, aV0, aU1, aV0, aU0, aV1, aU1, aV1 };
    AddQuad(aX0, aY0, aX1, aY1, uv);
    }

void CGlRenderer::AddQuad(TReal aX0, TReal aY0, TReal aX1, TReal aY1, const TReal* aUv)
    {
    if (iQuads >= KMaxQuads)
        return;
    float* v = iVertices + iQuads * KFloatsPerQuad;

    const TInt order[6] = { 0, 1, 2, 1, 3, 2 };
    for (TInt i = 0; i < 6; i++)
        {
        const TInt c = order[i];
        *v++ = (float)(c & 1 ? aX1 : aX0);
        *v++ = (float)(c & 2 ? aY1 : aY0);
        *v++ = (float)aUv[c * 2];
        *v++ = (float)aUv[c * 2 + 1];
        }
    iQuads++;
    }

void CGlRenderer::UseQuad(TUint aTexture, const float* aMul, const float* aAdd, TUint aProgram)
    {
    const GLuint p = aProgram ? aProgram : iQuadProgram;
    glUseProgram(p);
    glUniform2f(U(p, "uScreen"), (GLfloat)iSize.iWidth, (GLfloat)iSize.iHeight);
    glUniform1i(U(p, "uTex"), 0);
    glUniform4fv(U(p, "uMul"), 1, aMul);
    if (p != iTextProgram)
        glUniform4fv(U(p, "uAdd"), 1, aAdd);
    glBindTexture(GL_TEXTURE_2D, aTexture);
    }

void CGlRenderer::Flush()
    {
    if (iQuads == 0)
        return;
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), iVertices);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), iVertices + 2);
    glDrawArrays(GL_TRIANGLES, 0, iQuads * 6);
    iQuads = 0;
    iDrawCalls++;
    }

void CGlRenderer::DrawBatch(const TGlTexture& aTexture, TUint32 aRgb, TReal aAlpha)
    {
    if (!aTexture.iId || aAlpha <= 0)
        {
        iQuads = 0;
        return;
        }
    float r, g, b;
    Unpack(aRgb, r, g, b);
    const float a = (float)aAlpha, keep = (float)(1 - iDim);
    const float mul[4] = { r * a * keep, g * a * keep, b * a * keep, a };
    const float add[4] = { 0, 0, 0, 0 };
    UseQuad(aTexture.iId, mul, add, aTexture.iFormat == TGlTexture::ELuminance ? iTextProgram : 0);
    Flush();
    }

void CGlRenderer::DrawBatchFlat(TUint32 aRgb, TReal aAlpha)
    {
    if (aAlpha <= 0)
        {
        iQuads = 0;
        return;
        }
    float r, g, b;
    Unpack(aRgb, r, g, b);
    const float a = (float)aAlpha, keep = (float)(1 - iDim);
    const float mul[4] = { 0, 0, 0, 0 };
    const float add[4] = { r * a * keep, g * a * keep, b * a * keep, a };
    UseQuad(iWhiteTexture, mul, add);
    Flush();
    }

void CGlRenderer::PrepareVideo()
    {
    if (iVideoState != 0)
        return;
    const char* sources[2] = { KQuadVs, KVideoFs };
    const GLenum types[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    for (TInt i = 0; i < 2; i++)
        {
        iVideoShader[i] = glCreateShader(types[i]);
        glShaderSource(iVideoShader[i], 1, &sources[i], NULL);
        glCompileShader(iVideoShader[i]);
        }
    iVideoProgram = glCreateProgram();
    glAttachShader(iVideoProgram, iVideoShader[0]);
    glAttachShader(iVideoProgram, iVideoShader[1]);
    glBindAttribLocation(iVideoProgram, 0, "aPos");
    glBindAttribLocation(iVideoProgram, 1, "aUv");
    glLinkProgram(iVideoProgram);
    iVideoState = 1;
    }

TBool CGlRenderer::DrawVideo(const TGlTexture* aPlanes, TReal aX0, TReal aY0, TReal aX1, TReal aY1,
                             const TReal* aUv, TReal aAlpha)
    {
    if (iVideoState == 0)
        PrepareVideo();
    if (iVideoState == 1)
        {
        const TInt64 startUs = PerfNowUs();
        GLint vs = 0, fs = 0, linked = 0;
        glGetShaderiv(iVideoShader[0], GL_COMPILE_STATUS, &vs);
        glGetShaderiv(iVideoShader[1], GL_COMPILE_STATUS, &fs);
        glGetProgramiv(iVideoProgram, GL_LINK_STATUS, &linked);
        glDeleteShader(iVideoShader[0]);
        glDeleteShader(iVideoShader[1]);
        iVideoState = vs && fs && linked ? 2 : -1;
        TraceNow(_L("gpu: video program %d (compiled %d %d, linked %d), waited %d ms"), iVideoState, vs, fs, linked,
              (TInt)((PerfNowUs() - startUs) / 1000));
        }
    if (iVideoState != 2 || aAlpha <= 0)
        return iVideoState == 2;
    for (TInt i = 0; i < 3; i++)
        if (!aPlanes[i].iId)
            return ETrue;
    const GLuint p = iVideoProgram;
    glUseProgram(p);
    glUniform2f(U(p, "uScreen"), (GLfloat)iSize.iWidth, (GLfloat)iSize.iHeight);
    glUniform1i(U(p, "uY"), 0);
    glUniform1i(U(p, "uU"), 1);
    glUniform1i(U(p, "uV"), 2);
    glUniform1f(U(p, "uAlpha"), (GLfloat)(aAlpha * (1 - iDim)));
    for (TInt i = 2; i >= 0; i--)
        {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, aPlanes[i].iId);
        }
    AddQuad(aX0, aY0, aX1, aY1, aUv);
    Flush();
    return ETrue;
    }

void CGlRenderer::FillRect(const TRect& aRect, TUint32 aRgb, TReal aAlpha)
    {
    if (aRect.IsEmpty())
        return;
    AddQuad(aRect.iTl.iX, aRect.iTl.iY, aRect.iBr.iX, aRect.iBr.iY, 0, 0, 1, 1);
    DrawBatchFlat(aRgb, aAlpha);
    }

void CGlRenderer::DrawFrost(const TRect& aBand, TUint32 aTint, TReal aAmount, TReal aAlpha)
    {
    if (aBand.IsEmpty() || aAlpha <= 0)
        return;
    if (!iFrost)
        {
        FillRect(aBand, aTint, aAlpha);
        return;
        }
    float r, g, b;
    Unpack(aTint, r, g, b);
    const float k = (float)aAmount, a = (float)aAlpha, keep = (float)(1 - iDim) * a;
    const float mul[4] = { (1 - k) * keep, (1 - k) * keep, (1 - k) * keep, 0 };
    const float add[4] = { r * k * keep, g * k * keep, b * k * keep, a };
    UseQuad(iFrostTex, mul, add);

    const TReal w = iSize.iWidth, h = iSize.iHeight;
    AddQuad(aBand.iTl.iX, aBand.iTl.iY, aBand.iBr.iX, aBand.iBr.iY, aBand.iTl.iX / w, 1 - aBand.iTl.iY / h,
            aBand.iBr.iX / w, 1 - aBand.iBr.iY / h);
    Flush();
    }

TBool CGlRenderer::EndFrame()
    {
    if (eglSwapBuffers(iDisplay, iSurface) == EGL_FALSE)
        {
        EGLint err = eglGetError();
        if (err == EGL_BAD_ALLOC || err == EGL_BAD_NATIVE_WINDOW || err == EGL_BAD_SURFACE)
            {

            TRAPD(again, RecreateSurfaceL());
            Trace(_L("gpu: swap failed 0x%x, new surface err %d"), err, again);
            if (again == KErrNone)
                return ETrue;
            }
        if (err == EGL_CONTEXT_LOST || err == EGL_BAD_ALLOC || err == EGL_BAD_NATIVE_WINDOW || err == EGL_BAD_SURFACE)
            {
            SetError(_L("GPU context lost"), err);
            return EFalse;
            }
        }
    return ETrue;
    }
