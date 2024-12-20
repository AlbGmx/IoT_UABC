#ifndef __MY_TCP_H
#define __MY_TCP_H

#include "lwip/err.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "esp_log.h"
#include "my_functions.h"

#define LOCAL_IP_ADDR "192.168.1.70"             // Local IP
#define HOST_IP_ADDR "82.180.173.228"            // IoT Server
#define WOLRD_TIME_API_IP_ADDR "213.188.196.246"  // World Time API
#define PORT 8266
#define PORT_WORLD_TIME_API 80
#define TCP_BUFFER_SIZE 4096
#define STRING_SIZE 256
#define TCP_CONNECTED_BIT BIT0
#define TCP_LOGGED_IN_BIT BIT1
#define TCP_TIME_LOGGED_BIT BIT2


void keep_alive_task();
void tcp_client_task();
void tcp_get_time(void *);

#endif  // __MY_TCP_H