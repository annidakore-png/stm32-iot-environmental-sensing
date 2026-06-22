# Epic Sunfarm Monitoring System

An industry-sponsored smart agriculture monitoring system built for real-world farm deployment. The system collects environmental, soil, and location data across multiple wireless sensor nodes and delivers live readings to a farmer's mobile app — powered entirely by solar energy.

This is a 16-member cross-functional project spanning embedded hardware, embedded software, AI/ML, and mobile app development.

---

## What it does

Farmers currently have no affordable, real-time way to monitor their field conditions remotely. Epic Sunfarm solves this by deploying wireless sensor nodes across a farm that continuously measure:

- Air temperature and humidity
- Soil moisture, soil temperature, and soil electrical conductivity (EC)
- Wind speed and wind direction
- Precise GPS location of each node

All data travels wirelessly to a central server node, which pushes it to a mobile app the farmer can check from anywhere. Because each node reports its GPS coordinates, the farmer's app can show a live map of the farm with real-time readings pinned at the exact location of each sensor node.

---

## System architecture

The network consists of 4 nodes — 3 general sensor nodes and 1 server node.

Each general node runs on an STM32F401CCU6 microcontroller, reads all sensors including GPS, and transmits data wirelessly over LoRa (433 MHz) to the server node. The server node aggregates data from all three field nodes and forwards it to the cloud backend, which feeds the farmer-facing mobile app.

Every node is powered by a solar panel and rechargeable battery — no mains power required, making it deployable anywhere in a field.

```
[General Node 1] ──LoRa──┐
[General Node 2] ──LoRa──┤──► [Server Node] ──► Cloud ──► Farmer's Phone App
[General Node 3] ──LoRa──┘
       ↑
  Solar Panel + Battery
```

---

## Sensors on each general node

| Sensor | What it measures | Protocol |
|---|---|---|
| ZTS-3000 | Soil moisture, soil temperature, soil EC | RS485 Modbus RTU |
| DHT22 | Air temperature, air humidity | Single-wire digital |
| Anemometer | Wind speed | Analog (ADC) |
| Wind vane | Wind direction | Analog (ADC) |
| NEO-6M GPS | Precise latitude, longitude, altitude | UART (NMEA sentences) |

---

## My contribution

I was part of the embedded hardware domain and handled the full hardware bring-up for the sensor nodes:

- Sensor selection, wiring, and physical integration for all five sensor types
- RS485 Modbus RTU communication with the ZTS-3000 soil sensor via MAX485 transceiver
- DHT22 interfacing with microsecond-level timing using DWT cycle counter on STM32
- ADC configuration for analog wind speed and direction sensors (PA4, PA5 on ADC1)
- NEO-6M GPS module integration over UART — NMEA sentence parsing to extract latitude, longitude, and altitude
- LoRa E32-433T20D wireless transmission setup and packet protocol design
- STM32F401CCU6 clock configuration (84 MHz via HSI PLL) and peripheral initialisation using CubeMX
- Hardware debugging — traced and resolved RS485 DE/RE control issues, LoRa mode pin faults, and baud rate mismatches
- Also wrote the embedded C firmware for sensor reading, packet formatting, and transmission
- Built the Python receiver script on Raspberry Pi 5 for live terminal display of all sensor data

---

## Tech stack

**Microcontroller:** STM32F401CCU6 (Black Pill)  
**Firmware:** Embedded C using STM32 HAL  
**Wireless:** LoRa E32-433T20D at 433 MHz  
**Protocols:** RS485 Modbus RTU, UART, ADC, NMEA  
**GPS:** u-blox NEO-6M  
**Power:** Solar panel + LiPo battery  
**Receiver side:** Raspberry Pi 5, Python 3, pyserial  
**Team size:** 16 members across 4 domains  
**Type:** Industry sponsored project  

---

## Packet format

Each node transmits a compact readable packet every 5 seconds:

```
$AT:34.2,AH:63.1,SM:45.2,ST:28.3,EC:850,SQ:Medium,WS:3.5,WL:LOW,WD:180,WC:S,LAT:18.5204,LON:73.8567,ALT:559.2*
```

Fields: Air Temperature, Air Humidity, Soil Moisture, Soil Temperature, EC value, EC quality, Wind Speed, Wind Level, Wind Direction degrees, Wind Compass direction, GPS Latitude, GPS Longitude, GPS Altitude.

---

## Repository contents

```
firmware/          STM32 embedded C source files (main.c, dht22 driver)
raspberry-pi/      Python receiver script for live data display
docs/              Wiring diagrams and hardware photos
```

---

## Project context

Epic Sunfarm is an industry-sponsored project at Vishwakarma Institute of Technology, Pune. The goal is to deliver a production-ready agricultural monitoring solution — not a lab prototype — with solar autonomy, multi-node wireless networking, GPS-tagged sensor data, and a real farmer-facing mobile application.
