#include <aknapp.h>
#include <akndoc.h>
#include <eikstart.h>

#include "photosappui.h"

namespace
    {
    class CPhotosDocument : public CAknDocument
        {
    public:
        CPhotosDocument(CEikApplication& aApp) : CAknDocument(aApp) {}
        CEikAppUi* CreateAppUiL() { return new (ELeave) CPhotosAppUi; }
        };

    class CPhotosApplication : public CAknApplication
        {
        TUid AppDllUid() const { return TUid::Uid(0xE4E47003); }
        CApaDocument* CreateDocumentL() { return new (ELeave) CPhotosDocument(*this); }
        };

    CApaApplication* NewApplication()
        {
        return new CPhotosApplication;
        }
    }

GLDEF_C TInt E32Main()
    {
    return EikStart::RunApplication(NewApplication);
    }
