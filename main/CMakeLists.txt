idf_component_register(SRCS "main.c" "bsp_power.c" "bsp_lcd.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_lcd espressif__esp_lcd_sh8601 esp_driver_usb_serial_jtag)
