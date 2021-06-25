# Multi-threaded ds18 temperature blinky

This program blinks a light slower or faster depending on temperature.

Makes use of some of DavidAntliff's example code to read the temperature using the one wire protocol using the ds18b20 sensor. (https://github.com/DavidAntliff/esp32-ds18b20-example)

Uses rootamentary multi-threading to run the light blinking and temperature sensing in two different threads.
