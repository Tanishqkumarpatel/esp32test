set(srcs "mock_exo_main.c" "scenario.c" "frame.c")

if(CONFIG_MOCK_EXO_COMMS_STUB)
    list(APPEND srcs "comms_stub.c")
elseif(CONFIG_MOCK_EXO_COMMS_UART)
    list(APPEND srcs "comms_uart.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES driver esp_timer esp_hw_support)
