#include "my_functions.h"

static const char *TAG_FUNCTIONS = "My functions";

char prefix[BUFFER_SIZE/2] = {0};
int32_t lastStateChange = 0;
extern char device_number[2];
extern char prefix[];
extern char log_in[];
extern char keep_alive[];
extern char message[];

void delay_seconds(uint8_t seconds) { vTaskDelay(seconds * SECOND_IN_MILLIS / portTICK_PERIOD_MS); }

void delay_millis(uint16_t seconds) { vTaskDelay(seconds / portTICK_PERIOD_MS); }

void button_task(void *pvParameters) {
   int action = (int)pvParameters;
   bool buttonState = 0;
   lastStateChange = -SEND_MESSAGE_DELAY_TIME;

   while (true) {
      buttonState = gpio_get_level(BUTTON_SEND_MESSAGE);
      int64_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (buttonState == RELEASED) {
         while (gpio_get_level(BUTTON_SEND_MESSAGE) == RELEASED) {
            // ESP_LOGI(TAG_FUNCTIONS, "Esperando a que el boton sea presionado");
            vTaskDelay(10 / portTICK_PERIOD_MS);
         }
         if (now - lastStateChange > SEND_MESSAGE_DELAY_TIME) {
            ESP_LOGI(TAG_FUNCTIONS, "Boton presionado\n");
            switch (action) {
               case SMS_SEND:
                  // send(sock, MESSAGE_MESSAGE, strlen(MESSAGE_MESSAGE), 0);
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
         while (gpio_get_level(BUTTON_SEND_MESSAGE) == PRESSED) {
            // ESP_LOGI(TAG_FUNCTIONS, "Esperando a que el boton sea liberado");
            delay_millis(10);
         }
         // ESP_LOGI(TAG_FUNCTIONS, "Se ha soltado el button\n");
      }
      vTaskDelay(10 / portTICK_PERIOD_MS);
   }
}

void construct_strings() {
   if (device_number[0] == 0) {
      ESP_LOGE(TAG_FUNCTIONS, "Device number error %s", device_number);
   }

   snprintf(prefix, sizeof(prefix), "%s:%s:%c", IDENTIFIER, USER_KEY, device_number[0]);
   snprintf(log_in, BUFFER_SIZE, "%s:L:S:%s:", prefix, MESSAGE_LOG_IN);
   snprintf(keep_alive, BUFFER_SIZE, "%s:K:S:%s:", prefix, MESSAGE_KEEP_ALIVE);
   snprintf(message, BUFFER_SIZE, "%s:M:S:%s:", prefix, MESSAGE_MESSAGE);

   ESP_LOGI(TAG_FUNCTIONS, "'%s'", prefix);
}

void get_current_time(char *response) { ESP_LOGI(TAG_FUNCTIONS, "%s", response); }

void process_command(const char *command, char *response) {
   snprintf(prefix, sizeof(prefix), "%s:%s:%c:", IDENTIFIER, USER_KEY, device_number[0]);
   // ESP_LOGI(TAG_FUNCTIONS, "%s\n", prefix);
   if (strncmp(command, prefix, strlen(prefix)) != 0) {
      snprintf(response, BUFFER_SIZE, RESPONSE_NACK);
      return;
   }

   const char *cmd = command + strlen(prefix);
   char operation;
   char element;
   char value[3] = {0};
   char comment[BUFFER_SIZE] = {0};

   int parsed = sscanf(cmd, "%c:%c:%3[^:]s:%127[^:]s", &operation, &element, value, comment);
   if (parsed <= 2 || parsed > 4) {
      ESP_LOGE(TAG_FUNCTIONS, "Parsed: %d", parsed);
      snprintf(response, BUFFER_SIZE, RESPONSE_NACK);
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

   ESP_LOGI(TAG_FUNCTIONS, "Operation: %c, Element: %c, Value: %s, Comment: %s", operation, element, value, comment);

   switch (operation) {
      case WRITE_INSTRUCTION:
         if (element == LED_ELEMENT && (value[0] == '0' || value[0] == '1')) {
            set_led(value[0] - '0');
            snprintf(response, BUFFER_SIZE, RESPONSE_ACK ":%d", read_led());
         } else if (element == PWM_ELEMENT) {
            set_pwm(atoi(value));
            snprintf(response, BUFFER_SIZE, RESPONSE_ACK ":%d", read_pwm());
         } else {
            if (element == ADC_ELEMENT) ESP_LOGI(TAG_FUNCTIONS, "ADC value is readonly");
            snprintf(response, BUFFER_SIZE, RESPONSE_NACK);
         }
         break;
      case READ_INSTRUCTION:
         int readed_value = read_element(element);
         if (readed_value != ESP_FAIL) {
            snprintf(response, BUFFER_SIZE, RESPONSE_ACK ":%d", readed_value);
         } else {
            snprintf(response, BUFFER_SIZE, RESPONSE_NACK);
         }
         break;

      default:
         snprintf(response, BUFFER_SIZE, RESPONSE_NACK);
   }
}
