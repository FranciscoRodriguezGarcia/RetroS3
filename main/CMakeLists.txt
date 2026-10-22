
idf_component_register(
    SRCS
        "main.c"
        "storage.c"
        "display.c"
        "rom_manager.c"
        "emulator_manager.c"
        "emulator_diag.c"
        "launcher.c"
        "ui_media.c"
        "input.c"
        "gameboy.c"
        "nes.c"
        "nes_frantastic.cpp"
        "megaDrive.c"
        "status_led.c"
    INCLUDE_DIRS "."
    REQUIRES
        retro_audio
        gnuboy
        nofrendo
        frantastic_nes
        gwenesis
        fatfs
        sdmmc
        esp_driver_spi
        esp_lcd
        esp_timer
        heap
        esp_driver_usb_serial_jtag
        driver
)

# The NES integration catches std::exception from the standalone core.
target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions)
