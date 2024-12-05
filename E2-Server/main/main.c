#include <lwip/netdb.h>
#include <string.h>
#include <sys/param.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
// // Constants
// #define SSID "IoT_AP"
// #define PASS "12345678"
// #define SSID "prueba"
// #define PASS "iotprueba"
#define SSID "ESP_NET"
#define PASS "ESP_NET_IOT"
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
#define LOGIN_INSTRUCTION 'L'
#define KEEP_ALIVE_INSTRUCTION 'K'
#define LOGIN_INSTRUCTION 'L'
#define SERVER_ELEMENT 'S'
#define LED_ELEMENT 'L'
#define ADC_ELEMENT 'A'
#define PWM_ELEMENT 'P'
#define BUFFER_SIZE 128
#define INCORRECT_COMMAND false
#define CORRECT_COMMAND true
#define write_command true
#define read_command false
#define UDP_RECEIVED_BIT BIT0
#define TCP_RECEIVED_BIT BIT1
#define UDP_TO_SEND_BIT BIT2
#define TCP_TO_SEND_BIT BIT3
#define UDP_CONNECTED_BIT BIT4
#define TCP_CONNECTED_BIT BIT5
#define UDP_TASK_WAITING_BIT BIT6

// SERVER
#define PORT_TCP 8266
#define PORT_UDP 8267
#define LOCAL_HOST_IP_ADDR "192.168.1.69"     // Local IP
#define SERVER_HOST_IP_ADDR "82.180.173.228"  // IoT Server
#define KEEPALIVE_IDLE 15
#define KEEPALIVE_INTERVAL 10
#define KEEPALIVE_COUNT 1

#define LED_PWM GPIO_NUM_21
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_CHANNEL 0
#define LEDC_MODE 0
#define LEDC_OUTPUT_IO GPIO_NUM_15
#define LEDC_DUTY_RESOLUTION LEDC_TIMER_10_BIT
#define TWO_TO_THE_POWER_OF_10 1024  // Manually calculated to avoid math.h
#define LEDC_FREQUENCY 400

#define BUTTON_SEND_MESSAGE GPIO_NUM_23
#define BUTTON_BOUNCE_TIME 150
#define SECOND_IN_MILLIS 1000
#define SEND_MESSAGE_DELAY_TIME 60 * SECOND_IN_MILLIS
#define RELEASED 0
#define PRESSED 1
#define MINIMUM_DELAY_MS 10
#define FREED 1
#define PUSHED 0

static const char *TAG = "Prototipo en Red Local";
static const char *TAG_TCP = "TCP Server";
static const char *TAG_UDP = "UDP Server";
static const char *TAG_UDP_TASK = "UDP Task";
static const char *TAG_TCP_TASK = "TCP Task";
// static const char *IDENTIFIER = "UABC";
// static const char *USER_KEY = "EGC";
int32_t lastStateChange = 0;
TaskHandle_t keep_alive_task_handle = NULL;

// Global variables
bool wifi_connected = false;
bool logged_in = false;
bool logged_to_server = false;
int retry_num = 0;
int sock_tcp = 0;
int sock_udp = 0;
char tcp_aux[BUFFER_SIZE] = {0};
char udp_aux[BUFFER_SIZE] = {0};
EventGroupHandle_t connection_event_group;

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
   io_conf.pull_down_en = 0;
   io_conf.pull_up_en = 1;
   gpio_config(&io_conf);
}

void delaySeconds(uint8_t seconds) { vTaskDelay(seconds * SECOND_IN_MILLIS / portTICK_PERIOD_MS); }

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

   wifi_config_t wifi_configuration = {.sta = {.ssid = SSID, .password = PASS}};

   esp_wifi_set_mode(WIFI_MODE_STA);
   esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
   esp_wifi_start();

   ESP_LOGI(TAG, "Wi-Fi initialization complete. Attempting to connect to SSID: %s", SSID);
   esp_wifi_connect();
}

