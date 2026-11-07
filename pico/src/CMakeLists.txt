cmake_minimum_required(VERSION 3.13)
include("${CMAKE_CURRENT_LIST_DIR}/../../examples/pico_sdk_import.cmake")
project(buzz_lamps C CXX ASM)
pico_sdk_init()
# Private copies preserve A/B's eight-byte EP0 while allowing the DS3's 64-byte
# EP0 in the same selectable image. Do not mutate the installed SDK sources.
get_target_property(device_sources tinyusb_device_base INTERFACE_SOURCES)
list(FILTER device_sources EXCLUDE REGEX "/device/usbd(_control)?\\.c$")
list(FILTER device_sources EXCLUDE REGEX "/class/hid/hid_device\\.c$")
list(APPEND device_sources "${CMAKE_CURRENT_LIST_DIR}/tinyusb_runtime/usbd.c"
  "${CMAKE_CURRENT_LIST_DIR}/tinyusb_runtime/usbd_control.c"
  "${CMAKE_CURRENT_LIST_DIR}/tinyusb_runtime/hid_device.c")
set_property(TARGET tinyusb_device_base PROPERTY INTERFACE_SOURCES "${device_sources}")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../.." pico_pio_usb)
function(add_bridge target recovery)
  add_executable(${target} buzz_bridge.c report_map.c usb_descriptors.c local_lamps.c ds3.c
    ds3_trace.c usb_trace.c ds3_journal.c
    ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${PICO_TINYUSB_PATH}/src/device)
  target_compile_definitions(${target} PRIVATE PIO_USB_USE_TINYUSB BUZZ_RECOVERY=${recovery})
  target_compile_options(${target} PRIVATE -Wall -Wextra)
  target_link_libraries(${target} PRIVATE pico_stdlib pico_multicore pico_pio_usb
    tinyusb_device tinyusb_host hardware_flash pico_flash hardware_watchdog pico_unique_id)
  target_link_options(${target} PRIVATE "LINKER:-T,${CMAKE_CURRENT_LIST_DIR}/settings_guard.ld")
  pico_add_extra_outputs(${target})
endfunction()
set_source_files_properties(buzz_bridge.c local_lamps.c report_map.c usb_descriptors.c ds3.c
  ds3_trace.c usb_trace.c ds3_journal.c
  PROPERTIES COMPILE_OPTIONS "-Werror")
add_bridge(buzz_bridge_lamps 0)
add_bridge(buzz_bridge_lamps_recovery 1)
add_bridge(buzz_bridge_modec 0)
add_bridge(buzz_bridge_modec_recovery 1)
