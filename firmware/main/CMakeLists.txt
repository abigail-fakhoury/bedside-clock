idf_component_register(
    SRCS "main.c" "board.c" "rtc.c" "display.c" "touch.c" "ui.c" "config.c" "net.c" "readwise.c" "store.c" "audio.c" "voice.c" "flow.c" "post.c"
    INCLUDE_DIRS "."
    REQUIRES core esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_partition esp_driver_spi esp_driver_usb_serial_jtag esp_lcd esp_timer esp_hw_support esp_wifi esp_netif esp_event nvs_flash lwip esp_http_client mbedtls)

