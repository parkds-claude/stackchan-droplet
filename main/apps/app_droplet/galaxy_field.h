/*
 * GalaxyField — 은하수 스타일: 별 파티클 + 성운 먼지띠. 소리에 반응해 별이 모였다 흩어진다.
 * ESP/LVGL 의존 없음(호스트 미리보기 가능). 규격: README.md.
 */
#pragma once
#include <cstdint>

namespace galaxy {

constexpr int W = 320;
constexpr int H = 240;
constexpr int N_STARS = 700;

class GalaxyField {
public:
    GalaxyField();
    /// level 0..1 (소음). 프레임 33ms 기준.
    void step(float level);
    /// RGB565 W*H 렌더.
    void render(uint16_t* buf) const;
    float pull() const { return _pull; }

private:
    struct Star {
        float hx, hy;      // 홈 위치(느리게 흐름)
        float ox, oy;      // 홈에서의 변위 (소리 반응)
        float vx, vy;      // 변위 속도
        float depth;       // 0.3(멀다) ~ 1.0(가깝다)
        float bright;      // 0.25 ~ 1.0
        float temp;        // 0 파랑 ~ 1 노랑
        float tw_phase, tw_speed;
        uint8_t size;      // 0: 1px, 1: 2px, 2: 3px
    };
    Star _stars[N_STARS];
    float _t = 0.0f;
    float _level = 0.0f;
    float _pull = 0.0f;       // 모임 강도 (레벨을 부드럽게 따라감)
    float _gx = W / 2.0f, _gy = H / 2.0f;   // 중력점
    bool  _armed = true;
    float _neb_u = 0.0f, _neb_v = 0.0f;     // 성운 스크롤

    static constexpr int NEB_W = 64, NEB_H = 48;
    static uint8_t s_dust[NEB_H][NEB_W];    // 먼지 밀도 0..255 (갈색)
    static uint8_t s_glow[NEB_H][NEB_W];    // 빛 0..255 (보라·청)
    static bool s_ready;
    static void build_nebula();
};

}  // namespace galaxy
