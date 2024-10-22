
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
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/platform.h"
#include "mbedtls/ssl.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

/* Constants that are configurable in menuconfig */
#define MAIL_SERVER CONFIG_SMTP_SERVER
#define MAIL_PORT CONFIG_SMTP_PORT_NUMBER
#define SENDER_MAIL CONFIG_SMTP_SENDER_MAIL
#define SENDER_PASSWORD CONFIG_SMTP_SENDER_PASSWORD
#define RECIPIENT_MAIL CONFIG_SMTP_RECIPIENT_MAIL
#define SERVER_USES_STARTSSL 1

#define TASK_STACK_SIZE (8 * 1024)
#define BUF_SIZE 512

#define VALIDATE_MBEDTLS_RETURN(ret, min_valid_ret, max_valid_ret, goto_label) \
   do {                                                                        \
      if (ret < min_valid_ret || ret > max_valid_ret) {                        \
         goto goto_label;                                                      \
      }                                                                        \
   } while (0)

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

extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[] asm("_binary_server_root_cert_pem_end");

static const char *TAG = "Prototipo en Red Local";
static const char *log_in = "UABC:EGC:L:S:Log in";
static const char *keep_alive = "UABC:EGC:K:S:Keep alive";
TaskHandle_t keep_alive_task_handle = NULL;
QueueHandle_t buttonQueueHandler;
int32_t lastStateChange = 0;
uint32_t currentTime = 0;

// Global variables
bool wifi_connected = false;
bool logged_in = false;
int retry_num = 0;
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

void keep_alive_task(int *sock) {
   while (true) {
      delaySeconds(15);
      ESP_LOGI(TAG, "Sending keep alive message...");
      send(*sock, keep_alive, strlen(keep_alive), 0);
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

      int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
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
               xTaskCreate(keep_alive_task, "keep_alive", 4096, &sock, 5, &keep_alive_task_handle);
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

static int write_and_get_response(mbedtls_net_context *sock_fd, unsigned char *buf, size_t len) {
   int ret;
   const size_t DATA_SIZE = 128;
   unsigned char data[DATA_SIZE];
   char code[4];
   size_t i, idx = 0;

   if (len) {
      ESP_LOGD(TAG, "%s", buf);
   }

   if (len && (ret = mbedtls_net_send(sock_fd, buf, len)) <= 0) {
      ESP_LOGE(TAG, "mbedtls_net_send failed with error -0x%x", -ret);
      return ret;
   }

   do {
      len = DATA_SIZE - 1;
      memset(data, 0, DATA_SIZE);
      ret = mbedtls_net_recv(sock_fd, data, len);

      if (ret <= 0) {
         ESP_LOGE(TAG, "mbedtls_net_recv failed with error -0x%x", -ret);
         goto exit;
      }

      data[len] = '\0';
      printf("\n%s", data);
      len = ret;
      for (i = 0; i < len; i++) {
         if (data[i] != '\n') {
            if (idx < 4) {
               code[idx++] = data[i];
            }
            continue;
         }

         if (idx == 4 && code[0] >= '0' && code[0] <= '9' && code[3] == ' ') {
            code[3] = '\0';
            ret = atoi(code);
            goto exit;
         }

         idx = 0;
      }
   } while (1);

exit:
   return ret;
}

static int write_ssl_and_get_response(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len) {
   int ret;
   const size_t DATA_SIZE = 128;
   unsigned char data[DATA_SIZE];
   char code[4];
   size_t i, idx = 0;

   if (len) {
      ESP_LOGD(TAG, "%s", buf);
   }

   while (len && (ret = mbedtls_ssl_write(ssl, buf, len)) <= 0) {
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
         ESP_LOGE(TAG, "mbedtls_ssl_write failed with error -0x%x", -ret);
         goto exit;
      }
   }

   do {
      len = DATA_SIZE - 1;
      memset(data, 0, DATA_SIZE);
      ret = mbedtls_ssl_read(ssl, data, len);

      if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
         continue;
      }

      if (ret <= 0) {
         ESP_LOGE(TAG, "mbedtls_ssl_read failed with error -0x%x", -ret);
         goto exit;
      }

      ESP_LOGD(TAG, "%s", data);

      len = ret;
      for (i = 0; i < len; i++) {
         if (data[i] != '\n') {
            if (idx < 4) {
               code[idx++] = data[i];
            }
            continue;
         }

         if (idx == 4 && code[0] >= '0' && code[0] <= '9' && code[3] == ' ') {
            code[3] = '\0';
            ret = atoi(code);
            goto exit;
         }

         idx = 0;
      }
   } while (1);

exit:
   return ret;
}

static int write_ssl_data(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len) {
   int ret;

   if (len) {
      ESP_LOGD(TAG, "%s", buf);
   }

   while (len && (ret = mbedtls_ssl_write(ssl, buf, len)) <= 0) {
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
         ESP_LOGE(TAG, "mbedtls_ssl_write failed with error -0x%x", -ret);
         return ret;
      }
   }

   return 0;
}

