/* USB Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "tinyusb.h"

#include "tusb_audio.h"
#include "esp_signal_generator.h"
esp_sig_gen_t sig_gen;
uint8_t audio_data[CFG_TUD_AUDIO_EP_SZ_IN];

static const char *TAG = "USB audio example";

/*------------------------------------------*
 *               USB Callback
 *------------------------------------------*/
void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB mounted.");
}

/*------------------------------------------*
 *           USB AUDIO Callback
 *------------------------------------------*/
#if CONFIG_ESP_TINYUSB_AUDIO_ENABLED
bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
    (void)rhport;
    (void)itf;
    (void)ep_in;
    (void)cur_alt_setting;

    tud_audio_write(audio_data, CFG_TUD_AUDIO_EP_SZ_IN);

    return true;
}

bool tud_audio_tx_done_post_load_cb(uint8_t rhport, uint16_t n_bytes_copied, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
    (void)rhport;
    (void)n_bytes_copied;
    (void)itf;
    (void)ep_in;
    (void)cur_alt_setting;

    esp_sig_gen_fill(&sig_gen, audio_data, CFG_TUD_AUDIO_EP_SZ_IN, 48 * CONFIG_AUDIO_CHANNELS);

    return true;
}
#endif

/*------------------------------------------*
 *                 APP MAIN
 *------------------------------------------*/
void app_main(void)
{
    ESP_LOGI(TAG, "------------ app_main -----------");

    ESP_LOGI(TAG, "USB initialization");
    // Install TINYUSB driver
    tinyusb_config_t tusb_cfg = {
        .external_phy = false,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    ESP_LOGI(TAG, "USB initialization DONE");

    esp_sig_gen_config_t sig_gen_cfg = {}; // default configuration from menuconfig
    esp_sig_gen_init(&sig_gen, &sig_gen_cfg);
    tusb_audio_init();
}
