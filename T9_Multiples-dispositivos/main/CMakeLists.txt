idf_component_register(SRCS "main.c" "myMail.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "config.html" server_root_cert.pem)
