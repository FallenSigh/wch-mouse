# =============================================================================
#  dongle - 2.4G RF receiver dongle for the wch-mouse firmware (WCH CH585)
#
#  RF host (RX) -> USB HID mouse. Built as a subdirectory of the wch-mouse
#  project so it shares that project's vendor SDK, HAL, USB HID device layer
#  and RF link config (src/rf_cfg.h) through cmake/wch_sdk.cmake, then turns
#  each received report into a USB HID report for the PC.
#
#  Everything below comes from the parent project; this file only names the
#  dongle's own sources. Configure and build from the repository root:
#     cmake --preset wch && cmake --build build                   # both
#     cmake --build build --target wch-dongle                     # dongle only
#     cmake --build build --target flash-usb-dongle
# =============================================================================

add_executable(wch-dongle
    src/main.c
    src/rf_dongle.c)

target_include_directories(wch-dongle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Same vendor id as the mouse, different product id: the two share usb_desc.h,
# and the host tells them apart by the PID (host tooling and the udev rule key
# off it). Change this single value to renumber the dongle.
target_compile_definitions(wch-dongle PRIVATE DEF_USB_PID=0xFE0D)

wch_firmware(wch-dongle "-dongle")

# The dongle is always the RF host: radio HAL and library, but no GATT profile
# and no transport define - it drives the RF IP itself in src/rf_dongle.c.
wch_firmware_radio(wch-dongle RADIO)
