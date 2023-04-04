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

// #include "lwip/debug.h"
// #include "lwip/err.h"
// #include "lwip/tcp.h"

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
#include "esp_netif_types.h"
#include "tusb_esp_netif_glue.h"
#include "tusb_net.h"
static esp_netif_t* usb_netif = NULL;

/* this is used by this code, ./class/net/net_driver.c, and usb_descriptors.c */
/* ideally speaking, this should be generated from the hardware's unique ID (if available) */
/* it is suggested that the first byte is 0x02 to indicate a link-local address */
const uint8_t tud_network_mac_address[6] = { 0x02, 0x02, 0x84, 0x6A, 0x96, 0x00 };
uint8_t mac[6] = { 0x02, 0x02, 0x84, 0x6A, 0x96, 0x00 };
/* these test data are used to populate the ARP cache so the IPs are known */
static char arp1[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x01,
    0x08, 0x00, 0x06, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0x01
};
#endif // CONFIG_ESP_TINYUSB_NET_MODE_ECM_RNDIS

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
 *           USB NET Configuration
 *------------------------------------------*/
static esp_netif_driver_ifconfig_t usb_driver_ifconfig = {
    .driver_free_rx_buffer = NULL,
    .transmit = esp_usb_net_transmit,
    .transmit_wrap = esp_usb_net_transmit_wrap,
    .handle = "USB" // this IO object is a singleton, its handle uses as a name
};

void configure_usb_esp_netif(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // ESP Netif configs
    esp_netif_ip_info_t netif_ip_info = { 0 };
    esp_netif_inherent_config_t esp_netif_config = {
        .flags = (ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_GARP | ESP_NETIF_FLAG_EVENT_IP_MODIFIED),
        .get_ip_event = IP_EVENT_STA_GOT_IP,
        .lost_ip_event = IP_EVENT_STA_LOST_IP,
        .if_key = "USB_ETH",
        .if_desc = "usb-eth",
        .route_prio = 101, // one higher than WiFi
    };

    esp_netif_config_t usb_netif_cfg = {
        .base = &esp_netif_config,
        .driver = &usb_driver_ifconfig,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };

    usb_netif = esp_netif_new(&usb_netif_cfg);
    assert(usb_netif);

    esp_tusb_net_handle_t usb_net_handle = calloc(1, sizeof(esp_tusb_net_handle_t));
    ESP_LOGI(TAG, "usb_net_handle %p", usb_net_handle);
    esp_netif_attach(usb_netif, esp_usb_net_new_glue(usb_net_handle));

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 2, 2, 2, 2);
    IP4_ADDR(&ip_info.gw, 2, 2, 2, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_set_ip_info(usb_netif, &ip_info);

    ESP_ERROR_CHECK(esp_netif_set_mac(usb_netif, mac));
    esp_netif_action_start(usb_netif, NULL, 0, NULL);


    // esp_netif_receive(usb_netif, arp1, sizeof(arp1), NULL);

    // bool up = esp_netif_is_netif_up(usb_netif);
    // ESP_LOGI(TAG, "netif up?: %s", up == true ? "Yes" : "No");

    ESP_ERROR_CHECK(tusb_net_init(usb_netif));
    xTaskCreate(usb_net_task, "usb_net_task", 4096, NULL, 6, NULL);
}


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
    configure_usb_esp_netif();

#endif // CONFIG_ESP_TINYUSB_NET_MODE_ECM_RNDIS
}
