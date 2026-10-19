idf_component_register(
    SRCS
        "cpu.c"
        "gnuboy.c"
        "hw.c"
        "lcd.c"
        "sound.c"

    INCLUDE_DIRS
        "."
)
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
        -O3
        -Wno-error=maybe-uninitialized
)