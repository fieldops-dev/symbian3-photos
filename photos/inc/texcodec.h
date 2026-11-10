#ifndef TEXCODEC_H
#define TEXCODEC_H

#include <e32def.h>
#include <e32const.h>

inline TInt Etc1Bytes(TInt aWidth, TInt aHeight) { return aWidth * aHeight / 2; }

void Etc1Encode(const TUint16* aPixels, TInt aWidth, TInt aHeight, TUint8* aOut);

void HalveRgb565(const TUint16* aPixels, TInt aSide, TUint16* aOut);

#endif
