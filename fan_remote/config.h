#pragma once

// ---- CC1101 <-> ESP32-C3 SuperMini pin map ----
#define PIN_SCK    4
#define PIN_MISO   5
#define PIN_MOSI   6
#define PIN_CS     7
#define PIN_GDO0   10   // raw data + interrupt (RX/TX)

// ---- Radio defaults ----
#define DEFAULT_FREQ_MHZ  433.92f

// ---- Storage ----
#define MAX_SLOTS  16
