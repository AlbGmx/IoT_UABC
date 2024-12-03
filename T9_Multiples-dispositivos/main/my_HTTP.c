#include "my_HTTP.h"

static const char *TAG_SERVER = "WEB SERVER";
extern char device_number[2];
extern char ssid[MAX_WIFI_CHAR];
extern char pass[MAX_WIFI_CHAR];

static const httpd_uri_t configSite = {
    .uri = "/config",
    .method = HTTP_GET,
    .handler = config_get_handler,
};

void url_decode(char *str) {
   char *source = str;
   char *destination = str;
   while (*source) {
      if (*source == '%') {
         if (isxdigit((unsigned char)source[1]) && isxdigit((unsigned char)source[2])) {
            char hex[3] = {source[1], source[2], '\0'};
            *destination = (char)strtol(hex, NULL, 16);
            source += 3;
         } else {
            *destination = *source++;
         }
      } else if (*source == '+') {
         *destination = ' ';
         source++;
      } else {
         *destination = *source++;
      }
      destination++;
   }
   *destination = '\0';
}

esp_err_t config_get_handler(httpd_req_t *req) {
   char *buf;
   size_t buf_len;
   extern unsigned char config_start[] asm("_binary_config_html_start");
   extern unsigned char config_end[] asm("_binary_config_html_end");
   size_t config_len = config_end - config_start;
   char configHtml[config_len];

   // Copy the HTML template into the buffer
   memcpy(configHtml, config_start, config_len);

   buf_len = httpd_req_get_url_query_len(req) + 1;
   if (buf_len > 1) {
      buf = malloc(buf_len);

      if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
         if (httpd_query_key_value(buf, "query_name", device_number, sizeof(device_number)) == ESP_FAIL ||
             httpd_query_key_value(buf, "query_ssid", ssid, sizeof(ssid)) == ESP_FAIL ||
             httpd_query_key_value(buf, "query_pass", pass, sizeof(pass)) == ESP_FAIL) {
            ESP_LOGW(TAG_SERVER, "query_name not found in the query string.");
         }
         url_decode(ssid);
         url_decode(pass);
         if (strlen(ssid) > 0 && strlen(pass) > 0 && strlen(device_number) > 0) {
            ESP_LOGI(TAG_SERVER, "Parameters ...");
            set_wifi_credentials_and_id(ssid, pass, device_number);
            ESP_LOGW(TAG_SERVER, "Credentials set, restarting...");
            delay_seconds(2);
            free(buf);
            esp_restart();
         } else {
            ESP_LOGW(TAG_SERVER, "Missing required query parameters. No changes made.");
         }
      } else {
         ESP_LOGE(TAG_SERVER, "Failed to retrieve URL query string.");
      }
      free(buf);
   } else {
      ESP_LOGW(TAG_SERVER, "No query string found.");
   }

   httpd_resp_set_type(req, "text/html");
   httpd_resp_send(req, configHtml, config_len);

   if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
      ESP_LOGI(TAG_SERVER, "Request headers lost");
   }

   return ESP_OK;
}

httpd_handle_t start_webserver(void) {
   httpd_handle_t server = NULL;
   httpd_config_t config = HTTPD_DEFAULT_CONFIG();

   ESP_LOGI(TAG_SERVER, "Iniciando el servidor en el puerto: '%d'", config.server_port);
   if (httpd_start(&server, &config) == ESP_OK) {
      ESP_LOGI(TAG_SERVER, "Registrando manejadores de URI");
      httpd_register_uri_handler(server, &configSite);
      return server;
   }

   ESP_LOGE(TAG_SERVER, "Error starting server!");
   return NULL;
}