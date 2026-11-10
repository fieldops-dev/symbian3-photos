#ifndef E32MATH_H
#define E32MATH_H
#include "e32std.h"
class Math
    {
public:
    static TInt Sqrt(TReal& aResult, const TReal& aValue) { aResult = sqrt(aValue); return KErrNone; }
    static TInt Sin(TReal& aResult, const TReal& aValue) { aResult = sin(aValue); return KErrNone; }
    static TInt Cos(TReal& aResult, const TReal& aValue) { aResult = cos(aValue); return KErrNone; }
    static TInt Pow(TReal& aResult, const TReal& aX, const TReal& aY) { aResult = pow(aX, aY); return KErrNone; }
    static TInt ATan(TReal& aResult, const TReal& aY, const TReal& aX) { aResult = atan2(aY, aX); return KErrNone; }
    };
#endif
