#include "iconshapes.h"

#include <e32math.h>

namespace
    {
    const TReal KPi = 3.14159265358979;
    TReal Length(TReal aX, TReal aY)
        {
        TReal r;
        Math::Sqrt(r, aX * aX + aY * aY);
        return r;
        }

    TReal Segment(TReal aX, TReal aY, TReal aAx, TReal aAy, TReal aBx, TReal aBy)
        {
        const TReal dx = aBx - aAx, dy = aBy - aAy;
        const TReal len2 = dx * dx + dy * dy;
        TReal t = len2 > 0 ? ((aX - aAx) * dx + (aY - aAy) * dy) / len2 : 0;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return Length(aX - aAx - t * dx, aY - aAy - t * dy);
        }

    TReal RoundBox(TReal aX, TReal aY, TReal aCx, TReal aCy, TReal aHw, TReal aHh, TReal aR)
        {
        TReal qx = (aX < aCx ? aCx - aX : aX - aCx) - aHw + aR;
        TReal qy = (aY < aCy ? aCy - aY : aY - aCy) - aHh + aR;
        const TReal outside = Length(qx > 0 ? qx : 0, qy > 0 ? qy : 0);
        const TReal inside = qx > qy ? qx : qy;
        return outside + (inside < 0 ? inside : 0) - aR;
        }

    TReal Coverage(TReal aDistance)
        {
        const TReal c = 0.5 - aDistance;
        return c < 0 ? 0 : (c > 1 ? 1 : c);
        }

    TReal Polyline(TReal aX, TReal aY, const TReal* aPoints, TInt aCount, TReal aWidth)
        {
        TReal best = 1e9;
        for (TInt i = 0; i + 1 < aCount; i++)
            {
            const TReal d = Segment(aX, aY, aPoints[2 * i], aPoints[2 * i + 1], aPoints[2 * i + 2],
                                    aPoints[2 * i + 3]);
            if (d < best)
                best = d;
            }
        return best - aWidth / 2;
        }

    TReal Heart(TReal aX, TReal aY)
        {
        const TReal unit = 13.8;
        const TReal px = (aX < 12 ? 12 - aX : aX - 12) / unit, py = (19.6 - aY) / unit;
        TReal d;
        if (px + py > 1)
            d = Length(px - 0.25, py - 0.75) - 0.3535533906;
        else
            {
            const TReal m = (px + py > 0 ? px + py : 0) * 0.5;
            const TReal toTop = px * px + (py - 1) * (py - 1), toAxis = (px - m) * (px - m) + (py - m) * (py - m);
            TReal root;
            Math::Sqrt(root, toTop < toAxis ? toTop : toAxis);
            d = px > py ? root : -root;
            }
        return d * unit;
        }

    const TInt KKeyRadius = 10;
    const TInt KKeyHeight = 40;
    const TInt KShadow = 3;

    const TInt KButtonRadius = 8;
    const TInt KButtonHeight = 32;

    TSize DesignSize(TIconId aIcon)
        {
        switch (aIcon)
            {
            case EIconCorner: return TSize(7, 7);
            case EIconScreenCorner: return TSize(12, 12);

            case EIconLibrary:
            case EIconLibraryFull:
            case EIconAlbums:
            case EIconAlbumsFull:
            case EIconBack:
            case EIconHeart:
            case EIconHeartFull:
            case EIconMore:
            case EIconEye:
            case EIconRestore:
            case EIconShare:
            case EIconInfo:
            case EIconCheck:
            case EIconPlay:
            case EIconPause:
            case EIconLoop:
            case EIconExit:
            case EIconRing: return TSize(24, 24);
            case EIconDisk: return TSize(32, 32);
            case EIconPlayLarge: return TSize(30, 30);
            case EIconButton: return TSize(2 * KButtonRadius + 1, KButtonHeight);
            case EIconGradient: return TSize(2, 64);
            case EIconChevron: return TSize(14, 14);
            case EIconKey: return TSize(2 * KKeyRadius + 1, KKeyHeight);
            case EIconKeyShadow: return TSize(2 * (KKeyRadius + KShadow) + 1, KKeyHeight + 2 * KShadow);
            default: return TSize(26, 26);
            }
        }

    TReal Ramp(TReal aY, TReal aY0, TReal aY1, TInt aFrom, TInt aTo)
        {
        const TReal t = aY <= aY0 ? 0 : (aY >= aY1 ? 1 : (aY - aY0) / (aY1 - aY0));
        return (aFrom + (aTo - aFrom) * t) / 255.0;
        }

    TReal KeyGrey(TReal aY)
        {
        if (aY < 1) return 0x26 / 255.0;
        if (aY < 2) return 0x58 / 255.0;
        if (aY < 5) return 0x3A / 255.0;
        if (aY < 14) return Ramp(aY, 5, 14, 0x30, 0x1E);
        if (aY < 22) return Ramp(aY, 14, 22, 0x1E, 0x19);
        if (aY < 37) return 0x18 / 255.0;
        return Ramp(aY, 37, 44, 0x18, 0x18 + (0x3A - 0x18) / 4);
        }

    void Key(TReal aX, TReal aY, TReal& aCoverage, TReal& aGrey)
        {
        const TReal half = KKeyRadius + 0.5;
        aCoverage = Coverage(RoundBox(aX, aY, half, KKeyHeight / 2.0, half, KKeyHeight / 2.0, KKeyRadius));
        const TReal top = 1.5, bottom = KKeyHeight - 1.0;
        const TReal inner = Coverage(RoundBox(aX, aY, half, (top + bottom) / 2, half - 1.25, (bottom - top) / 2,
                                              KKeyRadius - 1.25));
        const TReal row = aY * 45 / KKeyHeight;
        const TReal grey = KeyGrey(row);
        const TReal edge = Min(1.0, grey + Ramp(row, 0, 45, 0x40, 0x0C));
        const TReal body = inner < aCoverage ? inner : aCoverage;
        aGrey = body * grey + (aCoverage - body) * edge;
        }

    TReal KeyShadow(TReal aX, TReal aY)
        {
        const TReal half = KKeyRadius + KShadow + 0.5;
        const TReal d = RoundBox(aX, aY, half, KKeyHeight / 2.0 + KShadow, KKeyRadius + 0.5, KKeyHeight / 2.0,
                                 KKeyRadius);
        const TReal a = 0.5 - d / (2.0 * KShadow);
        return a < 0 ? 0 : (a > 1 ? 1 : a);
        }

    void Button(TReal aX, TReal aY, TReal& aCoverage, TReal& aGrey)
        {
        const TReal half = KButtonRadius + 0.5;
        const TReal d = RoundBox(aX, aY, half, KButtonHeight / 2.0, half, KButtonHeight / 2.0, KButtonRadius);
        const TReal c = Coverage(d);
        const TReal t = aY / KButtonHeight;
        TReal grey = (0x1E + (0x02 - 0x1E) * t) / 255.0;
        if (aY < 1.2)
            grey = 0x55 / 255.0;
        else if (d > -1.2)
            grey += 0x18 / 255.0;
        aCoverage = c * 0.82;
        aGrey = aCoverage * grey;
        }

    TReal Triangle(TReal aX, TReal aY, const TReal* aP)
        {
        TReal best = 1e9;
        TInt sign = 0;
        for (TInt i = 0; i < 3; i++)
            {
            const TReal* a = aP + 2 * i;
            const TReal* b = aP + 2 * ((i + 1) % 3);
            const TReal d = Segment(aX, aY, a[0], a[1], b[0], b[1]);
            if (d < best)
                best = d;
            const TReal cross = (b[0] - a[0]) * (aY - a[1]) - (b[1] - a[1]) * (aX - a[0]);
            sign += cross > 0 ? 1 : -1;
            }
        return sign == 3 || sign == -3 ? -best : best;
        }

    TReal Outline(TReal aDistance, TReal aStroke)
        {
        return Coverage((aDistance < 0 ? -aDistance : aDistance) - aStroke / 2);
        }

    TReal Shape(TIconId aIcon, TReal aX, TReal aY)
        {
        const TReal stroke = 2.75;
        switch (aIcon)
            {
            case EIconLibrary:
                {

                TReal c = Outline(RoundBox(aX, aY, 12, 12, 9, 7, 3), stroke);
                const TReal hills[] = { 6.6, 15.6, 10.2, 11.6, 13.5, 14.8, 15.4, 13, 17.6, 15.4 };
                c = Max(c, Coverage(Polyline(aX, aY, hills, 5, stroke)));
                return Max(c, Coverage(Length(aX - 16.2, aY - 8.6) - 1.6));
                }
            case EIconLibraryFull:
                {

                const TReal frame = Coverage(RoundBox(aX, aY, 12, 12, 10.4, 8.4, 3.6));
                const TReal hills[] = { 6.6, 15.6, 10.2, 11.6, 13.5, 14.8, 15.4, 13, 17.6, 15.4 };
                const TReal cut = Max(Coverage(Polyline(aX, aY, hills, 5, 2.2)), Coverage(Length(aX - 16.2, aY - 8.6) - 1.8));
                return Max(0.0, frame - cut);
                }
            case EIconAlbumsFull:
                {

                TReal c = Coverage(RoundBox(aX, aY, 10, 14.5, 8.4, 7.9, 3.2));
                const TReal behind[] = { 7, 8, 7, 4.7, 8.7, 3, 19.8, 3, 21.5, 4.7, 21.5, 15.3, 19.8, 17, 17, 17 };
                return Max(c, Coverage(Polyline(aX, aY, behind, 8, stroke)));
                }
            case EIconAlbums:
                {

                TReal c = Outline(RoundBox(aX, aY, 10, 14.5, 7, 6.5, 2.5), stroke);
                const TReal behind[] = { 7, 8, 7, 4.7, 8.7, 3, 19.8, 3, 21.5, 4.7, 21.5, 15.3, 19.8, 17, 17, 17 };
                return Max(c, Coverage(Polyline(aX, aY, behind, 8, stroke)));
                }
            case EIconBack:
                {
                const TReal chevron[] = { 13.5, 4.5, 6, 12, 13.5, 19.5 };
                return Coverage(Polyline(aX, aY, chevron, 3, stroke));
                }
            case EIconChevron:
                {
                const TReal chevron[] = { 4.5, 2.5, 9.5, 7, 4.5, 11.5 };
                return Coverage(Polyline(aX, aY, chevron, 3, 2.2));
                }
            case EIconTrash:
                {

                const TReal lid[] = { 4, 7, 22, 7 };
                const TReal handle[] = { 10, 7, 10, 4.5, 16, 4.5, 16, 7 };
                const TReal body[] = { 6.5, 7.5, 7.8, 21, 9.6, 22.5, 16.4, 22.5, 18.2, 21, 19.5, 7.5 };
                const TReal rib1[] = { 11, 11, 11, 18.5 };
                const TReal rib2[] = { 15, 11, 15, 18.5 };
                TReal c = Coverage(Polyline(aX, aY, lid, 2, stroke));
                c = Max(c, Coverage(Polyline(aX, aY, handle, 4, 2.2)));
                c = Max(c, Coverage(Polyline(aX, aY, body, 6, stroke)));
                c = Max(c, Coverage(Polyline(aX, aY, rib1, 2, 2)));
                return Max(c, Coverage(Polyline(aX, aY, rib2, 2, 2)));
                }
            case EIconHidden:
                {

                const TReal upper[] = { 3, 13, 6.5, 9.2, 10, 7.6, 13, 7.1, 16, 7.6, 19.5, 9.2, 23, 13 };
                const TReal lower[] = { 3, 13, 6.5, 16.8, 10, 18.4, 13, 18.9, 16, 18.4, 19.5, 16.8, 23, 13 };
                const TReal slash[] = { 5, 22, 21, 4 };
                TReal c = Coverage(Polyline(aX, aY, upper, 7, 2.2));
                c = Max(c, Coverage(Polyline(aX, aY, lower, 7, 2.2)));
                c = Max(c, Coverage(Length(aX - 13, aY - 13) - 3));
                return Max(c, Coverage(Polyline(aX, aY, slash, 2, stroke)));
                }
            case EIconScreenshot:
                {

                TReal c = Outline(RoundBox(aX, aY, 13, 13, 7, 10.5, 2.5), 2.4);
                const TReal tl[] = { 9.5, 9.5, 9.5, 7.5, 11.5, 7.5 };
                const TReal br[] = { 16.5, 16.5, 16.5, 18.5, 14.5, 18.5 };
                c = Max(c, Coverage(Polyline(aX, aY, tl, 3, 1.8)));
                return Max(c, Coverage(Polyline(aX, aY, br, 3, 1.8)));
                }
            case EIconHeart:
                return Outline(Heart(aX, aY), stroke);
            case EIconHeartFull:
                return Coverage(Heart(aX, aY) - stroke / 2);
            case EIconMore:

                return Max(Coverage(Length(aX - 4.5, aY - 12) - 2.3), Max(Coverage(Length(aX - 12, aY - 12) - 2.3),
                                                                       Coverage(Length(aX - 19.5, aY - 12) - 2.3)));
            case EIconVideo:
                {

                TReal c = Outline(RoundBox(aX, aY, 10.5, 13, 7, 5.5, 2), 2.4);
                const TReal lens[] = { 17.5, 11, 22.5, 8, 22.5, 18, 17.5, 15 };
                return Max(c, Coverage(Polyline(aX, aY, lens, 4, 2.2)));
                }
            case EIconEye:
                {
                const TReal upper[] = { 2, 12, 5.5, 8.2, 9, 6.6, 12, 6.1, 15, 6.6, 18.5, 8.2, 22, 12 };
                const TReal lower[] = { 2, 12, 5.5, 15.8, 9, 17.4, 12, 17.9, 15, 17.4, 18.5, 15.8, 22, 12 };
                TReal c = Coverage(Polyline(aX, aY, upper, 7, stroke));
                c = Max(c, Coverage(Polyline(aX, aY, lower, 7, stroke)));
                return Max(c, Coverage(Length(aX - 12, aY - 12) - 3));
                }
            case EIconRestore:
                {

                TReal arc[22];
                for (TInt i = 0; i < 11; i++)
                    {
                    const TReal a = (160.0 - 300.0 * i / 10) * KPi / 180;
                    TReal sn, cs;
                    Math::Sin(sn, a);
                    Math::Cos(cs, a);
                    arc[2 * i] = 12.5 + 8 * cs;
                    arc[2 * i + 1] = 13 - 8 * sn;
                    }
                const TReal head[] = { 1.8, 7.4, 5, 10.8, 9, 8.2 };
                return Max(Coverage(Polyline(aX, aY, arc, 11, stroke)), Coverage(Polyline(aX, aY, head, 3, stroke)));
                }
            case EIconShare:
                {
                const TReal tray[] = { 8, 10, 5, 10, 5, 21, 19, 21, 19, 10, 16, 10 };
                const TReal shaft[] = { 12, 2.5, 12, 15 };
                const TReal head[] = { 8, 6.5, 12, 2.5, 16, 6.5 };
                TReal c = Coverage(Polyline(aX, aY, tray, 6, stroke));
                c = Max(c, Coverage(Polyline(aX, aY, shaft, 2, stroke)));
                return Max(c, Coverage(Polyline(aX, aY, head, 3, stroke)));
                }
            case EIconInfo:
                {
                const TReal stem[] = { 12, 11, 12, 17 };
                TReal c = Outline(Length(aX - 12, aY - 12) - 9.5, 2.2);
                c = Max(c, Coverage(Length(aX - 12, aY - 7.3) - 1.5));
                return Max(c, Coverage(Polyline(aX, aY, stem, 2, 2.4)));
                }
            case EIconCheck:
                {
                const TReal tick[] = { 7, 12.5, 10.5, 16, 17, 8.5 };
                return Coverage(Polyline(aX, aY, tick, 3, 2.4));
                }
            case EIconDisk:
                return Coverage(Length(aX - 16, aY - 16) - 15.5);
            case EIconRing:
                return Outline(Length(aX - 12, aY - 12) - 10, 1.8);
            case EIconPlay:
                {
                const TReal tri[] = { 7.5, 4.5, 7.5, 19.5, 19.5, 12 };
                return Coverage(Triangle(aX, aY, tri) - 1);
                }
            case EIconPause:
                return Max(Coverage(RoundBox(aX, aY, 8, 12, 2.2, 7, 1)), Coverage(RoundBox(aX, aY, 16, 12, 2.2, 7, 1)));
            case EIconLoop:
                {

                const TReal top[] = { 4.5, 13.5, 4.5, 9.5, 6.5, 7.5, 18.5, 7.5 };
                const TReal topHead[] = { 15.5, 4.5, 18.5, 7.5, 15.5, 10.5 };
                const TReal bottom[] = { 19.5, 10.5, 19.5, 14.5, 17.5, 16.5, 5.5, 16.5 };
                const TReal bottomHead[] = { 8.5, 13.5, 5.5, 16.5, 8.5, 19.5 };
                TReal c = Coverage(Polyline(aX, aY, top, 4, 2.2));
                c = Max(c, Coverage(Polyline(aX, aY, topHead, 3, 2.2)));
                c = Max(c, Coverage(Polyline(aX, aY, bottom, 4, 2.2)));
                return Max(c, Coverage(Polyline(aX, aY, bottomHead, 3, 2.2)));
                }
            case EIconExit:
                {
                const TReal shaft[] = { 19.5, 12, 5, 12 };
                const TReal head[] = { 11.5, 5.5, 5, 12, 11.5, 18.5 };
                return Max(Coverage(Polyline(aX, aY, shaft, 2, stroke)), Coverage(Polyline(aX, aY, head, 3, stroke)));
                }
            case EIconPlayLarge:
                {
                const TReal tri[] = { 9, 5, 9, 25, 25.5, 15 };
                return Coverage(Triangle(aX, aY, tri) - 1.5);
                }
            case EIconGradient:
                {

                const TReal t = aY / 64;
                return (1 - t) * (1 - t) * (1 + 2 * t);
                }
            case EIconCorner:
            case EIconScreenCorner:
                {

                const TReal r = DesignSize(aIcon).iWidth;
                return Coverage(r - Length(r - aX, r - aY));
                }
            default:
                return 0;
            }
        }
    }

