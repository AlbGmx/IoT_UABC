#ifndef __MY_ELEMENTS_H
#define __MY_ELEMENTS_H

#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_system.h"

// ELEMENTS
#define LED_INTERNAL GPIO_NUM_2
#define LED_PWM GPIO_NUM_21
#define BUTTON_RESTART_PIN GPIO_NUM_22
#define BUTTON_SEND_MESSAGE GPIO_NUM_23
#define ADC_SELECTED GPIO_NUM_34
#define ADC1_CHANNEL ADC_CHANNEL_6
#define ADC_WIDTH ADC_BITWIDTH_12
#define ADC_ATTEN ADC_ATTEN_DB_0
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_CHANNEL 0
#define LEDC_MODE 0
#define LEDC_DUTY_RESOLUTION LEDC_TIMER_10_BIT
#define TWO_TO_THE_POWER_OF_10 1024  // Manually calculated to avoid math.h
#define LEDC_FREQUENCY 400
#define RELEASED 0
#define PRESSED 1

typedef enum {
   ELEMENT_LED_INTERNAL = 'L',
   ELEMENT_LED_PWM = 'P',
   ELEMENT_RESTART = 'R',
   ELEMENT_ADC_SELECTED = 'A',
} Elements_t;

// Initializers
void gpio_init();
void adc_init();
void ledc_init();

// Setters
void set_led(int);
void set_pwm(uint16_t);
esp_err_t set_wifi_credentials_and_id(const char *, const char *, char *);

// Getters
int read_led();
uint16_t read_pwm();
int read_adc_value();
int read_element(Elements_t);
esp_err_t getWifiCredentials();

#endif  // __MY_ELEMENTS_H