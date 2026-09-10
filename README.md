# WT32-ETH01 -> Telnet test

Brings up Ethernet on the WT32-ETH01's onboard LAN8720 PHY, then opens a
TCP connection to a telnet server, sends a test string, and logs what
comes back.

## Before building

Edit these two lines in `main/main.c`:

```c
#define TELNET_SERVER_IP    "192.168.1.100"
#define TELNET_SERVER_PORT  23
```

To test without a real telnet server, you can point it at a netcat
listener on your PC: `nc -lk 23` (Linux/macOS) and watch it print the
"Hello from WT32-ETH01!" message.

## Build & flash

```
. $IDF_PATH/export.sh      # or export.bat on Windows
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Hardware notes specific to this board

- **PHY power**: GPIO16 must be driven high to enable the LAN8720 -
  the code does this on startup.
- **MDC/MDIO**: GPIO23 / GPIO18.
- **RMII clock**: WT32-ETH01 has a 50MHz crystal feeding the clock
  *into* GPIO0, rather than the ESP32 generating it. The code sets
  `EMAC_CLK_EXT_IN` on GPIO0 to match.
- **Flashing quirk**: because GPIO0 is also a boot-mode strapping pin
  and is tied to that oscillator, some USB-serial adapters/boards
  need a manual button-press (hold the board's reset/boot buttons in
  the right order) to enter download mode reliably. If `idf.py flash`
  can't find the chip, try that before assuming it's a wiring problem.
- If the PHY never links up, double check `ETH_PHY_ADDR` (1 is correct
  for the LAN8720 on virtually all WT32-ETH01 boards) and that the
  Ethernet cable's link/activity LEDs light up.

## What "telnet" means here

This is a raw TCP client on port 23 - no telnet option negotiation
(IAC commands, etc.). That's enough to talk to most simple telnet
servers, debug consoles, or a netcat listener. If you need full RFC 854
option negotiation, that would be a separate addition.
