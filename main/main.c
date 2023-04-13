/* USB Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "sdkconfig.h"

#include "tinyusb.h"

#if CONFIG_ESP_TINYUSB_CDC_ENABLED
#include "tusb_cdc_acm.h"
static uint8_t buf[CONFIG_ESP_TINYUSB_CDC_RX_BUFSIZE + 1];
#endif

#if CONFIG_ESP_TINYUSB_AUDIO_ENABLED
#include "esp_signal_generator.h"
#include "tusb_audio.h"
esp_sig_gen_t sig_gen;
uint8_t audio_data[CFG_TUD_AUDIO_EP_SZ_IN];
#endif

#if CONFIG_ESP_TINYUSB_NET_MODE_ECM_RNDIS
#include "tcp_server.h"
#include "tusb_net.h"
#include "websocket_server.h"
static httpd_handle_t server = NULL;
#endif

static const char* TAG = "USB example";

/*------------------------------------------*
 *               USB Callback
 *------------------------------------------*/
void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB mounted.");
}

/*------------------------------------------*
 *             USB CDC Callback
 *------------------------------------------*/
#if CONFIG_ESP_TINYUSB_CDC_ENABLED
void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t* event)
{
    /* initialization */
    size_t rx_size = 0;

    /* read */
    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, CONFIG_ESP_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    if (ret == ESP_OK) {
        buf[rx_size] = '\0';
        ESP_LOGI(TAG, "Got data (%d bytes): %s", rx_size, buf);
    } else {
        ESP_LOGE(TAG, "Read error");
    }

    /* write back */
    tinyusb_cdcacm_write_queue(itf, buf, rx_size);
    tinyusb_cdcacm_write_flush(itf, 0);
}

void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t* event)
{
    int dtr = event->line_state_changed_data.dtr;
    int rst = event->line_state_changed_data.rts;
    ESP_LOGI(TAG, "Line state changed! dtr:%d, rst:%d", dtr, rst);
}
#endif // CONFIG_ESP_TINYUSB_CDC_ENABLED

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
#endif // CONFIG_ESP_TINYUSB_AUDIO_ENABLED


/*------------------------------------------*
 *                 APP MAIN
 *------------------------------------------*/
void app_main(void)
{
    ESP_LOGI(TAG, "------------ app_main -----------");

#if CONFIG_ESP_TINYUSB_CDC_ENABLED
    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 64,
        .callback_rx = &tinyusb_cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
#endif

#if CONFIG_ESP_TINYUSB_AUDIO_ENABLED
    esp_sig_gen_config_t sig_gen_cfg = {}; // default configuration from menuconfig
    esp_sig_gen_init(&sig_gen, &sig_gen_cfg);
    tusb_audio_init();
#endif

#if CONFIG_ESP_TINYUSB_NET_MODE_ECM_RNDIS
    tusb_net_init();
    // xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)1234, 4, NULL);
    server = start_webserver();
#endif

    // Install TINYUSB driver
    tinyusb_config_t tusb_cfg = {
        .external_phy = false,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
}
