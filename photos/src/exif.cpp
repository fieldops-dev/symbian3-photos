#include "exif.h"

namespace
    {
    class TReader
        {
    public:
        TReader(const TUint8* aData, TInt aLength, TBool aBigEndian)
            : iData(aData), iLength(aLength), iBig(aBigEndian) {}
        TBool Has(TInt aOffset, TInt aBytes) const
            {
            return aOffset >= 0 && aBytes >= 0 && aOffset <= iLength - aBytes;
            }
        TUint U16(TInt aOffset) const
            {
            const TUint8* p = iData + aOffset;
            return iBig ? (p[0] << 8) | p[1] : (p[1] << 8) | p[0];
            }
        TUint32 U32(TInt aOffset) const
            {
            const TUint8* p = iData + aOffset;
            return iBig ? ((TUint32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]
                        : ((TUint32)p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
            }
        TReal Rational(TInt aOffset) const
            {
            const TUint32 d = U32(aOffset + 4);
            return d ? (TReal)U32(aOffset) / d : 0;
            }
        const TUint8* iData;
        TInt iLength;
        TBool iBig;
        };

    TInt Digits(const TUint8* aText, TInt aCount, TBool& aOk)
        {
        TInt value = 0;
        for (TInt i = 0; i < aCount; i++)
            {
            if (aText[i] < '0' || aText[i] > '9')
                {
                aOk = EFalse;
                return 0;
                }
            value = value * 10 + (aText[i] - '0');
            }
        return value;
        }

    TBool ParseDate(const TUint8* aText, TImageHeader& aHeader)
        {
        TBool ok = ETrue;
        const TInt year = Digits(aText, 4, ok), month = Digits(aText + 5, 2, ok), day = Digits(aText + 8, 2, ok);
        const TInt hour = Digits(aText + 11, 2, ok), minute = Digits(aText + 14, 2, ok);
        const TInt second = Digits(aText + 17, 2, ok);
        if (!ok || year < 1980 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
            minute > 59 || second > 60)
            return EFalse;
        aHeader.iHasDate = ETrue;
        aHeader.iYear = year;
        aHeader.iMonth = month;
        aHeader.iDay = day;
        aHeader.iHour = hour;
        aHeader.iMinute = minute;
        aHeader.iSecond = second;
        return ETrue;
        }

    class TExifScan
        {
    public:
        TInt iDates[3];
        TInt iExifIfd;
        TInt iGpsIfd;
        TBool iNorth;
        TBool iEast;
        TInt iLatitude;
        TInt iLongitude;
        };

    void Text(const TReader& aTiff, TInt aEntry, TUint32 aCount, TUint8* aText, TInt& aLength)
        {
        const TInt at = aCount <= 4 ? aEntry + 8 : (TInt)aTiff.U32(aEntry + 8);
        aLength = 0;
        if (!aTiff.Has(at, (TInt)aCount))
            return;
        for (TUint32 i = 0; i < aCount && aLength < TImageHeader::KTextLength; i++)
            {
            const TUint8 c = aTiff.iData[at + i];
            if (c == 0)
                break;
            aText[aLength++] = c;
            }
        while (aLength > 0 && aText[aLength - 1] == ' ')
            aLength--;
        }

    void Fraction(const TReader& aTiff, TInt aEntry, TUint32* aOut)
        {
        const TInt at = (TInt)aTiff.U32(aEntry + 8);
        if (aTiff.Has(at, 8))
            {
            aOut[0] = aTiff.U32(at);
            aOut[1] = aTiff.U32(at + 4);
            }
        }

    TInt ParseIfd(const TReader& aTiff, TInt aOffset, TBool aGps, TImageHeader& aHeader, TExifScan& aScan)
        {
        if (!aTiff.Has(aOffset, 2))
            return 0;
        const TInt count = aTiff.U16(aOffset);
        for (TInt i = 0; i < count && i < 512; i++)
            {
            const TInt entry = aOffset + 2 + i * 12;
            if (!aTiff.Has(entry, 12))
                return 0;
            const TUint tag = aTiff.U16(entry);
            const TUint type = aTiff.U16(entry + 2);
            const TUint32 n = aTiff.U32(entry + 4);

            const TUint32 value = type == 3 ? aTiff.U16(entry + 8) : aTiff.U32(entry + 8);
            if (aGps)
                {
                switch (tag)
                    {
                    case 1: aScan.iNorth = aTiff.iData[entry + 8] != 'S'; break;
                    case 2: if (n == 3) aScan.iLatitude = (TInt)value; break;
                    case 3: aScan.iEast = aTiff.iData[entry + 8] != 'W'; break;
                    case 4: if (n == 3) aScan.iLongitude = (TInt)value; break;
                    default: break;
                    }
                continue;
                }
            switch (tag)
                {
                case 0x0112:
                    if (value >= 1 && value <= 8)
                        aHeader.iOrientation = (TInt)value;
                    break;
                case 0x010F:
                    if (type == 2)
                        Text(aTiff, entry, n, aHeader.iMake, aHeader.iMakeLength);
                    break;
                case 0x0110:
                    if (type == 2)
                        Text(aTiff, entry, n, aHeader.iModel, aHeader.iModelLength);
                    break;
                case 0x8769:
                    aScan.iExifIfd = (TInt)value;
                    break;
                case 0x8825:
                    aScan.iGpsIfd = (TInt)value;
                    break;
                case 0x9003:
                case 0x9004:
                case 0x0132:
                    if (type == 2 && n >= 19)
                        aScan.iDates[tag == 0x9003 ? 0 : (tag == 0x9004 ? 1 : 2)] = (TInt)value;
                    break;
                case 0x829A:
                    Fraction(aTiff, entry, aHeader.iExposure);
                    break;
                case 0x829D:
                    Fraction(aTiff, entry, aHeader.iFNumber);
                    break;
                case 0x920A:
                    Fraction(aTiff, entry, aHeader.iFocal);
                    break;
                case 0x8827:
                    aHeader.iIso = (TInt)value;
                    break;
                case 0x9209:
                    aHeader.iFlash = (TInt)value;
                    break;
                case 0xA002:
                    if (aHeader.iWidth == 0)
                        aHeader.iWidth = (TInt)value;
                    break;
                case 0xA003:
                    if (aHeader.iHeight == 0)
                        aHeader.iHeight = (TInt)value;
                    break;
                case 0x0201:
                    aHeader.iThumbOffset = (TInt)value;
                    break;
                case 0x0202:
                    aHeader.iThumbLength = (TInt)value;
                    break;
                default:
                    break;
                }
            }
        const TInt next = aOffset + 2 + count * 12;
        return aTiff.Has(next, 4) ? (TInt)aTiff.U32(next) : 0;
        }

    TReal Degrees(const TReader& aTiff, TInt aOffset)
        {
        return aTiff.Rational(aOffset) + aTiff.Rational(aOffset + 8) / 60 + aTiff.Rational(aOffset + 16) / 3600;
        }

    void ParseExif(const TUint8* aData, TInt aLength, TInt aFileOffset, TImageHeader& aHeader)
        {
        if (aLength < 14 || aData[0] != 'E' || aData[1] != 'x' || aData[2] != 'i' || aData[3] != 'f')
            return;
        const TUint8* tiff = aData + 6;
        const TInt length = aLength - 6;
        TBool big;
        if (tiff[0] == 'I' && tiff[1] == 'I')
            big = EFalse;
        else if (tiff[0] == 'M' && tiff[1] == 'M')
            big = ETrue;
        else
            return;
        TReader r(tiff, length, big);
        if (r.U16(2) != 42)
            return;
        TExifScan scan;
        scan.iDates[0] = scan.iDates[1] = scan.iDates[2] = 0;
        scan.iExifIfd = scan.iGpsIfd = scan.iLatitude = scan.iLongitude = 0;
        scan.iNorth = scan.iEast = ETrue;

        const TInt ifd1 = ParseIfd(r, (TInt)r.U32(4), EFalse, aHeader, scan);
        aHeader.iThumbOffset = aHeader.iThumbLength = 0;
        if (ifd1 > 0)
            ParseIfd(r, ifd1, EFalse, aHeader, scan);
        if (aHeader.iThumbLength > 0 && aHeader.iThumbOffset > 0)
            aHeader.iThumbOffset += aFileOffset + 6;
        else
            aHeader.iThumbOffset = aHeader.iThumbLength = 0;

        if (scan.iExifIfd > 0)
            ParseIfd(r, scan.iExifIfd, EFalse, aHeader, scan);
        if (scan.iGpsIfd > 0)
            ParseIfd(r, scan.iGpsIfd, ETrue, aHeader, scan);
        if (scan.iLatitude > 0 && scan.iLongitude > 0 && r.Has(scan.iLatitude, 24) && r.Has(scan.iLongitude, 24))
            {
            aHeader.iHasPosition = ETrue;
            aHeader.iLatitude = Degrees(r, scan.iLatitude) * (scan.iNorth ? 1 : -1);
            aHeader.iLongitude = Degrees(r, scan.iLongitude) * (scan.iEast ? 1 : -1);
            }
        for (TInt i = 0; i < 3 && !aHeader.iHasDate; i++)
            if (scan.iDates[i] > 0 && r.Has(scan.iDates[i], 19))
                ParseDate(tiff + scan.iDates[i], aHeader);
        }

    void ParseJpeg(const TUint8* aData, TInt aLength, TImageHeader& aHeader)
        {
        TInt pos = 2;
        TInt exifWidth = 0, exifHeight = 0;
        TBool frame = EFalse;
        while (pos + 4 <= aLength)
            {
            if (aData[pos] != 0xFF)
                return;
            const TUint8 marker = aData[pos + 1];
            if (marker == 0xFF)
                {
                pos++;
                continue;
                }
            if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01)
                {
                pos += 2;
                continue;
                }
            if (marker == 0xD9 || marker == 0xDA)
                break;
            const TInt segment = (aData[pos + 2] << 8) | aData[pos + 3];
            const TInt body = pos + 4;
            const TInt bodyLength = segment - 2;
            if (segment < 2)
                return;
            if (marker == 0xE1 && !frame)
                {
                ParseExif(aData + body, bodyLength < aLength - body ? bodyLength : aLength - body, body, aHeader);
                exifWidth = aHeader.iWidth;
                exifHeight = aHeader.iHeight;
                }
            else if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC &&
                     body + 5 <= aLength)
                {
                aHeader.iHeight = (aData[body + 1] << 8) | aData[body + 2];
                aHeader.iWidth = (aData[body + 3] << 8) | aData[body + 4];
                frame = ETrue;
                break;
                }
            pos = body + bodyLength;
            }
        if (!frame || aHeader.iWidth == 0 || aHeader.iHeight == 0)
            {
            aHeader.iWidth = exifWidth;
            aHeader.iHeight = exifHeight;
            }
        }

    TUint32 Be32(const TUint8* p) { return ((TUint32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
    TUint32 Le32(const TUint8* p) { return ((TUint32)p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0]; }
    }

void TImageHeader::Reset()
    {
    iWidth = iHeight = 0;
    iOrientation = 1;
    iHasDate = EFalse;
    iYear = iMonth = iDay = iHour = iMinute = iSecond = 0;
    iMakeLength = iModelLength = 0;
    iExposure[0] = iExposure[1] = iFNumber[0] = iFNumber[1] = iFocal[0] = iFocal[1] = 0;
    iIso = iFlash = -1;
    iHasPosition = EFalse;
    iLatitude = iLongitude = 0;
    iThumbOffset = iThumbLength = 0;
    }

TImageFormat ParseImageHeader(const TUint8* aData, TInt aLength, TImageHeader& aHeader)
    {
    aHeader.Reset();
    if (aLength >= 4 && aData[0] == 0xFF && aData[1] == 0xD8)
        {
        ParseJpeg(aData, aLength, aHeader);
        return EFormatJpeg;
        }
    if (aLength >= 24 && aData[0] == 0x89 && aData[1] == 'P' && aData[2] == 'N' && aData[3] == 'G')
        {
        if (aData[12] == 'I' && aData[13] == 'H' && aData[14] == 'D' && aData[15] == 'R')
            {
            aHeader.iWidth = (TInt)Be32(aData + 16);
            aHeader.iHeight = (TInt)Be32(aData + 20);
            }
        return EFormatPng;
        }
    if (aLength >= 10 && aData[0] == 'G' && aData[1] == 'I' && aData[2] == 'F' && aData[3] == '8')
        {
        aHeader.iWidth = aData[6] | (aData[7] << 8);
        aHeader.iHeight = aData[8] | (aData[9] << 8);
        return EFormatGif;
        }
    if (aLength >= 26 && aData[0] == 'B' && aData[1] == 'M')
        {
        const TUint32 info = Le32(aData + 14);
        if (info == 12)
            {
            aHeader.iWidth = aData[18] | (aData[19] << 8);
            aHeader.iHeight = aData[20] | (aData[21] << 8);
            }
        else if (info >= 40)
            {
            const TInt height = (TInt)Le32(aData + 22);
            aHeader.iWidth = (TInt)Le32(aData + 18);
            aHeader.iHeight = height < 0 ? -height : height;
            }
        return EFormatBmp;
        }
    return EFormatUnknown;
    }

void OrientedToStored(TInt aOrientation, TReal aU, TReal aV, TReal& aS, TReal& aT)
    {
    switch (aOrientation)
        {
        case 2: aS = 1 - aU; aT = aV; break;
        case 3: aS = 1 - aU; aT = 1 - aV; break;
        case 4: aS = aU; aT = 1 - aV; break;
        case 5: aS = aV; aT = aU; break;
        case 6: aS = aV; aT = 1 - aU; break;
        case 7: aS = 1 - aV; aT = 1 - aU; break;
        case 8: aS = 1 - aV; aT = aU; break;
        default: aS = aU; aT = aV; break;
        }
    }
