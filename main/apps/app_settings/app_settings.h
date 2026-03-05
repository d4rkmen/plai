#pragma once

#include "../apps.h"
#include "nvs_flash.h"
#include <vector>
#include <string>

#include "apps/utils/theme/theme_define.h"
#include "apps/utils/anim/anim_define.h"
#include "apps/utils/icon/icon_define.h"
#include "apps/utils/anim/scroll_text.h"
#include "apps/utils/anim/hl_text.h"
#include "apps/utils/ui/dialog.h"
#include "settings/settings.h"

#include "assets/app_settings.h"

using namespace SETTINGS;
namespace MOONCAKE::APPS
{

    class AppSettings : public APP_BASE
    {
    private:
        struct
        {
            HAL::Hal* hal;
            std::vector<SettingGroup_t> groups;
            UTILS::SCROLL_TEXT::ScrollTextContext_t desc_scroll_ctx;
            UTILS::HL_TEXT::HLTextContext_t hint_hl_ctx;
        } _data;

    public:
        void onCreate() override;
        void onResume() override;
        void onRunning() override;
        void onDestroy() override;
    };

    class AppSettings_Packer : public APP_PACKER_BASE
    {
        std::string getAppName() override { return "SETTINGS"; }
        std::string getAppDesc() override { return "Configure system settings"; }
        void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_app_settings, nullptr)); }
        void* newApp() override { return new AppSettings; }
        void deleteApp(void* app) override { delete (AppSettings*)app; }
    };

} // namespace MOONCAKE::APPS