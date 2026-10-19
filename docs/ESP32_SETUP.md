# ESP32 & ESP32-CAM Hardware Setup Guide

This guide details hardware requirements, flashing procedures, partition table layouts, and memory constraints for deploying MimiClaw on ESP32 hardware targets, including vanilla ESP32 (no PSRAM) and ESP32-CAM (with PSRAM).

---

## 1. Hardware Target Matrix

| Target | Flash | PSRAM | Console / REPL Interface | sdkconfig Defaults | Partition Table |
|---|---|---|---|---|---|
| **ESP32-S3** *(Default)* | 16 MB | 8 MB / 16 MB (Octal) | USB Serial/JTAG (`/dev/tty.usbmodem*` or `/dev/ttyACM*`) | `sdkconfig.defaults;sdkconfig.defaults.esp32s3` | `partitions.csv` (16 MB) |
| **ESP32-CAM** | 4 MB | 4 MB (Quad SPI) | UART0 Bridge (`/dev/tty.usbserial*` or `/dev/ttyUSB*`) | `sdkconfig.defaults;sdkconfig.defaults.esp32cam` | `partitions_esp32.csv` (4 MB) |
| **Vanilla ESP32** *(WROOM-32 / NodeMCU)* | 4 MB | None (0 MB) | UART0 Bridge (`/dev/tty.usbserial*` or `/dev/ttyUSB*`) | `sdkconfig.defaults;sdkconfig.defaults.esp32` | `partitions_esp32.csv` (4 MB) |

---

## 2. Partition Layouts

MimiClaw supports two partition schemes depending on flash size:

### 16 MB Flash (`partitions.csv` — ESP32-S3)
The default layout provides generous 2 MB OTA application slots and an 11.8 MB SPIFFS filesystem for extensive local memory, skill scripts, and chat sessions.

```csv
# Name,    Type, SubType, Offset,   Size
nvs,       data, nvs,     0x9000,   0x6000
otadata,   data, ota,     0xF000,   0x2000
phy_init,  data, phy,     0x11000,  0x1000
ota_0,     app,  ota_0,   0x20000,  0x200000
ota_1,     app,  ota_1,   0x220000, 0x200000
spiffs,    data, spiffs,  0x420000, 0xBD0000
coredump,  data, coredump,0xFF0000, 0x10000
```

### 4 MB Flash (`partitions_esp32.csv` — ESP32 & ESP32-CAM)
Deduplicated 4 MB partition table optimized for standard 4 MB flash modules:

```csv
# Name,    Type, SubType, Offset,   Size
nvs,       data, nvs,     0x9000,   0x5000
otadata,   data, ota,     0xE000,   0x2000
ota_0,     app,  ota_0,   0x10000,  0x130000
ota_1,     app,  ota_1,   0x140000, 0x130000
spiffs,    data, spiffs,  0x270000, 0x180000
coredump,  data, coredump,0x3F0000, 0x10000
```
- Dual OTA partitions (`ota_0` and `ota_1`) sized at ~1.2 MB (0x130000) each.
- Dedicated 1.5 MB (0x180000) SPIFFS filesystem for personality files (`SOUL.md`, `USER.md`, `MEMORY.md`), skills, and configuration.

---

## 3. Build & Flashing Instructions

### Prerequisites
- ESP-IDF v5.5+ configured in your environment (`source $IDF_PATH/export.sh`).
- Copy `main/mimi_secrets.h.example` to `main/mimi_secrets.h` and configure your credentials, or configure them later over the serial CLI.

### Building for ESP32-S3 (Default)
```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodem1101 flash monitor
```

### Building for Vanilla ESP32 (No PSRAM)
```bash
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32" set-target esp32 fullclean build
idf.py -p /dev/tty.usbserial-0001 flash monitor
```

### Building for ESP32-CAM (4 MB PSRAM)
```bash
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32cam" set-target esp32 fullclean build
idf.py -p /dev/tty.usbserial-0001 flash monitor
```

> **Flashing ESP32-CAM Note**: Connect `GPIO 0` to `GND` before powering on or resetting the board to enter download mode. Disconnect `GPIO 0` from `GND` and press the Reset button to boot into runtime firmware.

---

## 4. Serial Console & REPL Connections

- **ESP32-S3**: MimiClaw connects to the native USB Serial/JTAG peripheral (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`). Simply connect via the USB port labeled "USB" or "COM".
- **ESP32 & ESP32-CAM**: Communication uses UART0 at **115200 baud, 8N1** (`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`):
  - **TX0**: GPIO 1 (connect to USB-TTL adapter RX)
  - **RX0**: GPIO 3 (connect to USB-TTL adapter TX)
  - **GND**: Common Ground with USB-TTL programmer

Once booted, interact with the interactive CLI via:
```text
mimi> help
mimi> status
mimi> wifi_set <ssid> <password>
```

---

## 5. Memory Tuning & Non-PSRAM Constraints

Vanilla ESP32 microcontrollers lack external PSRAM, leaving ~180 KB of usable internal SRAM for all application tasks, networking stacks, and TLS crypto contexts.

### Centralized Buffer Configuration (`main/mimi_config.h`)
Instead of hardcoded division factors, all core buffers are defined centrally in `main/mimi_config.h` governed by `#if CONFIG_SPIRAM`:

| Constant | SPIRAM (S3 / CAM) | Non-PSRAM (Vanilla ESP32) | Rationale |
|---|---|---|---|
| `MIMI_LLM_STREAM_BUF_SIZE` | 32 KB | 4 KB | Initial response buffer; automatically grows via `realloc()` if chunks require more capacity |
| `MIMI_CONTEXT_BUF_SIZE` | 16 KB | 4 KB | Limits system prompt and memory history size to preserve heap for mbedTLS |
| `MIMI_TOOL_OUTPUT_SIZE` | 8 KB | 4 KB | Constrains maximum return string from executing tools |
| `MIMI_SEARCH_BUF_SIZE` | 16 KB | 8 KB | Holds Brave / Tavily search JSON results |

### Dynamic TLS Optimization
When compiling with `sdkconfig.defaults.esp32`:
- `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`: TLS input/output buffers are allocated on-demand per record and freed immediately, preventing persistent 16 KB heap holds.
- `CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y` & `CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT=y`: Certificate bundles and handshake structures are released from heap after verification.
- `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=8192` and `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096`: Reduced TLS fragment length to prevent `ALLOC_FAILED` errors during HTTPS handshakes.

### Operational Recommendations for Non-PSRAM Devices
1. Keep `MEMORY.md` and system prompts concise.
2. Avoid scheduling simultaneous high-bandwidth cron transfers while handling active LLM streaming turns.
3. Monitor heap headroom using the CLI `status` command (`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`).
