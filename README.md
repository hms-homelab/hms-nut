# HMS-NUT

A high-performance C++ microservice for UPS (Uninterruptible Power Supply) monitoring via Network UPS Tools (NUT), with MQTT integration for Home Assistant and PostgreSQL storage for analytics.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-support-%23FFDD00.svg?logo=buy-me-a-coffee)](https://www.buymeacoffee.com/aamat09)

## Features

- **NUT Integration**: Polls Network UPS Tools daemon for real-time UPS metrics
- **MQTT Discovery**: Auto-registers sensors with Home Assistant via MQTT discovery protocol
- **Multi-Device Support**: Monitor multiple UPS devices (NUT + ESP32-based monitors)
- **PostgreSQL Storage**: Historical data persistence for ML analytics and dashboards
- **Low Memory Footprint**: ~3 MB RAM usage
- **Configurable**: All settings via environment variables

## Architecture

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   NUT Server    │────▶│    HMS-NUT      │────▶│   PostgreSQL    │
│   (upsd)        │     │   C++ Service   │     │   Database      │
└─────────────────┘     └────────┬────────┘     └─────────────────┘
                                 │
                                 ▼
                        ┌─────────────────┐     ┌─────────────────┐
                        │   MQTT Broker   │────▶│ Home Assistant  │
                        │   (Mosquitto)   │     │   Dashboard     │
                        └─────────────────┘     └─────────────────┘
```

## Quick Start

### Prerequisites

- C++17 compiler (GCC 9+ or Clang 10+)
- CMake 3.16+
- Network UPS Tools (NUT) server
- PostgreSQL 12+
- MQTT Broker (Mosquitto, EMQX, etc.)

**Required Libraries:**
```bash
# Debian/Ubuntu
sudo apt install libdrogon-dev libjsoncpp-dev libpqxx-dev libpaho-mqttpp3-dev libnut-dev
```

### Build

```bash
git clone https://github.com/hms-homelab/hms-nut.git
cd hms-nut
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Configure

Copy the example service file and customize:

```bash
cp hms-nut.service.example hms-nut.service
# Edit hms-nut.service with your settings
```

### Install

```bash
sudo cp hms-nut.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now hms-nut
```

## Configuration

All configuration is done via environment variables:

### NUT Server Settings

| Variable | Default | Description |
|----------|---------|-------------|
| `NUT_HOST` | `localhost` | NUT server hostname |
| `NUT_PORT` | `3493` | NUT server port |
| `NUT_UPS_NAME` | `ups@localhost` | UPS name in NUT format |
| `NUT_DEVICE_ID` | `ups` | MQTT device identifier |
| `NUT_DEVICE_NAME` | `UPS` | Human-readable device name |
| `NUT_POLL_INTERVAL` | `60` | Polling interval in seconds |

### Multi-Device Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `UPS_DEVICE_IDS` | - | Comma-separated MQTT device IDs to collect |
| `UPS_DB_MAPPING` | - | JSON: MQTT ID → DB identifier mapping |
| `UPS_FRIENDLY_NAMES` | - | JSON: MQTT ID → friendly name mapping |

Example for multiple devices:
```bash
UPS_DEVICE_IDS="main_ups,rack_ups,esp32_ups"
UPS_DB_MAPPING='{"main_ups": "apc_smart_ups", "rack_ups": "cyberpower_1500"}'
UPS_FRIENDLY_NAMES='{"main_ups": "Main Server UPS", "rack_ups": "Network Rack UPS"}'
```

### MQTT Settings

| Variable | Default | Description |
|----------|---------|-------------|
| `MQTT_BROKER` | `localhost` | MQTT broker hostname |
| `MQTT_PORT` | `1883` | MQTT broker port |
| `MQTT_USER` | - | MQTT username |
| `MQTT_PASSWORD` | - | MQTT password |
| `MQTT_CLIENT_ID` | `hms_nut_service` | MQTT client identifier |

### Database Settings

| Variable | Default | Description |
|----------|---------|-------------|
| `DB_HOST` | `localhost` | PostgreSQL hostname |
| `DB_PORT` | `5432` | PostgreSQL port |
| `DB_NAME` | `ups_monitoring` | Database name |
| `DB_USER` | - | Database username |
| `DB_PASSWORD` | - | Database password |

### Service Settings

| Variable | Default | Description |
|----------|---------|-------------|
| `COLLECTOR_SAVE_INTERVAL` | `3600` | DB save interval (seconds) |
| `HEALTH_CHECK_PORT` | `8891` | HTTP health check port |
| `LOG_LEVEL` | `info` | Log level (debug/info/warn/error) |

## Sensors Published

HMS-NUT publishes the following sensors to Home Assistant via MQTT discovery:

**Battery Metrics:**
- Battery charge (%)
- Battery voltage (V)
- Battery runtime (seconds)
- Battery nominal voltage
- Low/warning charge thresholds

**Input Metrics:**
- Input voltage (V)
- Input nominal voltage
- High/low voltage transfer points
- Input sensitivity
- Last transfer reason

**Output & Load:**
- Output voltage (V)
- Load percentage (%)
- Load power (W)
- Nominal power (VA)

**Status:**
- UPS status (online/on battery/etc.)
- Power failure (binary sensor)
- Beeper status
- Self-test result
- Temperature (if available)

## API Endpoints

### Health Check

```bash
curl http://localhost:8891/health
```

Response:
```json
{
  "service": "hms-nut",
  "version": "1.0",
  "status": "healthy",
  "components": {
    "mqtt": "connected",
    "database": "connected",
    "nut_bridge": "running",
    "collector": "running"
  },
  "devices_monitored": 1,
  "last_nut_poll": "2024-01-15T10:30:00Z"
}
```

## Database Schema

Required PostgreSQL table:

```sql
CREATE TABLE ups_metrics (
    id SERIAL PRIMARY KEY,
    device_identifier VARCHAR(64) NOT NULL,
    timestamp TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    battery_charge DECIMAL(5,2),
    battery_voltage DECIMAL(5,2),
    battery_runtime INTEGER,
    input_voltage DECIMAL(6,2),
    output_voltage DECIMAL(6,2),
    load_percentage DECIMAL(5,2),
    load_watts DECIMAL(8,2),
    ups_status VARCHAR(32),
    temperature DECIMAL(5,2)
);

CREATE INDEX idx_ups_metrics_device_time
ON ups_metrics(device_identifier, timestamp DESC);
```

## Running Tests

```bash
cd tests
mkdir build && cd build
cmake ..
make
ctest --output-on-failure
```

## Docker

Build and run with Docker:

```bash
docker build -t hms-nut .
docker run -d \
  -e NUT_HOST=your-nut-server \
  -e MQTT_BROKER=your-mqtt-broker \
  -e DB_HOST=your-postgres \
  -p 8891:8891 \
  hms-nut
```

Or use Docker Compose - see `docker-compose.yml`.

## Directory Structure

```
hms-nut/
├── src/
│   ├── main.cpp              # Application entry point
│   ├── nut/
│   │   ├── NutClient.cpp     # NUT protocol client
│   │   └── UpsData.cpp       # UPS data models
│   ├── services/
│   │   ├── NutBridgeService.cpp   # NUT → MQTT bridge
│   │   └── CollectorService.cpp   # MQTT → PostgreSQL collector
│   ├── database/
│   │   └── DatabaseService.cpp    # PostgreSQL interface
│   ├── mqtt/
│   │   ├── MqttClient.cpp         # MQTT client wrapper
│   │   └── DiscoveryPublisher.cpp # HA discovery messages
│   └── utils/
│       └── DeviceMapper.cpp       # Device ID mapping
├── include/                  # Header files
├── tests/                    # Unit tests
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── hms-nut.service.example   # Systemd service template
└── README.md
```

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request
---

## ☕ Support

If this project is useful to you, consider buying me a coffee!

[![Buy Me A Coffee](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/aamat09)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Related Projects

- [Network UPS Tools (NUT)](https://networkupstools.org/)
- [Home Assistant](https://www.home-assistant.io/)
- [Drogon C++ Framework](https://github.com/drogonframework/drogon)

## Acknowledgments

Part of the [HMS Homelab](https://github.com/hms-homelab) project - lightweight C++ microservices for home automation.
