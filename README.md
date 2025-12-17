# Low Power System Design Project

This project contains examples demonstrating low power system design techniques using ESP-IDF.


## How to build and flash the project on a esp32c6 board

Make sure that you have sourced (executed) the `export.sh` script from your ESP-IDF installation directory to set up the build environment.

```
. /path/to/esp-idf/export.sh
```

Before project configuration and build, be sure to set the correct chip target using `idf.py set-target esp32c6`.

### Configure the project

```
idf.py menuconfig
```
Open the project configuration menu (`idf.py menuconfig`) to configure Wi-Fi or Ethernet.

### Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT flash monitor
```

(Replace PORT with the name of the serial port to use.)

(To exit the serial monitor, type ``Ctrl-]``.)

## Before git commits

Before committing changes to git, delete all build files by running:

```
idf.py fullclean
```