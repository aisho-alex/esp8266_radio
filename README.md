# ESP8266 FM Radio Controller

A web-based FM radio controller built with ESP8266 (NodeMCU) and RDA5807M FM module. Features a modern responsive web interface for controlling radio tuning, volume, presets, and more.

## Table of Contents

- [Hardware](#hardware)
- [Connection Diagram](#connection-diagram)
- [Features](#features)
- [Installation](#installation)
- [Configuration](#configuration)
- [API Endpoints](#api-endpoints)
- [Web Interface](#web-interface)
- [License](#license)
- [Credits](#credits)

## Hardware

| Component | Description |
|-----------|-------------|
| **MCU** | NodeMCU ESP8266 (D1 WROOM) |
| **FM Module** | RDA5807M |
| **Display (Optional)** | LCD 1602 I2C (Blue) or OLED 1.3" 128x64 I2C (SH1106) |
| **Power** | 3.3V |

## Connection Diagram

### Basic Wiring (RDA5807M FM Module)

```
┌─────────────────────────────────────────────────────────────┐
│                      NodeMCU ESP8266                        │
│                                                              │
│  ┌─────────┐    ┌──────────────────────────────────────┐   │
│  │   D1    │────│ SCL (I2C Clock)                      │   │
│  │ GPIO4   │    └──────────────────────────────────────┘   │
│  └─────────┘                                                │
│  ┌─────────┐    ┌──────────────────────────────────────┐   │
│  │   D2    │────│ SDA (I2C Data)                       │   │
│  │ GPIO5   │    └──────────────────────────────────────┘   │
│  └─────────┘                                                │
│  ┌─────────┐    ┌──────────────────────────────────────┐   │
│  │  3.3V   │────│ VCC (Power)                           │   │
│  └─────────┘    └──────────────────────────────────────┘   │
│  ┌─────────┐    ┌──────────────────────────────────────┐   │
│  │   GND   │────│ GND (Ground)                          │   │
│  └─────────┘    └──────────────────────────────────────┘   │
│                                                              │
└─────────────────────────────────────────────────────────────┘
                          │
                          │ I2C Bus (shared)
                          │
┌─────────────────────────────────────────────────────────────┐
│                    RDA5807M FM Module                       │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ SCL  ───────────────────────────────────────────────  │ │
│  └──────────────────────────────────────────────────────┘ │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ SDA  ───────────────────────────────────────────────  │ │
│  └──────────────────────────────────────────────────────┘ │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ VCC  ───────────────────────────────────────────────  │ │
│  └──────────────────────────────────────────────────────┘ │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ GND  ───────────────────────────────────────────────  │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ L_OUT ────────┐                                      │ │
│  └───────────────┼─────────── Left Audio Output          │ │
│  ┌───────────────┼─────────── (to amplifier/speaker)     │ │
│  │ R_OUT ────────┘                                      │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Optional Display Wiring (I2C Shared Bus)

```
┌─────────────────────────────────────────────────────────────┐
│                      NodeMCU ESP8266                        │
│                                                              │
│  ┌─────────┐    ┌──────────────────────────────────────┐   │
│  │   D1    │────│ SCL (I2C Clock) ──┐                 │   │
│  │ GPIO4   │    │                    │                 │   │
│  └─────────┘    └────────────────────┼─────────────────┘   │
│  ┌─────────┐    ┌────────────────────┼─────────────────┐   │
│  │   D2    │────│ SDA (I2C Data) ───┼─────────────────┤   │
│  │ GPIO5   │    │                    │                 │   │
│  └─────────┘    └────────────────────┼─────────────────┘   │
│  ┌─────────┐    ┌────────────────────┼─────────────────┐   │
│  │  3.3V   │────│ VCC (Power) ───────┼─────────────────┤   │
│  └─────────┘    │                    │                 │   │
│  ┌─────────┘    └────────────────────┼─────────────────┘   │
│  │   GND   │────│ GND (Ground) ──────┼─────────────────┐   │
│  └─────────┘    └────────────────────┼─────────────────┼───┘
│                                       │                 │
│                                       │                 │
│                    ┌──────────────────┼─────────────────┼──────────┐
│                    │                  │                 │          │
│           ┌────────┴────────┐ ┌────────┴────────┐ ┌────┴────┐   │
│           │  RDA5807M       │ │  LCD 1602 I2C   │ │  OLED   │   │
│           │  (Addr: 0x22)   │ │  (Addr: 0x27)   │ │(0x3C)   │   │
│           │                 │ │                 │ │         │   │
│           │ SCL ────────────┼─┼─────────────────┼─┼─────────┼───┘
│           │ SDA ────────────┼─┼─────────────────┼─┼─────────┤
│           │ VCC ────────────┼─┼─────────────────┼─┼─────────┤
│           │ GND ────────────┼─┼─────────────────┼─┼─────────┤
│           │                 │ │                 │ │         │
│           │ L_OUT ── Audio  │ │                 │ │         │
│           │ R_OUT ── Output │ │                 │ │         │
│           └─────────────────┘ └─────────────────┘ └─────────┘
```

### Pinout Table

| ESP8266 Pin | GPIO | RDA5807M | LCD 1602 | OLED 1.3" |
|-------------|------|----------|----------|-----------|
| D1          | GPIO4 | SCL      | SCL      | SCL       |
| D2          | GPIO5 | SDA      | SDA      | SDA       |
| 3.3V        | -     | VCC      | VCC      | VCC       |
| GND         | -     | GND      | GND      | GND       |

### Audio Output

The RDA5807M module provides stereo audio output via two pins:
- **L_OUT** - Left channel audio output
- **R_OUT** - Right channel audio output

**Important**: Connect these to an audio amplifier or powered speakers. Do not connect directly to headphones or unamplified speakers as the output level is too low.

## Features

- **Web Interface**: Modern, responsive UI with dark theme
- **Frequency Control**: Manual tuning and auto-seek
- **Volume Control**: 16 levels (0-15) with mute toggle
- **Stereo Indicator**: Visual stereo/Mono status
- **Signal Strength**: RSSI display (0-25)
- **Station Presets**: Save up to 20 favorite stations
- **Station Scanning**: Auto-scan for available stations
- **WiFi Modes**: 
  - AP Mode (default): Creates "FM-Radio" hotspot (password: 12345678)
  - STA Mode: Connects to existing WiFi network
  - Automatic fallback to AP mode if STA connection fails
- **WiFi Configuration**: Web-based WiFi setup page
- **Persistent Storage**: Settings, presets, and WiFi credentials saved to EEPROM
- **Optional Display Support**:
  - LCD 1602 I2C (Blue backlight)
  - OLED 1.3" 128x64 I2C (SH1106 controller)
  - Display shows frequency, volume, signal strength, stereo status, and WiFi connection

## Installation

### Prerequisites

- [PlatformIO](https://platformio.org/) extension for VS Code
- Required libraries (auto-installed by PlatformIO):
  - `pu2clr/PU2CLR RDA5807@^1.1.9`
  - `bblanchon/ArduinoJson@^7.2.2`
  - `marcoschwartz/LiquidCrystal_I2C@^1.1.4` (for LCD 1602)
  - `adafruit/Adafruit SSD1306@^2.5.7` (for OLED)
  - `adafruit/Adafruit GFX Library@^1.11.9` (for OLED)

### Build and Upload

```bash
# Install dependencies and build
pio run

# Upload to board
pio run --target upload

# Upload filesystem (web interface)
pio run --target uploadfs

# Monitor serial output
pio device monitor
```

## Configuration

Edit [`src/radio.ino`](src/radio.ino) to customize:

### Display Type

```cpp
// Display type selection
// Set to 0: No display
// Set to 1: LCD 1602 I2C (Blue)
// Set to 2: OLED 1.3" 128x64 I2C (SH1106)
#define DISPLAY_TYPE 2
```

### I2C Pins

```cpp
// RDA5807M I2C pins
#define I2C_SDA D2  // GPIO5
#define I2C_SCL D1  // GPIO4
```

### WiFi Settings

```cpp
// AP mode settings (fallback when STA connection fails)
#define AP_SSID "FM-Radio"
#define AP_PASSWORD "12345678"  // min 8 chars, or empty for open network
#define AP_CHANNEL 1
```

WiFi credentials can also be configured via the web interface at `/wifi`.

### Display Configuration

```cpp
// LCD 1602 I2C configuration
#define LCD_ADDRESS 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

// OLED 1.3" 128x64 I2C configuration
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset pin (-1 if sharing Arduino reset pin)
```

## API Endpoints

The web server runs on port 80 with CORS enabled.

### Radio Control

#### GET `/api/status`

Returns current radio status.

**Response:**
```json
{
  "frequency": 10150,
  "volume": 5,
  "muted": false,
  "stereo": true,
  "rssi": 45
}
```

#### POST `/api/tune`

Tune to a specific frequency.

**Request:**
```json
{
  "frequency": 10150
}
```

**Response:**
```json
{
  "frequency": 10150,
  "rssi": 45,
  "stereo": true
}
```

#### POST `/api/seek`

Auto-seek next station.

**Request:**
```json
{
  "direction": "up"  // or "down"
}
```

**Response:**
```json
{
  "frequency": 10150,
  "rssi": 45,
  "stereo": true
}
```

#### POST `/api/volume`

Set volume level (0-15).

**Request:**
```json
{
  "volume": 5
}
```

**Response:**
```json
{
  "volume": 5
}
```

#### POST `/api/mute`

Toggle mute state.

**Request:**
```json
{
  "muted": true
}
```

**Response:**
```json
{
  "muted": true
}
```

#### GET `/api/scan`

Scan for available stations.

**Response:**
```json
{
  "stations": [
    {
      "frequency": 8750,
      "rssi": 20,
      "stereo": true
    },
    {
      "frequency": 8910,
      "rssi": 18,
      "stereo": false
    }
  ]
}
```

### Preset Management

#### GET `/api/presets`

Get all saved presets.

**Response:**
```json
{
  "presets": [
    {
      "name": "My Station",
      "frequency": 10150
    }
  ]
}
```

#### POST `/api/presets`

Add a new preset.

**Request:**
```json
{
  "name": "My Station",
  "frequency": 10150
}
```

**Response:**
```json
{
  "success": true,
  "index": 0
}
```

#### DELETE `/api/presets/{index}`

Delete a preset by index.

**Response:**
```json
{
  "success": true
}
```

### WiFi Configuration

#### GET `/api/wifi/status`

Get current WiFi status.

**Response:**
```json
{
  "mode": "STA",
  "sta_ssid": "MyWiFi",
  "sta_ip": "192.168.1.100",
  "connected": true
}
```

or in AP mode:
```json
{
  "mode": "AP",
  "ap_ssid": "FM-Radio",
  "ap_ip": "192.168.4.1",
  "connected": false
}
```

#### GET `/api/wifi/scan`

Scan for available WiFi networks.

**Response:**
```json
{
  "networks": [
    {
      "ssid": "MyWiFi",
      "rssi": -45,
      "encryption": true,
      "channel": 6
    },
    {
      "ssid": "OpenNetwork",
      "rssi": -60,
      "encryption": false,
      "channel": 11
    }
  ]
}
```

#### POST `/api/wifi/save`

Save WiFi credentials and reconnect.

**Request:**
```json
{
  "ssid": "MyWiFi",
  "password": "mypassword"
}
```

**Response:**
```json
{
  "success": true,
  "message": "Settings saved. Reconnecting..."
}
```

## Web Interface

### Main Interface (`/`)

Access the web interface at:
- **AP Mode**: `http://192.168.4.1`
- **STA Mode**: Check serial monitor for assigned IP address

The interface includes:
- Large frequency display with MHz indicator
- Stereo/Mono indicator
- Signal strength meter
- Volume slider with mute button
- Seek up/down buttons
- Fine-tuning buttons (+/- 0.1 MHz)
- Preset management panel
- Station scan button

### WiFi Configuration (`/wifi`)

The WiFi configuration page allows you to:
- Scan for available WiFi networks
- Enter SSID and password
- Save settings and automatically reconnect
- View current WiFi status

### Display Information

On startup, the display (if configured) shows:
- WiFi connection status and IP address for 3 seconds
- Then switches to the main radio display showing:
  - Current frequency
  - Volume level
  - Signal strength
  - Stereo/Mono status
  - Mute status
  - WiFi connection indicator (small dot on OLED)

## License

This project is provided as-is for educational and personal use.

## Credits

- RDA5807 library by [Ricardo Lima Caratti (pu2clr)](https://github.com/pu2clr/RDA5807)
- ESP8266 Arduino Core
- ArduinoJson library by Benoit Blanchon
