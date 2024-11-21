#include <esp_http_server.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "myMQTT.h"
#include "mySMTP.h"
#include "nvs_flash.h"

// Constants
// #define SSID "ESP_NET"
// #define PASS "ESP_NET_IOT"
// #define SSID "IoT_AP"
// #define PASS "12345678"
// Default AP NAME
#define AP_SSID "ESP_AP_CONFIG"
#define MAX_CHAR 32

#define LED GPIO_NUM_2
#define ADC_SELECTED GPIO_NUM_34
#define ADC1_CHANNEL ADC_CHANNEL_6
#define ADC_WIDTH ADC_BITWIDTH_12
#define ADC_ATTEN ADC_ATTEN_DB_0
#define WIFI_RETRY_MAX 20
#define NACK_RESPONSE "NACK"
#define ACK_RESPONSE "ACK"
#define WRITE_INSTRUCTION 'W'
#define READ_INSTRUCTION 'R'
#define LED_ELEMENT 'L'
#define ADC_ELEMENT 'A'
#define PWM_ELEMENT 'P'
#define BUFFER_SIZE 128
#define PORT 8266
// #define HOST_IP_ADDR "192.168.1.69"  // Local IP
#define HOST_IP_ADDR "82.180.173.228"  // IoT Server
#define LED_PWM GPIO_NUM_21
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_CHANNEL 0
#define LEDC_MODE 0
#define LEDC_OUTPUT_IO GPIO_NUM_15
#define LEDC_DUTY_RESOLUTION LEDC_TIMER_10_BIT
#define TWO_TO_THE_POWER_OF_10 1024  // Manually calculated to avoid math.h
#define LEDC_FREQUENCY 400

#define RESTART_PIN GPIO_NUM_22

#define BUTTON_SEND_MESSAGE GPIO_NUM_23
#define BUTTON_BOUNCE_TIME 150
#define SECOND_IN_MILLIS 1000
#define SEND_MESSAGE_DELAY_TIME 60 * SECOND_IN_MILLIS
#define RELEASED 0
#define PRESSED 1
#define MINIMUM_DELAY_MS 10
#define FREED 1
#define PUSHED 0

#define IDENTIFIER "UABC"
#define USER_KEY "EGC"
#define LOG_IN_MESSAGE "Log in"
#define KEEP_ALIVE_MESSAGE "Keep Alive"
#define MESSAGE_MESSAGE "Mensaje enviado desde ESP32"

#define WIFI_AP_STARTED_BIT BIT0
#define WIFI_STA_STARTED_BIT BIT1
#define WIFI_CONNECTED_TO_AP_BIT BIT2

enum {
   SMS_SEND = 0,
   MQTT_PUBLISH,
   MQTT_SUBSCRIBE,
   MQTT_UNSUBSCRIBE,
   SMTP_SEND,
};

static char prefix[64] = {0};
static const char *TAG = "Prototipo en Red Local";
static const char *TAG_WIFI_AP = "WiFi AP";
static const char *TAG_WIFI_STA = "WiFi STA";
static const char *TAG_NVS = "NVS";
static const char *TAG_SERVER = "WEB SERVER";
static const char *log_in = "UABC:EGC:0:L:S:Log in";
static const char *keep_alive = "UABC:EGC:0:K:S:Keep alive";
static const char *message = "UABC:EGC:M:S:6656560351:Mensaje enviado desde ESP32";

int32_t lastStateChange = 0;
TaskHandle_t keep_alive_task_handle = NULL;

// Global variables
bool wifi_connected = false;
bool logged_in = false;
bool is_mqtt_connected = false;
int retry_num = 0;
int sock = 0;
adc_oneshot_unit_handle_t adc1_handle;
EventGroupHandle_t wifi_event_group = NULL;
EventGroupHandle_t mqtt_event_group = NULL;
char deviceNumber[MAX_CHAR] = {0};
char ssid[MAX_CHAR] = {0};
char pass[MAX_CHAR] = {0};
httpd_handle_t server = NULL;
esp_mqtt_client_handle_t client = NULL;

