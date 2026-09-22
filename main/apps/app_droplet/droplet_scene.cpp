#include "droplet_scene.h"
#include <hal/hal.h>
#include <stackchan/stackchan.h>
#include <mooncake_log.h>
#include <board.h>
#include <audio/audio_codec.h>
#include <settings.h>
#include <cmath>
#include <cstring>

using namespace smooth_ui_toolkit::lvgl_cpp;

namespace droplet {

static const char* _tag = "DROPLET";

DropletScene::DropletScene() = default;

static const char* style_name(DropletScene::SceneStyle s)
{
    return s == DropletScene::SceneStyle::Dots ? "dots" : (s == DropletScene::SceneStyle::Blob ? "blob" : "galaxy");
}

static DropletScene::SceneStyle load_style()
{
    Settings st("droplet", false);
    int v = st.GetInt("style", 0);
    if (v < 0 || v > 2) v = 0;
    return (DropletScene::SceneStyle)v;
}

void DropletScene::_apply_style()
{
    if (_style == SceneStyle::Dots) _field.setStyle(Style::Dots);
    else if (_style == SceneStyle::Blob) _field.setStyle(Style::Blob);
}

void DropletScene::toggleStyle()
{
    _style = (SceneStyle)(((int)_style + 1) % 3);
    _apply_style();
    Settings st("droplet", true);
    st.SetInt("style", (int)_style);
    mclog::tagInfo(_tag, "style → {}", style_name(_style));
}

DropletScene::~DropletScene()
{
    if (_nod_phase != 0) {
        // 끄덕이던 중에 씬이 닫히면 고개를 원위치로 (고개가 들린 채 남는 것 방지)
        GetStackChan().motion().movePitchWithSpeed(_nod_base_pitch, 400);
        _nod_phase = 0;
    }
    if (_headpet_conn >= 0) {
        GetHAL().onHeadPetGesture.disconnect(_headpet_conn);
        _headpet_conn = -1;
    }
    {
        LvglLockGuard lock;
        _canvas.reset();
        _screen.reset();
        if (_prev_screen) {
            _prev_screen->load();
            _prev_screen.reset();
        }
    }
    if (_mic_ok) {
        GetHAL().clearupMicTest();   // 입력 코덱 닫기 (SETUP 마이크 테스트와 동일 경로)
    }
}

void DropletScene::init()
{
    {
        LvglLockGuard lock;
        _prev_screen = std::make_unique<ScreenActive>();
        _screen = std::make_unique<Screen>();
        _screen->setBgColor(lv_color_hex(0x000000));
        _screen->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _screen->setPadding(0, 0, 0, 0);
        _screen->load();

        _canvas = std::make_unique<Canvas>(_screen->get());
        _canvas->createBuffer(W, H, LV_COLOR_FORMAT_RGB565);   // ≈150KB → PSRAM
        _canvas->setPos(0, 0);
        _canvas->setSize(W, H);
        _canvas->addFlag(LV_OBJ_FLAG_CLICKABLE);
        _canvas->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _canvas->onClick().connect([this]() { _clicked = true; });
    }

    // 마이크: 런처 단계에서는 xiaozhi 오디오 서비스가 안 떠 있어 코덱 입력을 직접 연다
    auto audio_codec = Board::GetInstance().GetAudioCodec();
    if (audio_codec) {
        const size_t ch = audio_codec->input_channels() > 0 ? audio_codec->input_channels() : 1;
        _mic_chunk.assign(MIC_FRAMES * ch, 0);
        if (!audio_codec->input_enabled()) {
            audio_codec->EnableInput(true);
        }
        _mic_ok = true;
    } else {
        mclog::tagInfo(_tag, "audio codec unavailable - running without mic");
    }
    _style = load_style();
    _apply_style();
    // 머리 위 터치 패널 쓸어넘기기 → 스타일 전환 (시그널은 다른 태스크에서 발화 → 플래그만 세우고 update 에서 처리)
    _headpet_conn = GetHAL().onHeadPetGesture.connect([this](HeadPetGesture g) {
        if (g == HeadPetGesture::SwipeForward || g == HeadPetGesture::SwipeBackward) _swipe_flag = true;
    });
    _frame_at = GetHAL().millis();
    _log_at = _frame_at;
    _init_at = _frame_at;
    // 1회 벤치마크: 두 스타일 렌더 시간 로그 (NVS 스타일과 무관하게 측정)
    {
        lv_draw_buf_t* db = lv_canvas_get_draw_buf(_canvas->get());
        if (db && db->data && db->header.stride == (uint32_t)(W * 2)) {
            uint16_t* fb = reinterpret_cast<uint16_t*>(db->data);
            for (int st = 0; st < 3; ++st) {
                uint32_t t0 = GetHAL().millis();
                if (st < 2) {
                    _field.setStyle(st == 0 ? Style::Dots : Style::Blob);
                    for (int f = 0; f < 5; ++f) { _field.step(0.3f); _field.render(fb); }
                } else {
                    for (int f = 0; f < 5; ++f) { _galaxy.step(0.3f); _galaxy.render(fb); }
                }
                mclog::tagInfo(_tag, "bench {}: {} ms/frame", st == 0 ? "dots" : (st == 1 ? "blob" : "galaxy"), (GetHAL().millis() - t0) / 5);
            }
            _apply_style();
        }
    }
    // 유휴 자세 정렬: 머리 터치 반응 등으로 고개가 들린 채 남아 있으면 홈으로 (모디파이어 잠금 중이면 건너뜀)
    {
        auto& motion = GetStackChan().motion();
        if (!motion.isModifyLocked()) {
            auto cur = motion.getCurrentAngles();
            if (cur.y > 120 || cur.x > 150 || cur.x < -150) {
                mclog::tagInfo(_tag, "head not home (yaw {} pitch {}) → goHome", cur.x, cur.y);
                motion.goHome(300);
            }
        }
    }
    mclog::tagInfo(_tag, "scene init (mic={}, style={})", _mic_ok, style_name(_style));
}

void DropletScene::_mic_poll()
{
    if (!_mic_ok) return;
    auto audio_codec = Board::GetInstance().GetAudioCodec();
    if (!audio_codec) return;
    if (!audio_codec->InputData(_mic_chunk)) return;
    const size_t ch = audio_codec->input_channels() > 0 ? audio_codec->input_channels() : 1;
    uint64_t acc = 0;
    for (size_t i = 0; i < MIC_FRAMES; ++i) {
        int32_t s = _mic_chunk[i * ch];      // 채널 0 = 마이크 (채널 1 = AEC 레퍼런스)
        acc += (uint64_t)(s * s);
    }
    float rms = std::sqrt((float)acc / (float)MIC_FRAMES);
    _level.update(rms);
}

void DropletScene::_nod_update(uint32_t now)
{
    auto& motion = GetStackChan().motion();
    const float lv = _level.level;
    if (_nod_phase == 0) {
        if (lv >= VOICE_NOD_LEVEL) {
            if (_nod_armed && (now - _nod_at) >= VOICE_NOD_COOLDOWN_MS && !motion.isMoving() &&
                !motion.isModifyLocked()) {
                _nod_armed = false;
                _nod_at = now;
                auto cur = motion.getCurrentAngles();
                _nod_base_pitch = cur.y;
                int target = cur.y + NOD_PITCH_DELTA;
                if (target > 540) target = 540;
                if (target < 0) target = 0;
                motion.movePitchWithSpeed(target, 500);
                _nod_phase = 1;
                mclog::tagInfo(_tag, "voice level {:.2f} → nod (pitch {} → {})", lv, cur.y, target);
            }
        } else if (lv < VOICE_REARM_LEVEL) {
            _nod_armed = true;
        }
    } else if (_nod_phase == 1) {
        if (!motion.isMoving()) {
            motion.movePitchWithSpeed(_nod_base_pitch, 400);
            _nod_phase = 2;
        }
    } else if (_nod_phase == 2) {
        if (!motion.isMoving()) {
            _nod_phase = 0;
        }
    }
}

void DropletScene::update()
{
    const uint32_t now = GetHAL().millis();
    if (now - _frame_at < FRAME_MS) return;
    const uint32_t t0 = now;
    _frame_at = now;

    if (_swipe_flag) {
        _swipe_flag = false;
        if (now - _init_at > SWIPE_DEBOUNCE_MS && now - _swipe_at > SWIPE_REPEAT_MS) {   // 시작 직후·중복 보고 무시
            _swipe_at = now;
            toggleStyle();
        }
    }
    _mic_poll();
    if (_style == SceneStyle::Galaxy) _galaxy.step(_level.level);
    else                              _field.step(_level.level);

    // 캔버스 버퍼에 직접 렌더 (LVGL 락 없이 — 플러시와 겹쳐도 느린 모션이라 티어링이 보이지 않는다)
    lv_draw_buf_t* db = lv_canvas_get_draw_buf(_canvas->get());
    if (db && db->data) {
        const uint32_t stride = db->header.stride;
        if (stride == (uint32_t)(W * 2)) {
            if (_style == SceneStyle::Galaxy) _galaxy.render(reinterpret_cast<uint16_t*>(db->data));
            else                              _field.render(reinterpret_cast<uint16_t*>(db->data));
        } else {
            // 스트라이드가 다르면 행 단위 복사 (예비 버퍼는 PSRAM — 정적 DRAM 배열 금지: 링커 dram0 오버플로 방지)
            if (_line_buf.size() != (size_t)(W * H)) _line_buf.assign((size_t)W * H, 0);
            if (_style == SceneStyle::Galaxy) _galaxy.render(_line_buf.data());
            else                              _field.render(_line_buf.data());
            for (int y = 0; y < H; ++y) {
                memcpy(db->data + (size_t)y * stride, _line_buf.data() + (size_t)y * W, W * 2);
            }
        }
    }
    {
        LvglLockGuard lock;
        lv_obj_invalidate(_canvas->get());
    }

    _nod_update(now);

    _last_frame_ms = GetHAL().millis() - t0;
    if (now - _log_at > 5000) {
        _log_at = now;
        mclog::tagInfo(_tag, "frame {} ms, level {:.2f} (env {:.0f} floor {:.0f})", _last_frame_ms, _level.level,
                       _level.env, _level.floor);
    }
}

}  // namespace droplet
