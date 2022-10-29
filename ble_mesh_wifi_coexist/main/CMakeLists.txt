set(srcs "main.c" "app_mqtt.c" "app_http_server.c" "data_channel_config.c" )

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS  "."
                    EMBED_TXTFILES html/wifi.html html/channel.html)
