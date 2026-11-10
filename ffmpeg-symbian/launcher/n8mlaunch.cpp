#include <aknapp.h>
#include <aknappui.h>
#include <akndoc.h>
#include <aknnotewrappers.h>
#include <eikstart.h>

namespace
    {
    class CLaunchAppUi : public CAknAppUi
        {
    public:
        ~CLaunchAppUi()
            {
            delete iIdle;
            }

        void ConstructL()
            {
            BaseConstructL(EAknEnableSkin);
            iIdle = CIdle::NewL(CActive::EPriorityStandard);
            iIdle->Start(TCallBack(Launch, this));
            }

        void HandleCommandL(TInt aCommand)
            {
            if (aCommand == EEikCmdExit || aCommand == EAknSoftkeyExit)
                Exit();
            }

    private:
        static TInt Launch(TAny* aSelf)
            {
            CLaunchAppUi* self = static_cast<CLaunchAppUi*>(aSelf);
            RProcess bench;
            const TInt err = bench.Create(_L("n8mbench.exe"), KNullDesC);
            if (err == KErrNone)
                {
                bench.Resume();
                bench.Close();
                }
            else
                {
                TBuf<64> text;
                text.Format(_L("The benchmark didn't start: %d"), err);
                CAknErrorNote* note = new CAknErrorNote(ETrue);
                if (note)
                    TRAP_IGNORE(note->ExecuteLD(text));
                }
            self->Exit();
            return EFalse;
            }

        CIdle* iIdle;
        };

    class CLaunchDocument : public CAknDocument
        {
    public:
        CLaunchDocument(CEikApplication& aApp) : CAknDocument(aApp) {}
        CEikAppUi* CreateAppUiL() { return new (ELeave) CLaunchAppUi; }
        };

    class CLaunchApplication : public CAknApplication
        {
        TUid AppDllUid() const { return TUid::Uid(0xE4E4700B); }
        CApaDocument* CreateDocumentL() { return new (ELeave) CLaunchDocument(*this); }
        };

    CApaApplication* NewApplication()
        {
        return new CLaunchApplication;
        }
    }

GLDEF_C TInt E32Main()
    {
    return EikStart::RunApplication(NewApplication);
    }
