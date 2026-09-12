# ESP8266 IKEA Vindstyrka/Sensirion SEN54 + Sensirion SCD41 (CO2) +  MICS-5524 (CO) with collectd (Telegraf/influxDB 1.x) integration.

This project is a simple ESP8266 program designed to read environmental data from a Sensirion SEN54, Sensirion SCD41 (CO2) and MICS-5524 (CO) sensors and publish it with collectd to Telegraf/influxDB 1.x.

Branch vera contains integration to publish metrics to [Vera](https://getvera.com/), which is no longer maintained.
Branch original contains SENS54 only integration.

![IKEA Vindstyrka with ESP8266](images/Vindstyrka-ESP8266-small.jpg)

Sensirion SCD41 (CO2) and MICS-5524 (CO) sensors not shown in photo.
Those are placed to the bottom of Vindstryka housing and below SENS54 by removing some plastic from SENS54 retaining frame bottom corners.

## Based on
 - https://github.com/techniker/sen54_mqtt
 - https://github.com/Sensirion/arduino-i2c-sen5x/blob/master/examples/exampleUsage/exampleUsage.ino

## Features

- Reads data from Sensirion SEN54, Sensirion SCD41 (CO2) and MICS-5524 (CO) sensors.
- Publishes data to Telegraf/influxDB 1.x using collectd binary protocol
- Configurable reading interval with offset to avoid conflicts in multi-sensor setups.
- WebSerial for monitoring (http://<ip address>/webserial)

## Prerequisites

- ESP8266 module such as ESP8266 12F
- Sensirion SEN54 (IKEA Vindstyrka), Sensirion SCD41 (CO2) and MICS-5524 (CO) sensors
- WiFi network
- [Sensirion I2C SEN5X Arduino Library](https://github.com/Sensirion/arduino-i2c-sen5x) 
- [Sensirion I2C SCD4X Arduino Library](https://github.com/Sensirion/arduino-i2c-scd4x)
- [DFRobot MICS Library](https://github.com/DFRobot/DFRobot_MICS)
 
## Hardware Setup

1. Connect the Sensirion SEN54 and SCD41 (use test-pads on Vindstyrka PCB under display flat-flex ribbon cable) sensor to the ESP8266 via the I2C interface PINs:

* SCL -> D1/20/GPIO5=SCL
* SDA -> D2/19/GPIO4

MICS-5524 is connected to ESP8266 ADC pin for reading analog levels.

## Software Setup

### Configuration

Before compiling and uploading the code to your ESP8266, you need to configure the following:

- **WiFi Settings:** Enter your WiFi SSID and password in the provided variables in config.h
- **Telegraf/InfluxDB IP address :** Enter IP address in the provided variables in config.h
- **OTA Server IP address :** Enter [OTA server](https://github.com/garyttirn/8266OTA) IP address in the provided variables in config.h

### Dependencies

This project requires the following Arduino libraries:

- `ESP8266WiFi`
- `Wire`
- `SensirionI2CSen5x`
- `SensirionI2CScd4x`
- `DFRobot_MICS`

### Compilation and Upload

Using the Arduino IDE, compile the sketch and upload it to your ESP8266 device.

## Usage

Once powered, the ESP8266 connects to the configured WiFi network and begins reading data from the SEN54 sensor at specified _readInterval_. The sensor data is published to according to the set _reportingInterval_.
