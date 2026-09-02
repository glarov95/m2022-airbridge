# Require the physical printer. Run explicitly: ctest --test-dir build -L hardware
add_test(NAME hw_usb_probe COMMAND m2022-airbridge probe)
add_test(NAME hw_usb_replay_black_square
         COMMAND m2022-airbridge send ${PROJECT_SOURCE_DIR}/fixtures/oracle/samsung/black-square-a4.spl)
set_tests_properties(hw_usb_probe hw_usb_replay_black_square PROPERTIES LABELS "hardware")
