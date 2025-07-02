
# IR Remote for ESP32 using ESP-IDF

This project implements an **Infrared (IR) learning and transmitting system** using an **ESP32** and the **ESP-IDF framework**. It enables the ESP32 to:

- Learn IR signals (e.g. from TV, air conditioner remotes)
- Store them into the SPIFFS file system
- Replay them on demand via an IR LED
- Manage learned signals via command-line interface

## Features

- Learn IR codes using the RMT peripheral
- Store multiple IR keys in SPIFFS
- Transmit learned IR signals via IR LED
- Rename and delete keys
- Format SPIFFS partition
- UART command interface using `esp_console` and `argtable3`

## Requirements

- ESP32 board (tested on ESP32-WROOM)
- IR receiver module (e.g. TSOP38238)
- IR LED + resistor
- Push-button (optional)
- ESP-IDF v5.3+
- USB-UART console (e.g. `idf.py monitor`, PuTTY, screen)

## Getting Started

### 1. Clone the Repository

```
git clone git@github.com:Hdchipeo/ir-remote.git
cd ir-remote
```

### 2. Set Up ESP-IDF

Ensure you have ESP-IDF installed and sourced:

```
. $HOME/esp/esp-idf/export.sh
```

### 3. Configure the Project

```
idf.py menuconfig
```

Ensure:
- SPIFFS is enabled
- Partition table is set to `Custom partition table`
- File: `partitions_custom.csv`

### 4. Build and Flash

```
idf.py build flash monitor
```

## SPIFFS Partition Table

Example `partitions_custom.csv`:

```
# Name,     Type, SubType, Offset,   Size
nvs,        data, nvs,     0x9000,   0x5000
otadata,    data, ota,     0xe000,   0x2000
phy_init,   data, phy,     0x10000,  0x1000
ota_0,      app,  ota_0,   0x20000,  0x180000
ota_1,      app,  ota_1,   0x1A0000, 0x180000
storage,    data, spiffs,  0x320000, 0x40000
```

Make sure the `Offset` values do not overlap. Partition table errors will stop your build.

## Console Commands

Command-line control is available via UART:

- `learn <key_name>`  
  Learn a new IR signal and save under the key.

- `transmit <key_name>`  
  Send the signal saved under this key.

- `list`  
  Show all saved IR keys.

- `rename <old_key> <new_key>`  
  Rename a saved key.

- `delete <key_name>`  
  Delete a key from SPIFFS.

- `format`  
  Erase all saved IR data.

Example usage:

```
learn tv_power
send tv_power
rename tv_power samsung_power
list
delete samsung_power
format
```

## GPIO Configuration

| Function        | GPIO      | Description              |
|-----------------|-----------|--------------------------|
| IR Receiver     | GPIO4     | Connect IR receiver data |
| IR Transmitter  | GPIO5     | Connect IR LED (via resistor) |
| Trigger Button  | GPIO15    | Optional physical button |

Edit these in your `*.h` files as needed.

## Flashing Notes

If you're unsure whether SPIFFS was erased, run:

```
idf.py erase-flash flash
```

Or to avoid erasing SPIFFS:

```
idf.py flash --flash_mode dio --flash_freq 40m --flash_size 4MB
```

Use `idf.py fullclean` with caution, it **does not erase SPIFFS** by default unless SPIFFS is embedded in the firmware binary.

## License

MIT License. See `LICENSE` file.

## Author

Created by [Hdchipeo](https://github.com/Hdchipeo)
