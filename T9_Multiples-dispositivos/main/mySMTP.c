/*
 * SMTP email client
 *
 * Adapted from the `ssl_mail_client` example in mbedtls.
 *
 * SPDX-FileCopyrightText: The Mbed TLS Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPDX-FileContributor: 2015-2021 Espressif Systems (Shanghai) CO LTD
 */

#include "mySMTP.h"

const char *TAG_Mail = "mySMTP";

extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[] asm("_binary_server_root_cert_pem_end");

static int write_and_get_response(mbedtls_net_context *sock_fd, unsigned char *buf, size_t len) {
   int ret;
   const size_t DATA_SIZE = 128;
   unsigned char data[DATA_SIZE];
   char code[4];
   size_t i, idx = 0;

   if (len) {
      ESP_LOGD(TAG_Mail, "%s", buf);
   }

   if (len && (ret = mbedtls_net_send(sock_fd, buf, len)) <= 0) {
      ESP_LOGE(TAG_Mail, "mbedtls_net_send failed with error -0x%x", -ret);
      return ret;
   }

   do {
      len = DATA_SIZE - 1;
      memset(data, 0, DATA_SIZE);
      ret = mbedtls_net_recv(sock_fd, data, len);

      if (ret <= 0) {
         ESP_LOGE(TAG_Mail, "mbedtls_net_recv failed with error -0x%x", -ret);
         goto exit;
      }

      data[len] = '\0';
      printf("\n%s", data);
      len = ret;
      for (i = 0; i < len; i++) {
         if (data[i] != '\n') {
            if (idx < 4) {
               code[idx++] = data[i];
            }
            continue;
         }

         if (idx == 4 && code[0] >= '0' && code[0] <= '9' && code[3] == ' ') {
            code[3] = '\0';
            ret = atoi(code);
            goto exit;
         }

         idx = 0;
      }
   } while (1);

exit:
   return ret;
}

static int write_ssl_and_get_response(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len) {
   int ret;
   const size_t DATA_SIZE = 128;
   unsigned char data[DATA_SIZE];
   char code[4];
   size_t i, idx = 0;

   if (len) {
      ESP_LOGD(TAG_Mail, "%s", buf);
   }

   while (len && (ret = mbedtls_ssl_write(ssl, buf, len)) <= 0) {
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
         ESP_LOGE(TAG_Mail, "mbedtls_ssl_write failed with error -0x%x", -ret);
         goto exit;
      }
   }

   do {
      len = DATA_SIZE - 1;
      memset(data, 0, DATA_SIZE);
      ret = mbedtls_ssl_read(ssl, data, len);

      if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
         continue;
      }

      if (ret <= 0) {
         ESP_LOGE(TAG_Mail, "mbedtls_ssl_read failed with error -0x%x", -ret);
         goto exit;
      }

      ESP_LOGD(TAG_Mail, "%s", data);

      len = ret;
      for (i = 0; i < len; i++) {
         if (data[i] != '\n') {
            if (idx < 4) {
               code[idx++] = data[i];
            }
            continue;
         }

         if (idx == 4 && code[0] >= '0' && code[0] <= '9' && code[3] == ' ') {
            code[3] = '\0';
            ret = atoi(code);
            goto exit;
         }

         idx = 0;
      }
   } while (1);

exit:
   return ret;
}

static int write_ssl_data(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len) {
   int ret;

   if (len) {
      ESP_LOGD(TAG_Mail, "%s", buf);
   }

   while (len && (ret = mbedtls_ssl_write(ssl, buf, len)) <= 0) {
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
         ESP_LOGE(TAG_Mail, "mbedtls_ssl_write failed with error -0x%x", -ret);
         return ret;
      }
   }

   return 0;
}

static int perform_tls_handshake(mbedtls_ssl_context *ssl) {
   int ret = -1;
   uint32_t flags;
   char *buf = NULL;
   buf = (char *)calloc(1, BUF_SIZE);
   if (buf == NULL) {
      ESP_LOGE(TAG_Mail, "calloc failed for size %d", BUF_SIZE);
      goto exit;
   }

   ESP_LOGI(TAG_Mail, "Performing the SSL/TLS handshake...");

   fflush(stdout);
   while ((ret = mbedtls_ssl_handshake(ssl)) != 0) {
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
         ESP_LOGE(TAG_Mail, "mbedtls_ssl_handshake returned -0x%x", -ret);
         goto exit;
      }
   }

   ESP_LOGI(TAG_Mail, "Verifying peer X.509 certificate...");

   if ((flags = mbedtls_ssl_get_verify_result(ssl)) != 0) {
      /* In real life, we probably want to close connection if ret != 0 */
      ESP_LOGW(TAG_Mail, "Failed to verify peer certificate!");
      mbedtls_x509_crt_verify_info(buf, BUF_SIZE, "  ! ", flags);
      ESP_LOGW(TAG_Mail, "verification info: %s", buf);
   } else {
      ESP_LOGI(TAG_Mail, "Certificate verified.");
   }

   ESP_LOGI(TAG_Mail, "Cipher suite is %s", mbedtls_ssl_get_ciphersuite(ssl));
   ret = 0; /* No error */

