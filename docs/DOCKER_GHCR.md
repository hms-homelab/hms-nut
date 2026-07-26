# Docker & GHCR Publishing Guide

Reference for building, testing, and publishing HMS service Docker images to GitHub Container Registry.

## Quick Reference

```bash
# Build locally
docker build -t hms-nut:test .

# Run locally
docker run --rm \
  -e MQTT_BROKER=192.168.2.15 \
  -e NUT_HOST=localhost \
  -e DB_HOST=192.168.2.15 \
  hms-nut:test

# Pull from GHCR
docker pull ghcr.io/hms-homelab/hms-nut:latest
docker pull ghcr.io/hms-homelab/hms-nut:1.1.1
```

## Architecture

Multi-stage Dockerfile:
- **Builder stage:** `debian:trixie-slim` with full build toolchain
- **Runtime stage:** `debian:trixie-slim` with minimal runtime libs
- **Platforms:** `linux/amd64`, `linux/arm64`
- **Image size:** ~108 MB

### Why Trixie (Debian 13)?

Drogon framework (`libdrogon-dev`) is not available in Debian bookworm (12). Trixie provides it as a native package, avoiding the need to build Drogon from source (which would break multi-arch QEMU builds).

## Drogon Dependency Chain

Drogon's cmake config requires **all** its optional database backends at cmake time, even if not used. The builder stage must include:

```dockerfile
RUN apt-get install -y --no-install-recommends \
    libdrogon-dev \          # Drogon framework
    libsqlite3-dev \         # Required by Drogon cmake
    default-libmysqlclient-dev \  # Required by Drogon cmake
    libhiredis-dev \         # Required by Drogon cmake (Redis)
    libyaml-cpp-dev \        # Required by Drogon cmake
    ...
```

The runtime stage only needs `libdrogon1t64` — its transitive deps are pulled automatically.

## libpqxx Compatibility

Trixie ships `libpqxx-7.10` (dev: `libpqxx-dev`). In libpqxx 6.x and 7.x, `pqxx::connection::close()` is protected. Use `conn_.reset()` instead:

```cpp
// BAD: won't compile with libpqxx 6.x/7.x in Docker
conn_->close();

// GOOD: works across all versions
conn_.reset();
```

## NUT Package Names

| Debian Version | Dev Package | Runtime Package |
|---------------|-------------|-----------------|
| Bookworm (12) | `libnut-dev` | `libupsclient6` |
| Trixie (13) | `libupsclient-dev` | `libupsclient6t64` |

Both provide `<upsclient.h>` — the API is the same.

## CI/CD Workflow

`.github/workflows/docker-build.yml` triggers on:
- Push to `main`/`master`/`develop` branches
- Version tags (`v*`)
- Pull requests (build only, no push)

### Tag Strategy

| Event | Tags Generated |
|-------|---------------|
| Push to main | `latest`, `main`, `sha-abc1234` |
| Tag `v1.1.1` | `1.1.1`, `1.1`, `sha-abc1234` |
| PR | `pr-42` (not pushed) |

### Triggering a Release

```bash
# Bump VERSION file
echo "1.1.2" > VERSION

# Update CHANGELOG.md

# Commit, tag, push
git add -A && git commit -m "release: v1.1.2"
git tag v1.1.2
git push origin main && git push origin v1.1.2
```

## Required Files Checklist

For any HMS service to publish to GHCR:

- [ ] `Dockerfile` — Multi-stage build, non-root user, health check, `strip` binary
- [ ] `.dockerignore` — Exclude `build/`, `.git/`, `tests/`, `docs/`, `sysroot/`
- [ ] `.github/workflows/docker-build.yml` — CI workflow
- [ ] `VERSION` — Semver string
- [ ] `LICENSE` — MIT
- [ ] `CHANGELOG.md` — Keep a Changelog format

## Troubleshooting

### "Unable to locate package" in runtime stage

Trixie renamed many packages with `t64` suffix (64-bit time_t transition). Check:
```bash
docker run --rm debian:trixie-slim bash -c \
  "apt-get update -qq 2>/dev/null && apt-cache search <keyword>"
```

### arm64 build fails but amd64 works

QEMU emulation can break source compilation (especially OpenSSL cross-compile). Always prefer Debian repo packages over building from source.

### Drogon cmake error "Could NOT find X"

Add the missing `-dev` package to the builder stage. Drogon demands SQLite3, MySQL, hiredis, and yaml-cpp headers at cmake time.
