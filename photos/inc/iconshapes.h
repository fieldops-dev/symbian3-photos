#ifndef ICONSHAPES_H
#define ICONSHAPES_H

#include <e32std.h>

enum TIconId
    {
    EIconLibrary,
    EIconLibraryFull,
    EIconAlbums,
    EIconAlbumsFull,
    EIconBack,
    EIconHeart,
    EIconHeartFull,
    EIconMore,
    EIconCorner,
    EIconScreenCorner,
    EIconKey,
    EIconKeyShadow,
    EIconChevron,
    EIconTrash,
    EIconHidden,
    EIconScreenshot,
    EIconVideo,
    EIconEye,
    EIconRestore,
    EIconShare,
    EIconInfo,
    EIconCheck,
    EIconDisk,
    EIconRing,
    EIconPlay,
    EIconPause,
    EIconLoop,
    EIconExit,
    EIconPlayLarge,
    EIconButton,
    EIconGradient,
    EIconCount
    };

namespace IconShapes
    {

    TSize PixelSize(TIconId aIcon, TInt aScale);

    void Render(TIconId aIcon, TInt aScale, TUint8* aPixels, TInt aStride);
    }

#endif
