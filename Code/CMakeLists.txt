cmake_minimum_required(VERSION 3.13)

# Point to the Pico SDK either way:
#   cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk
#   export PICO_SDK_PATH=/path/to/pico-sdk
if (NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(lab_supply C CXX ASM)
set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(lab_supply
    src/main.cpp
    src/display.cpp
    src/psu_control.cpp
    src/peripherals.cpp
    src/cal_menu.cpp
    src/cal_wizard.cpp
    src/cal_fit.cpp
    src/flash_store.cpp
    src/mcp4728.cpp
    src/st7735.c
    src/mcp23s17.c
    src/ADC_measurement.c
)

target_include_directories(lab_supply PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(lab_supply PRIVATE
    pico_stdlib
    hardware_spi
    hardware_i2c
    hardware_pwm
    hardware_gpio
    hardware_irq
    hardware_adc
    hardware_sync     # save_and_disable_interrupts
    hardware_clocks
    hardware_timer
    hardware_flash    # flash_range_erase / flash_range_program
    m                 # libm for logf() in NTC temperature
)

pico_add_extra_outputs(lab_supply)   # generates .uf2 .bin .hex .map

pico_enable_stdio_usb(lab_supply  1)
pico_enable_stdio_uart(lab_supply 0)