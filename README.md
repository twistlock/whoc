# whoc
A container image that extracts the underlying container runtime and sends it to a remote server.
Poke at the underlying container runtime of your favorite CSP container platform!


## How does it work?
As shown by runc [CVE-2019-5736](https://unit42.paloaltonetworks.com/breaking-docker-via-runc-explaining-cve-2019-5736/), Linux fork&exec based container runtimes expose themselves to the containers they're running through `/proc/$pid/exe`. `whoc` uses this to read the container runtime running it.

### Dynamic Mode
This is `whoc` default mode that works against dynamicly linked container runtimes.

1. The `whoc` image entrypoint is set to `/proc/self/exe`
2. The image's dynamic linker (`ld.so`) is replaced with `upload_runtime`
3. Once the image is run, the container runtime re-executes itself inside the container
4. Given the runtime is dynamicly linked, the kernel loads our fake dynamic linker (`upload_runtime`) to the runtime process and passes execution to it. 
5. `upload_runtime` reads the runtime binary through `/proc/self/exe` and sends it to the configured remote server

![alt text](https://github.com/twistlock/whoc/blob/master/images/whoc_dynamic2.png?raw=true "whoc dynamic mode")


### Wait For Exec Mode
For staticly linked container runtimes, `whoc` comes in another flavor: `whoc:waitforexec`.

1. `upload_runtime` is the image entrypoint, and runs as the `whoc` container PID 1
2. The user is expected to exec into the `whoc` container and invoke a file pointing to `/proc/self/exe` (e.g. `docker exec whoc_ctr /proc/self/exe`)
3. Once the exec occures, the container runtime re-executes itself inside the container
4. `upload_runtime` reads the runtime binary through `/proc/$runtime-pid/exe` and sends it to the configured remote server

![alt text](https://github.com/twistlock/whoc/blob/master/images/whoc_waitforexec2.png?raw=true "whoc wait for exec mode after exec")

## Try Locally
Clone the repository:
```console
$ git clone https://github.com/twistlock/whoc
```
Set up a file server to receive the extracted container runtime:
```console
$ cd whoc && mkdir -p stash && cd stash
$ ../util/fileserver.py 
```
From another shell, run the `whoc` image in your container environment of choice, for example Docker:
```console
$ docker build -f Dockerfile_dynamic -t whoc:latest src  # or ./util/build.sh
$ docker run --rm -it --net=host whoc:latest  # or ./util/run_local.sh
```
See that the file server received the container runtime. Since we run `whoc` under Docker, the received container runtime should be [runc](https://github.com/opencontainers/runc). 

*`--net=host` is only used in local tests so that the `whoc` container could easily reach the host on `127.0.0.1`.*


## Help
Help for `whoc`'s main binary, `upload_runtime`:
```
Usage: upload_runtime [options] <server_ip>

Options:
 -p, --port                 Port of remote server, defaults to 8080
 -e, --exec                 Wait for exec mode for static container runtimes, waits until an exec to the container occurred
 -a, --exec-extra-argument  In exec mode, pass an additional argument to the runtime so it won't exit quickly (e.g. '--help')
```
