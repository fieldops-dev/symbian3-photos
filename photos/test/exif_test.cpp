#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>

#include "exif.h"

namespace
{
int failures = 0;

void Check(bool aOk, const char* aWhat)
{
    if (!aOk)
    {
        std::printf("FAIL: %s\n", aWhat);
        failures++;
    }
}

void Put16(std::vector<unsigned char>& b, unsigned v, bool big)
{
    if (big) { b.push_back(v >> 8); b.push_back(v & 0xFF); }
    else { b.push_back(v & 0xFF); b.push_back(v >> 8); }
}

void Put32(std::vector<unsigned char>& b, unsigned v, bool big)
{
    if (big) { Put16(b, v >> 16, true); Put16(b, v & 0xFFFF, true); }
    else { Put16(b, v & 0xFFFF, false); Put16(b, v >> 16, false); }
}

std::vector<unsigned char> Jpeg(bool big, unsigned orientation, const char* date, int w, int h)
{
    std::vector<unsigned char> tiff;
    tiff.push_back(big ? 'M' : 'I');
    tiff.push_back(big ? 'M' : 'I');
    Put16(tiff, 42, big);
    Put32(tiff, 8, big);

    Put16(tiff, 2, big);
    Put16(tiff, 0x0112, big); Put16(tiff, 3, big); Put32(tiff, 1, big);
    Put16(tiff, orientation, big); Put16(tiff, 0, big);
    Put16(tiff, 0x8769, big); Put16(tiff, 4, big); Put32(tiff, 1, big); Put32(tiff, 38, big);
    Put32(tiff, 0, big);

    Put16(tiff, 3, big);
    Put16(tiff, 0x9003, big); Put16(tiff, 2, big); Put32(tiff, 20, big); Put32(tiff, 80, big);
    Put16(tiff, 0xA002, big); Put16(tiff, 4, big); Put32(tiff, 1, big); Put32(tiff, 111, big);
    Put16(tiff, 0xA003, big); Put16(tiff, 4, big); Put32(tiff, 1, big); Put32(tiff, 222, big);
    Put32(tiff, 0, big);
    for (int i = 0; i < 20; i++)
        tiff.push_back(i < 19 ? date[i] : 0);

    std::vector<unsigned char> j;
    j.push_back(0xFF); j.push_back(0xD8);
    j.push_back(0xFF); j.push_back(0xE1);
    Put16(j, 2 + 6 + tiff.size(), true);
    const char exif[6] = { 'E', 'x', 'i', 'f', 0, 0 };
    j.insert(j.end(), exif, exif + 6);
    j.insert(j.end(), tiff.begin(), tiff.end());

    j.push_back(0xFF); j.push_back(0xDB); Put16(j, 4, true); j.push_back(0); j.push_back(0);
    j.push_back(0xFF); j.push_back(0xC0); Put16(j, 11, true);
    j.push_back(8); Put16(j, h, true); Put16(j, w, true);
    j.push_back(1); j.push_back(1); j.push_back(0x11); j.push_back(0);
    j.push_back(0xFF); j.push_back(0xDA);
    return j;
}

struct Entry
{
    unsigned tag, type, count;
    std::vector<unsigned char> data;
};

std::vector<unsigned char> Bytes16(unsigned v, bool big) { std::vector<unsigned char> b; Put16(b, v, big); return b; }
std::vector<unsigned char> Bytes32(unsigned v, bool big) { std::vector<unsigned char> b; Put32(b, v, big); return b; }
std::vector<unsigned char> Ascii(const char* text)
{
    std::vector<unsigned char> b(text, text + std::strlen(text) + 1);
    return b;
}
std::vector<unsigned char> Rationals(const unsigned* v, int n, bool big)
{
    std::vector<unsigned char> b;
    for (int i = 0; i < 2 * n; i++)
        Put32(b, v[i], big);
    return b;
}

size_t PutIfd(std::vector<unsigned char>& aTiff, const std::vector<Entry>& aEntries, bool big)
{
    const size_t start = aTiff.size();
    size_t data = start + 2 + aEntries.size() * 12 + 4;
    std::vector<unsigned char> area;
    Put16(aTiff, aEntries.size(), big);
    for (size_t i = 0; i < aEntries.size(); i++)
    {
        const Entry& e = aEntries[i];
        Put16(aTiff, e.tag, big);
        Put16(aTiff, e.type, big);
        Put32(aTiff, e.count, big);
        if (e.data.size() <= 4)
        {
            std::vector<unsigned char> v = e.data;
            v.resize(4, 0);
            aTiff.insert(aTiff.end(), v.begin(), v.end());
        }
        else
        {
            Put32(aTiff, data + area.size(), big);
            area.insert(area.end(), e.data.begin(), e.data.end());
        }
    }
    const size_t next = aTiff.size();
    Put32(aTiff, 0, big);
    aTiff.insert(aTiff.end(), area.begin(), area.end());
    return next;
}

void Patch32(std::vector<unsigned char>& b, size_t at, unsigned v, bool big)
{
    std::vector<unsigned char> x = Bytes32(v, big);
    std::copy(x.begin(), x.end(), b.begin() + at);
}

std::vector<unsigned char> CameraJpeg(bool big, int& aPreviewAt)
{
    std::vector<unsigned char> tiff;
    tiff.push_back(big ? 'M' : 'I');
    tiff.push_back(big ? 'M' : 'I');
    Put16(tiff, 42, big);
    Put32(tiff, 8, big);
    std::vector<Entry> ifd0;
    Entry make = { 0x010F, 2, 6, Ascii("Nokia") };
    Entry model = { 0x0110, 2, 3, Ascii("N8") };
    Entry orientation = { 0x0112, 3, 1, Bytes16(1, big) };
    Entry exif = { 0x8769, 4, 1, Bytes32(0, big) };
    Entry gps = { 0x8825, 4, 1, Bytes32(0, big) };
    ifd0.push_back(make); ifd0.push_back(model); ifd0.push_back(orientation); ifd0.push_back(exif); ifd0.push_back(gps);
    const size_t ifd0At = tiff.size();
    const size_t next0 = PutIfd(tiff, ifd0, big);

    const size_t exifValue = ifd0At + 2 + 3 * 12 + 8, gpsValue = ifd0At + 2 + 4 * 12 + 8;

    Patch32(tiff, next0, tiff.size(), big);
    std::vector<Entry> ifd1;
    Entry format = { 0x0201, 4, 1, Bytes32(0, big) };
    Entry length = { 0x0202, 4, 1, Bytes32(4, big) };
    ifd1.push_back(format); ifd1.push_back(length);
    const size_t ifd1At = tiff.size();
    PutIfd(tiff, ifd1, big);
    const size_t formatValue = ifd1At + 2 + 8;

    Patch32(tiff, exifValue, tiff.size(), big);
    const unsigned exposure[2] = { 1, 125 }, fnumber[2] = { 28, 10 }, focal[2] = { 59, 10 };
    std::vector<Entry> settings;
    Entry date = { 0x9003, 2, 20, Ascii("2011:03:05 17:20:01") };
    Entry e1 = { 0x829A, 5, 1, Rationals(exposure, 1, big) };
    Entry e2 = { 0x829D, 5, 1, Rationals(fnumber, 1, big) };
    Entry e3 = { 0x8827, 3, 1, Bytes16(200, big) };
    Entry e4 = { 0x9209, 3, 1, Bytes16(25, big) };
    Entry e5 = { 0x920A, 5, 1, Rationals(focal, 1, big) };
    settings.push_back(date); settings.push_back(e1); settings.push_back(e2); settings.push_back(e3);
    settings.push_back(e4); settings.push_back(e5);
    PutIfd(tiff, settings, big);

    Patch32(tiff, gpsValue, tiff.size(), big);
    const unsigned latitude[6] = { 41, 1, 0, 1, 1830, 100 }, longitude[6] = { 28, 1, 58, 1, 30, 1 };
    std::vector<Entry> position;
    Entry n = { 1, 2, 2, Ascii("N") };
    Entry lat = { 2, 5, 3, Rationals(latitude, 3, big) };
    Entry w = { 3, 2, 2, Ascii("W") };
    Entry lon = { 4, 5, 3, Rationals(longitude, 3, big) };
    position.push_back(n); position.push_back(lat); position.push_back(w); position.push_back(lon);
    PutIfd(tiff, position, big);

    Patch32(tiff, formatValue, tiff.size(), big);
    const size_t previewInTiff = tiff.size();
    const unsigned char preview[4] = { 0xFF, 0xD8, 0xFF, 0xD9 };
    tiff.insert(tiff.end(), preview, preview + 4);

    std::vector<unsigned char> j;
    j.push_back(0xFF); j.push_back(0xD8);
    j.push_back(0xFF); j.push_back(0xE1);
    Put16(j, 2 + 6 + tiff.size(), true);
    const char header[6] = { 'E', 'x', 'i', 'f', 0, 0 };
    j.insert(j.end(), header, header + 6);
    aPreviewAt = j.size() + previewInTiff;
    j.insert(j.end(), tiff.begin(), tiff.end());
    j.push_back(0xFF); j.push_back(0xC0); Put16(j, 11, true);
    j.push_back(8); Put16(j, 2248, true); Put16(j, 4000, true);
    j.push_back(1); j.push_back(1); j.push_back(0x11); j.push_back(0);
    j.push_back(0xFF); j.push_back(0xDA);
    return j;
}

bool Near(double a, double b) { return a - b < 1e-6 && b - a < 1e-6; }
}

