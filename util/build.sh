#!/bin/bash
set -e
echo "[+] Building whoc:dynamic"
docker build -f Dockerfile_dynamic -t whoc:dynamic src
echo "[+] Tagging whoc:dynamic as whoc:latest"
docker tag whoc:dynamic whoc:latest
