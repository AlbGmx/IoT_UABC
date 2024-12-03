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
   // xTaskCreate(tcp_client_task, "tcp_client_task", 4096, NULL, 5, NULL);
   xTaskCreate(tcp_client_task, "tcp_client_task", 4096, NULL, 5, NULL);
   // xTaskCreate(mqtt_subscriber_task, "mqtt_subscriber_task", 4096, NULL, 5, NULL);
   // xTaskCreate(mqtt_publisher_task, "mqtt_publisher_task", 4096, NULL, 5, NULL);
}