static int perform_tls_handshake(mbedtls_ssl_context *ssl) {
   int ret = -1;
   uint32_t flags;
   char *buf = NULL;
   buf = (char *)calloc(1, BUF_SIZE);
   if (buf == NULL) {
      ESP_LOGE(TAG, "calloc failed for size %d", BUF_SIZE);
      goto exit;
   }

   ESP_LOGI(TAG, "Performing the SSL/TLS handshake...");

   fflush(stdout);
   while ((ret = mbedtls_ssl_handshake(ssl)) != 0) {
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
         ESP_LOGE(TAG, "mbedtls_ssl_handshake returned -0x%x", -ret);
         goto exit;
      }
   }

   ESP_LOGI(TAG, "Verifying peer X.509 certificate...");

   if ((flags = mbedtls_ssl_get_verify_result(ssl)) != 0) {
      /* In real life, we probably want to close connection if ret != 0 */
      ESP_LOGW(TAG, "Failed to verify peer certificate!");
      mbedtls_x509_crt_verify_info(buf, BUF_SIZE, "  ! ", flags);
      ESP_LOGW(TAG, "verification info: %s", buf);
   } else {
      ESP_LOGI(TAG, "Certificate verified.");
   }

   ESP_LOGI(TAG, "Cipher suite is %s", mbedtls_ssl_get_ciphersuite(ssl));
   ret = 0; /* No error */

exit:
   if (buf) {
      free(buf);
   }
   return ret;
}

static void smtp_client() {
   char *buf = NULL;
   unsigned char base64_buffer[128];
   int ret, len;
   size_t base64_len;

   mbedtls_entropy_context entropy;
   mbedtls_ctr_drbg_context ctr_drbg;
   mbedtls_ssl_context ssl;
   mbedtls_x509_crt cacert;
   mbedtls_ssl_config conf;
   mbedtls_net_context server_fd;

   mbedtls_ssl_init(&ssl);
   mbedtls_x509_crt_init(&cacert);
   mbedtls_ctr_drbg_init(&ctr_drbg);
   ESP_LOGI(TAG, "Seeding the random number generator");

   mbedtls_ssl_config_init(&conf);

   mbedtls_entropy_init(&entropy);
   if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0)) != 0) {
      ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned -0x%x", -ret);
      goto exit;
   }

   ESP_LOGI(TAG, "Loading the CA root certificate...");

   ret = mbedtls_x509_crt_parse(&cacert, server_root_cert_pem_start,
                                server_root_cert_pem_end - server_root_cert_pem_start);

   if (ret < 0) {
      ESP_LOGE(TAG, "mbedtls_x509_crt_parse returned -0x%x", -ret);
      goto exit;
   }

   ESP_LOGI(TAG, "Setting hostname for TLS session...");

   /* Hostname set here should match CN in server certificate */
   if ((ret = mbedtls_ssl_set_hostname(&ssl, MAIL_SERVER)) != 0) {
      ESP_LOGE(TAG, "mbedtls_ssl_set_hostname returned -0x%x", -ret);
      goto exit;
   }

   ESP_LOGI(TAG, "Setting up the SSL/TLS structure...");

   if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
      ESP_LOGE(TAG, "mbedtls_ssl_config_defaults returned -0x%x", -ret);
      goto exit;
   }

   mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
   mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
   mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
#ifdef CONFIG_MBEDTLS_DEBUG
   mbedtls_esp_enable_debug_log(&conf, 4);
#endif

   if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
      ESP_LOGE(TAG, "mbedtls_ssl_setup returned -0x%x", -ret);
      goto exit;
   }

   mbedtls_net_init(&server_fd);

   ESP_LOGI(TAG, "Connecting to %s:%s...", MAIL_SERVER, MAIL_PORT);

   if ((ret = mbedtls_net_connect(&server_fd, MAIL_SERVER, MAIL_PORT, MBEDTLS_NET_PROTO_TCP)) != 0) {
      ESP_LOGE(TAG, "mbedtls_net_connect returned -0x%x", -ret);
      goto exit;
   }

   ESP_LOGI(TAG, "Connected.");

   mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

   buf = (char *)calloc(1, BUF_SIZE);
   if (buf == NULL) {
      ESP_LOGE(TAG, "calloc failed for size %d", BUF_SIZE);
      goto exit;
   }
#if SERVER_USES_STARTSSL
   /* Get response */
   ret = write_and_get_response(&server_fd, (unsigned char *)buf, 0);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

   ESP_LOGI(TAG, "Writing EHLO to server...");
   len = snprintf((char *)buf, BUF_SIZE, "EHLO %s\r\n", "ESP32");
   ret = write_and_get_response(&server_fd, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

   ESP_LOGI(TAG, "Writing STARTTLS to server...");
   len = snprintf((char *)buf, BUF_SIZE, "STARTTLS\r\n");
   ret = write_and_get_response(&server_fd, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

   ret = perform_tls_handshake(&ssl);
   if (ret != 0) {
      goto exit;
   }

#else /* SERVER_USES_STARTSSL */
   ret = perform_tls_handshake(&ssl);
   if (ret != 0) {
      goto exit;
   }

   /* Get response */
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, 0);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);
   ESP_LOGI(TAG, "Writing EHLO to server...");

   len = snprintf((char *)buf, BUF_SIZE, "EHLO %s\r\n", "ESP32");
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

