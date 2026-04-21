# ESP8266 FM Radio Controller

A web-based FM radio controller built with ESP8266 (NodeMCU) and RDA5807M FM module. Features a modern responsive web interface for controlling radio tuning, volume, presets, and more.

## Hardware

- **MCU**: NodeMCU ESP8266 (D1 WROOM)
- **FM Module**: RDA5807M
- **Power**: 3.3V

## Wiring

| ESP8266 Pin | RDA5807M Pin |
|-------------|--------------|
| D1 (GPIO5)  | SDA          |
| D2 (GPIO4)  | SCL          |
| 3.3V        | VCC          |
| GND         | GND          |

## Features

- **Web Interface**: Modern, responsive UI with dark theme
- **Frequency Control**: Manual tuning and auto-seek
- **Volume Control**: 16 levels (0-15) with mute toggle
- **Stereo Indicator**: Visual stereo/Mono status
- **Signal Strength**: RSSI display
- **Station Presets**: Save up to 20 favorite stations
- **WiFi Modes**: 
  - AP Mode (default): Creates "FM-Radio" hotspot (password: 12345678)
  - STA Mode: Connects to existing WiFi network
- **Persistent Storage**: Presets saved to EEPROM

## Installation

### Prerequisites

- [PlatformIO](https://platformio.org/) extension for VS Code
- Required libraries (auto-installed by PlatformIO):
  - `pu2clr/PU2CLR RDA5807@^1.1.9`
  - `bblanchon/ArduinoJson@^7.2.2`

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

```cpp
// WiFi mode: true = AP mode, false = STA mode
#define WIFI_AP_MODE true

// STA mode credentials (ignored if AP mode)
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// AP mode settings
#define AP_SSID "FM-Radio"
#define AP_PASSWORD "12345678"
```

## API Endpoints

The web server runs on port 80 with CORS enabled.

### GET `/api/status`

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

### POST `/api/tune`

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

### POST `/api/seek`

Auto-seek next station.

**Request:**
```json
{
  "direction": "up"  // or "down"
}
```

### POST `/api/volume`

Set volume level (0-15).

**Request:**
```json
{
  "volume": 5
}
```

### POST `/api/mute`

Toggle mute state.

**Request:**
```json
{
  "muted": true
}
```

### POST `/api/presets`

Manage station presets.

**Add preset:**
```json
{
  "action": "add",
  "name": "My Station",
  "frequency": 10150
}
```

**Delete preset:**
```json
{
  "action": "delete",
  "index": 0
}
```

**Load preset:**
```json
{
  "action": "load",
  "index": 0
}
```

## Web Interface

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

## License

This project is provided as-is for educational and personal use.

## Credits

- RDA5807 library by [Ricardo Lima Caratti (pu2clr)](https://github.com/pu2clr/RDA5807)
