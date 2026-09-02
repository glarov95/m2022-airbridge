# M2022 AirBridge

A Printer Application that turns a USB-only Samsung Xpress SL-M2022 into a modern,
driverless AirPrint / IPP Everywhere printer, using our own raster pipeline, JBIG and
SPL/QPDL encoders, and USB transport. Native Apple Silicon; no vendor driver.

The full design is in [SPEC.md](SPEC.md). Decisions are recorded in [docs/adr](docs/adr).

## Status

M0 (environment probe) done. M1 (repository, USB transport, replay) in progress.

## Prerequisites (macOS)

```sh
xcode-select --install
brew install cmake ninja pkg-config libusb jpeg-turbo libpng openssl@3 jbigkit
```

## Build and test

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure -LE hardware
./build/src/m2022-airbridge version
```

Hardware tests (require the printer): `ctest --test-dir build -L hardware`.

## Fixtures

`scripts/gen-fixtures.py` generates deterministic test pages; `scripts/capture-vendor-output.sh`
runs them through the vendor filter chain and stores the native output under
`fixtures/oracle/samsung/`. See `fixtures/README.md`.

## License

MIT. See [LICENSE](LICENSE) and [ADR-010](docs/adr/0010-licensing-clean-room.md).