void gpio_init() {
   gpio_config_t io_conf;
   io_conf.intr_type = GPIO_INTR_DISABLE;
   io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
   io_conf.pin_bit_mask = (1ULL << LED);
   io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
   io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
   gpio_config(&io_conf);

   io_conf.intr_type = GPIO_INTR_DISABLE;
   io_conf.mode = GPIO_MODE_INPUT;
   io_conf.pin_bit_mask = (1ULL << ADC_SELECTED);
   io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
   io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
   gpio_config(&io_conf);

   io_conf.intr_type = GPIO_INTR_NEGEDGE;
   io_conf.mode = GPIO_MODE_INPUT;
   io_conf.pin_bit_mask = (1 << BUTTON_SEND_MESSAGE | 1 << RESTART_PIN);
   io_conf.pull_down_en = 0;
   io_conf.pull_up_en = 1;
   gpio_config(&io_conf);
}

void adc_init() {
   adc_oneshot_unit_init_cfg_t adc_config = {
       .unit_id = ADC_UNIT_1,
   };

   if (adc_oneshot_new_unit(&adc_config, &adc1_handle) == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to initialize ADC unit");
      return;
   }

   adc_oneshot_chan_cfg_t adc_channel_config = {
       .atten = ADC_ATTEN,
       .bitwidth = ADC_WIDTH,
   };

   if (adc_oneshot_config_channel(adc1_handle, ADC1_CHANNEL, &adc_channel_config) == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to configure ADC channel");
      adc_oneshot_del_unit(adc1_handle);
      return;
   }
   // ESP_LOGI(TAG, "ADC initialized");
}

void ledc_init() {
   ledc_timer_config_t ledc_timer = {
       .duty_resolution = LEDC_TIMER_10_BIT,
       .freq_hz = LEDC_FREQUENCY,
       .speed_mode = LEDC_HIGH_SPEED_MODE,
       .timer_num = LEDC_TIMER_0,
       .clk_cfg = LEDC_AUTO_CLK,
   };
   ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

   ledc_channel_config_t ledc_channel = {
       .speed_mode = LEDC_MODE,
       .channel = LEDC_CHANNEL,
       .timer_sel = LEDC_TIMER,
       .intr_type = LEDC_INTR_DISABLE,
       .gpio_num = LEDC_OUTPUT_IO,
       .duty = 0,  // Set duty to 0%
       .hpoint = 0,
   };
   ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void set_led(int value) {
   gpio_set_level(LED, value);
   ESP_LOGI(TAG, "LED set to: %d", value);
}

int read_led() {
   int led_state = gpio_get_level(LED);
   ESP_LOGI(TAG, "LED state is: %d", led_state);
   return led_state;
}

void set_pwm(uint16_t percentage) {
   // Formula for value = (2 ^ LEDC_DUTY_RESOLUTION) * percentage / 100
   int32_t value = (TWO_TO_THE_POWER_OF_10 * percentage) / 100;
   ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, value);
   ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
   ESP_LOGI(TAG, "PWM LED set to: %%%d, %ld", percentage, value);
}

uint16_t read_pwm() {
   uint16_t pwm_value = ledc_get_duty(LEDC_MODE, LEDC_CHANNEL);
   pwm_value = (pwm_value * 100) / TWO_TO_THE_POWER_OF_10;
   ESP_LOGI(TAG, "PWM LED is: %d", pwm_value);
   return pwm_value;
}

int read_adc_value() {
   int adc_value = 0;
   if (adc_oneshot_read(adc1_handle, ADC1_CHANNEL, &adc_value) == ESP_OK) {
      ESP_LOGI(TAG, "ADC value: %d", adc_value);
      return adc_value;
   }
   ESP_LOGE(TAG, "Failed to read ADC value");
   return ESP_FAIL;
}

void delaySeconds(uint8_t seconds) { vTaskDelay(seconds * SECOND_IN_MILLIS / portTICK_PERIOD_MS); }
void delayMillis(uint8_t seconds) { vTaskDelay(seconds / portTICK_PERIOD_MS); }

