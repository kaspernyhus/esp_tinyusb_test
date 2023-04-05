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
#include "esp_netif.h"
#include "tcp_server.h"
#include "tusb_esp_netif_glue.h"
#include "tusb_net.h"
static esp_netif_t* usb_netif = NULL;

/* this is used by this code, ./class/net/net_driver.c, and usb_descriptors.c */
/* ideally speaking, this should be generated from the hardware's unique ID (if available) */
/* it is suggested that the first byte is 0x02 to indicate a link-local address */
const uint8_t tud_network_mac_address[6] = { 0x02, 0x02, 0x84, 0x6A, 0x96, 0x00 };
uint8_t mac[6] = { 0x02, 0x02, 0x84, 0x6A, 0x96, 0x00 };
/* these test data are used to populate the ARP cache so the IPs are known */
// static char arp1[] = {
//     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x01,
//     0x08, 0x00, 0x06, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x02,
//     0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0x01
// };
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
/** Event handler for Ethernet events */
static void usb_net_event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "Ethernet Link Up");
        break;
    }
    case ETHERNET_EVENT_DISCONNECTED: {
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    }
    case ETHERNET_EVENT_START: {
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    }
    case ETHERNET_EVENT_STOP: {
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    }
    default: {
        break;
    }
    }
}

static void got_ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    const esp_netif_ip_info_t* ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
}

void configure_usb_esp_netif(void)
{
    /* Initialize TCP/IP network interface (should be called only once in application) */
    ESP_ERROR_CHECK(esp_netif_init());
    /* Create default event loop that running in background */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    const esp_netif_ip_info_t ip_info = {
        .ip.addr = ESP_IP4TOADDR(192, 168, 7, 1),
        .netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0),
        .gw.addr = ESP_IP4TOADDR(0, 0, 0, 0)
    };

    const esp_netif_inherent_config_t base = {
        .flags = (esp_netif_flags_t)(ESP_NETIF_FLAG_GARP | ESP_NETIF_FLAG_EVENT_IP_MODIFIED),
        ESP_COMPILER_DESIGNATED_INIT_AGGREGATE_TYPE_EMPTY(mac)
            .ip_info
        = &ip_info,
        .get_ip_event = IP_EVENT_ETH_GOT_IP,
        .lost_ip_event = IP_EVENT_ETH_LOST_IP,
        .if_key = "ETH_DEF",
        .if_desc = "eth",
        .route_prio = 50
    };

    esp_netif_config_t usb_netif_cfg = {
        .base = &base,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };

    // esp_netif_config_t usb_netif_cfg = ESP_NETIF_DEFAULT_ETH();
    usb_netif = esp_netif_new(&usb_netif_cfg);
    assert(usb_netif);

    esp_usb_net_set_default_handlers(usb_netif);
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &usb_net_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    /* attach Ethernet driver to TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_attach(usb_netif, esp_usb_net_new_glue()));


    // esp_netif_action_start(usb_netif, NULL, 0, NULL);


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
    xTaskCreate(tcp_server_task, "tcp_server1", 4096, (void*)1234, 4, NULL);
#endif // CONFIG_ESP_TINYUSB_NET_MODE_ECM_RNDIS
}
