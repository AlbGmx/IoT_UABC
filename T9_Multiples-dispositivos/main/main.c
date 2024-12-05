#include "my_MQTT.h"
#include "my_SMTP.h"
#include "my_TCP.h"
#include "my_elements.h"
#include "my_functions.h"
#include "my_HTTP.h"
#include "my_wifi.h"

// static const char *TAG = "Prototipo en Red Local";

// Global variables
extern EventGroupHandle_t wifi_event_group;
extern EventGroupHandle_t tcp_event_group;

My_time_t current_time = {0};

void count_time_task() {
   xEventGroupWaitBits(tcp_event_group, TCP_TIME_LOGGED_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
   while (true) {
      delay_millis(1);
      current_time.millis++;
      if (current_time.millis == 1000) {
         current_time.millis = 0;
         current_time.seconds++;
         if (current_time.seconds == 60) {
            current_time.seconds = 0;
            current_time.minutes++;
            if (current_time.minutes == 60) {
               current_time.minutes = 0;
               current_time.hours++;
               if (current_time.hours == 24) {
                  current_time.hours = 0;
                  current_time.day++;
                  if (current_time.day == 31) {
                     current_time.day = 1;
                     current_time.month++;
                     if (current_time.month == 13) {
                        current_time.month = 1;
                        current_time.year++;
                     }
                  }
               }
            }
         }
      }
   }
}

void app_main(void) {
   gpio_init();
   nvs_init();
   wifi_init();
   adc_init();
   ledc_init();

   construct_strings();
   xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_TO_AP_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
   // mqtt5_app_start();
   // xEventGroupWaitBits(mqtt_event_group, MQTT_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

   // char aux[MAX_CHAR] = {0};
   // snprintf(aux, sizeof(aux), "%d:%s%s", read_led(), USER_KEY, "Prueba myMQTT");
   // my_mqtt_publish("device/led", aux);

   // xTaskCreate(smtp_client_task, "smtp_client_task", TASK_STACK_SIZE, NULL, 5, NULL);
   // xTaskCreate(button_task, "button_task", 2048, (void *)SMTP_SEND, 1, NULL);
   tcp_get_time(&current_time);
   // xTaskCreate(tcp_client_task, "tcp_client_task", 4096, NULL, 5, NULL);
   // xTaskCreate(mqtt_subscriber_task, "mqtt_subscriber_task", 4096, NULL, 5, NULL);
   // xTaskCreate(mqtt_publisher_task, "mqtt_publisher_task", 4096, NULL, 5, NULL);
   while (true)
   {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      print_date(&current_time);
   }

}
