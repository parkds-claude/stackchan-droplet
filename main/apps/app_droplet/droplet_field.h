/*
 * DropletField — 소음 반응 물방울 필드 렌더러 v3 (두 스타일). ESP/LVGL 의존 없음(호스트 미리보기 가능).
 *
 * Style::Dots  — 도트 매트릭스 룩: 16×12 도트(반지름 3~11)가 필렛 목으로 캡슐처럼 이어짐.
 *                픽셀 단위 부호거리장(SDF: 원 ∪ 필렛 목)으로 그려 안티에일리어싱된 고화질.
 * Style::Blob  — 액체 룩: 12×9 격자, 유한 지지 메타볼 커널 합 → 부드럽게 녹아 합쳐짐.
 * 색: 주황-레드 계열(작은 방울 깊은 레드 → 큰 방울 주황·살구), 색상은 그 범위에서 천천히 흔들림. 소음이 크면 밝아진다.
 * 규격: README.md
 */
#pragma once
#include <cstdint>
#include <cstddef>

namespace droplet {

constexpr int W    = 320;
constexpr int H    = 240;
constexpr int NMAX = 16 * 12;

enum class Style : uint8_t { Blob = 0, Dots = 1 };

// 소음 → 레벨 (적응형 노이즈 플로어)
constexpr float ENV_ATTACK    = 0.55f;
constexpr float ENV_RELEASE   = 0.08f;
constexpr float FLOOR_RISE    = 0.010f;
constexpr float FLOOR_MIN     = 20.0f;
constexpr float LEVEL_OCTAVES = 4.0f;

struct LevelState {
    float env   = 0.0f;
    float floor = FLOOR_MIN;
    float level = 0.0f;
    bool  boot  = false;
    void update(float rms);
};

class DropletField {
public:
    explicit DropletField(Style style = Style::Dots);

    void  setStyle(Style s);
    Style style() const { return _style; }

    void step(float level, float dtFrames = 1.0f);
    /// RGB565 버퍼 W*H (스트라이드 W) 에 렌더.
    void render(uint16_t* buf) const;

    float time() const { return _t; }

private:
    Style _style;
    // 격자 (스타일별)
    int   _cols = 16, _rows = 12, _pitch = 20, _ox = 10, _oy = 10, _n = NMAX;
    float _rmin = 3.0f, _rmax = 11.0f;
    bool  _wobble = false;

    float _t = 0.0f;
    float _level = 0.0f;
    float _rad[NMAX];
    float _jitter[NMAX];
    uint8_t _ring[NMAX];

    uint16_t _pal_blob[256];        // Blob: 필드값 F×64 → 색
    uint16_t _pal_dots[32][16];     // Dots: 반지름 32단(연속 그라데이션) × 커버리지(0..15) → 색

    void _apply_style();
    void _update_field();
    void _update_palettes();
    void _render_blob(uint16_t* buf) const;
    void _render_dots(uint16_t* buf) const;
};

uint16_t hsv_rgb565(float h, float s, float v);

}  // namespace droplet
