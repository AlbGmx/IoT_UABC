
#include <mbedtls/base64.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

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


#define TASK_STACK_SIZE (8 * 1024)
#define BUF_SIZE 512

// Logging variable
#define LOG false

// Constants
#define SSID "prueba"
#define PASS "iotprueba"
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
#define BUFFER_SIZE 128
#define PORT 8266
// #define HOST_IP_ADDR "192.168.1.69"  // Local IP
#define HOST_IP_ADDR "82.180.173.228"  // IoT Server
#define LED_PWM GPIO_NUM_21

#define BUTTON_SEND_MESSAGE GPIO_NUM_18
#define BUTTON_BOUNCE_TIME 150
#define RELEASED 0
#define PRESSED 1
#define MINIMUM_DELAY_MS 10

static const char *TAG = "Prototipo en Red Local";
static const char *log_in = "UABC:EGC:L:S:Log in";
static const char *keep_alive = "UABC:EGC:K:S:Keep alive";
static const char *message = "UABC:EGC:W:SMS:6656560351:Hola Mundo";
TaskHandle_t keep_alive_task_handle = NULL;
QueueHandle_t buttonQueueHandler;
int32_t lastStateChange = 0;
uint32_t currentTime = 0;

// Global variables
bool wifi_connected = false;
bool logged_in = false;
int retry_num = 0;
int sock = 0;
static adc_oneshot_unit_handle_t adc1_handle;

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
   io_conf.pin_bit_mask = (1 << BUTTON_SEND_MESSAGE);
   io_conf.pull_down_en = 1;
   io_conf.pull_up_en = 0;
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

void delaySeconds(uint8_t seconds) { vTaskDelay(seconds * 1000 / portTICK_PERIOD_MS); }

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
         // ESP_LOGI(TAG, "Connected with IP %s", ip4addr_ntoa(&((ip_event_got_ip_t *)event_data)->ip_info.ip));
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
               "\n\tPrefix: \t\"%s\"\n\tOperation: \t\"%c\"\n\tElement: "
               "\t\"%c\"\n\tComment: \t\"%s\"\n\n\tResponse: "
               "\t\"%s\"",
               prefix, operation, element, comment, response);
   else
      ESP_LOGI(TAG,
               "\n\tPrefix: \t\"%s\"\n\tOperation: \t\"%c\"\n\t\tElement: "
               "\t\"%c\"\n\tValue: \t\"%c\"\n\tComment: "
               "\t\"%s\"\n\n\tResponse: \t\"%s\"",
               prefix, operation, element, value, comment, response);
}

void process_command(const char *command, char *response) {
   const char *prefix = "UABC:EGC:";
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
         if (element == LED_ELEMENT && (value == '0' || value == '1')) {
            set_led(value - '0');
            snprintf(response, BUFFER_SIZE, ACK_RESPONSE ":%d", read_led());
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

   print_command(prefix, operation, element, value, comment, response);
   if (LOG) {
      print_command_parsed(prefix, operation, element, value, comment, response);
   }
}

void keep_alive_task() {
   while (true) {
      delaySeconds(15);
      ESP_LOGI(TAG, "Sending keep alive message...");
      send( sock, keep_alive, strlen(keep_alive), 0);
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
               process_command(rx_buffer, answer);
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

void send_email() {
   int pinNumber;
   while (true) {
      if (xQueueReceive(buttonQueueHandler, &pinNumber, portMAX_DELAY)) {
         if (logged_in) {
            ESP_LOGI(TAG, "Sending message...");
            send(sock, message, strlen(message), 0);

         }
      }
   }
}

static void IRAM_ATTR buttonInterruptHandler(void *args) {
   uint32_t buttonActioned = (uint32_t)args;

   currentTime = xTaskGetTickCount() * portTICK_PERIOD_MS;

   if (currentTime - lastStateChange < BUTTON_BOUNCE_TIME) {
      return;
   }
   lastStateChange = currentTime;
   xQueueSendFromISR(buttonQueueHandler, &buttonActioned, NULL);
}

void configInterruptions() {
   buttonQueueHandler = xQueueCreate(10, sizeof(uint32_t));
   xTaskCreate(send_email, "sendEmail", 2048, NULL, 1, NULL);

   gpio_install_isr_service(0);
   gpio_isr_handler_add(BUTTON_SEND_MESSAGE, buttonInterruptHandler, (void *)BUTTON_SEND_MESSAGE);
}

void app_main(void) {
   ESP_ERROR_CHECK(nvs_flash_init());
   wifi_init();
   gpio_init();
   adc_init();
   configInterruptions();
   while (!wifi_connected) {
      if (retry_num == WIFI_RETRY_MAX) {
         ESP_LOGE(TAG,
                  "Connection failed. Maximum retries reached, it is likely "
                  "that the SSID cannot be found.");
         return;
      }
      ESP_LOGI(TAG, "Waiting for WIFI before starting TCP server connection...\n");
      fflush(stdout);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
   }

   xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
}
