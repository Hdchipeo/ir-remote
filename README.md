
# ESP32 IR Light Control

This project uses an ESP32 with ESP-IDF to receive IR signals from a remote and control a light (toggle ON/OFF).

## Features

- Receive and decode IR remote signals using RMT peripheral
- Store learned IR signal into NVS
- Compare received signal with stored signal
- Toggle light state based on matching signal
- Long-press button to reset learned IR data

## Hardware Requirements

- ESP32 development board
- IR Receiver module (e.g., VS1838B)
- LED or Relay connected to GPIO
- Button for long-press reset
