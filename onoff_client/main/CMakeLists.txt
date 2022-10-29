set(srcs "main.c" "dht11.c" "sd_card.c" "app_http_server.c"
       )

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS  "."
                    EMBED_TXTFILES html/time.html)