exit:
   if (buf) {
      free(buf);
   }
   return ret;
}

void smtp_client_task() {
   char *buf = NULL;
   unsigned char base64_buffer[128];
   int ret, len;
   size_t base64_len;

   mbedtls_entropy_context entropy;
   mbedtls_ctr_drbg_context ctr_drbg;
   mbedtls_ssl_context ssl;
   mbedtls_x509_crt cacert;
   mbedtls_ssl_config conf;
   mbedtls_net_context server_fd;

   mbedtls_ssl_init(&ssl);
   mbedtls_x509_crt_init(&cacert);
   mbedtls_ctr_drbg_init(&ctr_drbg);
   ESP_LOGI(TAG_Mail, "Seeding the random number generator");

   mbedtls_ssl_config_init(&conf);

   mbedtls_entropy_init(&entropy);
   if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0)) != 0) {
      ESP_LOGE(TAG_Mail, "mbedtls_ctr_drbg_seed returned -0x%x", -ret);
      goto exit;
   }

   ESP_LOGI(TAG_Mail, "Loading the CA root certificate...");

   ret = mbedtls_x509_crt_parse(&cacert, server_root_cert_pem_start,
                                server_root_cert_pem_end - server_root_cert_pem_start);

   if (ret < 0) {
      ESP_LOGE(TAG_Mail, "mbedtls_x509_crt_parse returned -0x%x", -ret);
      goto exit;
   }

   ESP_LOGI(TAG_Mail, "Setting hostname for TLS session...");

   /* Hostname set here should match CN in server certificate */
   if ((ret = mbedtls_ssl_set_hostname(&ssl, MAIL_SERVER)) != 0) {
      ESP_LOGE(TAG_Mail, "mbedtls_ssl_set_hostname returned -0x%x", -ret);
      goto exit;
   }

   ESP_LOGI(TAG_Mail, "Setting up the SSL/TLS structure...");

   if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
      ESP_LOGE(TAG_Mail, "mbedtls_ssl_config_defaults returned -0x%x", -ret);
      goto exit;
   }

   mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
   mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
   mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
#ifdef CONFIG_MBEDTLS_DEBUG
   mbedtls_esp_enable_debug_log(&conf, 4);
#endif

   if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
      ESP_LOGE(TAG_Mail, "mbedtls_ssl_setup returned -0x%x", -ret);
      goto exit;
   }

   mbedtls_net_init(&server_fd);

   ESP_LOGI(TAG_Mail, "Connecting to %s:%s...", MAIL_SERVER, MAIL_PORT);

   if ((ret = mbedtls_net_connect(&server_fd, MAIL_SERVER, MAIL_PORT, MBEDTLS_NET_PROTO_TCP)) != 0) {
      ESP_LOGE(TAG_Mail, "mbedtls_net_connect returned -0x%x", -ret);
      goto exit;
   }

   ESP_LOGI(TAG_Mail, "Connected.");

   mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

   buf = (char *)calloc(1, BUF_SIZE);
   if (buf == NULL) {
      ESP_LOGE(TAG_Mail, "calloc failed for size %d", BUF_SIZE);
      goto exit;
   }
#if SERVER_USES_STARTSSL
   /* Get response */
   ret = write_and_get_response(&server_fd, (unsigned char *)buf, 0);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

   ESP_LOGI(TAG_Mail, "Writing EHLO to server...");
   len = snprintf((char *)buf, BUF_SIZE, "EHLO %s\r\n", "ESP32");
   ret = write_and_get_response(&server_fd, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

   ESP_LOGI(TAG_Mail, "Writing STARTTLS to server...");
   len = snprintf((char *)buf, BUF_SIZE, "STARTTLS\r\n");
   ret = write_and_get_response(&server_fd, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

   ret = perform_tls_handshake(&ssl);
   if (ret != 0) {
      goto exit;
   }

#else /* SERVER_USES_STARTSSL */
   ret = perform_tls_handshake(&ssl);
   if (ret != 0) {
      goto exit;
   }

   /* Get response */
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, 0);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);
   ESP_LOGI(TAG_Mail, "Writing EHLO to server...");

   len = snprintf((char *)buf, BUF_SIZE, "EHLO %s\r\n", "ESP32");
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

