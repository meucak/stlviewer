#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="stlviewer-builder:trixie"

if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "[build.sh] Building Docker image $IMAGE_NAME..."
    docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile.build" "$SCRIPT_DIR"
fi

mkdir -p "$SCRIPT_DIR/build"

docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$SCRIPT_DIR:/src" \
    -v "/tmp:/tmp" \
    -w /src \
    "$IMAGE_NAME" \
    bash -c 'cmake -S /src -B /src/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /src/build && ctest --test-dir /src/build --output-on-failure'