int process_answer(const char *answer) {
   char aux[10] = {0};
   int value = 0;

   sscanf(answer, "%s:%d", aux, &value);

   if (strncmp(aux, ACK_RESPONSE, strlen(ACK_RESPONSE)) != 0) return -1;
   return value;
}

/* int write_element(char element, int value, char *comment) {
   char aux[BUFFER_SIZE] = {0};

   snprintf(aux, BUFFER_SIZE, "%s:%s:%c:%c:%d:%s", IDENTIFIER, USER_KEY, WRITE_INSTRUCTION, element, value, comment);
   send(sock_tcp, aux, strlen(aux), 0);
   ESP_LOGW(TAG_TCP, "Sent to TCP device %s", aux);

   // send command to device via TCP
   int len = recv(sock_tcp, aux, sizeof(aux) - 1, 0);
   if (len < 0) {
      ESP_LOGE(TAG_TCP, "Error occurred during receiving: errno %d", errno);
   } else if (len == 0) {
      ESP_LOGW(TAG_TCP, "Connection closed");
   } else {
      aux[len] = 0;
      ESP_LOGI(TAG_TCP, "Received %d bytes: %s", len, aux);
      return process_answer(aux);
   }
   return -1;
}

int read_element(char element, char *comment) {
   char aux[BUFFER_SIZE] = {0};
   snprintf(aux, BUFFER_SIZE, "%s:%s:%c:%c:%s", IDENTIFIER, USER_KEY, READ_INSTRUCTION, element, comment);
   send(sock_tcp, aux, strlen(aux), 0);
   ESP_LOGW(TAG_TCP, "Sent to TCP device %s", aux);

   // send command to device via TCP
   int len = recv(sock_tcp, aux, sizeof(aux) - 1, 0);
   if (len < 0) {
      ESP_LOGE(TAG_TCP, "Error occurred during receiving: errno %d", errno);
   } else if (len == 0) {
      ESP_LOGW(TAG_TCP, "Connection closed");
   } else {
      aux[len] = 0;
      ESP_LOGI(TAG_TCP, "Received %d bytes: %s", len, aux);
      return process_answer(aux);
   }
   return -1;
}
 */