#endif /* SERVER_USES_STARTSSL */

   /* Authentication */
   ESP_LOGI(TAG_Mail, "Authentication...");

   ESP_LOGI(TAG_Mail, "Write AUTH LOGIN");
   len = snprintf((char *)buf, BUF_SIZE, "AUTH LOGIN\r\n");
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 399, exit);

   ESP_LOGI(TAG_Mail, "Write USER NAME");
   ret = mbedtls_base64_encode((unsigned char *)base64_buffer, sizeof(base64_buffer), &base64_len,
                               (unsigned char *)SENDER_MAIL, strlen(SENDER_MAIL));
   if (ret != 0) {
      ESP_LOGE(TAG_Mail, "Error in mbedtls encode! ret = -0x%x", -ret);
      goto exit;
   }
   len = snprintf((char *)buf, BUF_SIZE, "%s\r\n", base64_buffer);
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 300, 399, exit);

   ESP_LOGI(TAG_Mail, "Write PASSWORD");
   ret = mbedtls_base64_encode((unsigned char *)base64_buffer, sizeof(base64_buffer), &base64_len,
                               (unsigned char *)SENDER_PASSWORD, strlen(SENDER_PASSWORD));
   if (ret != 0) {
      ESP_LOGE(TAG_Mail, "Error in mbedtls encode! ret = -0x%x", -ret);
      goto exit;
   }
   len = snprintf((char *)buf, BUF_SIZE, "%s\r\n", base64_buffer);
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 399, exit);

   /* Compose email */
   ESP_LOGI(TAG_Mail, "Write MAIL FROM");
   len = snprintf((char *)buf, BUF_SIZE, "MAIL FROM:<%s>\r\n", SENDER_MAIL);
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

   ESP_LOGI(TAG_Mail, "Write RCPT");
   len = snprintf((char *)buf, BUF_SIZE, "RCPT TO:<%s>\r\n", RECIPIENT_MAIL);
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);

   ESP_LOGI(TAG_Mail, "Write DATA");
   len = snprintf((char *)buf, BUF_SIZE, "DATA\r\n");
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 300, 399, exit);

   ESP_LOGI(TAG_Mail, "Write Content");
   /* We do not take action if message sending is partly failed. */
   len = snprintf((char *)buf, BUF_SIZE,
                  "From: %s\r\nSubject: Correo enviado desde el ESP32\r\n"
                  "To: %s\r\n"
                  "MIME-Version: 1.0 (mime-construct 1.9)\n",
                  "ESP32 SMTP Client", RECIPIENT_MAIL);

   /**
    * Note: We are not validating return for some ssl_writes.
    * If by chance, it's failed; at worst email will be incomplete!
    */
   ret = write_ssl_data(&ssl, (unsigned char *)buf, len);

   /* Multipart boundary */
   len = snprintf((char *)buf, BUF_SIZE,
                  "Content-Type: multipart/mixed;boundary=XYZabcd1234\n"
                  "--XYZabcd1234\n");
   ret = write_ssl_data(&ssl, (unsigned char *)buf, len);

   /* Text */
   len = snprintf((char *)buf, BUF_SIZE,
                  "Content-Type: text/plain\n"
                  "Este es un ejemplo del ESP32 SMTP Protocol.\r\n"
                  "\r\n"
                  "- Emmanuel\n\n--XYZabcd1234\n");
   ret = write_ssl_data(&ssl, (unsigned char *)buf, len);

   len = snprintf((char *)buf, BUF_SIZE, "\n--XYZabcd1234\n");
   ret = write_ssl_data(&ssl, (unsigned char *)buf, len);

   len = snprintf((char *)buf, BUF_SIZE, "\r\n.\r\n");
   ret = write_ssl_and_get_response(&ssl, (unsigned char *)buf, len);
   VALIDATE_MBEDTLS_RETURN(ret, 200, 299, exit);
   ESP_LOGI(TAG_Mail, "Email sent!");

   /* Close connection */
   mbedtls_ssl_close_notify(&ssl);
   ret = 0; /* No errors */

exit:
   mbedtls_net_free(&server_fd);
   mbedtls_x509_crt_free(&cacert);
   mbedtls_ssl_free(&ssl);
   mbedtls_ssl_config_free(&conf);
   mbedtls_ctr_drbg_free(&ctr_drbg);
   mbedtls_entropy_free(&entropy);

   if (ret != 0) {
      mbedtls_strerror(ret, buf, 100);
      ESP_LOGE(TAG_Mail, "Last error was: -0x%x - %s", -ret, buf);
   }

   putchar('\n'); /* Just a new line */
   if (buf) {
      free(buf);
   }
   vTaskDelete(NULL);
}
