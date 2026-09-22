#include "galaxy_field.h"
#include <cmath>
#include <cstring>

namespace galaxy {

uint8_t GalaxyField::s_dust[NEB_H][NEB_W];
uint8_t GalaxyField::s_glow[NEB_H][NEB_W];
bool    GalaxyField::s_ready = false;

static uint32_t g_seed = 0x9A1AC7u;
static inline float frand()
{
    g_seed = (g_seed * 1103515245u + 12345u) & 0x7FFFFFFFu;
    return ((g_seed >> 8) & 0xFFFFu) / 65535.0f;
}

// 값 노이즈 (해시 격자 + 스무스 보간) → fbm 3옥타브
static float hash2(int x, int y, int seed)
{
    uint32_t h = (uint32_t)(x * 374761393 + y * 668265263 + seed * 1442695041);
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
}
static float vnoise(float x, float y, int seed)
{
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed), c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}
static float fbm(float x, float y, int seed)
{
    return 0.55f * vnoise(x, y, seed) + 0.30f * vnoise(x * 2.1f, y * 2.1f, seed + 7) + 0.15f * vnoise(x * 4.3f, y * 4.3f, seed + 13);
}

void GalaxyField::build_nebula()
{
    if (s_ready) return;
    // 사진처럼 좌상→우하 대각선 띠. 띠 중심선까지의 거리로 마스크.
    for (int j = 0; j < NEB_H; ++j) {
        for (int i = 0; i < NEB_W; ++i) {
            float u = i / (float)NEB_W, v = j / (float)NEB_H;
            float wob  = 0.06f * std::sin(u * 9.0f) + 0.04f * std::sin(v * 13.0f + 1.0f);   // 띠가 구불거림
            float band = (u * 0.80f + 0.10f + wob) - v;      // 0 이면 띠 중심
            float m = std::exp(-band * band / 0.035f);       // 띠 폭
            float d = fbm(u * 3.5f, v * 3.5f, 3);
            float g = fbm(u * 2.2f + 5.0f, v * 2.2f, 11);
            float lane = fbm(u * 6.0f + 9.0f, v * 6.0f + 3.0f, 21);       // 암흑 성운(먼지 골)
            float dark = lane < 0.42f ? (0.42f - lane) / 0.42f : 0.0f;    // 0..1
            float dust = m * (0.30f + 0.70f * d) * 1.25f * (1.0f - 0.85f * dark);
            float glow = m * (0.20f + 0.80f * g) * 1.0f * (1.0f - 0.55f * dark) + 0.05f * (1 - m);
            if (dust > 1) dust = 1;
            if (glow > 1) glow = 1;
            s_dust[j][i] = (uint8_t)(dust * 255);
            s_glow[j][i] = (uint8_t)(glow * 255);
        }
    }
    s_ready = true;
}

GalaxyField::GalaxyField()
{
    build_nebula();
    for (int k = 0; k < N_STARS; ++k) {
        Star& s = _stars[k];
        s.hx = frand() * W; s.hy = frand() * H;
        if (frand() < 0.45f) {                       // 45% 는 은하수 띠 근처에 배치
            float u = s.hx / W;
            float bandy = (u * 0.80f + 0.10f) * H;
            s.hy = bandy + (frand() + frand() - 1.0f) * 55.0f;
            if (s.hy < 0) s.hy += H;
            if (s.hy >= H) s.hy -= H;
        }
        s.ox = s.oy = s.vx = s.vy = 0;
        s.depth = 0.3f + 0.7f * frand() * frand();
        float b = frand();
        s.bright = 0.30f + 0.70f * b * b;
        s.temp = frand();
        s.tw_phase = frand() * 6.2832f;
        s.tw_speed = 0.04f + 0.10f * frand();
        s.size = (b > 0.975f) ? 2 : (b > 0.86f ? 1 : 0);
    }
}

void GalaxyField::step(float level)
{
    _level = level;
    _t += 1.0f;
    // 모임 강도: 레벨을 빠르게 따라 올라가고 천천히 내려온다
    float target = level > 0.30f ? (level - 0.30f) / 0.70f : 0.0f;
    if (target > _pull) _pull += (target - _pull) * 0.25f;
    else                _pull += (target - _pull) * 0.06f;
    // 새 소리가 시작될 때 중력점을 무작위로 (중앙 근처)
    if (_armed && target > 0.35f) {
        _armed = false;
        _gx = W * (0.35f + 0.30f * frand());
        _gy = H * (0.35f + 0.30f * frand());
    }
    bool burst = false;
    if (!_armed && target < 0.10f && _pull < 0.25f) {
        _armed = true;
        burst = true;              // 소리가 끝남 → 흩어짐
    }
    // 성운 천천히 흐름 (소리에 살짝 빨라짐)
    // 성운은 반복 텍스처가 아니므로 스크롤 대신 아주 느린 흔들림(클램프 샘플링)
    _neb_u = 0.012f * std::sin(_t * 0.0025f) + 0.004f * level;
    _neb_v = 0.010f * std::cos(_t * 0.0019f);

    for (int k = 0; k < N_STARS; ++k) {
        Star& s = _stars[k];
        // 홈 드리프트: 시차. 전체가 우상단 방향으로 아주 느리게
        s.hx += (0.04f + 0.16f * s.depth) * 0.6f;
        s.hy -= (0.04f + 0.16f * s.depth) * 0.25f;
        if (s.hx >= W) s.hx -= W;
        if (s.hy < 0) s.hy += H;
        // 소리 반응: 중력점으로 당기는 힘 + 홈으로 돌아가는 스프링 + 감쇠
        float px = s.hx + s.ox, py = s.hy + s.oy;
        float dx = _gx - px, dy = _gy - py;
        float dist = std::sqrt(dx * dx + dy * dy) + 1.0f;
        float f = _pull * (0.9f + 0.6f * s.depth) * 0.20f;
        s.vx += dx / dist * f * 3.0f;
        s.vy += dy / dist * f * 3.0f;
        s.vx += -s.ox * 0.010f;
        s.vy += -s.oy * 0.010f;
        if (burst) {
            float bx = -dx / dist, by = -dy / dist;
            float mag = 4.0f + 4.0f * s.depth;
            s.vx += bx * mag + (frand() - 0.5f) * 2.0f;
            s.vy += by * mag + (frand() - 0.5f) * 2.0f;
        }
        s.vx *= 0.93f; s.vy *= 0.93f;
        s.ox += s.vx; s.oy += s.vy;
    }
}

