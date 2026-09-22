/*
 * AppDroplet — 런처 아이콘으로 여는 "DROPLET" 앱. 화면 터치로 종료.
 */
#pragma once
#include "droplet_scene.h"
#include <mooncake.h>
#include <memory>

class AppDroplet : public mooncake::AppAbility {
public:
    AppDroplet();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<droplet::DropletScene> _scene;
};
