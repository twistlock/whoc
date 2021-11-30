#!/bin/bash
set -e
echo "[+] Building whoc:arm64"
docker buildx build --build-arg "PLATFORM_LD_PATH_ARG=/lib/ld-linux-aarch64.so.1" \
  --platform linux/arm64 -f Dockerfile_dynamic -t whoc:arm64 src