#endif /* SERVER_USES_STARTSSL */

   /* Authentication */
   ESP_LOGI(TAG, "Authentication...");

   ESP_LOGI(TAG, "Write AUTH LOGIN");
   len = snprintf((char *)buf, BUF_SIZE, "AUTH LOGIN\r\n");
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 399, exit);

   ESP_LOGI(TAG, "Write USER NAME");
   ret = mbedtls_base64_encode((unsigned char *)base64_buffer, sizeof(base64_buffer), &base64_len,
                               (unsigned char *)SENDER_MAIL, strlen(SENDER_MAIL));
   if (ret != 0) {
      ESP_LOGE(TAG, "Error in mbedtls encode! ret = -0x%x", -ret);
      goto exit;
   }
   len = snprintf((char *)buf, BUF_SIZE, "%s\r\n", base64_buffer);
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 300, 399, exit);

   ESP_LOGI(TAG, "Write PASSWORD");
   ret = mbedtls_base64_encode((unsigned char *)base64_buffer, sizeof(base64_buffer), &base64_len,
                               (unsigned char *)SENDER_PASSWORD, strlen(SENDER_PASSWORD));
   if (ret != 0) {
      ESP_LOGE(TAG, "Error in mbedtls encode! ret = -0x%x", -ret);
      goto exit;
   }
   len = snprintf((char *)buf, BUF_SIZE, "%s\r\n", base64_buffer);
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 399, exit);

   /* Compose email */
   ESP_LOGI(TAG, "Write MAIL FROM");
   len = snprintf((char *)buf, BUF_SIZE, "MAIL FROM:<%s>\r\n", SENDER_MAIL);
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

   ESP_LOGI(TAG, "Write RCPT");
   len = snprintf((char *)buf, BUF_SIZE, "RCPT TO:<%s>\r\n", RECIPIENT_MAIL);
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

   ESP_LOGI(TAG, "Write DATA");
   len = snprintf((char *)buf, BUF_SIZE, "DATA\r\n");
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 300, 399, exit);

   ESP_LOGI(TAG, "Write Content");
   /* We do not take action if message sending is partly failed. */
   len = snprintf((char *)buf, BUF_SIZE,
                  "From: %s\r\nSubject: Correo enviado desde el ESP32\r\n"
                  "To: %s\r\n"
                  "MIME-Version: 1.0 (mime-construct 1.9)\n",
                  "ESP32 SMTP Client", RECIPIENT_MAIL);

   /**
    * Note: We are not validating return for some ssl_writes.
    * If by chance, it's failed; at worst email will be incomplete!
    */
   ret = write_ssl_data(&ssl, (unsigned char *)buf, len);

   /* Multipart boundary */
   len = snprintf((char *)buf, BUF_SIZE,
                  "Content-Type: multipart/mixed;boundary=XYZabcd1234\n"
                  "--XYZabcd1234\n");
   ret = write_ssl_data(&ssl, (unsigned char *)buf, len);

   /* Text */
   len = snprintf((char *)buf, BUF_SIZE,
                  "Content-Type: text/plain\n"
                  "Este es un ejemplo del ESP32 SMTP Protocol.\r\n"
                  "\r\n"
                  "- Emmanuel\n\n--XYZabcd1234\n");
   ret = write_ssl_data(&ssl, (unsigned char *)buf, len);

   len = snprintf((char *)buf, BUF_SIZE, "\n--XYZabcd1234\n");
   ret = write_ssl_data(&ssl, (unsigned char *)buf, len);

   len = snprintf((char *)buf, BUF_SIZE, "\r\n.\r\n");
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);
   ESP_LOGI(TAG, "Email sent!");

   /* Close connection */
   mbedtls_ssl_close_notify(&ssl);
   ret = 0; /* No errors */

exit:
   mbedtls_net_free(&server_fd);
   mbedtls_x509_crt_free(&cacert);
   mbedtls_ssl_free(&ssl);
   mbedtls_ssl_config_free(&conf);
   mbedtls_ctr_drbg_free(&ctr_drbg);
   mbedtls_entropy_free(&entropy);

   if (ret != 0) {
      mbedtls_strerror(ret, buf, 100);
      ESP_LOGE(TAG, "Last error was: -0x%x - %s", -ret, buf);
   }

   putchar('\n'); /* Just a new line */
   if (buf) {
      free(buf);
   }
}

void send_email() {
   int pinNumber;
   while (true) {
      if (xQueueReceive(buttonQueueHandler, &pinNumber, portMAX_DELAY)) {
         if (gpio_get_level(pinNumber) == RELEASED) {
            smtp_client();
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