#include "my_wifi.h"

static const char *TAG_WIFI_AP = "WiFi AP";
static const char *TAG_WIFI_STA = "WiFi STA";
static const char *TAG_WIFI_NVS = "NVS";

EventGroupHandle_t wifi_event_group = NULL;

char device_number[2] = {0};
char ssid[MAX_WIFI_CHAR] = {0};
char pass[MAX_WIFI_CHAR] = {0};

httpd_handle_t server = NULL;

static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
   switch (event_id) {
      case WIFI_EVENT_AP_START:
         xEventGroupSetBits(wifi_event_group, WIFI_AP_STARTED_BIT);
         ESP_LOGI(TAG_WIFI_AP, "Access Point started");
         break;

      case WIFI_EVENT_AP_STOP:
         ESP_LOGI(TAG_WIFI_AP, "Access Point stopped");
         break;

      case WIFI_EVENT_AP_STACONNECTED:
         wifi_event_ap_staconnected_t *event_ap_staconnected = (wifi_event_ap_staconnected_t *)event_data;
         ESP_LOGI(TAG_WIFI_AP, "%s connected to AP, ID: %d", event_ap_staconnected->mac, event_ap_staconnected->aid);
         break;

      case WIFI_EVENT_AP_STADISCONNECTED:
         wifi_event_ap_stadisconnected_t *event_ap_stadisconnected = (wifi_event_ap_stadisconnected_t *)event_data;
         ESP_LOGI(TAG_WIFI_AP, "%s disconnected from AP, ID: %d", event_ap_stadisconnected->mac,
                  event_ap_stadisconnected->aid);
         break;

      case WIFI_EVENT_STA_START:
         ESP_LOGI(TAG_WIFI_STA, "Wi-Fi starting...");
         break;

      case WIFI_EVENT_STA_CONNECTED:
         xEventGroupSetBits(wifi_event_group, WIFI_STA_STARTED_BIT);
         wifi_event_sta_connected_t *event_sta_connected = (wifi_event_sta_connected_t *)event_data;
         ESP_LOGI(TAG_WIFI_STA, "Connected to AP, SSID: %s", event_sta_connected->ssid);
         break;

      case WIFI_EVENT_STA_DISCONNECTED:
         wifi_event_sta_disconnected_t *event_sta_disconnected = (wifi_event_sta_disconnected_t *)event_data;
         ESP_LOGE(TAG_WIFI_STA, "Wi-Fi lost connection to AP %s, reason: %d", event_sta_disconnected->ssid,
                  event_sta_disconnected->reason);
         esp_wifi_connect();
         ESP_LOGE(TAG_WIFI_STA, "Retrying connection...");
         break;

      case IP_EVENT_STA_GOT_IP:
         ip_event_got_ip_t *ip_event_got_ip = (ip_event_got_ip_t *)event_data;
         ESP_LOGI(TAG_WIFI_STA, "Got IP: " IPSTR, IP2STR(&ip_event_got_ip->ip_info.ip));
         xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_TO_AP_BIT);
         break;

      default:
         ESP_LOGW(TAG_WIFI_STA, "Unhandled event ID: %ld", event_id);
   }
}

void wifi_start_as_ap() {
   esp_netif_create_default_wifi_ap();
   wifi_config_t wifi_config = {.ap = {.ssid = AP_SSID, .max_connection = 4, .authmode = WIFI_AUTH_OPEN}};
   esp_wifi_set_mode(WIFI_MODE_AP);
   esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
   esp_wifi_start();
   ESP_LOGW(TAG_WIFI_AP, "Wi-Fi AP started as SSID: %s", AP_SSID);
}

void wifi_init() {
   wifi_event_group = xEventGroupCreate();
   esp_netif_init();
   esp_event_loop_create_default();

   wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
   esp_wifi_init(&wifi_initiation);

   esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
   esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

   if (getWifiCredentials() == ESP_OK) {
      esp_netif_create_default_wifi_sta();
      esp_wifi_set_mode(WIFI_MODE_STA);
      wifi_config_t wifi_configuration = {
          .sta = {.failure_retry_cnt = WIFI_RETRY_MAX, .scan_method = WIFI_ALL_CHANNEL_SCAN}};

      snprintf((char *)wifi_configuration.sta.ssid, sizeof(wifi_configuration.sta.ssid), "%s", ssid);
      snprintf((char *)wifi_configuration.sta.password, sizeof(wifi_configuration.sta.password), "%s", pass);

      esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
      esp_wifi_start();

      ESP_LOGI(TAG_WIFI_STA, "Wi-Fi initialization complete. Attempting to connect to AP: %s", ssid);
      esp_wifi_connect();
   } else {
      wifi_start_as_ap();
      server = start_webserver();
      ESP_LOGI(TAG_WIFI_AP, "Webserver started, configure Wi-Fi credentials via browser (192.168.4.1/config)");
      while (true)
      {
         delay_millis(1000);
      }
      
   }
}

void nvs_init() {
   esp_err_t ret = nvs_flash_init();
   if (gpio_get_level(BUTTON_RESTART_PIN) == PRESSED) {
      ESP_LOGW(TAG_WIFI_NVS, "Manual NVS erase requested");
      delay_seconds(2);
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
   }

   if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_LOGW(TAG_WIFI_NVS, "Error (%s) initializing NVS, erasing...", esp_err_to_name(ret));
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
   }
   ESP_ERROR_CHECK(ret);
}

esp_err_t getWifiCredentials() {
   nvs_handle_t nvs_handle;
   esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
   if (err != ESP_OK) {
      ESP_LOGE(TAG_WIFI_NVS, "Error (%s) opening NVS handle", esp_err_to_name(err));
      return ESP_ERR_NVS_NOT_FOUND;
   }
   size_t device_size = sizeof(device_number);
   size_t ssid_size = sizeof(ssid);
   size_t pass_size = sizeof(pass);
   if (nvs_get_str(nvs_handle, "device_number", device_number, &device_size) != ESP_OK) {
      ESP_LOGW(TAG_WIFI_NVS, "Error getting device number");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   if (nvs_get_str(nvs_handle, "ssid", ssid, &ssid_size) != ESP_OK) {
      ESP_LOGE(TAG_WIFI_NVS, "Error getting SSID");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   if (nvs_get_str(nvs_handle, "pass", pass, &pass_size) != ESP_OK) {
      ESP_LOGE(TAG_WIFI_NVS, "Error getting password");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   ESP_LOGW(TAG_WIFI_NVS, "Device number: %s, SSID: %s, Password: %s", device_number, ssid, pass);
   nvs_close(nvs_handle);
   return ESP_OK;
}

esp_err_t set_wifi_credentials_and_id(const char *ssid, const char *pass, char *device_number) {
   nvs_handle_t nvs_handle;
   ESP_LOGI(TAG_WIFI_NVS, "Setting %s, %s, %s", ssid, pass, device_number);
   esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
   if (err != ESP_OK) {
      ESP_LOGE(TAG_WIFI_NVS, "Error (%s) opening NVS handle", esp_err_to_name(err));
      return err;
   }
   if (nvs_set_str(nvs_handle, "device_number", device_number) != ESP_OK) {
      ESP_LOGE(TAG_WIFI_NVS, "Error setting device name");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   if (nvs_set_str(nvs_handle, "ssid", ssid) != ESP_OK) {
      ESP_LOGE(TAG_WIFI_NVS, "Error setting SSID");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   if (nvs_set_str(nvs_handle, "pass", pass) != ESP_OK) {
      ESP_LOGE(TAG_WIFI_NVS, "Error setting password");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   nvs_close(nvs_handle);
   return ESP_OK;
}