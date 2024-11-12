idf_component_register(SRCS "main.c" "mySMTP.c" "myMQTT.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "config.html" server_root_cert.pem)
