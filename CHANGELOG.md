# Changelog

All notable changes to HMS-NUT will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.4.0] - 2026-07-27

### Added
- **Every reported metric is now exposed and shown.** `UpsData::toJson()` emitted only 9 of
  the ~30 fields the collector holds, so `/api/devices` silently dropped nominal voltages,
  transfer thresholds, battery type/date, self-test, beeper, firmware, driver info, output
  voltage and the shutdown/reboot timers. All of them are serialized now, and each dashboard
  card gets a collapsible **Show all metrics** panel grouped by
  Battery / Input / Output / Load / UPS / Driver / Environment.
- **Full metric history.** `queryHistory()` selected 3 of the 22 columns `insertUpsMetrics()`
  writes; it now returns every stored numeric column plus `ups_status` / `power_failure`.
  The two threshold columns are aliased to their live-metric names so the UI has one vocabulary.
- **Multi-node charts.** `GET /api/history?device=a,b,c` accepts a comma-separated list and
  returns a `series` array (one entry per node) in a single round trip. `device`/`points`
  still mirror the first series, so existing single-device callers are unaffected.
- **History has two modes**: *Per node* (one node, metrics bucketed by unit, one chart per
  unit family, never a dual axis) and *All nodes* (one metric, every node overlaid), each with
  a min/avg/max/last statistics table for the selected range.
- **Fleet summary strip** on the dashboard: nodes online, total draw, nodes on battery,
  lowest battery, shortest runtime, stale count.
- **24 h sparklines** for battery and load on each card, and a **last-seen / stale** indicator
  driven by the live metric timestamp.
- **Daily energy summaries are persisted and surfaced.** New `ups_daily_summaries` table
  (created idempotently at startup), written on every generation. `GET /api/summaries?limit=N`
  returns them newest-first and `POST /api/summary?date=YYYY-MM-DD` regenerates on demand.
  The dashboard shows the latest summary with earlier ones behind a toggle. Previously the
  summary only existed in memory and on MQTT, so the n8n relay workflow was the only place
  it ever landed.

## [1.3.0] - 2026-07-24

### Added
- **Web UI** (Angular 21, served by Drogon on the existing port): live status dashboard,
  history charts (chart.js), power-events table, and device management.
- **REST API**: `GET /api/devices` (config + live snapshot), `GET /api/history`,
  `GET /api/events`, and `GET/POST /api/config/devices` + `PUT/DELETE /api/config/devices/{id}`.
- **Editable device management** persisted in a new `device_config` table (seeded once from
  the `UPS_*` env vars). Adds/edits/enable-disable/delete apply live — the collector
  re-subscribes MQTT topics without a service restart.
- **`NUT_ENABLED`** env flag (default `true`): set `false` to run MQTT-only with no NUT
  bridge, e.g. after retiring a locally NUT-attached USB UPS.

### Changed
- Collector subscriptions are now reconciled on config change (hot-reload) instead of
  fixed at startup; `onMqttMessage` ignores messages from unconfigured devices.
- `/health` reports the real version and treats the NUT bridge as optional.

## [1.2.0] - 2026-03-14

### Added
- **Daily energy summary**: Queries PostgreSQL for yesterday's UPS metrics across all
  configured devices, sends to LLM (Ollama/OpenAI/Gemini/Anthropic) for analysis,
  publishes summary to MQTT with HA discovery. Runs automatically at configurable hour
  (default 7 AM).
- **Manual summary endpoint**: `POST /summary?date=YYYY-MM-DD` triggers on-demand
  summary generation for any date.
- **LLM integration** via `hms-shared` library (`hms::LLMClient`): multi-provider
  support with configurable prompt template, temperature, keep_alive.
- **DatabaseService::queryDailyMetrics()**: aggregates voltage ranges, load, battery,
  runtime, power failures, and transfer reasons per device per day.
- **Unit + integration tests**: 16 unit tests (provider parsing, template substitution,
  prompt loading) + 4 integration tests (PostgreSQL daily metrics queries).

## [1.1.2] - 2026-03-05

### Fixed
- **MQTT reconnect zombie bug**: After connection loss, paho auto-reconnect restored TCP but
  `connected_` flag stayed `false` (no `set_connected_handler`). Service became deaf — running
  but unable to publish or receive commands. Added `onReconnected()` callback that restores
  `connected_` and re-subscribes all stored topic callbacks (lost due to `clean_session=true`).

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
  - `apc_ups_<esp32-1-mac>` (ESP32 #1)
  - `apc_ups_<esp32-2-mac>` (ESP32 #2)

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
