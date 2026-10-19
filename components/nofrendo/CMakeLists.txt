
file(GLOB NOFRENDO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

file(GLOB NOFRENDO_CPU
    "${CMAKE_CURRENT_SOURCE_DIR}/cpu/*.c"
)

file(GLOB NOFRENDO_NES
    "${CMAKE_CURRENT_SOURCE_DIR}/nes/*.c"
)

file(GLOB NOFRENDO_MAPPERS
    "${CMAKE_CURRENT_SOURCE_DIR}/mappers/*.c"
)

file(GLOB NOFRENDO_SOUND
    "${CMAKE_CURRENT_SOURCE_DIR}/sndhrdw/*.c"
)

file(GLOB NOFRENDO_SNSS
    "${CMAKE_CURRENT_SOURCE_DIR}/libsnss/*.c"
)

idf_component_register(
    SRCS
        ${NOFRENDO_ROOT}
        ${NOFRENDO_CPU}
        ${NOFRENDO_NES}
        ${NOFRENDO_MAPPERS}
        ${NOFRENDO_SOUND}
        ${NOFRENDO_SNSS}

    INCLUDE_DIRS
        "."
        "cpu"
        "nes"
        "mappers"
        "sndhrdw"
        "libsnss"

    PRIV_INCLUDE_DIRS
        "${PROJECT_DIR}/main"

    PRIV_REQUIRES
        retro_audio
        esp_timer
        esp_psram
        heap
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=misleading-indentation
)