static inline void add_px(uint16_t* p, int r, int g, int b)
{
    // RGB565 가산 (포화)
    uint16_t c = *p;
    int R = ((c >> 11) & 31) + r, G = ((c >> 5) & 63) + g, B = (c & 31) + b;
    if (R > 31) R = 31;
    if (G > 63) G = 63;
    if (B > 31) B = 31;
    *p = (uint16_t)((R << 11) | (G << 5) | B);
}

void GalaxyField::render(uint16_t* buf) const
{
    // 1) 성운 배경 (48×36 텍스처를 스크롤·보간해 2×2 블록으로) — 깊은 남색 하늘 + 갈색 먼지 + 보라 빛
    const float glow_boost = 1.0f + 0.35f * _pull;
    for (int y = 0; y < H; y += 2) {
        float v = (y / (float)H * 0.94f + 0.03f + _neb_v) * (NEB_H - 1);
        if (v < 0) v = 0;
        if (v > NEB_H - 1.001f) v = NEB_H - 1.001f;
        int vj = (int)v; float fv = v - vj;
        int j0 = vj, j1 = vj + 1;
        uint16_t* r0 = buf + y * W;
        uint16_t* r1 = r0 + W;
        for (int x = 0; x < W; x += 2) {
            float u = (x / (float)W * 0.94f + 0.03f + _neb_u) * (NEB_W - 1);
            if (u < 0) u = 0;
            if (u > NEB_W - 1.001f) u = NEB_W - 1.001f;
            int ui = (int)u; float fu = u - ui;
            int i0 = ui, i1 = ui + 1;
            float dust = (s_dust[j0][i0] * (1 - fu) + s_dust[j0][i1] * fu) * (1 - fv) + (s_dust[j1][i0] * (1 - fu) + s_dust[j1][i1] * fu) * fv;
            float glow = (s_glow[j0][i0] * (1 - fu) + s_glow[j0][i1] * fu) * (1 - fv) + (s_glow[j1][i0] * (1 - fu) + s_glow[j1][i1] * fu) * fv;
            dust *= (1.0f / 255.0f); glow *= glow_boost / 255.0f;
            // 하늘 (12,10,30) + 먼지 갈색 (150,110,95)·dust*0.55 + 빛 보라 (120,95,200)·glow*0.45
            float R = 12 + 150 * dust * 0.55f + 120 * glow * 0.45f;
            float G = 10 + 110 * dust * 0.55f + 95 * glow * 0.45f;
            float B = 30 + 95 * dust * 0.55f + 200 * glow * 0.45f;
            if (R > 255) R = 255;
            if (G > 255) G = 255;
            if (B > 255) B = 255;
            uint16_t c = (uint16_t)((((int)R >> 3) << 11) | (((int)G >> 2) << 5) | ((int)B >> 3));
            r0[x] = c; r0[x + 1] = c; r1[x] = c; r1[x + 1] = c;
        }
    }
    // 2) 별
    for (int k = 0; k < N_STARS; ++k) {
        const Star& s = _stars[k];
        float px = s.hx + s.ox, py = s.hy + s.oy;
        int x = (int)px, y = (int)py;
        if (x < 1 || x >= W - 1 || y < 1 || y >= H - 1) continue;
        float tw = 0.72f + 0.28f * std::sin(_t * s.tw_speed + s.tw_phase);
        float b = s.bright * tw * (0.85f + 0.35f * _pull);
        if (b > 1.0f) b = 1.0f;
        // 색온도: 파랑(0.75,0.85,1.0) ~ 노랑(1.0,0.92,0.72)
        float cr = 0.75f + 0.25f * s.temp, cg = 0.85f + 0.07f * s.temp, cb = 1.0f - 0.28f * s.temp;
        int r = (int)(31 * b * cr), g = (int)(63 * b * cg), bb = (int)(31 * b * cb);
        uint16_t* p = buf + y * W + x;
        if (s.size == 0) {
            add_px(p, r, g, bb);
        } else if (s.size == 1) {
            add_px(p, r, g, bb);
            add_px(p + 1, r / 2, g / 2, bb / 2); add_px(p - 1, r / 2, g / 2, bb / 2);
            add_px(p + W, r / 2, g / 2, bb / 2); add_px(p - W, r / 2, g / 2, bb / 2);
        } else {
            add_px(p, r, g, bb);
            add_px(p + 1, r * 3 / 4, g * 3 / 4, bb * 3 / 4); add_px(p - 1, r * 3 / 4, g * 3 / 4, bb * 3 / 4);
            add_px(p + W, r * 3 / 4, g * 3 / 4, bb * 3 / 4); add_px(p - W, r * 3 / 4, g * 3 / 4, bb * 3 / 4);
            add_px(p + W + 1, r / 3, g / 3, bb / 3); add_px(p + W - 1, r / 3, g / 3, bb / 3);
            add_px(p - W + 1, r / 3, g / 3, bb / 3); add_px(p - W - 1, r / 3, g / 3, bb / 3);
        }
    }
}

}  // namespace galaxy
