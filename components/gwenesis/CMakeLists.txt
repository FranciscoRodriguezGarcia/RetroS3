# Gwenesis vendored for RetroS3, pinned to bzhxx/gwenesis 168e4666.
idf_component_register(
    SRCS
        "src/bus/gwenesis_bus.c"
        "src/cpus/M68K/m68kcpu.c"
        "src/cpus/Z80/Z80.c"
        "src/io/gwenesis_io.c"
        "src/savestate/gwenesis_savestate.c"
        "src/sound/z80inst.c"
        "src/sound/ym2612.c"
        "src/sound/gwenesis_sn76489.c"
        "src/vdp/gwenesis_vdp_mem.c"
        "src/vdp/gwenesis_vdp_gfx.c"
    INCLUDE_DIRS
        "src/bus" "src/cpus/M68K" "src/cpus/Z80"
        "src/io" "src/savestate" "src/sound" "src/vdp"
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    GNW_TARGET_MARIO=0 GNW_TARGET_ZELDA=0
)
