#ifndef GRIDLAYOUT_H
#define GRIDLAYOUT_H

#include <e32base.h>

class CLibrary;

class CGridLayout : public CBase
    {
public:
    enum TGrouping { EByYear, EByMonth, EByDay, ENoHeaders };
    class TSection
        {
    public:
        TInt iFirst;
        TInt iCount;
        TInt iTop;
        TInt iRowsTop;
        TInt iYear;
        TInt iMonth;
        TInt iDay;
        };

    static CGridLayout* NewL();
    ~CGridLayout();

    void BuildL(const CLibrary& aLibrary, const RArray<TInt>* aItems, TInt aWidth, TInt aColumns, TInt aGap,
                TGrouping aGrouping, TInt aHeaderHeight, TInt aFooterHeight, TInt aTopInset, TInt aBottomInset);

    TInt Count() const { return iCount; }
    TInt PhotoAt(TInt aItem) const { return iItems ? (*iItems)[aItem] : aItem; }

    TInt ItemOf(TInt aPhoto) const;
    TInt ContentHeight() const { return iHeight; }
    TInt Columns() const { return iColumns; }
    TInt RowHeight() const { return iRowHeight; }
    TInt CellHeight() const { return iRowHeight - iGap; }
    TInt HeaderHeight() const { return iHeaderHeight; }
    TGrouping Grouping() const { return iGrouping; }

    TInt NearestItem(const TPoint& aContent) const;
    TInt FooterTop() const { return iFooterTop; }
    TRect ItemRect(TInt aItem) const;
    TInt ItemAt(const TPoint& aContent) const;
    TInt SectionCount() const { return iSections.Count(); }
    const TSection& Section(TInt aIndex) const { return iSections[aIndex]; }

    TInt SectionAt(TInt aY) const;

    TInt ColumnX0(TInt aColumn) const { return aColumn * (iWidth + iGap) / iColumns; }
    TInt ColumnX1(TInt aColumn) const { return (aColumn + 1) * (iWidth + iGap) / iColumns - iGap; }

private:
    const RArray<TInt>* iItems;
    TInt iCount;
    TInt iWidth;
    TInt iColumns;
    TInt iGap;
    TInt iRowHeight;
    TInt iHeaderHeight;
    TGrouping iGrouping;
    TInt iFooterTop;
    TInt iHeight;
    RArray<TSection> iSections;
    };

#endif
