#include "app_settings.h"
#include "apps/utils/ui/settings_screen.h"
#include "esp_log.h"

static const char* TAG = "APP_SETTINGS";
// scroll constants
#define DESC_SCROLL_PAUSE 1000
#define DESC_SCROLL_SPEED 20

using namespace MOONCAKE::APPS;

void AppSettings::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();

    scroll_text_init(&_data.desc_scroll_ctx,
                     _data.hal->canvas(),
                     _data.hal->canvas()->width(),
                     16,
                     DESC_SCROLL_SPEED,
                     DESC_SCROLL_PAUSE);
    hl_text_init(&_data.hint_hl_ctx, _data.hal->canvas(), 20, 1500);
    // Get metadata from settings
    _data.groups = _data.hal->settings()->getMetadata();
}

void AppSettings::onResume()
{
    ANIM_APP_OPEN();
    _data.hal->canvas()->fillScreen(THEME_COLOR_BG);
    _data.hal->canvas()->setFont(FONT_16);
    _data.hal->canvas()->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    _data.hal->canvas()->setTextSize(1);
    _data.hal->canvas_update();
}

void AppSettings::onRunning()
{
    // Update the settings screen
    bool need_update = UTILS::UI::SETTINGS_SCREEN::update(_data.hal,
                                                          _data.groups,
                                                          &_data.hint_hl_ctx,
                                                          &_data.desc_scroll_ctx,
                                                          [this]()
                                                          {
                                                              ESP_LOGI(TAG, "handleSettingsMenu on_exit()");
                                                              destroyApp();
                                                          });

    // Update the display if needed
    if (need_update)
    {
        _data.hal->canvas_update();
    }
}

void AppSettings::onDestroy()
{
    // Free scroll context
    scroll_text_free(&_data.desc_scroll_ctx);
    // Free hint text context
    hl_text_free(&_data.hint_hl_ctx);
}