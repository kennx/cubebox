idf_component_register(SRCS "main.c" "bsp_power.c" "bsp_rtc.c" "bsp_lcd.c" "ui_clock.c" "ui_settings.c" "bsp_touch.c" "app_wifi.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_lcd espressif__esp_lcd_sh8601 esp_driver_usb_serial_jtag waveshare__esp_lcd_touch_cst9217 nvs_flash esp_wifi esp_netif)
