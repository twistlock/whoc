# whoc
A container image that extracts the underlying container runtime and sends it to a remote server.
Poke at the underlying container runtime of your favorite CSP container platform!


## How
As shown by runc [CVE-2019-5736](https://unit42.paloaltonetworks.com/breaking-docker-via-runc-explaining-cve-2019-5736/), Linux fork&exec based container runtimes expose themselves to the containers they're running through `/proc/$pid/exe`. `whoc` uses this to read the container runtime running it.


## Run Locally
Clone the repository:
```console
$ git clone https://github.com/twistlock/whoc
```
Set up a file server to receive the extracted container runtime:
```console
$ cd whoc && mkdir -p stash && cd stash
$ ../util/fileserver.py 
```
From another shell, run the whoc image in your container environment of choice, for example Docker:
```console
$ docker build -f Dockerfile_dynamic -t whoc:latest src  # or ./util/build.sh
$ docker run --rm -it --net=host whoc:latest  # or ./util/run_local.sh
```
See that the file server received the container runtime. Since we run `whoc` under docker, the received container runtime should be [runc](https://github.com/opencontainers/runc). 

*`--net=host` is only used in local tests so that the `whoc` container could easily reach the host on `127.0.0.1`.*


## Help
Help for the `whoc` image entry point, `upload_runtime`:
```
Usage: upload_runtime [options] <server_ip>

Options:
 -p, --port                 Port of remote server, defaults to 8080
 -e, --exec                 Wait for exec mode for static container runtimes, waits until an exec to the container occurred
 -a, --exec-extra-argument  In exec mode, pass an additional argument to the runtime so it won't exit quickly (e.g. '--help')
 -c, --pid-count            In exec mode, how many pids to check when searching for the runtime process, defaults to 6
```
