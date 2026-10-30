# Static libusb (Windows backend only), so SDL's HIDAPI drivers that need raw USB access work.
# The Switch 2 Pro Controller is the reason: SDL can only initialize it over libusb.
# Sources are libusb v1.0.30 (87a55632db62c9bdc58cd31d3ccfa673f1bb017f), LGPL-2.1, see COPYING.
cmake_minimum_required(VERSION 3.20)
project(libusb C)

add_library(usb-1.0 STATIC
    libusb/core.c
    libusb/descriptor.c
    libusb/hotplug.c
    libusb/io.c
    libusb/strerror.c
    libusb/sync.c
    libusb/os/events_windows.c
    libusb/os/threads_windows.c
    libusb/os/windows_common.c
    libusb/os/windows_usbdk.c
    libusb/os/windows_winusb.c
)
target_include_directories(usb-1.0 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} libusb libusb/os)
target_compile_options(usb-1.0 PRIVATE -w)
set_target_properties(usb-1.0 PROPERTIES
    OUTPUT_NAME usb-1.0
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
