#ifndef E32STD_H
#define E32STD_H
#include <math.h>
typedef int TInt;
typedef unsigned int TUint32;
typedef unsigned short TUint16;
typedef unsigned char TUint8;
typedef double TReal;
typedef int TBool;
const TBool ETrue = 1;
const TBool EFalse = 0;
const TInt KErrNone = 0;
template <class T> inline T Max(T a, T b) { return a > b ? a : b; }
template <class T> inline T Min(T a, T b) { return a < b ? a : b; }
class TSize
    {
public:
    TSize() : iWidth(0), iHeight(0) {}
    TSize(TInt aWidth, TInt aHeight) : iWidth(aWidth), iHeight(aHeight) {}
    TInt iWidth;
    TInt iHeight;
    };
#endif
