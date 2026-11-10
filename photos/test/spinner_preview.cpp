#include <cstdio>
#include <vector>

#include "spinner.h"

int main(int argc, char** argv)
    {
    const int side = 80, frames = 10;
    std::vector<unsigned char> strip(side * frames * side);
    std::vector<unsigned char> baked(side * side * Spinner::KFrames), shape(side * side * 2);
    Spinner::Bake(side, shape.data(), baked.data());
    for (int f = 0; f < frames; f++)
        {
        const unsigned char* frame = baked.data() + Spinner::FrameAt(f * Spinner::KPeriodMs / frames) * side * side;
        for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++)
                strip[y * side * frames + f * side + x] = frame[y * side + x];
        }
    FILE* out = fopen(argc > 1 ? argv[1] : "spinner.pgm", "wb");
    if (!out)
        return 1;
    fprintf(out, "P5 %d %d 255\n", side * frames, side);
    fwrite(strip.data(), 1, strip.size(), out);
    fclose(out);
    return 0;
    }