esp_err_t set_nvs_creds_and_name(const char *ssid, const char *pass, char *deviceNumber) {
   nvs_handle_t nvs_handle;
   ESP_LOGI(TAG_NVS, "Setting %s, %s, %s", ssid, pass, deviceNumber);
   esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
   if (err != ESP_OK) {
      ESP_LOGE(TAG_NVS, "Error (%s) opening NVS handle", esp_err_to_name(err));
      return err;
   }
   if (nvs_set_str(nvs_handle, "deviceNumber", deviceNumber) != ESP_OK) {
      ESP_LOGE(TAG_NVS, "Error setting device name");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   if (nvs_set_str(nvs_handle, "ssid", ssid) != ESP_OK) {
      ESP_LOGE(TAG_NVS, "Error setting SSID");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   if (nvs_set_str(nvs_handle, "pass", pass) != ESP_OK) {
      ESP_LOGE(TAG_NVS, "Error setting password");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   nvs_close(nvs_handle);
   return ESP_OK;
}

// Function to decode URL-encoded strings
void url_decode(char *str) {
   char *source = str;
   char *destination = str;
   while (*source) {
      if (*source == '%') {
         if (isxdigit((unsigned char)source[1]) && isxdigit((unsigned char)source[2])) {
            char hex[3] = {source[1], source[2], '\0'};
            *destination = (char)strtol(hex, NULL, 16);
            source += 3;
         } else {
            *destination = *source++;
         }
      } else if (*source == '+') {
         *destination = ' ';
         source++;
      } else {
         *destination = *source++;
      }
      destination++;
   }
   *destination = '\0';
}

esp_err_t config_get_handler(httpd_req_t *req) {
   char *buf;
   size_t buf_len;
   extern unsigned char config_start[] asm("_binary_config_html_start");
   extern unsigned char config_end[] asm("_binary_config_html_end");
   size_t config_len = config_end - config_start;
   char configHtml[config_len];

   // Copy the HTML template into the buffer
   memcpy(configHtml, config_start, config_len);

   buf_len = httpd_req_get_url_query_len(req) + 1;
   if (buf_len > 1) {
      buf = malloc(buf_len);

      if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
         if (httpd_query_key_value(buf, "query_name", deviceNumber, sizeof(deviceNumber)) == ESP_FAIL ||
             httpd_query_key_value(buf, "query_ssid", ssid, sizeof(ssid)) == ESP_FAIL ||
             httpd_query_key_value(buf, "query_pass", pass, sizeof(pass)) == ESP_FAIL) {
            ESP_LOGW(TAG_SERVER, "query_name not found in the query string.");
         }
         url_decode(ssid);
         url_decode(pass);
         if (strlen(ssid) > 0 && strlen(pass) > 0 && strlen(deviceNumber) > 0) {
            ESP_LOGI(TAG_SERVER, "All parameters found, setting credentials...");
            set_nvs_creds_and_name(ssid, pass, deviceNumber);
            ESP_LOGW(TAG_SERVER, "Credentials set, restarting...");
            delaySeconds(2);
            free(buf);
            esp_restart();
         } else {
            ESP_LOGW(TAG_SERVER, "Missing required query parameters. No changes made.");
         }
      } else {
         ESP_LOGE(TAG_SERVER, "Failed to retrieve URL query string.");
      }
      free(buf);
   } else {
      ESP_LOGW(TAG_SERVER, "No query string found.");
   }

   httpd_resp_set_type(req, "text/html");
   httpd_resp_send(req, configHtml, config_len);

   if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
      ESP_LOGI(TAG_SERVER, "Request headers lost");
   }

   return ESP_OK;
}

static const httpd_uri_t configSite = {
    .uri = "/config",
    .method = HTTP_GET,
    .handler = config_get_handler,
};

httpd_handle_t start_webserver(void) {
   httpd_handle_t server = NULL;
   httpd_config_t config = HTTPD_DEFAULT_CONFIG();

   ESP_LOGI(TAG_SERVER, "Iniciando el servidor en el puerto: '%d'", config.server_port);
   if (httpd_start(&server, &config) == ESP_OK) {
      ESP_LOGI(TAG_SERVER, "Registrando manejadores de URI");
      httpd_register_uri_handler(server, &configSite);
      return server;
   }

   ESP_LOGE(TAG_SERVER, "Error starting server!");
   return NULL;
}

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
         retry_num = 0;
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
         if (retry_num < WIFI_RETRY_MAX) {
            esp_wifi_connect();
            retry_num++;
            ESP_LOGE(TAG_WIFI_STA, "Retrying connection...");
         }
         break;
      case IP_EVENT_STA_GOT_IP:
         ip_event_got_ip_t *ip_event_got_ip = (ip_event_got_ip_t *)event_data;
         ESP_LOGI(TAG_WIFI_STA, "Got IP: " IPSTR, IP2STR(&ip_event_got_ip->ip_info.ip));
         xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_TO_AP_BIT);
         break;
      default:
         ESP_LOGW(TAG, "Unhandled event ID: %ld", event_id);
         break;
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

