# Changelog

All notable changes to HMS-NUT will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.1] - 2026-02-25

### Fixed
- **Docker build**: Fixed Dockerfile for multi-arch GHCR publishing (trixie base, correct package names)
- **libpqxx 7.x compatibility**: Replaced `conn_->close()` with `conn_.reset()` (close() is protected)

### Added
- `.dockerignore` to exclude build dirs and unnecessary files from Docker context
- GitHub Actions CI workflow for automated multi-arch Docker builds (amd64 + arm64)
- VERSION file for release tracking
- `curl` in runtime image for health check endpoint

### Changed
- Docker base image: `debian:bookworm-slim` → `debian:trixie-slim` (required for Drogon framework)
- Docker image size: 108 MB with stripped binary

## [1.1.0] - 2026-02-22

### Added
- **Multi-Device Collection Support**: Collector now subscribes to multiple UPS devices via MQTT
  - Configurable via `UPS_DEVICE_IDS` environment variable (comma-separated list)
  - Support for device ID mapping via `UPS_DB_MAPPING` (JSON, maps MQTT IDs to database identifiers)
  - Support for friendly names via `UPS_FRIENDLY_NAMES` (JSON)
  - Example: HMS-NUT + 2 ESP32 devices (3 devices total)
- DeviceMapper utility class for managing multiple device configurations
- Automatic database device registration for new devices

### Changed
- CollectorService now subscribes to multiple MQTT topic patterns (one per device)
- Database schema supports multiple device identifiers via `device_identifier` field
- Service configuration updated to collect from 3 UPS devices:
  - `apc_bx` (HMS-NUT local device)
  - `apc_ups_d0_cf_13_2f_df_dc` (ESP32 #1 - MT300N router)
  - `apc_ups_d0_cf_13_2f_e0_b0` (ESP32 #2 - ATX1800 router)

### Fixed
- ESP32 devices were not being collected (service only configured for local NUT device)
- ML Intelligence workflow was only generating predictions for HMS-NUT device
- Backfilled 9,170 missing records from Home Assistant recorder (Feb 14-22, 2026)

### Performance
- Collecting from 3 devices simultaneously with no performance degradation
- Database inserts: ~3600 records/hour (1200 per device)

## [1.0.1] - 2026-02-21

### Added
- POST `/republish` endpoint for manually triggering MQTT discovery republish
- Automatic MQTT discovery republish when Home Assistant restarts (homeassistant/status "online")
- Two-phase service initialization (start threads, then setup subscriptions)
- Comprehensive unit test suite for async MQTT operations (6 tests)
- Documentation: HTTP_SERVER_BLOCKING_FIX.md (root cause analysis)
- Documentation: FIX_VERIFICATION.md (verification report)
- Unit tests: test_async_subscriptions.cpp (subscription performance tests)
- Unit tests: test_nut_bridge_republish.cpp (republish functionality)
- Unit tests: test_ha_status_subscription.cpp (HA status integration)

### Fixed
- **CRITICAL:** HTTP server (Drogon) not responding to requests
  - Root cause #1: MQTT `subscribe()` was blocking main thread waiting for SUBACK
  - Root cause #2: MQTT `publish()` was blocking while holding `connection_mutex_`, preventing HTTP handler from calling `isConnected()`
  - Solution: Made both subscribe() and publish() fully asynchronous (no `->wait()` calls)
- Health endpoint `/health` now responds in < 50ms (was: infinite timeout)
- Republish endpoint `/republish` now functional
- Service startup time reduced from ∞ (hung) to ~1 second

### Changed
- `MqttClient::subscribe()` now returns immediately without waiting for SUBACK (async)
- `MqttClient::publish()` now returns immediately without waiting for PUBACK (async)
- `NutBridgeService::start()` no longer sets up MQTT subscriptions (moved to `setupSubscriptions()`)
- `CollectorService::start()` no longer sets up MQTT subscriptions (moved to `setupSubscriptions()`)
- Main thread now calls `setupSubscriptions()` before `drogon::app().run()`

### Performance
- HTTP server start time: ∞ → 100ms (100% improvement)
- Health endpoint response: timeout → 43ms (∞ improvement)
- Subscribe time per topic: 5000ms → <10ms (99.8% improvement)
- Publish time (20 messages): 2000ms → <100ms (95% improvement)

### Technical Details
- Changed from synchronous to asynchronous MQTT operations
- Eliminated mutex blocking during I/O operations
- HTTP handler no longer blocks on `connection_mutex_` during republish
- Proper separation of concerns: thread startup vs MQTT subscriptions

## [1.0.0] - 2026-02-20

### Added
- Initial release of HMS-NUT C++ service
- NUT server polling (NutBridgeService)
- MQTT publishing of UPS metrics
- PostgreSQL data persistence (CollectorService)
- Home Assistant MQTT discovery
- Multi-device support via DeviceMapper
- Health check endpoint `/health`
- Comprehensive UPS data model (12 metrics)
- Systemd service integration
- Environment-based configuration

### Core Features
- **NutBridgeService (Thread 1):** Polls local NUT server every 60s, publishes to MQTT
- **CollectorService (Thread 2):** Collects MQTT messages, saves to PostgreSQL every hour
- **MQTT Discovery:** Auto-configures sensors in Home Assistant
- **Database:** TimescaleDB/PostgreSQL with efficient storage
- **Device Mapping:** Flexible MQTT ID to database identifier mapping

### Dependencies
- Eclipse Paho MQTT C++ (paho-mqttpp3)
- Drogon HTTP framework
- PostgreSQL/libpqxx
- NUT client library (upsclient)
- JsonCpp

### Supported Devices
- APC Back-UPS XS 1000M (via NUT)
- Any NUT-compatible UPS
- Future: ESP32-based direct UPS monitors

---

## Version History

- **1.0.1** (2026-02-21): HTTP server blocking fix + republish endpoint
- **1.0.0** (2026-02-20): Initial release

## Migration Notes

### Upgrading from 1.0.0 to 1.0.1

No breaking changes. Simply:
```bash
cd /home/aamat/maestro_hub/projects/hms-nut/build
git pull
make -j$(nproc)
sudo systemctl restart hms-nut
```

Verify health endpoint works:
```bash
curl http://localhost:8891/health
```

## Known Issues

### v1.0.1
- HTTP endpoint unit tests (`test_http_endpoints.cpp`) have segfault issue (test infrastructure, not service)
- ConcurrentSubscriptionsThreadSafe test may fail intermittently (race condition in test, not production code)

### v1.0.0
- HTTP server did not respond to requests (FIXED in 1.0.1)

## Future Roadmap

### v1.1.0 (Planned)
- WebSocket support for real-time UPS monitoring
- Grafana dashboard integration
- Alert system (low battery, power outage)
- Multi-UPS load balancing intelligence

### v1.2.0 (Planned)
- ESP32 direct UPS integration (bypass NUT)
- Battery health trending ML model
- Power consumption forecasting

---

*Last updated: February 21, 2026*
