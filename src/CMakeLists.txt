find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

# Common pieces shared by the CLI and (later) the PAPPL application.
add_library(m2022_common STATIC common/version.c common/json.c)
target_include_directories(m2022_common PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(m2022_common PRIVATE M2022_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(m2022_common PUBLIC m2022_warnings)

# USB printer-class transport plus its pure helpers (IEEE 1284 parsing, status mapping).
add_library(m2022_usb STATIC usb/usb.c usb/ieee1284.c usb/status.c)
target_link_libraries(m2022_usb PUBLIC m2022_common PkgConfig::LIBUSB)

# Read-only view of local CUPS queues through the system libcups (macOS SDK).
add_library(m2022_cups STATIC cups/queues.c)
target_link_libraries(m2022_cups PUBLIC m2022_common cups)
target_compile_options(m2022_cups PRIVATE -Wno-deprecated-declarations)

# SPL/QPDL records, job walker, 0x11 band codec, band layout. Pure (ADR-005).
add_library(m2022_qpdl STATIC qpdl/records.c qpdl/walk.c qpdl/codec11.c qpdl/band.c)
target_link_libraries(m2022_qpdl PUBLIC m2022_common)

add_executable(m2022-airbridge cli/main.c cli/cmd_probe.c cli/cmd_send.c cli/cmd_decode.c)
target_link_libraries(m2022-airbridge PRIVATE m2022_usb m2022_cups m2022_qpdl)
