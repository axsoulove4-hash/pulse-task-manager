#!/usr/bin/env bash
set -e
runtime=podman
[ -n "$PULSE_CONTAINER_RUNTIME" ] && runtime="$PULSE_CONTAINER_RUNTIME"
exec "$runtime" build -t pulse-builder -f packaging/Containerfile .
