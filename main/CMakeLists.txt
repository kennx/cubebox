idf_component_register(SRCS "main.c" "bsp_power.c" "bsp_rtc.c" "bsp_lcd.c" "ui_clock.c" "bsp_touch.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_lcd espressif__esp_lcd_sh8601 esp_driver_usb_serial_jtag waveshare__esp_lcd_touch_cst9217)
