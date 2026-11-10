#ifndef PHOTOSAPPUI_H
#define PHOTOSAPPUI_H

#include <aknappui.h>

#include "photosview.h"

class CPhotosAppUi : public CAknAppUi, public MStatusBar
    {
public:
    void ConstructL();
    ~CPhotosAppUi();

    TBool StatusBarShown();
    TInt ShowStatusBar(TBool aShow);

private:

    TInt StatusBarHeight();
    void Relayout(const TDesC& aWhy);

    void HandleCommandL(TInt aCommand);
    void HandleResourceChangeL(TInt aType);
    void HandleForegroundEventL(TBool aForeground);
    void HandleWsEventL(const TWsEvent& aEvent, CCoeControl* aDestination);
    void HandleStatusPaneSizeChange();

    CPhotosView* iView;
    TInt iStatusBarHeight;
    TInt iPaneEvents;
    };

#endif
