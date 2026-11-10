#ifndef VIDEOPLAYER2_H
#define VIDEOPLAYER2_H

#include <videoplayer.h>
#include <mmf/common/mmfvideo.h>

class CVideoPlayerUtility2 : public CVideoPlayerUtility
    {
public:
    IMPORT_C static CVideoPlayerUtility2* NewL(MVideoPlayerUtilityObserver& aObserver, TInt aPriority, TInt aPref);
    IMPORT_C void AddDisplayWindowL(RWsSession& aWs, CWsScreenDevice& aScreenDevice, RWindow& aWindow,
                                    const TRect& aVideoExtent, const TRect& aWindowClipRect);
    IMPORT_C void AddDisplayWindowL(RWsSession& aWs, CWsScreenDevice& aScreenDevice, RWindow& aWindow);
    IMPORT_C void RemoveDisplayWindow(RWindow& aWindow);
    IMPORT_C void SetVideoExtentL(const RWindow& aWindow, const TRect& aVideoExtent);
    IMPORT_C void SetWindowClipRectL(const RWindow& aWindow, const TRect& aWindowClipRect);
    IMPORT_C void SetAutoScaleL(const RWindow& aWindow, TAutoScaleType aScaleType);

private:
    CVideoPlayerUtility2();
    };

#endif
