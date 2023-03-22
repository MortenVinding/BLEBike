# BLEBike

This is a fork of https://github.com/arpruss/BLEBike

Simple ESP32 project to bluetooth enable an old exercise bike.
Uses CPS (Cycling Power Service) Bluetooth Low Energy (BLE), so compatible with popular fitnes apps like Zwift, Wahoo RGT, ROUVY and the like.

Support for a HD44780 display and different types of hearth rate sensores:
puls input on GPIO pin,
MI Band

# Installation

## 1. Install platformio:
* MacOS: brew install platformio
* Linux: apt install platformio
  
## 2. Clone this git repo:
* git clone https://github.com/MortenVinding/BLEBike.git
  
## 3. Build and install:
* cd BLEBike
* pio run -v -t upload
