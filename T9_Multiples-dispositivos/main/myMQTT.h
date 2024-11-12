#ifndef myMQTT_h
#define myMQTT_h

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

#define BROKER_URL CONFIG_BROKER_URL
#define MQTT_CONNECTED_BIT BIT0

extern EventGroupHandle_t mqtt_event_group;
extern bool is_mqtt_connected;
extern esp_mqtt_client_handle_t client;

void my_mqtt_publish(char *, char *);
void my_mqtt_subscribe(char *);
void my_mqtt_unsubscribe(char *);
void mqtt5_app_start();

#endif