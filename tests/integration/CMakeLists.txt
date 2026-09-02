# Whole-application tests without hardware: the server with a file device, driven over IPP
# with the ipptool that ships with macOS.
add_test(NAME it_print_raster
         COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/print-raster.sh $<TARGET_FILE:m2022-airbridge>
                 ${PROJECT_SOURCE_DIR}/fixtures/oracle/samsung ${PROJECT_BINARY_DIR}/it-print-raster)
set_tests_properties(it_print_raster PROPERTIES LABELS "integration" TIMEOUT 120)
