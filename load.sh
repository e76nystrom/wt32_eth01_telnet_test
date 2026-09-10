#!/bin/zsh

esptool.py --chip esp32 --port /dev/tty.wchusbserial5B0B0099851 --baud 921600 write_flash 0x00 ./cmake-build-debug-esp32/merged-binary.bin

# esptool.py --chip esp32 --port /dev/tty.wchusbserial5B0B0103351 --baud 921600 write_flash 0x00 ./cmake-build-debug-esp32/merged-binary.bin
