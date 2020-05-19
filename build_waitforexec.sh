#!/bin/bash
set -e
echo "[+] Building whoc:waitforexec"
docker build -f Dockerfile_waitforexec -t whoc:waitforexec src
