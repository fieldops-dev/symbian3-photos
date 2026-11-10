#include <cstdio>
#include <vector>

#include "fontmetrics.h"

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

typedef std::vector<unsigned char> TBytes;

void Put16(TBytes& b, unsigned v) { b.push_back((v >> 8) & 0xFF); b.push_back(v & 0xFF); }
void Put32(TBytes& b, unsigned v) { Put16(b, v >> 16); Put16(b, v & 0xFFFF); }

TBytes Font()
{
    TBytes head(54, 0), hhea(36, 0), hmtx, cmap, kern;
    head[18] = 0x03; head[19] = 0xE8;
    hhea[34] = 0; hhea[35] = 5;
    const unsigned advances[5] = { 500, 250, 600, 610, 560 };
    for (int i = 0; i < 5; i++) { Put16(hmtx, advances[i]); Put16(hmtx, 0); }

    const unsigned starts[7] = { 0x20, 'A', 'T', 'V', 'o', 0xF6, 0xFFFF };
    const unsigned glyph[7] = { 1, 2, 4, 3, 5, 5, 0 };
    Put16(cmap, 0); Put16(cmap, 1); Put16(cmap, 3); Put16(cmap, 1); Put32(cmap, 12);
    const int segments = 7;
    Put16(cmap, 4); Put16(cmap, 16 + segments * 8); Put16(cmap, 0);
    Put16(cmap, segments * 2); Put16(cmap, 8); Put16(cmap, 2); Put16(cmap, 6);
    for (int s = 0; s < segments; s++) Put16(cmap, starts[s]);
    Put16(cmap, 0);
    for (int s = 0; s < segments; s++) Put16(cmap, starts[s]);
    for (int s = 0; s < segments; s++) Put16(cmap, s == segments - 1 ? 1 : (glyph[s] - starts[s]) & 0xFFFF);
    for (int s = 0; s < segments; s++) Put16(cmap, 0);

    Put16(kern, 0); Put16(kern, 1);
    Put16(kern, 0); Put16(kern, 14 + 3 * 6); Put16(kern, 1);
    Put16(kern, 3); Put16(kern, 12); Put16(kern, 1); Put16(kern, 6);
    Put16(kern, 2); Put16(kern, 3); Put16(kern, (unsigned)-80 & 0xFFFF);
    Put16(kern, 3); Put16(kern, 2); Put16(kern, (unsigned)-75 & 0xFFFF);
    Put16(kern, 4); Put16(kern, 5); Put16(kern, (unsigned)-60 & 0xFFFF);
    const TBytes* tables[5] = { &cmap, &head, &hhea, &hmtx, &kern };
    const char* tags[5] = { "cmap", "head", "hhea", "hmtx", "kern" };
    TBytes file;
    Put32(file, 0x00010000); Put16(file, 5); Put16(file, 64); Put16(file, 2); Put16(file, 16);
    unsigned offset = 12 + 5 * 16;
    for (int i = 0; i < 5; i++)
    {
        Put32(file, TFontMetrics::Tag(tags[i][0], tags[i][1], tags[i][2], tags[i][3]));
        Put32(file, 0); Put32(file, offset); Put32(file, (unsigned)tables[i]->size());
        offset += (unsigned)tables[i]->size();
    }
    for (int i = 0; i < 5; i++) file.insert(file.end(), tables[i]->begin(), tables[i]->end());
    return file;
}

bool Read(const TBytes& aFile, TFontMetrics& aMetrics)
{
    const char* tags[5] = { "head", "hhea", "hmtx", "cmap", "kern" };
    const TUint8* at[5] = { 0, 0, 0, 0, 0 };
    TInt sizes[5] = { 0, 0, 0, 0, 0 };
    for (int i = 0; i < 5; i++)
    {
        TInt offset = 0, size = 0;
        if (TFontMetrics::FindTable(&aFile[0], (TInt)aFile.size(), TFontMetrics::Tag(tags[i][0], tags[i][1],
                                    tags[i][2], tags[i][3]), offset, size) && offset + size <= (TInt)aFile.size())
        {
            at[i] = &aFile[offset];
            sizes[i] = size;
        }
    }
    return aMetrics.Parse(at[0], sizes[0], at[1], sizes[1], at[2], sizes[2], at[3], sizes[3], at[4], sizes[4]);
}
}

int main(int argc, char** argv)
{
    static TFontMetrics metrics;
    Check(Read(Font(), metrics), "the small font reads");
    Check(metrics.UnitsPerEm() == 1000, "units per em");
    Check(metrics.Advance(' ') == 250 && metrics.Advance('A') == 600 && metrics.Advance('V') == 610 &&
          metrics.Advance('T') == 560, "advances");
    Check(metrics.Advance('o') == 560, "a glyph past the last metric shares its advance");
    Check(metrics.Advance('B') == -1, "a character not in the font");
    Check(metrics.Kern('A', 'V') == -80 && metrics.Kern('V', 'A') == -75, "kerning both ways");
    Check(metrics.Kern('T', 'o') == -60 && metrics.Kern('T', 0xF6) == -60, "a shared glyph kerns for both");
    Check(metrics.Kern('A', 'A') == 0, "no pair, no kerning");
    for (int i = 1; i < argc; i++)
    {
        FILE* f = std::fopen(argv[i], "rb");
        if (!f)
            continue;
        TBytes file;
        int c;
        while ((c = std::fgetc(f)) != EOF)
            file.push_back((unsigned char)c);
        std::fclose(f);
        static TFontMetrics real;
        const bool ok = Read(file, real);
        std::printf("%s: %d units/em, A %d, T %d, pairs %d, AV %d, To %d\n", argv[i], real.UnitsPerEm(),
                    real.Advance('A'), real.Advance('T'), real.PairCount(), real.Kern('A', 'V'), real.Kern('T', 'o'));
        Check(ok && real.Advance('A') > 0 && real.PairCount() > 100 && real.Kern('A', 'V') < 0, "a real font reads");
    }
    if (failures == 0)
        std::printf("fontmetrics: all passed\n");
    return failures ? 1 : 0;
}
