idf_component_register(SRCS "my_wifi.c" "my_HTTP.c" "my_elements.c" "my_TCP.c" "my_SMTP.c" "my_MQTT.c" "my_functions.c" "main.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "config.html" server_root_cert.pem)
