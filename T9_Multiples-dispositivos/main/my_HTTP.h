#ifndef __MY_HTTP_H
#define __MY_HTTP_H

#include <ctype.h>
#include <esp_http_server.h>

#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "my_wifi.h"

void url_decode(char *);
esp_err_t config_get_handler(httpd_req_t *);
httpd_handle_t start_webserver(void);

#endif  // __MY_HTTP_H