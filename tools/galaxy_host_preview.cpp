// 은하수 미리보기: 조용 / 소리 중(모임) / 소리 직후(흩어짐) 3장.
#include "galaxy_field.h"
#include <cstdio>
#include <vector>
#include <chrono>
int main(int argc, char** argv)
{
    using namespace galaxy;
    const int n = 3;
    std::vector<uint16_t> frames(n * W * H);
    GalaxyField g;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 300; ++i) g.step(0.0f);
    g.render(frames.data());
    for (int i = 0; i < 45; ++i) g.step(0.9f);
    g.render(frames.data() + W * H);
    for (int i = 0; i < 40; ++i) g.step(0.0f);
    g.render(frames.data() + 2 * W * H);
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("host total %.1f ms for 385 steps + 3 renders\n", ms);
    FILE* f = fopen(argc > 1 ? argv[1] : "galaxy.ppm", "wb");
    fprintf(f, "P6\n%d %d\n255\n", W * n + 10 * (n - 1), H);
    for (int y = 0; y < H; ++y) for (int k = 0; k < n; ++k) {
        for (int x = 0; x < W; ++x) {
            uint16_t c = frames[k * W * H + y * W + x];
            unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)};
            fwrite(rgb, 1, 3, f);
        }
        if (k < n - 1) for (int q = 0; q < 10; ++q) { unsigned char gr[3] = {40, 40, 40}; fwrite(gr, 1, 3, f); }
    }
    fclose(f);
    return 0;
}
