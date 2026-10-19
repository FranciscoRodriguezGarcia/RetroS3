#pragma once

#include "driver/spi_master.h"

// ---------------- SD ----------------

#define SD_MOUNT_POINT "/sdcard"

#define SD_SCK   8
#define SD_MISO  18
#define SD_MOSI  17
#define SD_CS    16


// ---------------- TFT ----------------

#define TFT_HOST SPI3_HOST

#define TFT_SCK   12
#define TFT_MOSI  11
#define TFT_CS    10
#define TFT_RST   9
#define TFT_DC    7

#define TFT_WIDTH   320
#define TFT_HEIGHT  240

#define TFT_STRIPE_LINES 20

#define TFT_STRIPE_PIXELS \
    (TFT_WIDTH * TFT_STRIPE_LINES)
