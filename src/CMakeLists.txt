find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

# PAPPL is built from the pinned submodule into build/pappl-install by scripts/build-pappl.sh.
set(M2022_PAPPL_PREFIX "${PROJECT_BINARY_DIR}/pappl-install")
if(NOT EXISTS "${M2022_PAPPL_PREFIX}/lib/pkgconfig/pappl.pc")
    message(STATUS "Building PAPPL (first time only) ...")
    execute_process(COMMAND "${PROJECT_SOURCE_DIR}/scripts/build-pappl.sh" "${M2022_PAPPL_PREFIX}"
                    RESULT_VARIABLE _pappl_rc OUTPUT_QUIET)
    if(NOT _pappl_rc EQUAL 0)
        message(FATAL_ERROR "PAPPL build failed; see ${M2022_PAPPL_PREFIX}.*.log")
    endif()
endif()
set(ENV{PKG_CONFIG_PATH} "${M2022_PAPPL_PREFIX}/lib/pkgconfig:/opt/homebrew/opt/openssl@3/lib/pkgconfig:/opt/homebrew/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(PAPPL REQUIRED pappl)
# pkg_check_modules splits "-framework X" into two tokens and turns X into -lX, so take the raw
# static link line from pkg-config and re-join the framework pairs ourselves.
execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --libs --static pappl
                OUTPUT_VARIABLE _pappl_libs OUTPUT_STRIP_TRAILING_WHITESPACE)
separate_arguments(_pappl_tokens NATIVE_COMMAND "${_pappl_libs}")
set(M2022_PAPPL_LINK "")
set(_fw FALSE)
foreach(tok IN LISTS _pappl_tokens)
    if(_fw)
        list(APPEND M2022_PAPPL_LINK "-framework ${tok}")
        set(_fw FALSE)
    elseif(tok STREQUAL "-framework")
        set(_fw TRUE)
    else()
        list(APPEND M2022_PAPPL_LINK "${tok}")
    endif()
endforeach()
add_library(pappl::pappl INTERFACE IMPORTED)
target_include_directories(pappl::pappl SYSTEM INTERFACE "${M2022_PAPPL_PREFIX}/include")
target_link_libraries(pappl::pappl INTERFACE ${M2022_PAPPL_LINK})

# Common pieces shared by the CLI and (later) the PAPPL application.
find_package(ZLIB REQUIRED)
add_library(m2022_common STATIC common/version.c common/json.c common/fileio.c)
target_include_directories(m2022_common PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(m2022_common PRIVATE M2022_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(m2022_common PUBLIC m2022_warnings ZLIB::ZLIB)

# Raster ingest, tone curve, geometry, CUPS raster header. Pure (ADR-005).
add_library(m2022_raster STATIC raster/convert.c raster/tone.c raster/cupsraster.c)
target_link_libraries(m2022_raster PUBLIC m2022_common m)

# Halftoning and presets. Pure.
add_library(m2022_halftone STATIC halftone/halftone.c halftone/bluenoise64.c)
target_link_libraries(m2022_halftone PUBLIC m2022_raster)

# USB printer-class transport plus its pure helpers (IEEE 1284 parsing, status mapping).
add_library(m2022_usb STATIC usb/usb.c usb/ieee1284.c usb/status.c)
target_link_libraries(m2022_usb PUBLIC m2022_common PkgConfig::LIBUSB)

# Read-only view of local CUPS queues through the system libcups (macOS SDK).
add_library(m2022_cups STATIC cups/queues.c)
target_link_libraries(m2022_cups PUBLIC m2022_common cups)
target_compile_options(m2022_cups PRIVATE -Wno-deprecated-declarations)

# SPL/QPDL records, job walker, 0x11 band codec, band layout. Pure (ADR-005).
add_library(m2022_qpdl STATIC qpdl/records.c qpdl/walk.c qpdl/codec11.c qpdl/band.c qpdl/encode.c)
target_link_libraries(m2022_qpdl PUBLIC m2022_common)

# Media table shared by the application (PWG names) and the encoder (QPDL codes), and the
# mapping from IPP job options to presets, PJL values and page geometry. Pure.
add_library(m2022_media STATIC app/media.c app/jobmap.c)
target_link_libraries(m2022_media PUBLIC m2022_common m)

# The Printer Application: PAPPL system, driver data, the print pipeline behind the raster
# callbacks, and the USB device scheme (static PAPPL).
add_library(m2022_app STATIC app/app.c app/usbdev.c)
target_link_libraries(m2022_app PUBLIC m2022_common m2022_media m2022_qpdl m2022_halftone m2022_usb pappl::pappl)

add_executable(m2022-airbridge cli/main.c cli/cmd_probe.c cli/cmd_send.c cli/cmd_decode.c cli/cmd_server.c cli/cmd_render.c cli/cmd_encode.c cli/pipeline.c)
target_link_libraries(m2022-airbridge PRIVATE m2022_usb m2022_cups m2022_qpdl m2022_app m2022_halftone)
