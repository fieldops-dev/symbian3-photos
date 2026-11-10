#ifndef EXIF_H
#define EXIF_H

#include <e32def.h>
#include <e32const.h>

class TImageHeader
    {
public:
    enum { KTextLength = 32 };
    void Reset();

    TInt iWidth;
    TInt iHeight;
    TInt iOrientation;
    TBool iHasDate;
    TInt iYear;
    TInt iMonth;
    TInt iDay;
    TInt iHour;
    TInt iMinute;
    TInt iSecond;

    TUint8 iMake[KTextLength];
    TInt iMakeLength;
    TUint8 iModel[KTextLength];
    TInt iModelLength;
    TUint32 iExposure[2];
    TUint32 iFNumber[2];
    TUint32 iFocal[2];
    TInt iIso;
    TInt iFlash;

    TBool iHasPosition;
    TReal iLatitude;
    TReal iLongitude;

    TInt iThumbOffset;
    TInt iThumbLength;
    };

enum TImageFormat
    {
    EFormatUnknown, EFormatJpeg, EFormatPng, EFormatGif, EFormatBmp
    };

TImageFormat ParseImageHeader(const TUint8* aData, TInt aLength, TImageHeader& aHeader);

void OrientedToStored(TInt aOrientation, TReal aU, TReal aV, TReal& aS, TReal& aT);

inline TBool SwapsAxes(TInt aOrientation) { return aOrientation >= 5 && aOrientation <= 8; }

#endif