void udp_to_tcp_task(void *pvParameters) {
   while (true) {
      ESP_LOGW(TAG_UDP_TASK, "Waiting for command to process from UDP...\n");
      xEventGroupWaitBits(connection_event_group, UDP_RECEIVED_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
      ESP_LOGW(TAG_UDP_TASK, "Processing command '%s' from UDP", udp_aux);
      int err = send(sock_tcp, udp_aux, strlen(udp_aux) + 1, 0);
      if (err < 0) {
         ESP_LOGE(TAG_UDP_TASK, "Error occurred during sending: errno %d", errno);
         break;
      }
      delaySeconds(1);
   }
}

void process_command_from_device_task(void *pvParameters) {
   char command[BUFFER_SIZE] = {0};
   const char *pre_fix = "UABC:EGC:0:";
   while (true) {
      ESP_LOGI(TAG_TCP_TASK, "Waiting for command to process from device...\n");
      xEventGroupWaitBits(connection_event_group, TCP_RECEIVED_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
      snprintf(command, strlen(tcp_aux) + 1, tcp_aux);
      // ESP_LOGI(TAG_TCP_TASK, "Processing command '%s' from device", command);
      if (strncmp(command, pre_fix, strlen(pre_fix)) == 0) {
         const char *cmd = command + strlen(pre_fix);
         char operation;
         char element;
         char comment[BUFFER_SIZE] = {0};

         // ESP_LOGI(TAG_TCP_TASK, "Processing command '%s' from cmd, %d", cmd, strlen(pre_fix));
         int parsed = sscanf(cmd, "%c:%c:%127[^:]s", &operation, &element, comment);
         ESP_LOGI(TAG_TCP_TASK, "Parsed: %d, Operation: %c, Element: %c, Comment: %s", parsed, operation, element, comment);
         if (parsed < 2 || parsed > 3) {
            ESP_LOGE(TAG_TCP_TASK, "Parsed: %d", parsed);
         } else if (element == SERVER_ELEMENT) {
            if (operation == LOGIN_INSTRUCTION) {
               snprintf(tcp_aux, strlen(tcp_aux), ACK_RESPONSE);
               xEventGroupSetBits(connection_event_group, TCP_CONNECTED_BIT);
            } else if (operation == KEEP_ALIVE_INSTRUCTION) {
               if (xEventGroupGetBits(connection_event_group) & TCP_CONNECTED_BIT) {
                  snprintf(tcp_aux, strlen(tcp_aux), ACK_RESPONSE);
               } else {
                  ESP_LOGI(TAG_TCP_TASK, "Not Logged in");
                  snprintf(tcp_aux, strlen(tcp_aux), NACK_RESPONSE);
               }
            }
         }
      } else {
         ESP_LOGW(TAG_TCP_TASK, "Command received from device: %s", command);
         snprintf(udp_aux, strlen(command) + 1, command);
         tcp_aux[0] = '\0';
         xEventGroupSetBits(connection_event_group, UDP_TO_SEND_BIT);
      }
      xEventGroupSetBits(connection_event_group, TCP_TO_SEND_BIT);
      ESP_LOGI(TAG_TCP_TASK, "Command processed");
      delaySeconds(1);
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

   struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
   dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
   dest_addr_ip4->sin_family = AF_INET;
   dest_addr_ip4->sin_port = htons(PORT_UDP);
   ip_protocol = IPPROTO_IP;

   // Creating UDP socket
   sock_udp = socket(addr_family, SOCK_DGRAM, ip_protocol);
   if (sock_udp < 0) {
      ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
      vTaskDelete(NULL);
      return;
   }
   ESP_LOGW(TAG_UDP, "Socket created");

   // Binding the socket to the address
   if (bind(sock_udp, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
      ESP_LOGE(TAG_UDP, "Socket unable to bind: errno %d", errno);
      close(sock_udp);
      vTaskDelete(NULL);
      return;
   }
   ESP_LOGW(TAG_UDP, "Socket bound, UDP port %d", PORT_UDP);

   while (1) {
      ESP_LOGW(TAG_UDP, "Waiting for data...\n");
      int len = recvfrom(sock_udp, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
      if (len < 0) {
         if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGE(TAG_UDP, "recvfrom failed: errno %d", errno);
            break;
         }
      } else if (len > 0) {
         rx_buffer[len] = 0;

         if (source_addr.ss_family == AF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
         }

         ESP_LOGW(TAG_UDP, "Received %d bytes from %s:", len, addr_str);
         ESP_LOGW(TAG_UDP, "%s", rx_buffer);
         snprintf(udp_aux, strlen(rx_buffer) + 1, rx_buffer);
         xEventGroupSetBits(connection_event_group, UDP_RECEIVED_BIT);
         xEventGroupWaitBits(connection_event_group, UDP_TO_SEND_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
         ESP_LOGW(TAG_UDP, "Responding '%s' to device", udp_aux);
         int err = sendto(sock_udp, udp_aux, strlen(udp_aux), 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
         if (err < 0) {
            ESP_LOGE(TAG_UDP, "Error occurred during sending: errno %d", errno);
            break;
         }
      }
   }

   if (sock_udp != -1) {
      ESP_LOGE(TAG, "Shutting down socket and restarting...");
      shutdown(sock_udp, 0);
      close(sock_udp);
   }
   vTaskDelete(NULL);
}

static void do_retransmit(const int sock_tcp) {
   int len;
   char aux[128];

   do {
      ESP_LOGI(TAG_TCP, "Waiting for data from device...\n");
      len = recv(sock_tcp, aux, sizeof(aux) - 1, 0);
      if (len < 0) {
         ESP_LOGE(TAG_TCP, "Error occurred during receiving: errno %d", errno);
      } else if (len == 0) {
         ESP_LOGW(TAG_TCP, "Connection closed");
      } else {
         aux[len] = 0;
         ESP_LOGI(TAG_TCP, "Received %d bytes: %s", len, aux);
         snprintf(tcp_aux, strlen(aux) + 1, aux);
         xEventGroupSetBits(connection_event_group, TCP_RECEIVED_BIT);
         xEventGroupWaitBits(connection_event_group, TCP_TO_SEND_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
         if (strlen(tcp_aux) > 0) {
            ESP_LOGI(TAG_TCP, "Responding '%s' to device", tcp_aux);
            int err = send(sock_tcp, tcp_aux, len, 0);
            if (err < 0) {
               ESP_LOGE(TAG_TCP, "Error occurred during sending: errno %d", errno);
               break;
            }
         }
      }
   } while (len > 0);
}

static void tcp_server_task(void *pvParameters) {
   char addr_str[128];
   int addr_family = (int)pvParameters;
   int ip_protocol = 0;
   int keepAlive = 1;
   int keepIdle = KEEPALIVE_IDLE;
   int keepInterval = KEEPALIVE_INTERVAL;
   int keepCount = KEEPALIVE_COUNT;
   struct sockaddr_storage dest_addr;

   if (addr_family == AF_INET) {
      struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
      dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
      dest_addr_ip4->sin_family = AF_INET;
      dest_addr_ip4->sin_port = htons(PORT_TCP);
      ip_protocol = IPPROTO_IP;
   }

   int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
   if (listen_sock < 0) {
      ESP_LOGE(TAG_TCP, "Unable to create socket: errno %d", errno);
      vTaskDelete(NULL);
      return;
   }
   int opt = 1;
   setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

   ESP_LOGI(TAG_TCP, "Socket created");

   int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
   if (err != 0) {
      ESP_LOGE(TAG_TCP, "Socket unable to bind: errno %d", errno);
      ESP_LOGE(TAG_TCP, "IPPROTO: %d", addr_family);
      goto CLEAN_UP;
   }
   ESP_LOGI(TAG_TCP, "Socket bound, TCP port %d", PORT_TCP);

   err = listen(listen_sock, 1);
   if (err != 0) {
      ESP_LOGE(TAG_TCP, "Error occurred during listen: errno %d", errno);
      goto CLEAN_UP;
   }

   while (1) {
      ESP_LOGI(TAG_TCP, "Socket listening");

      struct sockaddr_storage source_addr;
      socklen_t addr_len = sizeof(source_addr);
      sock_tcp = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
      if (sock_tcp < 0) {
         ESP_LOGE(TAG_TCP, "Unable to accept connection: errno %d", errno);
         break;
      }

      // Set tcp keepalive option
      setsockopt(sock_tcp, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
      setsockopt(sock_tcp, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
      setsockopt(sock_tcp, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
      setsockopt(sock_tcp, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
      // Convert ip address to string
      if (source_addr.ss_family == PF_INET) {
         inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
      }
      ESP_LOGI(TAG_TCP, "Socket accepted ip address: %s", addr_str);

      do_retransmit(sock_tcp);

      shutdown(sock_tcp, 0);
      close(sock_tcp);
   }

CLEAN_UP:
   close(listen_sock);
   vTaskDelete(NULL);
}

void app_main(void) {
   ESP_ERROR_CHECK(nvs_flash_init());
   wifi_init();
   gpio_init();
   connection_event_group = xEventGroupCreate();
   while (!wifi_connected) {
      if (retry_num == WIFI_RETRY_MAX) {
         ESP_LOGE(TAG,
                  "Connection failed. Maximum retries reached, it is likely "
                  "that the SSID cannot be found.");
         return;
      }
      ESP_LOGI(TAG, "Waiting for WIFI before starting TCP/UDP servers...\n");
      fflush(stdout);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
   }
   xTaskCreate(udp_to_tcp_task, "udp_command", 4096, NULL, 5, NULL);
   xTaskCreate(process_command_from_device_task, "process_command", 4096, NULL, 5, NULL);
   xTaskCreate(udp_server_task, "udp_server", 4096, (void *)AF_INET, 5, NULL);
   xTaskCreate(tcp_server_task, "tcp_server", 4096, (void *)AF_INET, 5, NULL);
}