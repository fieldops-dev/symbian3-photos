#include <cstdio>
#include <vector>

#include "mp4.h"

namespace
    {
    int gFailures = 0;

    void Check(bool aOk, const char* aWhat)
        {
        if (!aOk)
            {
            std::printf("FAIL: %s\n", aWhat);
            gFailures++;
            }
        }

    void Put32(std::vector<unsigned char>& aOut, unsigned aValue)
        {
        for (int s = 24; s >= 0; s -= 8)
            aOut.push_back((unsigned char)(aValue >> s));
        }

    std::vector<unsigned char> Box(const char* aType, const std::vector<unsigned char>& aBody)
        {
        std::vector<unsigned char> box;
        Put32(box, (unsigned)(aBody.size() + 8));
        for (int i = 0; i < 4; i++)
            box.push_back((unsigned char)aType[i]);
        box.insert(box.end(), aBody.begin(), aBody.end());
        return box;
        }

    std::vector<unsigned char> Mvhd(unsigned aCreated, unsigned aScale, unsigned aDuration)
        {
        std::vector<unsigned char> body;
        Put32(body, 0);
        Put32(body, aCreated);
        Put32(body, aCreated);
        Put32(body, aScale);
        Put32(body, aDuration);
        body.resize(100, 0);
        return Box("mvhd", body);
        }

    std::vector<unsigned char> Tkhd(unsigned aWidth, unsigned aHeight, bool aQuarter)
        {
        std::vector<unsigned char> body;
        Put32(body, 0);
        for (int i = 0; i < 9; i++)
            Put32(body, 0);
        const unsigned matrix[9] = { aQuarter ? 0u : 0x10000u, aQuarter ? 0x10000u : 0u, 0, aQuarter ? 0xFFFF0000u : 0u,
                                     aQuarter ? 0u : 0x10000u, 0, 0, 0, 0x40000000u };
        for (int i = 0; i < 9; i++)
            Put32(body, matrix[i]);
        Put32(body, aWidth << 16);
        Put32(body, aHeight << 16);
        return Box("tkhd", body);
        }

    std::vector<unsigned char> Join(const std::vector<unsigned char>& aA, const std::vector<unsigned char>& aB)
        {
        std::vector<unsigned char> out(aA);
        out.insert(out.end(), aB.begin(), aB.end());
        return out;
        }

    std::vector<unsigned char> VideoTrak(bool aWithStss)
        {
        std::vector<unsigned char> hdlr, mdhd, stts, stss;
        Put32(hdlr, 0); Put32(hdlr, 0);
        hdlr.push_back('v'); hdlr.push_back('i'); hdlr.push_back('d'); hdlr.push_back('e');
        Put32(hdlr, 0); Put32(hdlr, 0); Put32(hdlr, 0);
        Put32(mdhd, 0); Put32(mdhd, 0); Put32(mdhd, 0); Put32(mdhd, 15360); Put32(mdhd, 300 * 512); Put32(mdhd, 0);
        Put32(stts, 0); Put32(stts, 2); Put32(stts, 100); Put32(stts, 512); Put32(stts, 200); Put32(stts, 512);
        const unsigned syncs[4] = { 1, 61, 121, 245 };
        Put32(stss, 0); Put32(stss, 4);
        for (int i = 0; i < 4; i++)
            Put32(stss, syncs[i]);
        std::vector<unsigned char> stbl = Box("stts", stts);
        if (aWithStss)
            {
            const std::vector<unsigned char> table = Box("stss", stss);
            stbl.insert(stbl.end(), table.begin(), table.end());
            }
        std::vector<unsigned char> mdia = Box("hdlr", hdlr);
        const std::vector<unsigned char> m = Box("mdhd", mdhd);
        mdia.insert(mdia.end(), m.begin(), m.end());
        const std::vector<unsigned char> minf = Box("minf", Box("stbl", stbl));
        mdia.insert(mdia.end(), minf.begin(), minf.end());
        return Box("trak", Join(Tkhd(640, 360, false), Box("mdia", mdia)));
        }
    }

int main()
    {

    std::vector<unsigned char> moov = Join(Mvhd(3800000000u, 600, 54000), Box("trak", Tkhd(1280, 720, false)));
    TVideoHeader header;
    Check(ParseMoov(&moov[0], (TInt)moov.size(), header), "landscape: found");
    Check(header.iDurationMs == 90000, "landscape: duration");
    Check(header.iCreated == 3800000000LL, "landscape: created");
    Check(header.iWidth == 1280 && header.iHeight == 720, "landscape: size");

    moov = Join(Join(Mvhd(1, 1000, 1500), Box("trak", Tkhd(0, 0, false))), Box("trak", Tkhd(640, 480, true)));
    Check(ParseMoov(&moov[0], (TInt)moov.size(), header), "portrait: found");
    Check(header.iWidth == 480 && header.iHeight == 640, "portrait: size swapped");
    Check(header.iDurationMs == 1500, "portrait: duration");

    std::vector<unsigned char> large;
    Put32(large, 1);
    large.push_back('f'); large.push_back('r'); large.push_back('e'); large.push_back('e');
    Put32(large, 0);
    Put32(large, 24);
    Put32(large, 0);
    Put32(large, 0);
    TUint32 type;
    TInt64 size;
    TInt headerBytes;
    Check(Mp4BoxHeader(&large[0], (TInt)large.size(), type, size, headerBytes) && size == 24 && headerBytes == 16,
          "64-bit size");

    moov = Join(Mvhd(5, 1000, 2000), Box("trak", Tkhd(1280, 720, false)));
    Check(ParseMoov(&moov[0], 120, header), "cut: found");
    Check(header.iDurationMs == 2000 && header.iWidth == 0, "cut: duration only");

    moov = Join(Mvhd(1, 1000, 10000), VideoTrak(true));
    TInt keys[8];
    const TInt count = Mp4Keyframes(&moov[0], (TInt)moov.size(), keys, 8);
    Check(count == 4, "keyframes: count");
    Check(count == 4 && keys[0] == 0 && keys[1] == 2000 && keys[2] == 4000 && keys[3] == 8133, "keyframes: times");
    moov = Join(Mvhd(1, 1000, 10000), VideoTrak(false));
    Check(Mp4Keyframes(&moov[0], (TInt)moov.size(), keys, 8) == 0, "keyframes: none listed, all are");
    moov = Join(Mvhd(1, 1000, 10000), Box("trak", Tkhd(640, 360, false)));
    Check(Mp4Keyframes(&moov[0], (TInt)moov.size(), keys, 8) == -1, "keyframes: no video track");

    unsigned char junk[16] = { 0, 0, 0, 3, 'x', 'x', 'x', 'x' };
    Check(!ParseMoov(junk, sizeof(junk), header), "junk");

    if (gFailures == 0)
        std::printf("mp4: all checks passed\n");
    return gFailures ? 1 : 0;
    }
