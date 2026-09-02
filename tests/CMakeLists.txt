# Unit tests: one executable per module, plain C, no framework (tests/unit/m2022_test.h).
# Hardware tests carry the label "hardware" so CI can run: ctest -LE hardware
add_subdirectory(unit)
add_subdirectory(hardware)

add_subdirectory(integration)
