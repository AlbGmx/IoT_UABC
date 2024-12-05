#ifndef __MY_FUNCTIONS_H
#define __MY_FUNCTIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "my_MQTT.h"
#include "my_SMTP.h"
#include "my_TCP.h"
#include "my_elements.h"
// Constants
// #define SSID "ESP_NET"
// #define PASS "ESP_NET_IOT"
// #define SSID "IoT_AP"
// #define PASS "12345678"
// Default AP NAME
#define AP_SSID "ESP_AP_CONFIG"
#define MAX_CHAR 32

#define RESPONSE_ACK "ACK"
#define RESPONSE_NACK "NACK"
#define WRITE_INSTRUCTION 'W'
#define READ_INSTRUCTION 'R'
#define LED_ELEMENT 'L'
#define ADC_ELEMENT 'A'
#define PWM_ELEMENT 'P'
#define BUFFER_SIZE 128
#define WIFI_RETRY_MAX 20
#define USE_LOCAL_IP true
#define IDENTIFIER "UABC"
#define USER_KEY "EGC"
#define MESSAGE_LOG_IN "Log in"
#define MESSAGE_KEEP_ALIVE "Keep Alive"
#define MESSAGE_MESSAGE "Mensaje enviado desde ESP32"

#define BUTTON_BOUNCE_TIME 150
#define SECOND_IN_MILLIS 1000
#define SEND_MESSAGE_DELAY_TIME 60 * SECOND_IN_MILLIS
#define MINIMUM_DELAY_MS portTICK_PERIOD_MS

enum {
   SMS_SEND = 0,
   MQTT_PUBLISH,
   MQTT_SUBSCRIBE,
   MQTT_UNSUBSCRIBE,
   SMTP_SEND,
};

typedef struct {
   uint16_t millis, seconds, minutes, hours, day, month, year;
} My_time_t;

void button_task(void *pvParameter);
void construct_strings();
void get_current_time(char *);
void process_command(const char *, char *);
void delay_seconds(uint8_t);
void delay_millis(uint16_t);
void print_date(My_time_t *);
#endif  // __MY_FUNCTIONS_H