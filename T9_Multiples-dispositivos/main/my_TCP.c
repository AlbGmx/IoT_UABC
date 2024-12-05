#include "my_TCP.h"

static const char *TAG_TCP = "My TCP";
const char *get = "GET /api/timezone/America/Tijuana.txt HTTP/1.1\r\nHost: worldtimeapi.org\r\n\r\n";
char log_in[STRING_SIZE] = {0};
char keep_alive[STRING_SIZE] = {0};
char message[STRING_SIZE] = {0};
int sock = -1;

TaskHandle_t keep_alive_task_handle = NULL;
EventGroupHandle_t tcp_event_group = NULL;
extern char device_number[2];

void keep_alive_task() {
   while (true) {
      delay_seconds(15);
      ESP_LOGI(TAG_TCP, "Sending keep alive message...");
      send(sock, keep_alive, strlen(keep_alive), 0);
   }
}

void tcp_client_task() {
   tcp_event_group = xEventGroupCreate();
   char rx_buffer[128];
   char *host_ip = (USE_LOCAL_IP) ? LOCAL_IP_ADDR : HOST_IP_ADDR;
   int addr_family = 0;
   int ip_protocol = 0;

   while (true) {
      struct sockaddr_in dest_addr;
      inet_pton(AF_INET, host_ip, &dest_addr.sin_addr);
      dest_addr.sin_family = AF_INET;
      dest_addr.sin_port = htons(PORT);
      addr_family = AF_INET;
      ip_protocol = IPPROTO_IP;

      sock = socket(addr_family, SOCK_STREAM, ip_protocol);
      if (sock < 0) {
         ESP_LOGE(TAG_TCP, "Unable to create socket: errno %d", errno);
         break;
      }
      ESP_LOGI(TAG_TCP, "Socket created, connecting to %s:%d", host_ip, PORT);

      int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
      if (err != 0) {
         ESP_LOGE(TAG_TCP, "Socket unable to connect: errno %d", errno);
         break;
      }
      ESP_LOGI(TAG_TCP, "Successfully connected");

      while (true) {
         err = 0;
         if (!(xEventGroupGetBits(tcp_event_group) & TCP_LOGGED_IN_BIT)) {
            ESP_LOGI(TAG_TCP, "Sending login message...");
            err = send(sock, log_in, strlen(log_in), 0);
            if (keep_alive_task_handle != NULL)
               vTaskResume(keep_alive_task_handle);
            else
               xTaskCreate(&keep_alive_task, "keep_alive", 4096, NULL, 5, &keep_alive_task_handle);
            xEventGroupSetBits(tcp_event_group, TCP_LOGGED_IN_BIT);
         }

         if (err < 0) {
            ESP_LOGE(TAG_TCP, "Error occurred during sending: errno %d", errno);
            break;
         }

         int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
         if (len < 0) {
            ESP_LOGE(TAG_TCP, "recv failed: errno %d", errno);
            break;
         }

         else {
            rx_buffer[len] = 0;
            if (strstr(rx_buffer, RESPONSE_NACK) == rx_buffer || strstr(rx_buffer, RESPONSE_ACK) == rx_buffer ||
                rx_buffer[0] == '\0') {
               // TODO: Add logic for nack
               ESP_LOGI(TAG_TCP, "RECEIVED FROM %s: \'%s\'\n", host_ip, rx_buffer);
            } else {
               ESP_LOGI(TAG_TCP, "RECEIVED FROM %s:", host_ip);
               ESP_LOGI(TAG_TCP, "\'%s\'\n", rx_buffer);

               char answer[BUFFER_SIZE] = RESPONSE_NACK;  // Default response
               process_command(rx_buffer, answer);
               send(sock, answer, strlen(answer), 0);
               ESP_LOGI(TAG_TCP, "SENT %s TO %s\n", answer, host_ip);
            }
         }
      }

      if (sock != -1) {
         ESP_LOGE(TAG_TCP, "Shutting down socket and restarting...");
         shutdown(sock, 0);
         close(sock);
      } else if (sock == 0) {
         ESP_LOGE(TAG_TCP, "Connection closed by server");
         vTaskSuspend(keep_alive_task_handle);
      }
   }
}

void tcp_get_time(My_time_t *current_time) {
   char rx_buffer[1024];
   char host_ip[] = WOLRD_TIME_API_IP_ADDR;
   struct sockaddr_in dest_addr;

   inet_pton(AF_INET, host_ip, &dest_addr.sin_addr);
   dest_addr.sin_family = AF_INET;
   dest_addr.sin_port = htons(80);

   int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
   if (sock < 0) {
      ESP_LOGE(TAG_TCP, "Unable to create socket: errno %d", errno);
      return;
   }

   if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
      ESP_LOGE(TAG_TCP, "Socket connection failed: errno %d", errno);
      close(sock);
      return;
   }

   send(sock, get, strlen(get), 0);

   int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
   if (len > 0) {
      rx_buffer[len] = '\0';  // Null-terminate the received data

      // Find the body of the HTTP response
      char *body = strstr(rx_buffer, "\r\n\r\n");
      if (body) {
         body += 4;  // Skip the "\r\n\r\n" to the start of the body

         // Extract the `datetime` field
         char *datetime_start = strstr(body, "datetime: ");
         if (datetime_start) {
            datetime_start += 10; 
            char *datetime_end = strchr(datetime_start, '\n');
            if (datetime_end) {
               *datetime_end = '\0';  
               sscanf(datetime_start, "%4hd-%2hd-%2hdT%2hd:%2hd:%2hd:%3hd", &current_time->year, &current_time->month, &current_time->day,
                      &current_time->hours, &current_time->minutes, &current_time->seconds, &current_time->millis);
               xEventGroupSetBits(tcp_event_group, TCP_TIME_LOGGED_BIT);
            }
         }
      }
   } else {
      ESP_LOGE(TAG_TCP, "Error receiving data: errno %d", errno);
   }

   close(sock);
}
