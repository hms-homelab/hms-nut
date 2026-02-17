# HMS-NUT Dockerfile
# Multi-stage build for minimal image size

# Build stage
FROM debian:bookworm-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    libdrogon-dev \
    libjsoncpp-dev \
    libpqxx-dev \
    libpaho-mqttpp3-dev \
    libpaho-mqtt3as-dev \
    libnut-dev \
    libssl-dev \
    libpq-dev \
    uuid-dev \
    zlib1g-dev \
    libbrotli-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy source code
WORKDIR /build
COPY CMakeLists.txt .
COPY src/ src/
COPY include/ include/

# Build
RUN mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)

# Runtime stage
FROM debian:bookworm-slim

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libjsoncpp25 \
    libpqxx-7.8 \
    libpaho-mqttpp3-1 \
    libpaho-mqtt3as1 \
    libupsclient6 \
    libssl3 \
    libpq5 \
    libuuid1 \
    zlib1g \
    libbrotli1 \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -r -s /bin/false hms

# Copy binary from builder
COPY --from=builder /build/build/hms_nut /usr/local/bin/hms_nut

# Create working directory
WORKDIR /app

# Set ownership
RUN chown -R hms:hms /app

# Switch to non-root user
USER hms

# Default environment variables
ENV NUT_HOST=localhost \
    NUT_PORT=3493 \
    NUT_UPS_NAME=ups@localhost \
    NUT_DEVICE_ID=ups \
    NUT_DEVICE_NAME=UPS \
    NUT_POLL_INTERVAL=60 \
    MQTT_BROKER=localhost \
    MQTT_PORT=1883 \
    MQTT_CLIENT_ID=hms_nut_service \
    DB_HOST=localhost \
    DB_PORT=5432 \
    DB_NAME=ups_monitoring \
    COLLECTOR_SAVE_INTERVAL=3600 \
    HEALTH_CHECK_PORT=8891 \
    LOG_LEVEL=info

# Expose health check port
EXPOSE 8891

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8891/health || exit 1

# Run the application
CMD ["/usr/local/bin/hms_nut"]
