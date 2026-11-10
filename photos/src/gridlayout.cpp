#include "gridlayout.h"

#include "library.h"

CGridLayout* CGridLayout::NewL()
    {
    return new (ELeave) CGridLayout;
    }

CGridLayout::~CGridLayout()
    {
    iSections.Close();
    }

void CGridLayout::BuildL(const CLibrary& aLibrary, const RArray<TInt>* aItems, TInt aWidth, TInt aColumns,
                         TInt aGap, TGrouping aGrouping, TInt aHeaderHeight, TInt aFooterHeight, TInt aTopInset,
                         TInt aBottomInset)
    {
    if (aGrouping == ENoHeaders)
        aHeaderHeight = 0;
    iGrouping = aGrouping;
    iSections.Reset();
    iItems = aItems;
    iCount = aItems ? aItems->Count() : aLibrary.Count();
    iWidth = aWidth;
    iColumns = Max(1, aColumns);
    iGap = aGap;
    iHeaderHeight = aHeaderHeight;

    iRowHeight = (aWidth - (iColumns - 1) * aGap) / iColumns + aGap;
    TInt y = aTopInset;
    TInt i = 0;
    while (i < iCount)
        {
        TSection section;
        TDateTime when = TTime(aLibrary.Photo(PhotoAt(i)).iTaken).DateTime();
        section.iYear = when.Year();
        section.iMonth = when.Month() + 1;
        section.iDay = when.Day() + 1;
        section.iFirst = i;
        section.iCount = 0;
        if (aHeaderHeight > 0)
            {
            while (i < iCount)
                {
                TDateTime next = TTime(aLibrary.Photo(PhotoAt(i)).iTaken).DateTime();
                if (next.Year() != section.iYear ||
                    (aGrouping != EByYear && next.Month() + 1 != section.iMonth) ||
                    (aGrouping == EByDay && next.Day() + 1 != section.iDay))
                    break;
                section.iCount++;
                i++;
                }
            }
        else
            {
            section.iCount = iCount;
            i = iCount;
            }
        section.iTop = y;
        section.iRowsTop = y + aHeaderHeight;
        const TInt rows = (section.iCount + iColumns - 1) / iColumns;
        y = section.iRowsTop + rows * iRowHeight;
        iSections.AppendL(section);
        }
    iFooterTop = y;
    iHeight = y + aFooterHeight + aBottomInset;
    }

TInt CGridLayout::SectionAt(TInt aY) const
    {
    TInt lo = 0, hi = iSections.Count() - 1, found = 0;
    while (lo <= hi)
        {
        const TInt mid = (lo + hi) / 2;
        if (iSections[mid].iTop <= aY)
            {
            found = mid;
            lo = mid + 1;
            }
        else
            hi = mid - 1;
        }
    return found;
    }

TInt CGridLayout::ItemOf(TInt aPhoto) const
    {
    if (!iItems)
        return aPhoto >= 0 && aPhoto < iCount ? aPhoto : -1;

    TInt lo = 0, hi = iCount - 1;
    while (lo <= hi)
        {
        const TInt mid = (lo + hi) / 2;
        const TInt at = (*iItems)[mid];
        if (at == aPhoto)
            return mid;
        if (at < aPhoto)
            lo = mid + 1;
        else
            hi = mid - 1;
        }
    return -1;
    }

TRect CGridLayout::ItemRect(TInt aItem) const
    {
    if (aItem < 0 || aItem >= iCount || iSections.Count() == 0)
        return TRect();

    TInt lo = 0, hi = iSections.Count() - 1, s = 0;
    while (lo <= hi)
        {
        const TInt mid = (lo + hi) / 2;
        if (iSections[mid].iFirst <= aItem)
            {
            s = mid;
            lo = mid + 1;
            }
        else
            hi = mid - 1;
        }
    const TSection& section = iSections[s];
    const TInt k = aItem - section.iFirst;
    const TInt column = k % iColumns;
    const TInt top = section.iRowsTop + (k / iColumns) * iRowHeight;
    return TRect(ColumnX0(column), top, ColumnX1(column), top + iRowHeight - iGap);
    }

TInt CGridLayout::ItemAt(const TPoint& aContent) const
    {
    if (iSections.Count() == 0)
        return -1;
    const TSection& section = iSections[SectionAt(aContent.iY)];
    if (aContent.iY < section.iRowsTop)
        return -1;
    const TInt row = (aContent.iY - section.iRowsTop) / iRowHeight;
    TInt column = -1;
    for (TInt c = 0; c < iColumns && column < 0; c++)
        if (aContent.iX >= ColumnX0(c) && aContent.iX < ColumnX1(c) + iGap)
            column = c;
    const TInt k = row * iColumns + column;
    if (column < 0 || k >= section.iCount)
        return -1;
    return section.iFirst + k;
    }

TInt CGridLayout::NearestItem(const TPoint& aContent) const
    {
    if (iCount == 0 || iSections.Count() == 0)
        return -1;
    const TSection& section = iSections[SectionAt(aContent.iY)];
    const TInt rows = (section.iCount + iColumns - 1) / iColumns;
    TInt row = (aContent.iY - section.iRowsTop) / iRowHeight;
    row = row < 0 ? 0 : (row >= rows ? rows - 1 : row);
    TInt column = aContent.iX * iColumns / Max(1, iWidth);
    column = column < 0 ? 0 : (column >= iColumns ? iColumns - 1 : column);
    return section.iFirst + Min(section.iCount - 1, row * iColumns + column);
    }
