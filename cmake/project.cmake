cmake_minimum_required(VERSION 3.22)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_PROJECT_NAME gateway_f407)
include(cmake/gcc-arm-none-eabi.cmake)
project(${CMAKE_PROJECT_NAME} C ASM)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(${CMAKE_PROJECT_NAME})
add_subdirectory(cmake/stm32cubemx)
target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/Tasks/sensor_task.c
    App/Tasks/gateway_task.c
    App/Tasks/screen_task.c
    App/Tasks/button_task.c
    App/Tasks/network_task.c
    App/Tasks/task_health.c
    App/Drivers/st7789.c
    App/Drivers/w5500_port.c
    App/Drivers/clock_us.c
    App/Protocol/modbus_rtu.c
    App/Protocol/kth2r.c
    App/Storage/app_params.c
    App/Storage/app_params_store.c
    App/Storage/internal_flash.c
    App/Data/device_health.c
    App/Data/app_snapshot.c
    App/UI/screen_lvgl.c
    App/UI/screen_ui.c
    App/Drivers/app_watchdog.c
    App/Drivers/w25q64.c
    App/Storage/flash_store.c
    App/Protocol/retry_delay.c
    App/Protocol/mqtt_packet.c
    ioLibrary_Driver/Ethernet/socket.c
    App/Tasks/gateway.c

    ioLibrary_Driver/Ethernet/wizchip_conf.c
    ioLibrary_Driver/Ethernet/W5500/w5500.c
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App/Config App/Tasks App/Drivers App/Protocol App/Storage App/Data App/UI
    Middlewares/lvgl ioLibrary_Driver/Ethernet
)
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS Middlewares/lvgl/src/*.c)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${LVGL_SOURCES})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE _WIZCHIP_=5500
)
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gateway_flash.ld")
