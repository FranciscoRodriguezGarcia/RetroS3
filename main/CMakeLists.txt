
idf_component_register(
    SRCS
        "main.c"
        "storage.c"
        "display.c"
        "rom_manager.c"
        "emulator_manager.c"
        "launcher.c"
        "ui_media.c"
        "input.c"
        "gameboy.c"
        "nes.c"
        "status_led.c"
    INCLUDE_DIRS "."
    REQUIRES
        retro_audio
        gnuboy
        nofrendo
        fatfs
        sdmmc
        esp_driver_spi
        esp_lcd
        esp_timer
        esp_driver_usb_serial_jtag
        driver
)
