idf_component_register(SRCS "lpsd_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_client esp_wifi esp_netif esp_event nvs_flash esp_adc

                    
)
