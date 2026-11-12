# hal/ 与 apps/ 源码递归收集（对照 StackChan firmware/main）。
# 注意 GLOB 仅在 configure 时求值：新增/删除源文件后必须 idf.py reconfigure。
file(GLOB_RECURSE MAIN_MODULE_SOURCES
    "hal/*.c"
    "apps/*.c"
)

idf_component_register(SRCS main.c config_model.c config_store.c time_sync.c low_power.c ${MAIN_MODULE_SOURCES}
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_lcd espressif__esp_lcd_sh8601 esp_driver_usb_serial_jtag esp_timer lvgl__lvgl
                             nvs_flash json app_update esp_wifi esp_netif esp_event)
