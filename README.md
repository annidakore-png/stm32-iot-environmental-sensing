# STM32 IoT Environmental Sensing System

An end-to-end embedded IoT system built on the STM32F401CCU6 (Black Pill) that monitors soil and atmospheric conditions and transmits sensor data wirelessly over LoRa to a Raspberry Pi 5 receiver.

## Overview

This system reads data from four different sensors using three different communication protocols, packages it into a compact text packet, and transmits it over a 433 MHz LoRa link to a remote receiver — useful for agricultural and weather-monitoring applications where wired connections aren't practical.

## System Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    STM32F401CCU6 (Black Pill)            │
│                                                           │
│   USART1 (PA9/PA10) ──► MAX485 ──► ZTS-3000 Soil Sensor  │
│   PA1                ──► DHT22 Temp/Humidity Sensor      │
│   PA4 (ADC1_IN4)     ──► Wind Speed Sensor (analog)      │
│   PA5 (ADC1_IN5)     ──► Wind Direction Sensor (analog)  │
│   USART2 (PA2/PA3)   ──► LoRa E32-433T20D (TX)            │
└───────────────────────────┬───────────────────────────────┘
                             │ 433 MHz LoRa
                             ▼
┌─────────────────────────────────────────────────────────┐
│              Raspberry Pi 5 (Receiver)                  │
│                                                           │
│   UART0 (GPIO14/15)  ◄── LoRa E32-433T20D (RX)            │
│   Python script parses packet and displays live readings│
└─────────────────────────────────────────────────────────┘
```

## Hardware Used

| Component | Purpose | Interface |
|---|---|---|
| STM32F401CCU6 (Black Pill) | Main microcontroller | — |
| ZTS-3000 | Soil moisture, temperature, EC sensor | RS485 Modbus RTU via MAX485 |
| DHT22 | Air temperature and humidity | Single-wire digital |
| Wind speed sensor | Wind speed (analog voltage output) | ADC (12-bit) |
| Wind direction sensor | Wind direction (analog voltage output) | ADC (12-bit) |
| LoRa E32-433T20D | Long-range wireless transmission | UART (transparent mode) |
| Raspberry Pi 5 | Remote data receiver and display | UART |

## Key Technical Details

- **Clock configuration:** SYSCLK at 84 MHz (HSI → PLL /8 ×84 /2), ADC clock at 21 MHz (PCLK2/4)
- **Auto baud detection:** Firmware automatically detects the ZTS-3000's configured baud rate (4800/9600/19200/2400) on boot
- **Dual UART design:** USART1 dedicated to RS485 Modbus communication, USART2 dedicated to LoRa — fully decoupled so a LoRa transmission never interferes with sensor polling
- **Graceful sensor failure handling:** If any sensor fails to respond, the packet still transmits with `ERR` markers for that field instead of dropping the whole reading
- **Custom packet protocol:** Compact `$KEY:VAL,KEY:VAL...*` text format designed to be both human-readable for debugging and easy to parse programmatically

## Sample Output (Raspberry Pi receiver)

```
┌────────────────────────────────────────────────────┐
│  [2026-05-23 17:08:45]  Packet #1
├────────────────────────────────────────────────────┤
│  Air  (DHT22)
│    Temperature  :  34.2 °C
│    Humidity     :  63.2 %RH
├────────────────────────────────────────────────────┤
│  Soil (ZTS-3000)
│    Moisture     :  45.1 %
│    Temperature  :  28.3 °C
│    EC           :  [████████░░░░░░░░] 850 µS/cm
│    Quality      :  Medium
├────────────────────────────────────────────────────┤
│  Wind (Anemometer + Vane)
│    Speed        :  3.5 m/s   [LOW]
│    Direction    :  180°   S
└────────────────────────────────────────────────────┘
```

## Repository Structure

```
firmware/           STM32 main.c and DHT22 driver
raspberry-pi/        Python receiver script for the Pi
docs/                 Wiring diagrams and system photos
```

## Skills Demonstrated

- Embedded C firmware development (STM32 HAL)
- RS485 / Modbus RTU protocol implementation
- ADC configuration and analog sensor signal conditioning
- UART-based wireless communication (LoRa)
- Multi-peripheral system integration on a single MCU
- Python serial communication and data parsing (Raspberry Pi)
- Hardware debugging using systematic fault isolation

## Author

Aniket Dakore — B.Tech Electronics & Telecommunication Engineering, VIT Pune
