# dmvsi

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmvsi/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmvsi/actions/workflows/ci.yml)

dmvsi DMOD application module.

## Description

TODO: describe what this module does.

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

## Testing

Tests are built automatically alongside the module (see `tests/`). Once built,
run them with `ctest`:

```bash
cd build
ctest --output-on-failure
```

`ctest` installs the test module's dependencies with `dmf-get` and then runs
it through `dmod_loader`. To run it manually instead:

```bash
export DMOD_DMF_DIR=$(pwd)/build/dmf
dmf-get install -d ${DMOD_DMF_DIR}/test_dmvsi-local.dmd -y
dmod_loader build/dmf/test_dmvsi.dmf
```

## Usage

<TBD>

This application module can be loaded and executed using the DMOD loader:

```bash
dmod_loader /path/to/dmvsi.dmf
```

## API

`dmvsi` is loaded and executed through the DMOD loader - it does not
expose a callable module API of its own. See
[docs/api-reference.md](docs/api-reference.md) for its command-line
arguments and exit codes.

## Documentation

See the `docs/` directory:

- **[api-reference.md](docs/api-reference.md)** - Command-line usage

View documentation using `dmf-man dmvsi`.

## Project Structure

```
dmvsi/
├── docs/              # Documentation (markdown format)
├── src/
│   └── dmvsi.c
├── tests/
│   ├── CMakeLists.txt
│   └── dmvsi_test.c
├── CMakeLists.txt
├── Makefile
├── dmvsi.dmr
└── manifest.dmm
```

## Author

Patryk Kubiak

## License

MIT
