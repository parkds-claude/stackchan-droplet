/*
 * DropletScene — LVGL 화면 + 캔버스 + 마이크 레벨 + 끄덕임을 묶은 물방울 씬.
 * AppDroplet(런처 아이콘으로 여는 앱)과 런처 유휴 스크린세이버가 공유한다.
 * 규격: README.md
 */
#pragma once
#include "droplet_field.h"
#include "galaxy_field.h"
#include <smooth_lvgl.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace droplet {

class DropletScene {
public:
    DropletScene();
    ~DropletScene();

    /// 화면 구성 + 마이크 켜기. 이전 활성 화면은 소멸 시 복구된다(런처 스크린세이버 트릭).
    void init();
    /// 매 루프 호출. 33ms 프레임 게이팅. LVGL 락은 내부에서 필요한 구간만 잡는다.
    void update();
    /// 화면을 터치했는가 (앱이 close() 판단에 사용). 스크린세이버는 lv inactive time 으로 자동 해제.
    bool clicked() const { return _clicked; }
    float level() const { return _level.level; }
    enum class SceneStyle : uint8_t { Dots = 0, Blob = 1, Galaxy = 2 };
    SceneStyle style() const { return _style; }
    /// 스타일 순환(Dots → Blob → Galaxy). NVS "droplet/style" 에 저장. 머리 터치 스와이프로도 호출된다.
    void toggleStyle();
    uint32_t lastFrameMs() const { return _last_frame_ms; }

    static constexpr uint32_t FRAME_MS          = 33;
    static constexpr float    VOICE_NOD_LEVEL   = 0.55f;
    static constexpr float    VOICE_REARM_LEVEL = 0.25f;
    static constexpr uint32_t VOICE_NOD_COOLDOWN_MS = 8000;
    static constexpr int      NOD_PITCH_DELTA   = 220;   // 0.1도 단위. head_pet "抬头" 150~250 과 같은 범위
    static constexpr size_t   MIC_FRAMES        = 120;   // 24kHz 기준 5ms
    static constexpr uint32_t SWIPE_DEBOUNCE_MS = 1500;
    static constexpr uint32_t SWIPE_REPEAT_MS   = 1200;   // 한 제스처가 두 번 보고되는 것 무시

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::ScreenActive> _prev_screen;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Screen> _screen;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Canvas> _canvas;
    DropletField _field{Style::Dots};
    galaxy::GalaxyField _galaxy;
    SceneStyle _style = SceneStyle::Dots;
    LevelState _level;
    int _headpet_conn = -1;
    volatile bool _swipe_flag = false;
    uint32_t _swipe_at = 0;
    void _apply_style();
    std::vector<int16_t> _mic_chunk;
    std::vector<uint16_t> _line_buf;   // 스트라이드 불일치 시에만 사용
    bool _mic_ok = false;
    volatile bool _clicked = false;
    uint32_t _frame_at = 0;
    uint32_t _last_frame_ms = 0;
    uint32_t _log_at = 0;
    uint32_t _init_at = 0;
    // 끄덕임 상태
    bool _nod_armed = true;
    uint32_t _nod_at = 0;
    int _nod_phase = 0;        // 0 대기, 1 올리는 중, 2 내리는 중
    int _nod_base_pitch = 0;

    void _mic_poll();
    void _nod_update(uint32_t now);
};

}  // namespace droplet
