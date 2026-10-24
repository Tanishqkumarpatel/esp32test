idf_component_register(SRCS "uart_async_rxtxtasks_main.c" "frame.c" "scenario.c"
                    REQUIRES esp_driver_uart esp_driver_gpio esp_timer
                    INCLUDE_DIRS ".")
