#!/bin/bash
docker run --rm -it --net=host --name whoc whoc:latest "$@"
# '--net=host' is only used in local tests so that the container could easily reach the host on `127.0.0.1`.
# Not needed when the file server is remote
