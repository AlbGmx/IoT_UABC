#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include <string.h>

//Logging variable
#define LOG false

// Constants
#define PORT 377
#define SSID "ESP_NET"
#define PASS "ESP_NET_IOT"
#define LED GPIO_NUM_2
#define ADC_SELECTED GPIO_NUM_34 
#define ADC1_CHANNEL ADC_CHANNEL_6
#define ADC_WIDTH ADC_BITWIDTH_12
#define ADC_ATTEN ADC_ATTEN_DB_0
#define WIFI_RETRY_MAX 5
#define NACK_RESPONSE "NACK"
#define ACK_RESPONSE "ACK"
#define WRITE_INSTRUCTION 'W'
#define READ_INSTRUCTION 'R'
#define LED_ELEMENT 'L'
#define ADC_ELEMENT 'A'
#define BUFFER_SIZE 128

static const char *TAG = "Prototipo en Red Local";

// Global variables
bool wifi_connected = false;
int retry_num = 0;
static adc_oneshot_unit_handle_t adc1_handle;

void gpio_init() {
   esp_rom_gpio_pad_select_gpio(LED);
   gpio_set_direction(LED, GPIO_MODE_OUTPUT);

   esp_rom_gpio_pad_select_gpio(ADC_SELECTED);
   gpio_set_direction(ADC_SELECTED, GPIO_MODE_INPUT);
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
   ESP_LOGI(TAG, "ADC initialized");
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

int read_adc_value() {
   int adc_value = 0;
   if (adc_oneshot_read(adc1_handle, ADC1_CHANNEL, &adc_value) == ESP_OK) {
      ESP_LOGI(TAG, "ADC value: %d", adc_value);
      return adc_value;
   }
   ESP_LOGE(TAG, "Failed to read ADC value");
   return ESP_FAIL;
}

static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
   switch (event_id) {
      case WIFI_EVENT_STA_START:
         ESP_LOGI(TAG, "Wi-Fi starting...");
         retry_num = 0;
         break;
      case WIFI_EVENT_STA_CONNECTED:
         ESP_LOGI(TAG, "Wi-Fi connected");
         break;
      case WIFI_EVENT_STA_DISCONNECTED:
         ESP_LOGE(TAG, "Wi-Fi lost connection");
         if (retry_num < WIFI_RETRY_MAX) {
            esp_wifi_connect();
            retry_num++;
            ESP_LOGE(TAG, "Retrying connection...");
         }
         break;
      case IP_EVENT_STA_GOT_IP:
         ESP_LOGI(TAG, "Connected with IP %s", ip4addr_ntoa(&((ip_event_got_ip_t *)event_data)->ip_info.ip));
         wifi_connected = true;
         break;
      default:
         ESP_LOGW(TAG, "Unhandled event ID: %ld", event_id);
         break;
   }
}

void wifi_init() {
   esp_netif_init();
   esp_event_loop_create_default();
   esp_netif_create_default_wifi_sta();

   wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
   esp_wifi_init(&wifi_initiation);

   esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
   esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

   wifi_config_t wifi_configuration = {.sta = {
                                           .ssid = SSID,
                                           .password = PASS,
                                       }};

   esp_wifi_set_mode(WIFI_MODE_STA);
   esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
   esp_wifi_start();
   esp_wifi_connect();

   ESP_LOGI(TAG, "Wi-Fi initialization complete. Attempting to connect to SSID: %s", SSID);
}

void print_command(const char *prefix, char operation, char element, int value, const char *comment, char *response) {
   if (operation == 'R')
      ESP_LOGI(TAG, "%s%c:%c:%s -> %s", prefix, operation, element, comment, response);
   else
      ESP_LOGI(TAG, "%s%c:%c:%c:%s -> %s", prefix, operation, element, value, comment, response);
}

void print_command_parsed(const char *prefix, char operation, char element, int value, const char *comment,
                          char *response) {
   if (operation == 'R')
      ESP_LOGI(TAG,
               "\n\tPrefix: \t\"%s\"\n\tOperation: \t\"%c\"\n\tElement: \t\"%c\"\n\tComment: \t\"%s\"\n\n\tResponse: \t\"%s\"",
               prefix, operation, element, comment, response);
   else
      ESP_LOGI(TAG,
               "\n\tPrefix: \t\"%s\"\n\tOperation: \t\"%c\"\n\tElement: \t\"%c\"\n\tValue: \t\"%c\"\n\tComment: "
               "\t\"%s\"\n\n\tResponse: \t\"%s\"",
               prefix, operation, element, value, comment, response);
}

