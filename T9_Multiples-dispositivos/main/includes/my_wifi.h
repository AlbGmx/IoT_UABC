#ifndef __MY_WIFI_H
#define __MY_WIFI_H

#include "esp_eth.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "my_HTTP.h"
#include "my_functions.h"
#include "nvs_flash.h"

#define MAX_WIFI_CHAR 32
#define WIFI_AP_STARTED_BIT BIT0
#define WIFI_STA_STARTED_BIT BIT1
#define WIFI_CONNECTED_TO_AP_BIT BIT2

void wifi_start_as_ap();
void wifi_init();
void nvs_init();
esp_err_t getWifiCredentials();
esp_err_t set_wifi_credentials_and_id(const char *, const char *, char *);

#endif  // __MY_WIFI_H