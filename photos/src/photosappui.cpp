#include "photosappui.h"

#include <AknDef.h>
#include <apgtask.h>
#include <avkon.hrh>
#include <avkon.rsg>
#include <eikbtgpc.h>
#include <eikspane.h>

#include "n8mlog.h"
#include "photosview.h"
#include "trace.h"

namespace
    {

    const TInt KGoomMemoryLowEvent = 0x10282DBF;
    const TInt KGoomMemoryGoodEvent = 0x20026790;
    }

void CPhotosAppUi::ConstructL()
    {
    TraceReset();
    Trace(_L("appui: start"));
    const TInt64 startUs = PerfNowUs();
    N8mInstallL();
    BaseConstructL(EAknEnableSkin | EAppOrientationAutomatic);

    if (StatusPane())
        {
        TRAP_IGNORE(StatusPane()->SwitchLayoutL(R_AVKON_STATUS_PANE_LAYOUT_USUAL_FLAT));
        StatusPane()->MakeVisible(ETrue);
        }
    if (Cba())
        Cba()->MakeVisible(EFalse);

    const TRect app = ApplicationRect();
    iStatusBarHeight = StatusBarHeight();
    Trace(_L("appui: application rect %d,%d %dx%d, client rect %d,%d %dx%d, status bar %d px"), app.iTl.iX,
          app.iTl.iY, app.Width(), app.Height(), ClientRect().iTl.iX, ClientRect().iTl.iY, ClientRect().Width(),
          ClientRect().Height(), iStatusBarHeight);
    iView = CPhotosView::NewL(app, iStatusBarHeight, *this);
    AddToStackL(iView);
    Trace(_L("appui: constructed in %d ms"), (TInt)((PerfNowUs() - startUs) / 1000));
    TraceMemory(_L("after start"));
    }

CPhotosAppUi::~CPhotosAppUi()
    {
    struct TCloseLog
        {
        ~TCloseLog() { TraceClose(); }
        } closeLog;
    Trace(_L("appui: exit"));
    if (iView)
        {
        RemoveFromStack(iView);
        delete iView;
        }
    N8mUninstall();
    }

void CPhotosAppUi::HandleCommandL(TInt aCommand)
    {
    if (aCommand == EEikCmdExit || aCommand == EAknSoftkeyExit || aCommand == EAknSoftkeyBack)
        Exit();
    else if (iView)
        iView->HandleCommandL(aCommand);
    }

void CPhotosAppUi::HandleResourceChangeL(TInt aType)
    {
    CAknAppUi::HandleResourceChangeL(aType);
    if (iView && aType == KEikDynamicLayoutVariantSwitch)
        Relayout(_L("layout switch"));
    }

void CPhotosAppUi::HandleWsEventL(const TWsEvent& aEvent, CCoeControl* aDestination)
    {
    const TInt type = aEvent.Type();
    if (type == KGoomMemoryLowEvent)
        {
        if (iView)
            iView->GpuMemoryLow();
        }
    else if (type == KGoomMemoryGoodEvent)
        Trace(_L("goom: graphics memory good again"));
    else if (type == EEventUser)
        {

        const TInt32* data = reinterpret_cast<const TInt32*>(aEvent.EventData());
        if (data[0] == EApaSystemEventShutdown && data[1] == KGoomMemoryLowEvent)
            {
            Trace(_L("goom: looking for apps to close: GPU freed, probe handled"));
            if (iView)
                iView->GpuMemoryLow();
            return;
            }
        }
    if (iView && aEvent.Type() == KAknFullOrPartialForegroundLost)
        {
        Trace(_L("appui: fully hidden"));
        iView->SetVisible(EFalse);
        }
    else if (iView && aEvent.Type() == KAknFullOrPartialForegroundGained)
        {
        Trace(_L("appui: partly visible"));
        iView->SetVisible(ETrue);
        }
    CAknAppUi::HandleWsEventL(aEvent, aDestination);
    }

void CPhotosAppUi::HandleStatusPaneSizeChange()
    {
    iPaneEvents++;
    if (iView)
        Relayout(_L("status pane size event"));
    }

void CPhotosAppUi::Relayout(const TDesC& aWhy)
    {
    const TRect app = ApplicationRect();
    const TInt height = StatusBarHeight();
    const TInt sizes = iView->SizeEvents();
    const TBool changed = app != iView->Rect() || height != iStatusBarHeight;
    iStatusBarHeight = height;
    iView->SetLayout(app, height);
    Trace(_L("appui: %S, status bar shown %d, %d px; view %dx%d%s, view size events %d"), &aWhy, StatusBarShown(),
          height, app.Width(), app.Height(), changed ? _S(" (changed)") : _S(" kept"), iView->SizeEvents() - sizes);
    }

TInt CPhotosAppUi::StatusBarHeight()
    {
    CEikStatusPane* pane = StatusPane();
    if (!pane)
        return 0;
    const TRect app = ApplicationRect();
    if (pane->IsVisible())
        return Max(0, ClientRect().iTl.iY - app.iTl.iY);
    TRect rest = app;
    pane->ReduceRect(rest);
    if (rest.iTl.iY > app.iTl.iY)
        return rest.iTl.iY - app.iTl.iY;
    return iStatusBarHeight;
    }

TBool CPhotosAppUi::StatusBarShown()
    {
    return StatusPane() && StatusPane()->IsVisible();
    }

TInt CPhotosAppUi::ShowStatusBar(TBool aShow)
    {
    CEikStatusPane* pane = StatusPane();
    if (!pane)
        return 0;
    const TInt before = iPaneEvents;
    pane->MakeVisible(aShow);
    return iPaneEvents - before;
    }

void CPhotosAppUi::HandleForegroundEventL(TBool aForeground)
    {
    Trace(_L("appui: foreground %d"), aForeground);
    CAknAppUi::HandleForegroundEventL(aForeground);
    if (iView)
        iView->SetForeground(aForeground);
    }
