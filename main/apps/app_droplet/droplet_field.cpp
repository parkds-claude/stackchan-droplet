/*
 * DropletField v3 구현. 설계는 droplet_field.h 와 README.md.
 */
#include "droplet_field.h"
#include <cmath>
#include <cstring>

namespace droplet {

// ── 공통 상수 ─────────────────────────────────────────────────────────────
static constexpr float LEVEL_LIFT  = 0.25f;
static constexpr float RIPPLE_GAIN = 0.32f;
static constexpr float T_STEP_BASE = 0.020f;
static constexpr float T_STEP_GAIN = 0.100f;
// 주황-레드 팔레트: 색상 0.00(레드)~0.07(주황) 사이를 천천히 흔들림
static constexpr float HUE_CENTER = 0.030f;
static constexpr float HUE_SWING  = 0.028f;
static constexpr float HUE_SPEED  = 1.0f / 70.0f;
// Blob 커널
static constexpr float REACH  = 1.9f;
static constexpr int   KLUT_N = 4096;
static uint16_t s_klut[KLUT_N + 1];
static bool     s_klut_ready = false;
static const float K_GAIN = 1.0f / ((1.0f - 1.0f / (REACH * REACH)) * (1.0f - 1.0f / (REACH * REACH)));
// Dots 필렛 목
static constexpr float FILLET_R = 8.0f;
static constexpr float NECK_MIN = 3.0f;

static uint32_t lcg_next(uint32_t& x)
{
    x = (x * 1103515245u + 12345u) & 0x7FFFFFFFu;
    return (x >> 8) & 0xFFFFu;
}

static inline float smoothstep(float a, float b, float x)
{
    float t = (x - a) / (b - a);
    if (t < 0) t = 0; else if (t > 1) t = 1;
    return t * t * (3.0f - 2.0f * t);
}

uint16_t hsv_rgb565(float h, float s, float v)
{
    h -= std::floor(h);
    int i = (int)(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1 - s), q = v * (1 - s * f), u = v * (1 - s * (1 - f));
    float r, g, b;
    switch (i % 6) {
        case 0: r = v; g = u; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = u; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = u; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    uint16_t R = (uint16_t)(r * 31.0f + 0.5f), G = (uint16_t)(g * 63.0f + 0.5f), B = (uint16_t)(b * 31.0f + 0.5f);
    return (uint16_t)((R << 11) | (G << 5) | B);
}

void LevelState::update(float rms)
{
    if (!boot && rms > 0.0f) {
        env   = rms;
        floor = rms > FLOOR_MIN ? rms : FLOOR_MIN;
        boot  = true;
    }
    if (rms > env) env += (rms - env) * ENV_ATTACK;
    else           env += (rms - env) * ENV_RELEASE;
    if (env < floor) floor = env;
    else             floor += (env - floor) * FLOOR_RISE;
    if (floor < FLOOR_MIN) floor = FLOOR_MIN;
    float ratio = floor > 0.0f ? env / floor : 1.0f;
    if (ratio <= 1.0f) level = 0.0f;
    else {
        level = std::log2(ratio) / LEVEL_OCTAVES;
        if (level > 1.0f) level = 1.0f;
    }
}

DropletField::DropletField(Style style) : _style(style)
{
    if (!s_klut_ready) {
        for (int i = 0; i <= KLUT_N; ++i) {
            float u = (float)i / KLUT_N;
            float w = (1.0f - u) * (1.0f - u) * K_GAIN;
            s_klut[i] = (uint16_t)(w * 256.0f + 0.5f);
        }
        s_klut_ready = true;
    }
    _apply_style();
    _update_palettes();
}

void DropletField::setStyle(Style s)
{
    if (s == _style) return;
    _style = s;
    _apply_style();
    _update_field();
}

void DropletField::_apply_style()
{
    if (_style == Style::Dots) {
        _cols = 16; _rows = 12; _pitch = 20; _ox = 10; _oy = 10;
        _rmin = 3.0f; _rmax = 11.0f; _wobble = false;
    } else {
        _cols = 12; _rows = 9; _pitch = 26;
        _ox = (W - (_cols - 1) * _pitch) / 2; _oy = (H - (_rows - 1) * _pitch) / 2;
        _rmin = 3.0f; _rmax = 15.0f; _wobble = true;
    }
    _n = _cols * _rows;
    uint32_t seed = 0x5EED00u;
    const float jit = (_style == Style::Dots) ? 0.10f : 0.06f;
    for (int k = 0; k < _n; ++k) _jitter[k] = ((float)lcg_next(seed) / 65535.0f * 2.0f - 1.0f) * jit;
    const float cx = (_cols - 1) / 2.0f, cy = (_rows - 1) / 2.0f;
    for (int j = 0; j < _rows; ++j)
        for (int i = 0; i < _cols; ++i)
            _ring[j * _cols + i] = (uint8_t)(std::sqrt((i - cx) * (i - cx) + (j - cy) * (j - cy)) + 0.5f);
    for (int k = 0; k < _n; ++k) _rad[k] = _rmin;
}

void DropletField::_update_field()
{
    float rip[16];
    const float t = _t;
    for (int r = 0; r < 16; ++r) rip[r] = std::sin(r * 0.85f - t * 2.8f) * RIPPLE_GAIN * _level;
    const float lift = LEVEL_LIFT * _level;
    int idx = 0;
    for (int j = 0; j < _rows; ++j) {
        for (int i = 0; i < _cols; ++i, ++idx) {
            float v;
            if (_style == Style::Dots) {
                // 도트 매트릭스 필드
                v = 0.5f + 0.25f * std::sin(0.9f * i + 1.3f * t) * std::cos(0.7f * j - 0.8f * t)
                         + 0.25f * std::sin(0.5f * (i + j) + 0.6f * t);
            } else {
                v = 0.50f + 0.22f * std::sin(0.55f * i + 0.9f * t) * std::cos(0.45f * j - 0.7f * t)
                          + 0.20f * std::sin(0.35f * (i + j) + 0.5f * t + 1.3f)
                          + 0.16f * std::sin(0.50f * i - 0.62f * j - 1.1f * t + 0.7f);
            }
            v += rip[_ring[idx]] + _jitter[idx];
            float f = v + lift;
            if (f < 0.0f) f = 0.0f; else if (f > 1.0f) f = 1.0f;
            if (_style == Style::Dots) f = f * f * (0.4f + 0.6f * f);      // ≈ f^1.6 (dots.py 동일)
            else                       f = f * f * (0.55f + 0.45f * f);    // ≈ f^1.4
            _rad[idx] = _rmin + (_rmax - _rmin) * f;
        }
    }
}

void DropletField::_update_palettes()
{
    const float hue = HUE_CENTER + HUE_SWING * std::sin(_t * HUE_SPEED * 6.2832f);
    const float lift = 0.10f * _level;      // 소음이 크면 전체가 조금 밝아진다

    // Blob: F<0.80 검정 | 0.80~1.0 림 램프 | 1.0~2.6 내부(림 → 중심 살구빛)
    const float v_rim = 0.60f + lift, v_core = 0.98f;
    for (int i = 0; i < 256; ++i) {
        float F = i / 64.0f;
        float v, s, h = hue;
        if (F < 0.80f) { v = 0; s = 0; }
        else if (F < 1.0f) { v = v_rim * smoothstep(0.80f, 1.0f, F); s = 0.92f; h = hue - 0.012f; }
        else {
            float g = (F - 1.0f) / 1.6f; if (g > 1.0f) g = 1.0f;
            g = g * (2.0f - g);
            v = v_rim + (v_core - v_rim) * g;
            s = 0.92f - 0.50f * g;                   // 중심으로 갈수록 채도↓ → 살구·크림
            h = hue + 0.030f * g;                    // 중심은 주황 쪽
        }
        _pal_blob[i] = hsv_rgb565(h, s, v > 1.0f ? 1.0f : v);
    }

    // Dots: 반지름 32단(작음 → 큼 연속) × 커버리지 0..15. 작은 도트 = 깊은 레드, 큰 도트 = 주황·살구
    for (int b = 0; b < 32; ++b) {
        const float g = b / 31.0f;
        float v = 0.40f + 0.60f * g + lift; if (v > 1.0f) v = 1.0f;
        const float sat = 0.96f - 0.36f * g * g;
        const float hh  = hue - 0.012f + 0.052f * g;
        for (int c = 0; c < 16; ++c) {
            float cov = c / 15.0f;
            float vv = v * std::pow(cov, 0.62f);        // 가장자리 AA 는 감마 보정 혼합
            _pal_dots[b][c] = (c == 0) ? 0 : hsv_rgb565(hh, sat, vv);
        }
    }
}

void DropletField::step(float level, float dtFrames)
{
    _level = level;
    _t += (T_STEP_BASE + T_STEP_GAIN * level) * dtFrames;
    _update_field();
    _update_palettes();
}

void DropletField::render(uint16_t* buf) const
{
    if (_style == Style::Dots) _render_dots(buf);
    else                       _render_blob(buf);
}

// ── Blob: 유한 지지 메타볼 커널, 절반 해상도 평가 + 선형 보간 ─────────────────
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("O2")))
#endif
void DropletField::_render_blob(uint16_t* buf) const
{
    const int cols = _cols, rows = _rows, pitch = _pitch;
    uint32_t invR2[NMAX], R2[NMAX];
    for (int k = 0; k < _n; ++k) {
        float R = _rad[k] * REACH, r2 = R * R;
        R2[k] = (uint32_t)(r2 + 0.5f);
        invR2[k] = (uint32_t)(4096.0f / r2 + 0.5f);
    }
    int colx[NMAX / 8 + 3], rowy[NMAX / 8 + 3];
    for (int i = -1; i <= cols; ++i)
        colx[i + 1] = _ox + i * pitch + (_wobble ? (int)(5.0f * std::sin(_t * 0.6f + i * 1.7f) + 0.5f) : 0);
    for (int j = -1; j <= rows; ++j)
        rowy[j + 1] = _oy + j * pitch + (_wobble ? (int)(5.0f * std::cos(_t * 0.5f + j * 2.1f) + 0.5f) : 0);
    static int16_t dx2[W][3], dy2[H][3];
    static int8_t cix[W], cjy[H];
    for (int x = 0; x < W; ++x) {
        int ci = (x - _ox + pitch / 2) / pitch;
        if (ci < 0) ci = 0;
        if (ci >= cols) ci = cols - 1;
        cix[x] = (int8_t)ci;
        for (int a = 0; a < 3; ++a) { int dx = x - colx[ci + a]; dx2[x][a] = (int16_t)(dx * dx); }
    }
    for (int y = 0; y < H; ++y) {
        int cj = (y - _oy + pitch / 2) / pitch;
        if (cj < 0) cj = 0;
        if (cj >= rows) cj = rows - 1;
        cjy[y] = (int8_t)cj;
        for (int b = 0; b < 3; ++b) { int dy = y - rowy[cj + b]; dy2[y][b] = (int16_t)(dy * dy); }
    }
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const uint16_t* klut = s_klut;
    const uint16_t* pal = _pal_blob;
    constexpr int HW = W / 2, HH = H / 2;
    static uint16_t fsub[HH + 1][HW + 1];
    for (int sy = 0; sy <= HH; ++sy) {
        const int y = (sy < HH) ? sy * 2 : H - 1;
        const int j0 = cjy[y] - 1;
        const int b0 = (j0 < 0) ? 1 : 0, b1 = (j0 + 2 >= rows) ? 2 : 3;
        for (int sx = 0; sx <= HW; ++sx) {
            const int x = (sx < HW) ? sx * 2 : W - 1;
            const int i0 = cix[x] - 1;
            const int a0 = (i0 < 0) ? 1 : 0, a1 = (i0 + 2 >= cols) ? 2 : 3;
            const int16_t* dxr = dx2[x];
            uint32_t sum = 0;
            for (int b = b0; b < b1; ++b) {
                const uint32_t y2 = dy2[y][b];
                const int kbase = (j0 + b) * cols + i0;
                for (int a = a0; a < a1; ++a) {
                    const int k = kbase + a;
                    const uint32_t d2 = y2 + (uint32_t)dxr[a];
                    if (d2 < R2[k]) {
                        uint32_t u = d2 * invR2[k];
                        if (u > 4096) u = 4096;
                        sum += klut[u];
                    }
                }
            }
            if (sum > 1023) sum = 1023;
            fsub[sy][sx] = (uint16_t)sum;
        }
    }
    uint16_t* row = buf;
    for (int y = 0; y < H; ++y, row += W) {
        const int sy = y >> 1;
        const uint16_t* f0 = fsub[sy];
        const uint16_t* f1 = fsub[sy + 1];
        const uint32_t wy = y & 1;
        const uint8_t* bay = bayer[y & 3];
        for (int x = 0; x < W; ++x) {
            const int sx = x >> 1;
            uint32_t a = f0[sx], b = f0[sx + 1];
            if (wy) { a = (a + f1[sx]) >> 1; b = (b + f1[sx + 1]) >> 1; }
            const uint32_t F = (x & 1) ? ((a + b) >> 1) : a;
            uint32_t pi = (F + (bay[x & 3] >> 2)) >> 2;
            if (pi > 255) pi = 255;
            row[x] = pal[pi];
        }
    }
}

