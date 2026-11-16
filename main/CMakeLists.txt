idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES ipp_client
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash
                       EMBED_FILES "test.jpg")
