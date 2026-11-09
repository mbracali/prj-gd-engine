# gd-engine-core

The libraries every Guitar Dream game module can share: **platform**,
**inputs**, **audio**, **render**, **image**, **ui**, each an area folder with
one interface and many backends. A game links one CMake target, `gdcore`.

- Design notes, how a game links the core, how to add a backend, the
  dependency table: [docs/README.md](docs/README.md)
- The specification: [engine-backend-guidelines.md](../docs/guidelines/engine-backend-guidelines.md)
- Pinned libraries: [cmake/gdcore_deps.cmake](cmake/gdcore_deps.cmake); vendored
  headers: [third_party/README.md](third_party/README.md)

Self-test (Linux, headless):

```sh
cmake -S gd-engine-core -B gd-engine-core/build/dev -G Ninja
cmake --build gd-engine-core/build/dev --target gdcore_selftest
gd-engine-core/build/dev/gdcore_selftest
```
