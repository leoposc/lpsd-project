idf_component_register(SRCS "http_request_example_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_client esp_wifi esp_netif esp_event nvs_flash
)
