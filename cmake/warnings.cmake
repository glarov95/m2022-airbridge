# Strict warning set shared by every target. Applied through an interface library
# so that third-party code (PAPPL, oracles) is never affected.
add_library(m2022_warnings INTERFACE)
target_compile_options(m2022_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion -Wvla
    -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition
    -Wformat=2 -Wundef -Wcast-qual -Wpointer-arith
    -Wimplicit-fallthrough)
if(M2022_WERROR)
    target_compile_options(m2022_warnings INTERFACE -Werror)
endif()
if(M2022_SANITIZERS)
    target_compile_options(m2022_warnings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(m2022_warnings INTERFACE -fsanitize=address,undefined)
endif()
