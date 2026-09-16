#!/bin/zsh

if [[ -z "$IDF_PATH" ]]; then
 source ~/.espressif/v6.0.1/esp-idf/export.sh
fi

TTYS=/dev/tty.wchusbserial5B0B0103351
TTYC=/dev/tty.wchusbserial5B0B0099851

if [[ "$1" == "s" ]]; then
 echo "Load SERVER"
 TTY=$TTYS
 esptool --chip esp32 --port $TTY --baud 921600 write_flash 0x00 ./cmake-build-server/merged-binary.bin
fi   

if [[ "$1" == "c" ]]; then
 echo "Load CLIENT"
 TTY=$TTYC
 esptool --chip esp32 --port $TTY --baud 921600 write_flash 0x00 ./cmake-build-client/merged-binary.bin
fi

if [[ "$1" == "S" ]]; then
 echo "Reset SERVER"
 TTY=$TTYS
 esptool --port $TTY --before default-reset --after hard-reset run
fi

if [[ "$1" == "C" ]]; then
 echo "Reset CLIENT"
 TTY=$TTYC
 esptool --port $TTY --before default-reset --after hard-reset run
fi

echo $TTY
picocom -b 115200 $TTY
