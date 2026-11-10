#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "texcodec.h"

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

const int KModifiers[8][4] = { { 2, 8, -2, -8 }, { 5, 17, -5, -17 }, { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
                               { 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 },
                               { 47, 183, -47, -183 } };

int Clamp(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

void DecodeBlock(const unsigned char* b, unsigned char* aOut, int aStride)
{
    const unsigned high = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    const unsigned low = ((unsigned)b[4] << 24) | (b[5] << 16) | (b[6] << 8) | b[7];
    const bool diff = high & 2, flip = high & 1;
    int base[2][3];
    for (int c = 0; c < 3; c++)
    {
        const int shift = 24 - 8 * c;
        if (diff)
        {
            const int c1 = (high >> (shift + 3)) & 31;
            int d = (high >> shift) & 7;
            if (d >= 4)
                d -= 8;
            const int c2 = c1 + d;
            base[0][c] = (c1 << 3) | (c1 >> 2);
            base[1][c] = (c2 << 3) | (c2 >> 2);
        }
        else
        {
            base[0][c] = ((high >> (shift + 4)) & 15) * 17;
            base[1][c] = ((high >> shift) & 15) * 17;
        }
    }
    const int table[2] = { (int)((high >> 5) & 7), (int)((high >> 2) & 7) };
    for (int x = 0; x < 4; x++)
        for (int y = 0; y < 4; y++)
        {
            const int j = x * 4 + y;
            const int h = flip ? (y >= 2) : (x >= 2);
            const int index = (((low >> (16 + j)) & 1) << 1) | ((low >> j) & 1);
            const int m = KModifiers[table[h]][index];
            unsigned char* p = aOut + (y * aStride + x) * 3;
            for (int c = 0; c < 3; c++)
                p[c] = (unsigned char)Clamp(base[h][c] + m);
        }
}

void Expand(unsigned p, unsigned char* aRgb)
{
    const unsigned r = p >> 11, g = (p >> 5) & 63, b = p & 31;
    aRgb[0] = (unsigned char)((r << 3) | (r >> 2));
    aRgb[1] = (unsigned char)((g << 2) | (g >> 4));
    aRgb[2] = (unsigned char)((b << 3) | (b >> 2));
}

unsigned short Pack(int r, int g, int b)
{
    return (unsigned short)(((Clamp(r) >> 3) << 11) | ((Clamp(g) >> 2) << 5) | (Clamp(b) >> 3));
}

double Psnr(const std::vector<unsigned short>& aPixels, int aSide)
{
    std::vector<unsigned char> blocks(Etc1Bytes(aSide, aSide));
    Etc1Encode(&aPixels[0], aSide, aSide, &blocks[0]);
    std::vector<unsigned char> decoded(aSide * aSide * 3);
    int n = 0;
    for (int by = 0; by < aSide; by += 4)
        for (int bx = 0; bx < aSide; bx += 4)
            DecodeBlock(&blocks[8 * n++], &decoded[(by * aSide + bx) * 3], aSide);
    double sum = 0;
    for (int i = 0; i < aSide * aSide; i++)
    {
        unsigned char rgb[3];
        Expand(aPixels[i], rgb);
        for (int c = 0; c < 3; c++)
        {
            const double d = (double)rgb[c] - decoded[i * 3 + c];
            sum += d * d;
        }
    }
    const double mse = sum / (aSide * aSide * 3);
    return mse == 0 ? 99 : 10 * std::log10(255.0 * 255.0 / mse);
}

std::vector<unsigned short> Photo(int aSide, unsigned aSeed)
{
    std::srand(aSeed);
    std::vector<unsigned short> p(aSide * aSide);
    for (int y = 0; y < aSide; y++)
        for (int x = 0; x < aSide; x++)
        {
            const double fx = (double)x / aSide, fy = (double)y / aSide;
            int r, g, b;
            if (fy < 0.4 + 0.1 * std::sin(fx * 6))
            {
                r = (int)(90 + 80 * fy);
                g = (int)(140 + 60 * fy);
                b = (int)(220 - 20 * fx);
            }
            else
            {
                r = (int)(110 + 60 * std::sin(fx * 9 + fy * 3));
                g = (int)(100 + 50 * std::cos(fx * 4 - fy * 7));
                b = (int)(60 + 30 * std::sin(fy * 11));
            }
            const int grain = std::rand() % 9 - 4;
            p[y * aSide + x] = Pack(r + grain, g + grain, b + grain);
        }
    return p;
}
}

int main()
{
    const double photo128 = Psnr(Photo(128, 1), 128), photo64 = Psnr(Photo(64, 2), 64);
    std::printf("ETC1 PSNR: photo-like 128 %.2f dB, 64 %.2f dB\n", photo128, photo64);
    Check(photo128 >= 32, "a photo-like 128 thumbnail keeps 32 dB");
    Check(photo64 >= 30, "a photo-like 64 thumbnail keeps 30 dB");

    std::vector<unsigned short> flat(64 * 64, Pack(200, 120, 40));
    const double flatPsnr = Psnr(flat, 64);
    std::printf("ETC1 PSNR: flat %.2f dB\n", flatPsnr);
    Check(flatPsnr >= 40, "a flat colour stays within a few levels");

    std::vector<unsigned short> extremes(64 * 64);
    for (int i = 0; i < 64 * 64; i++)
        extremes[i] = ((i / 64 + i % 64) / 8) % 2 ? 0xFFFF : 0x0000;
    const double extremesPsnr = Psnr(extremes, 64);
    std::printf("ETC1 PSNR: black and white diagonal stripes %.2f dB\n", extremesPsnr);
    Check(extremesPsnr >= 30, "black and white diagonal stripes keep 30 dB");

    std::vector<unsigned short> square(4 * 4), half(2 * 2);
    for (int i = 0; i < 16; i++)
        square[i] = Pack((i % 4) * 64, 128, 255 - (i / 4) * 64);
    HalveRgb565(&square[0], 4, &half[0]);
    unsigned char rgb[3];
    Expand(half[0], rgb);
    Check(std::abs(rgb[0] - 32) <= 8 && std::abs(rgb[1] - 128) <= 4 && std::abs(rgb[2] - 223) <= 8,
          "halving averages a 2x2 square");
    Expand(half[3], rgb);
    Check(std::abs(rgb[0] - 160) <= 8 && std::abs(rgb[2] - 95) <= 8, "halving's last pixel");

    if (failures == 0)
        std::printf("texcodec: all passed\n");
    return failures ? 1 : 0;
}
