// 호스트 미리보기: 두 스타일 × (조용/시끄러움) PPM 출력.
// 빌드: c++ -O2 -std=c++17 -I main/apps/app_droplet main/apps/app_droplet/droplet_field.cpp ../../tools/droplet_host_preview.cpp -o /tmp/dp
#include "droplet_field.h"
#include <cstdio>
#include <vector>
#include <chrono>

static void write_ppm(const char* path, const std::vector<uint16_t>& frames, int n)
{
    FILE* f = fopen(path, "wb");
    int W = droplet::W, H = droplet::H;
    fprintf(f, "P6\n%d %d\n255\n", W * n + 10 * (n - 1), H);
    for (int y = 0; y < H; ++y) {
        for (int k = 0; k < n; ++k) {
            for (int x = 0; x < W; ++x) {
                uint16_t c = frames[k * W * H + y * W + x];
                unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31),
                                        (unsigned char)(((c >> 5) & 63) * 255 / 63),
                                        (unsigned char)((c & 31) * 255 / 31)};
                fwrite(rgb, 1, 3, f);
            }
            if (k < n - 1) for (int g = 0; g < 10; ++g) { unsigned char grey[3] = {40, 40, 40}; fwrite(grey, 1, 3, f); }
        }
    }
    fclose(f);
}

int main(int argc, char** argv)
{
    const int n = 4;
    std::vector<uint16_t> frames(n * droplet::W * droplet::H);
    droplet::Style styles[n] = {droplet::Style::Dots, droplet::Style::Dots, droplet::Style::Blob, droplet::Style::Blob};
    float levels[n] = {0.0f, 0.7f, 0.0f, 0.7f};
    int warm[n] = {150, 150, 150, 150};
    for (int k = 0; k < n; ++k) {
        droplet::DropletField fld(styles[k]);
        for (int i = 0; i < warm[k]; ++i) fld.step(levels[k]);
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) { fld.step(levels[k]); fld.render(frames.data() + k * droplet::W * droplet::H); }
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20;
        printf("style %d level %.1f: %.2f ms/frame (host)\n", (int)styles[k], levels[k], ms);
    }
    write_ppm(argc > 1 ? argv[1] : "droplet_preview.ppm", frames, n);
    return 0;
}
