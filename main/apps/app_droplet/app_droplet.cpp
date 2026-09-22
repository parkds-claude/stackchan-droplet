#include "app_droplet.h"
#include <hal/hal.h>
#include <stackchan/stackchan.h>
#include <mooncake_log.h>

using namespace mooncake;

AppDroplet::AppDroplet()
{
    setAppInfo().name = "DROPLET";
    // 아이콘은 assets 파티션이 필요하므로 생략(런처가 nullptr 처리: 이름 라벨 + 테마색)
    static uint32_t theme_color = 0x2A6F97;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppDroplet::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppDroplet::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _scene = std::make_unique<droplet::DropletScene>();
    _scene->init();
}

void AppDroplet::onRunning()
{
    if (!_scene) return;
    _scene->update();
    GetStackChan().update();
    if (_scene->clicked()) {
        close();
    }
}

void AppDroplet::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    _scene.reset();
}