int main()
{
    TImageHeader h;

    std::vector<unsigned char> a = Jpeg(false, 6, "2025:07:14 09:41:30", 4000, 3000);
    Check(ParseImageHeader(&a[0], a.size(), h) == EFormatJpeg, "jpeg format");
    Check(h.iWidth == 4000 && h.iHeight == 3000, "jpeg frame size wins over exif");
    Check(h.iOrientation == 6, "orientation");
    Check(h.iHasDate && h.iYear == 2025 && h.iMonth == 7 && h.iDay == 14 && h.iHour == 9 && h.iMinute == 41 &&
          h.iSecond == 30, "date taken");
    Check(h.iThumbLength == 0 && !h.iHasPosition && h.iIso == -1 && h.iMakeLength == 0, "absent tags stay absent");

    std::vector<unsigned char> b = Jpeg(true, 1, "0000:00:00 00:00:00", 640, 480);
    ParseImageHeader(&b[0], b.size(), h);
    Check(h.iWidth == 640 && h.iHeight == 480 && h.iOrientation == 1, "big-endian jpeg");
    Check(!h.iHasDate, "a zero date is no date");

    std::vector<unsigned char> c = Jpeg(false, 1, "2024:01:02 03:04:05", 4000, 3000);
    c.resize(c.size() - 20);
    ParseImageHeader(&c[0], c.size(), h);
    Check(h.iWidth == 111 && h.iHeight == 222 && h.iHasDate, "exif sizes when the frame header is cut off");

    for (int order = 0; order < 2; order++)
    {
        int previewAt = 0;
        std::vector<unsigned char> cam = CameraJpeg(order == 1, previewAt);
        ParseImageHeader(&cam[0], cam.size(), h);
        Check(h.iWidth == 4000 && h.iHeight == 2248, "camera frame size");
        Check(h.iMakeLength == 5 && std::memcmp(h.iMake, "Nokia", 5) == 0, "make");
        Check(h.iModelLength == 2 && std::memcmp(h.iModel, "N8", 2) == 0, "model (inline ascii)");
        Check(h.iExposure[0] == 1 && h.iExposure[1] == 125, "exposure");
        Check(h.iFNumber[0] == 28 && h.iFNumber[1] == 10, "f-number");
        Check(h.iFocal[0] == 59 && h.iFocal[1] == 10, "focal length");
        Check(h.iIso == 200 && h.iFlash == 25, "iso and flash");
        Check(h.iHasDate && h.iYear == 2011 && h.iSecond == 1, "camera date");
        Check(h.iHasPosition && Near(h.iLatitude, 41.0050833333) && Near(h.iLongitude, -28.975), "gps position");
        Check(h.iThumbOffset == previewAt && h.iThumbLength == 4, "preview's place in the file");
        Check(cam[h.iThumbOffset] == 0xFF && cam[h.iThumbOffset + 1] == 0xD8, "preview starts a jpeg");
    }

    const unsigned char png[24] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                    0, 0, 2, 0x80, 0, 0, 1, 0x68 };
    Check(ParseImageHeader(png, 24, h) == EFormatPng && h.iWidth == 640 && h.iHeight == 360, "png");

    const unsigned char gif[10] = { 'G', 'I', 'F', '8', '9', 'a', 0x68, 1, 0x80, 2 };
    Check(ParseImageHeader(gif, 10, h) == EFormatGif && h.iWidth == 360 && h.iHeight == 640, "gif");

    unsigned char bmp[26] = { 'B', 'M' };
    bmp[14] = 40; bmp[18] = 0x20; bmp[19] = 3; bmp[22] = 0x20; bmp[23] = 0xFE; bmp[24] = 0xFF; bmp[25] = 0xFF;
    Check(ParseImageHeader(bmp, 26, h) == EFormatBmp && h.iWidth == 800 && h.iHeight == 480, "top-down bmp");

    const unsigned char junk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    Check(ParseImageHeader(junk, 8, h) == EFormatUnknown, "unknown");

    TReal s, t;
    OrientedToStored(6, 0, 0, s, t);
    Check(s == 0 && t == 1, "orientation 6 top-left");
    OrientedToStored(8, 0, 0, s, t);
    Check(s == 1 && t == 0, "orientation 8 top-left");

    std::printf(failures ? "%d failed\n" : "exif: all passed\n", failures);
    return failures ? 1 : 0;
}
