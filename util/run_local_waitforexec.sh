#!/bin/bash
docker run --rm -it --net=host --name whoc_waitforexec whoc:waitforexec "$@"