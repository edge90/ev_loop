## Docker Instructions

If you have [Docker](https://www.docker.com/) installed, you can run this
in your terminal, when the Dockerfile is inside the `.devcontainer` directory:

```bash
docker build -f ./.devcontainer/Dockerfile --tag=ev_loop:latest .
docker run -it ev_loop:latest
```

This command will put you in a `bash` session in a Docker container,
with all of the tools listed in the [Dependencies](README_dependencies.md) section already installed.

If you want to build this container using specific versions of gcc and clang,
you may do so with the `GCC_VER` and `LLVM_VER` arguments:

```bash
docker build --tag=ev_loop:latest --build-arg GCC_VER=13 --build-arg LLVM_VER=17 .
```

To use clang as your default CC and CXX environment variables:

```bash
docker build --tag=ev_loop:latest --build-arg USE_CLANG=1 .
```

You will be logged in as root, so you will see the `#` symbol as your prompt.
You will be in a directory that contains a copy of the `ev_loop` project;
any changes you make to your local copy will not be updated in the Docker image
until you rebuild it.
If you need to mount your local copy directly in the Docker image, see
[Docker volumes docs](https://docs.docker.com/storage/volumes/).
TLDR:

```bash
docker run -it \
	-v absolute_path_on_host_machine:absolute_path_in_guest_container \
	ev_loop:latest
```

You can configure and build using these commands:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The `ccmake` tool is also installed; you can substitute `ccmake` for `cmake` to
configure the project interactively.
All of the tools this project supports are installed in the Docker image;
enabling them is as simple as flipping a switch using the `ccmake` interface.
Be aware that some of the sanitizers conflict with each other, so be sure to
run them separately.

