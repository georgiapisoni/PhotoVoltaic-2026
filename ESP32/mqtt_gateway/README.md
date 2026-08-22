# ESP32 Nano serial receiver

This first implementation verifies one-way communication from the Arduino
Nano to an ESP32-WROOM-32 development board. It does not use Wi-Fi or MQTT yet.

## Wiring

- Nano D1/TX -> 5 V to 3.3 V level shifter/divider -> ESP32 GPIO16/RX2
- Nano GND -> ESP32 GND
- ESP32 GPIO17/TX2 is configured but does not need to be connected
- Keep both serial ports at 9600 baud

Power the ESP32 from its own USB connection or a suitable supply. Do not power
the ESP32 from the Nano 3.3 V pin.

## Install and run

Flash the standard MicroPython ESP32 firmware, then copy the test files from
this directory to the ESP32:

```sh
mpremote connect auto fs cp serial_receiver.py :serial_receiver.py
mpremote connect auto fs cp main.py :main.py
mpremote connect auto reset
mpremote connect auto repl
```

The ESP32 should display every complete line received from the Nano as:

```text
NANO: LOAD CH1: shunt: ...
```

Press `Ctrl-C` in the REPL to interrupt the receiver. Because the Nano already
prints its measurements through D1/TX, no Nano firmware change is required for
this initial electrical test.

## Future MQTT configuration

`config.example.py` documents the future Wi-Fi and MQTT settings. Copy it to
`config.py` only when the MQTT stage is added. `config.py` is ignored by Git so
credentials are not committed.

