# Common pieces shared by the CLI and (later) the PAPPL application.
add_library(m2022_common STATIC common/version.c)
target_include_directories(m2022_common PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(m2022_common PRIVATE M2022_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(m2022_common PUBLIC m2022_warnings)

add_executable(m2022-airbridge cli/main.c)
target_link_libraries(m2022-airbridge PRIVATE m2022_common)