void process_command(const char *command, char *response) {
   const char *prefix = "UABC:";
   if (strncmp(command, prefix, strlen(prefix)) != 0) {
      snprintf(response, BUFFER_SIZE, NACK_RESPONSE);
      return;
   }

   const char *cmd = command + strlen(prefix);
   char operation;
   char element;
   char value;
   char comment[BUFFER_SIZE] = {0};

   int parsed = sscanf(cmd, "%c:%c:%c:%127[^:]s", &operation, &element, &value, comment);
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
         snprintf(comment, BUFFER_SIZE, "%c%s", value, temp);
      }
      value = -1;
   }

   switch (operation) {
      case WRITE_INSTRUCTION:
         if (element == LED_ELEMENT || value == '0' || value == '1') {
            set_led((value == '1') ? 1 : 0);
            snprintf(response, BUFFER_SIZE, ACK_RESPONSE);
         } else {
            if (element == ADC_ELEMENT) ESP_LOGI(TAG, "ADC value is readonly");
            snprintf(response, BUFFER_SIZE, NACK_RESPONSE);
         }
         break;
      case READ_INSTRUCTION:
         if (element == LED_ELEMENT) {
            snprintf(response, BUFFER_SIZE, ACK_RESPONSE ":%d", read_led());
         } else if (element == ADC_ELEMENT) {
            snprintf(response, BUFFER_SIZE, ACK_RESPONSE ":%d", read_adc_value());
         } else {
            snprintf(response, BUFFER_SIZE, NACK_RESPONSE);
         }
         break;
      default: {
         snprintf(response, BUFFER_SIZE, NACK_RESPONSE);
      }
   }
   if (LOG) {
    print_command(prefix, operation, element, value, comment, response);
    print_command_parsed(prefix, operation, element, value, comment, response);
  }
}

static void udp_server_task(void *pvParameters) {
   char rx_buffer[BUFFER_SIZE];
   char addr_str[BUFFER_SIZE];
   int addr_family = (int)pvParameters;
   int ip_protocol;
   struct sockaddr_in6 dest_addr;
   struct sockaddr_storage source_addr;
   socklen_t socklen = sizeof(source_addr);
   memset(&dest_addr, 0, sizeof(dest_addr));

   switch (addr_family) {
      case AF_INET:  // IPv4
         struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
         dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
         dest_addr_ip4->sin_family = AF_INET;
         dest_addr_ip4->sin_port = htons(PORT);
         ip_protocol = IPPROTO_IP;
         break;
      case AF_INET6:  // IPv6
         dest_addr.sin6_family = AF_INET6;
         dest_addr.sin6_port = htons(PORT);
         ip_protocol = IPPROTO_IPV6;
         break;
      default:
         ESP_LOGE(TAG, "Unsupported address family: %d", addr_family);
         vTaskDelete(NULL);
         return;
   }

   // Creating UDP socket
   int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
   if (sock < 0) {
      ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
      vTaskDelete(NULL);
      return;
   }
   ESP_LOGI(TAG, "Socket created");

   // Binding the socket to the address
   if (bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
      ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
      close(sock);
      vTaskDelete(NULL);
      return;
   }
   ESP_LOGI(TAG, "Socket bound, port %d", PORT);

   while (1) {
      ESP_LOGI(TAG, "Waiting for data...");

      int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

      if (len < 0) {
         if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
         }
      } else if (len > 0) {
         rx_buffer[len] = 0;

         if (source_addr.ss_family == AF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
         } else if (source_addr.ss_family == AF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
         }

         ESP_LOGW(TAG, "Received %d bytes from %s:", len, addr_str);
         ESP_LOGW(TAG, "%s", rx_buffer);

         char answer[BUFFER_SIZE] = NACK_RESPONSE;  // Default response
         process_command(rx_buffer, answer);

         int err = sendto(sock, answer, strlen(answer), 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
         if (err < 0) {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            break;
         }
      }
   }

   if (sock != -1) {
      ESP_LOGE(TAG, "Shutting down socket and restarting...");
      shutdown(sock, 0);
      close(sock);
   }
   vTaskDelete(NULL);
}

void app_main(void) {
   ESP_ERROR_CHECK(nvs_flash_init());
   wifi_init();
   gpio_init();
   adc_init();
   while (!wifi_connected) {
      if (retry_num == WIFI_RETRY_MAX) {
         ESP_LOGE(TAG, "Connection failed. Maximum retries reached, it is likely that the SSID cannot be found.");
         return;
      }
      ESP_LOGI(TAG, "Waiting for WIFI connection before starting UDP server...\n");
      fflush(stdout);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
   }
   xTaskCreate(udp_server_task, "udp_server", 4096, (void *)AF_INET, 5, NULL);
}