esp_err_t getWifiCredentials() {
   nvs_handle_t nvs_handle;
   esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
   if (err != ESP_OK) {
      ESP_LOGE(TAG_NVS, "Error (%s) opening NVS handle", esp_err_to_name(err));
      return ESP_ERR_NVS_NOT_FOUND;
   }
   size_t device_size = sizeof(deviceNumber);
   size_t ssid_size = sizeof(ssid);
   size_t pass_size = sizeof(pass);
   if (nvs_get_str(nvs_handle, "deviceNumber", deviceNumber, &device_size) != ESP_OK) {
      ESP_LOGW(TAG_NVS, "Error getting device number");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   if (nvs_get_str(nvs_handle, "ssid", ssid, &ssid_size) != ESP_OK) {
      ESP_LOGE(TAG_NVS, "Error getting SSID");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   if (nvs_get_str(nvs_handle, "pass", pass, &pass_size) != ESP_OK) {
      ESP_LOGE(TAG_NVS, "Error getting password");
      return ESP_ERR_NVS_NOT_FOUND;
   }
   ESP_LOGW(TAG_NVS, "Device number: %s, SSID: %s, Password: %s", deviceNumber, ssid, pass);
   nvs_close(nvs_handle);
   return ESP_OK;
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

      ESP_LOGI(TAG, "Wi-Fi initialization complete. Attempting to connect to AP: %s", ssid);
      esp_wifi_connect();
   } else {
      wifi_start_as_ap();
      server = start_webserver();
   }
}

int read_element(int element) {
   switch (element) {
      case LED_ELEMENT:
         return read_led();
      case ADC_ELEMENT:
         return read_adc_value();
      case PWM_ELEMENT:
         return read_pwm();
      default:
         return ESP_FAIL;
   }
}

void process_command(const char *command, char *response) {
   snprintf(prefix, sizeof(prefix), "%s:%s:%c", IDENTIFIER, USER_KEY, deviceNumber[0]);
   ESP_LOGI(TAG, "%s", prefix);
   if (strncmp(command, prefix, strlen(prefix)) != 0) {
      snprintf(response, BUFFER_SIZE, NACK_RESPONSE);
      return;
   }

   const char *cmd = command + strlen(prefix);
   char operation;
   char element;
   char value[3] = {0};
   char comment[BUFFER_SIZE] = {0};

   int parsed = sscanf(cmd, "%c:%c:%3[^:]s:%127[^:]s", &operation, &element, value, comment);
   if (parsed <= 2 || parsed > 4) {
      ESP_LOGE(TAG, "Parsed: %d", parsed);
      snprintf(response, BUFFER_SIZE, NACK_RESPONSE);
      return;
   }
   if (operation == READ_INSTRUCTION) {
      if (parsed == 3) {
         sscanf(cmd, "%c:%c:%127[^:]s", &operation, &element, comment);
      } else {
         char temp[BUFFER_SIZE - sizeof(value)] = {0};
         strcpy(temp, comment);
         snprintf(comment, BUFFER_SIZE, "%s%s", value, temp);
      }
      value[0] = 0;
   }

   switch (operation) {
      case WRITE_INSTRUCTION:
         if (element == LED_ELEMENT && (value[0] == '0' || value[0] == '1')) {
            set_led(value[0] - '0');
            snprintf(response, BUFFER_SIZE, ACK_RESPONSE ":%d", read_led());
         } else if (element == PWM_ELEMENT) {
            set_pwm(atoi(value));
            snprintf(response, BUFFER_SIZE, ACK_RESPONSE ":%d", read_pwm());
         } else {
            if (element == ADC_ELEMENT) ESP_LOGI(TAG, "ADC value is readonly");
            snprintf(response, BUFFER_SIZE, NACK_RESPONSE);
         }
         break;
      case READ_INSTRUCTION:
         int readed_value = read_element(element);
         if (readed_value != ESP_FAIL) {
            snprintf(response, BUFFER_SIZE, ACK_RESPONSE ":%d", readed_value);
         } else {
            snprintf(response, BUFFER_SIZE, NACK_RESPONSE);
         }
         break;

      default:
         snprintf(response, BUFFER_SIZE, NACK_RESPONSE);
   }
}

void keep_alive_task() {
   while (true) {
      delaySeconds(15);
      ESP_LOGI(TAG, "Sending keep alive message...");
      send(sock, keep_alive, strlen(keep_alive), 0);
   }
}

void tcp_client_task() {
   char rx_buffer[128];
   char host_ip[] = HOST_IP_ADDR;
   int addr_family = 0;
   int ip_protocol = 0;

   while (true) {
      struct sockaddr_in dest_addr;
      inet_pton(AF_INET, host_ip, &dest_addr.sin_addr);
      dest_addr.sin_family = AF_INET;
      dest_addr.sin_port = htons(PORT);
      addr_family = AF_INET;
      ip_protocol = IPPROTO_IP;

      sock = socket(addr_family, SOCK_STREAM, ip_protocol);
      if (sock < 0) {
         ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
         break;
      }
      ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, PORT);

      int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
      if (err != 0) {
         ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
         break;
      }
      ESP_LOGI(TAG, "Successfully connected");

      while (true) {
         err = 0;
         if (logged_in == false) {
            ESP_LOGI(TAG, "Sending login message...");
            err = send(sock, log_in, strlen(log_in), 0);
            if (keep_alive_task_handle != NULL)
               vTaskResume(keep_alive_task_handle);
            else
               xTaskCreate(&keep_alive_task, "keep_alive", 4096, NULL, 5, &keep_alive_task_handle);
            logged_in = true;
         }

         if (err < 0) {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            break;
         }

         int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
         if (len < 0) {
            ESP_LOGE(TAG, "recv failed: errno %d", errno);
            break;
         }

         else {
            rx_buffer[len] = 0;
            if (strstr(rx_buffer, NACK_RESPONSE) == rx_buffer || strstr(rx_buffer, ACK_RESPONSE) == rx_buffer) {
               // TODO: Add logic for nack
               ESP_LOGI(TAG, "RECEIVED FROM %s: \'%s\'\n", host_ip, rx_buffer);
            } else {
               ESP_LOGI(TAG, "RECEIVED FROM %s:", host_ip);
               ESP_LOGI(TAG, "\'%s\'\n", rx_buffer);

               char answer[BUFFER_SIZE] = NACK_RESPONSE;  // Default response
               // process_command(rx_buffer, answer);
               send(sock, answer, strlen(answer), 0);
               ESP_LOGI(TAG, "SENT %s TO %s\n", answer, host_ip);
            }
         }
      }

      if (sock != -1) {
         ESP_LOGE(TAG, "Shutting down socket and restarting...");
         shutdown(sock, 0);
         close(sock);
      } else if (sock == 0) {
         ESP_LOGE(TAG, "Connection closed by server");
         vTaskSuspend(keep_alive_task_handle);
      }
   }
}
void constructStrings() {
   if (deviceNumber[0] == 0) {
      ESP_LOGE(TAG, "Device number error %s", deviceNumber);
   }
   snprintf(prefix, sizeof(prefix), "%s:%s:%c", IDENTIFIER, USER_KEY, deviceNumber[0]);

   ESP_LOGI(TAG, "'%s'", prefix);
}

