#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#define WIFI_SSID_KEY "wifi_ssid"
#define WIFI_PASS_KEY "wifi_pass"

#define DEFAULT_AP_SSID "ESP32_AP"
#define DEFAULT_AP_PASS "12345678"

static const char* TAG = "wifi_setup";

void wifi_init_sta(const char* ssid, const char* pass);
void wifi_init_ap(void);

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
   if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
      esp_wifi_connect();
      ESP_LOGI(TAG, "WiFi station started, attempting to connect...");
   } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
      ESP_LOGI(TAG, "Disconnected from WiFi, retrying...");
      esp_wifi_connect();
   } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
      ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
   } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
      ESP_LOGI(TAG, "Access point started");
   } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP) {
      ESP_LOGI(TAG, "Access point stopped");
   }
}

void app_main(void) {
   esp_err_t ret = nvs_flash_init();
   if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
   }
   ESP_ERROR_CHECK(ret);

   ESP_ERROR_CHECK(esp_netif_init());
   ESP_ERROR_CHECK(esp_event_loop_create_default());

   ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
   ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

   // Open NVS handle
   nvs_handle_t nvs_handle;
   ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
   if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error opening NVS handle!");
   }

   size_t ssid_len = 32;
   size_t pass_len = 64;
   char ssid[ssid_len];
   char pass[pass_len];

   // Read SSID from NVS
   ret = nvs_get_str(nvs_handle, WIFI_SSID_KEY, ssid, &ssid_len);
   if (ret == ESP_OK) {
      // SSID found, read password
      ret = nvs_get_str(nvs_handle, WIFI_PASS_KEY, pass, &pass_len);
      if (ret == ESP_OK) {
         ESP_LOGI(TAG, "SSID and password found in NVS, starting in STA mode");
         wifi_init_sta(ssid, pass);
      } else {
         ESP_LOGE(TAG, "Error reading WiFi password from NVS");
         wifi_init_ap();
      }
   } else {
      ESP_LOGI(TAG, "No SSID found in NVS, starting in AP mode");
      wifi_init_ap();
   }

   nvs_close(nvs_handle);
}

void wifi_init_sta(const char* ssid, const char* pass) {
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_wifi_init(&cfg));

   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

   wifi_config_t wifi_config = {
       .sta =
           {
               .ssid = "",
               .password = "",
           },
   };
   strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
   strncpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

   ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
   ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_init_ap(void) {
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_wifi_init(&cfg));

   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

   wifi_config_t wifi_config = {
       .ap =
           {
               .ssid = DEFAULT_AP_SSID,
               .ssid_len = strlen(DEFAULT_AP_SSID),
               .password = DEFAULT_AP_PASS,
               .max_connection = 4,
               .authmode = WIFI_AUTH_WPA_WPA2_PSK,
           },
   };

   if (strlen(DEFAULT_AP_PASS) == 0) {
      wifi_config.ap.authmode = WIFI_AUTH_OPEN;
   }

   ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
   ESP_ERROR_CHECK(esp_wifi_start());
   esp_netif_t* netif = esp_netif_create_default_wifi_ap();
   esp_netif_dhcps_start(netif);  // Start DHCP server
}
