cmake_minimum_required(VERSION 3.13)

# Set PICO_SDK_PATH before running cmake:
#   export PICO_SDK_PATH=/path/to/pico-sdk
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(lab_supply C CXX ASM)
set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(lab_supply
    main.cpp
    st7735.c
    mcp23s17.c
    mcp4728.cpp
    ADC_measurement.c
)

target_include_directories(lab_supply PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(lab_supply PRIVATE
    pico_stdlib
    hardware_spi
    hardware_i2c
    hardware_pwm
    hardware_gpio
    hardware_irq
    hardware_adc
    hardware_sync
    hardware_clocks
    m                 # libm for logf() in NTC temperature
)

pico_add_extra_outputs(lab_supply)   # generates .uf2 .bin .hex .map

pico_enable_stdio_usb(lab_supply  1)
pico_enable_stdio_uart(lab_supply 0)