void button_task(void *pvParameters) {
   int action = (int)pvParameters;
   bool buttonState = 0;
   lastStateChange = -SEND_MESSAGE_DELAY_TIME;

   while (true) {
      buttonState = gpio_get_level(BUTTON_SEND_MESSAGE);
      int64_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
      // ESP_LOGI(TAG, "buttonState %s, now %lld", buttonState == FREED ? "Liberado" : "P", now);

      // ESP_LOGI(TAG, "buttonState %d, now %lld", buttonState, now);
      if (buttonState == FREED) {
         while (gpio_get_level(BUTTON_SEND_MESSAGE) == FREED) {
            // ESP_LOGI(TAG, "Esperando a que el boton sea presionado");
            vTaskDelay(10 / portTICK_PERIOD_MS);
         }
         if (now - lastStateChange > SEND_MESSAGE_DELAY_TIME) {
            ESP_LOGI(TAG, "Boton presionado\n");
            switch (action) {
               case SMS_SEND:
                  send(sock, message, strlen(message), 0);
                  break;
               case MQTT_PUBLISH:
                  char aux[32] = {0};
                  snprintf(aux, sizeof(aux), "%s:%d", USER_KEY, read_led());
                  my_mqtt_publish("device/led", aux);
                  break;
               case MQTT_SUBSCRIBE:
                  my_mqtt_subscribe("device/led");
                  break;
               case MQTT_UNSUBSCRIBE:
                  my_mqtt_unsubscribe("device/led");
                  break;
               case SMTP_SEND:
                  smtp_client_task();
                  break;
               default:
                  break;
            }
            ESP_LOGI(TAG, "Boton presionado\n");
            switch (action) {
               case SMS_SEND:
                  send(sock, message, strlen(message), 0);
                  break;
               case MQTT_PUBLISH:
                  char aux[32] = {0};
                  snprintf(aux, sizeof(aux), "%s:%d", USER_KEY, read_led());
                  my_mqtt_publish("device/led", aux);
                  break;
               case MQTT_SUBSCRIBE:
                  my_mqtt_subscribe("device/led");
                  break;
               case MQTT_UNSUBSCRIBE:
                  my_mqtt_unsubscribe("device/led");
                  break;
               case SMTP_SEND:
                  smtp_client_task();
                  break;
               default:
                  break;
            }
            lastStateChange = xTaskGetTickCount() * portTICK_PERIOD_MS;
         }
         while (gpio_get_level(BUTTON_SEND_MESSAGE) == PUSHED) {
            // ESP_LOGI(TAG, "Esperando a que el boton sea liberado");
            vTaskDelay(10 / portTICK_PERIOD_MS);
         }
         // ESP_LOGI(TAG, "Se ha soltado el button\n");
      }
      vTaskDelay(10 / portTICK_PERIOD_MS);
   }
}

