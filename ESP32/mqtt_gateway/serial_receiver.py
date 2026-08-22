"""Receive Arduino Nano Serial output through ESP32 UART2."""

import time
from machine import UART


UART_ID = 2
UART_BAUD = 9600
UART_RX_PIN = 16
UART_TX_PIN = 17


def run():
    """Print each complete Nano line to the ESP32 USB/REPL console."""
    nano_uart = UART(
        UART_ID,
        baudrate=UART_BAUD,
        bits=8,
        parity=None,
        stop=1,
        rx=UART_RX_PIN,
        tx=UART_TX_PIN,
        timeout=100,
        rxbuf=1024,
    )

    print("ESP32 Nano serial receiver")
    print("UART2 RX=GPIO16, TX=GPIO17, baud=9600")
    print("Waiting for Nano data...")

    while True:
        line = nano_uart.readline()
        if line:
            try:
                message = line.decode("utf-8").strip()
            except UnicodeError:
                print("NANO [invalid UTF-8]:", line)
                continue

            if message:
                print("NANO:", message)
        else:
            time.sleep_ms(10)