TSize IconShapes::PixelSize(TIconId aIcon, TInt aScale)
    {
    const TSize design = DesignSize(aIcon);
    return TSize(Max(1, design.iWidth * aScale / 256), Max(1, design.iHeight * aScale / 256));
    }

void IconShapes::Render(TIconId aIcon, TInt aScale, TUint8* aPixels, TInt aStride)
    {
    const TSize design = DesignSize(aIcon);
    const TSize size = PixelSize(aIcon, aScale);
    const TInt w = size.iWidth, h = size.iHeight;
    const TReal ux = (TReal)design.iWidth / w, uy = (TReal)design.iHeight / h;
    for (TInt y = 0; y < h; y++)
        for (TInt x = 0; x < w; x++)
            {

            TReal coverage = 0, grey = 0;
            for (TInt s = 0; s < 4; s++)
                {
                const TReal sx = (x + 0.25 + 0.5 * (s & 1)) * ux, sy = (y + 0.25 + 0.5 * (s >> 1)) * uy;
                if (aIcon == EIconKey || aIcon == EIconButton)
                    {
                    TReal c, g;
                    if (aIcon == EIconKey)
                        Key(sx, sy, c, g);
                    else
                        Button(sx, sy, c, g);
                    coverage += c;
                    grey += g;
                    }
                else
                    {
                    const TReal c = aIcon == EIconKeyShadow ? KeyShadow(sx, sy) : Shape(aIcon, sx, sy);
                    coverage += c;
                    grey += c;
                    }
                }

            aPixels[y * aStride + 2 * x] = (TUint8)(grey / 4 * 255 + 0.5);
            aPixels[y * aStride + 2 * x + 1] = (TUint8)(coverage / 4 * 255 + 0.5);
            }
    }
