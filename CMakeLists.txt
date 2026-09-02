cmake_minimum_required(VERSION 3.25)

project(m2022-airbridge
        VERSION 0.1.0
        DESCRIPTION "Printer Application for the Samsung SL-M2022 (SPL/QPDL over USB, AirPrint/IPP Everywhere facing)"
        LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(M2022_WERROR "Treat warnings as errors" ON)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_m2022_sanitizers_default ON)
else()
    set(_m2022_sanitizers_default OFF)
endif()
option(M2022_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" ${_m2022_sanitizers_default})

include(cmake/warnings.cmake)

# Pure modules are added here as they are implemented (raster, halftone, jbig, qpdl, usb, ...).
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