// ── Dots: 원 ∪ 필렛 목 부호거리장(SDF), 절반 해상도 평가 + 보간 + AA ─────────────
namespace {
struct Neck { float hw, fy; };   // hw ≤ 0 이면 없음
inline Neck neck_geometry(float r1, float r2, float dist)
{
    float r = (r1 + r2) * 0.5f, h = dist * 0.5f, rf = r + FILLET_R;
    float q = rf * rf - h * h;
    if (q <= 0.0f) return {0.0f, 0.0f};
    float fy = std::sqrt(q);
    float hw = fy - FILLET_R;
    if (hw < NECK_MIN - 0.5f) return {0.0f, 0.0f};
    return {hw, fy};
}
}  // namespace

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("O2")))
#endif
void DropletField::_render_dots(uint16_t* buf) const
{
    // 스캐터 방식: 절반 해상도 SDF 격자를 +∞ 로 초기화한 뒤, 원과 활성 목을 각자의 바운딩 박스 안에서만 찍는다(min 병합).
    // 샘플마다 이웃을 뒤지는 gather 보다 프리미티브 면적에 비례해 훨씬 싸다.
    const int cols = _cols, rows = _rows, pitch = _pitch, n = _n;
    const float D = (float)pitch, DD = D * 1.41421356f;
    constexpr int HW = W / 2, HH = H / 2;
    static int16_t sdf8[HH + 1][HW + 1];
    static uint8_t csub[HH + 1][HW + 1];
    for (int sy = 0; sy <= HH; ++sy) {
        for (int sx = 0; sx <= HW; ++sx) { sdf8[sy][sx] = 4000; csub[sy][sx] = 0; }
    }
    const float rscale = 31.0f / (_rmax - _rmin);
    static uint8_t rq[NMAX];
    for (int k = 0; k < n; ++k) {
        int b = (int)((_rad[k] - _rmin) * rscale + 0.5f);
        rq[k] = (uint8_t)(b > 31 ? 31 : (b < 0 ? 0 : b));
    }
    auto sample_x = [](int sx) { return (sx < HW) ? sx * 2 : W - 1; };
    auto sample_y = [](int sy) { return (sy < HH) ? sy * 2 : H - 1; };

    // 1) 원
    for (int k = 0; k < n; ++k) {
        const int i = k % cols, j = k / cols;
        const float cx = (float)(_ox + i * pitch), cy = (float)(_oy + j * pitch), r = _rad[k];
        const int reach = (int)(r + 2.5f);
        int sx0 = ((int)cx - reach) / 2, sx1 = ((int)cx + reach + 1) / 2;
        int sy0 = ((int)cy - reach) / 2, sy1 = ((int)cy + reach + 1) / 2;
        if (sx0 < 0) sx0 = 0;
        if (sy0 < 0) sy0 = 0;
        if (sx1 > HW) sx1 = HW;
        if (sy1 > HH) sy1 = HH;
        const uint8_t c = rq[k];
        for (int sy = sy0; sy <= sy1; ++sy) {
            const float dy = (float)sample_y(sy) - cy;
            for (int sx = sx0; sx <= sx1; ++sx) {
                const float dx = (float)sample_x(sx) - cx;
                const int v = (int)((std::sqrt(dx * dx + dy * dy) - r) * 8.0f);
                if (v < sdf8[sy][sx]) { sdf8[sy][sx] = (int16_t)v; csub[sy][sx] = c; }
            }
        }
    }
    // 2) 필렛 목: 방향 0 오른쪽, 1 아래, 2 우하, 3 좌하
    static const float UX[4] = {1.0f, 0.0f, 0.70710678f, -0.70710678f};
    static const float UY[4] = {0.0f, 1.0f, 0.70710678f, 0.70710678f};
    static const int   DI[4] = {1, 0, 1, -1};
    static const int   DJ[4] = {0, 1, 1, 1};
    for (int k = 0; k < n; ++k) {
        const int i = k % cols, j = k / cols;
        for (int d = 0; d < 4; ++d) {
            const int i2 = i + DI[d], j2 = j + DJ[d];
            if (i2 < 0 || i2 >= cols || j2 >= rows) continue;
            const int k2 = j2 * cols + i2;
            const float len = (d < 2) ? D : DD;
            const Neck nk = neck_geometry(_rad[k], _rad[k2], len);
            if (nk.hw <= 0.0f) continue;
            const float cx = (float)(_ox + i * pitch), cy = (float)(_oy + j * pitch);
            const float ex = (float)(_ox + i2 * pitch), ey = (float)(_oy + j2 * pitch);
            const float reach = nk.fy + 1.0f;
            const float bx0 = (cx < ex ? cx : ex) - reach, bx1 = (cx > ex ? cx : ex) + reach;
            const float by0 = cy - reach, by1 = ey + reach;
            int sx0 = (int)bx0 / 2, sx1 = ((int)bx1 + 1) / 2, sy0 = (int)by0 / 2, sy1 = ((int)by1 + 1) / 2;
            if (sx0 < 0) sx0 = 0;
            if (sy0 < 0) sy0 = 0;
            if (sx1 > HW) sx1 = HW;
            if (sy1 > HH) sy1 = HH;
            const uint8_t c = rq[k] < rq[k2] ? rq[k] : rq[k2];
            const float half = len * 0.5f;
            for (int sy = sy0; sy <= sy1; ++sy) {
                const float ry = (float)sample_y(sy) - cy;
                for (int sx = sx0; sx <= sx1; ++sx) {
                    const float rx = (float)sample_x(sx) - cx;
                    const float along = rx * UX[d] + ry * UY[d];
                    if (along < 0.0f || along > len) continue;
                    float perp = -rx * UY[d] + ry * UX[d];
                    if (perp < 0) perp = -perp;
                    if (perp > nk.fy) continue;
                    const float ax = along - half, ay = perp - nk.fy;
                    float ns = FILLET_R - std::sqrt(ax * ax + ay * ay);   // 필렛 원 밖(<0)이어야 목
                    const float band_s = (along < len - along) ? -along : along - len;
                    if (band_s > ns) ns = band_s;
                    const int v = (int)(ns * 8.0f);
                    if (v < sdf8[sy][sx]) { sdf8[sy][sx] = (int16_t)v; csub[sy][sx] = c; }
                }
            }
        }
    }
    // 3) 보간 + AA 커버리지 + 팔레트
    uint16_t* row = buf;
    for (int y = 0; y < H; ++y, row += W) {
        const int sy = y >> 1;
        const int16_t* s0 = sdf8[sy];
        const int16_t* s1 = sdf8[sy + 1];
        const uint8_t* b0 = csub[sy];
        const uint8_t* b1 = csub[sy + 1];
        const int wy = y & 1;
        for (int x = 0; x < W; ++x) {
            const int sx = x >> 1;
            int a = s0[sx], b = s0[sx + 1];
            int ca = b0[sx], cb = b0[sx + 1];
            if (wy) { a = (a + s1[sx]) >> 1; b = (b + s1[sx + 1]) >> 1; ca = (ca + b1[sx]) >> 1; cb = (cb + b1[sx + 1]) >> 1; }
            const int s = (x & 1) ? ((a + b) >> 1) : a;
            const int ci2 = (x & 1) ? ((ca + cb) >> 1) : ca;
            int cov = ((4 - s) * 15) >> 3;
            if (cov <= 0) { row[x] = 0; continue; }
            if (cov > 15) cov = 15;
            row[x] = _pal_dots[ci2][cov];
        }
    }
}

}  // namespace droplet
