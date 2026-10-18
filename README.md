Smart Trash Bin — 8051-Based Embedded System

Project Overview

A smart trash bin prototype developed as a Semester 1 team project. The system uses an 8051 microcontroller to monitor trash levels, detect fire hazards, control the lid using a servo motor, and display system status on an LCD.

An ESP8266 module provides Wi-Fi connectivity for remote monitoring and control through ThingsBoard.

Key Features

Automatic lid control using an ultrasonic sensor and an SG90 servo motor.

Trash-level monitoring.

Temperature and flame detection.

LCD status display and audio alerts.

UART communication between the 8051 and ESP8266.

IoT telemetry and remote control through ThingsBoard.

Technologies

Embedded C · 8051 · Keil µVision · ESP8266 · Arduino IDE · UART · MQTT · ThingsBoard

## Project Documentation

- [Project Report](docs/project_report.pdf)
- [Block Diagram](docs/block_diagram.png)
- [Circuit Diagram](docs/circuit_diagram.png)

## Source Code

- [8051 Firmware](firmware/8051/main.c)
- [ESP8266 Firmware](firmware/esp8266/smart_trash_bin.ino)

## Demo

See the `media/` directory for prototype photos and demonstration media.