void app_main(void) {
   esp_err_t ret = nvs_flash_init();
   gpio_init();
   if (gpio_get_level(RESTART_PIN) == PUSHED) {
      ESP_LOGW(TAG_NVS, "Manual NVS erase requested");
      delaySeconds(2);
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
   }

   if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_LOGW(TAG_NVS, "Error (%s) initializing NVS, erasing...", esp_err_to_name(ret));
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
   }
   ESP_ERROR_CHECK(ret);

   constructStrings();
   wifi_init();
   adc_init();
   ledc_init();

   constructStrings();
   // xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_TO_AP_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
   // mqtt5_app_start();
   // xEventGroupWaitBits(mqtt_event_group, MQTT_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

   // char aux[MAX_CHAR] = {0};
   // snprintf(aux, sizeof(aux), "%d:%s%s", read_led(), USER_KEY, "Prueba myMQTT");
   // my_mqtt_publish("device/led", aux);

   // xTaskCreate(smtp_client_task, "smtp_client_task", TASK_STACK_SIZE, NULL, 5, NULL);
   // xTaskCreate(button_task, "button_task", 2048, (void *)SMTP_SEND, 1, NULL);
   // xTaskCreate(tcp_client_task, "tcp_client_task", 4096, NULL, 5, NULL);
   // xTaskCreate(mqtt_subscriber_task, "mqtt_subscriber_task", 4096, NULL, 5, NULL);
   // xTaskCreate(mqtt_publisher_task, "mqtt_publisher_task", 4096, NULL, 5, NULL);
}
