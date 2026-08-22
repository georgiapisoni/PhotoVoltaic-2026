# PV monitor telemetry protocol

## Phase 1: raw Serial verification

The Arduino Nano transmits its existing human-readable diagnostic and
measurement lines at 9600 baud using 8 data bits, no parity, and one stop bit.
The ESP32 receives them on UART2 GPIO16 and echoes complete lines to its USB
MicroPython console.

## Planned phase 2: machine-readable telemetry

The Nano will append one newline-terminated record after each complete loaded,
open-circuit, and short-circuit measurement cycle. The record will use a unique
`PV1,` prefix so the ESP32 can ignore human-readable diagnostics. The ESP32 will
validate and parse that record, convert it to JSON, and publish it through MQTT.

The SD card remains the authoritative local data log if Wi-Fi or MQTT is
temporarily unavailable.

