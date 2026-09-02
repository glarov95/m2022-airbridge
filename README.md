# M2022 AirBridge

Turns a USB-only **Samsung Xpress SL-M2022** into a modern, driverless **AirPrint / IPP
Everywhere** printer, with our own raster pipeline, halftoning, SPL/QPDL encoder and USB
transport. Native Apple Silicon. No vendor driver, no Rosetta, nothing to install on the phone.

Why: the Samsung driver is an Intel-only binary that stops working when macOS 28 removes
Rosetta 2, and macOS never exposes this printer to iPhones. Also because building a printer
driver from the bytes up is a good way to learn how printing actually works.

## Status

**M1 complete, v0.1.** The printer's language is fully decoded and verified against 42 captured
vendor jobs, and a captured job replayed through our own USB transport prints. Next: the PAPPL
front end so the iPhone can see the printer (M2). See [PROGRESS.md](PROGRESS.md).

| Milestone | What it delivers |
|---|---|
| M0–M1 ✅ | probe, vendor fixtures, USB transport, `decode` |
| M2 | printer visible to iPhone/iPad/Mac; incoming raster captured |
| M3–M5 | raster, halftone, band codec, QPDL encoder |
| M6 | first page printed with no vendor code (v0.3) |
| M7–M8 | launchd service, installer, reliability soak (v1.0) |
| M9–M11 | status reporting, print quality program, signed release |

## How it works

```text
iPhone / iPad / Mac ──IPP + Bonjour──> m2022-airbridge ──USB──> Samsung SL-M2022
                                        PAPPL (IPP, discovery, spooling)
                                        raster → halftone → bands → 0x11 → QPDL/PJL
```

Clients render the page at 600 dpi and send it as Apple Raster or PWG Raster, like they would
to any AirPrint printer. The daemon halftones it, compresses 128-line bands with the printer's
0x11 scheme, wraps them in QPDL records and a PJL envelope, and writes the bytes over USB.
Details: [docs/architecture.md](docs/architecture.md), the printer language in
[docs/spl-qpdl.md](docs/spl-qpdl.md), the full design in [SPEC.md](SPEC.md).

## Build and test (macOS, Apple Silicon)

```sh
xcode-select --install
brew install cmake ninja pkg-config libusb jpeg-turbo libpng openssl@3
export PATH=/opt/homebrew/bin:$PATH
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure -LE hardware
```

Hardware tests need the printer switched on and print a page: `ctest --test-dir build -L hardware`.

## Commands so far

```sh
./build/src/m2022-airbridge probe            # host, USB printers (device id, status), CUPS queues
./build/src/m2022-airbridge probe --json
./build/src/m2022-airbridge send JOB.spl     # write a printer-native job over USB
./build/src/m2022-airbridge decode JOB.spl --pbm out   # explain a job; pages to out-p1.pbm ...
```

## Fixtures

`scripts/gen-fixtures.py` generates deterministic test pages; `scripts/capture-vendor-output.sh`
runs them through the vendor filter chain and stores the native output under
`fixtures/oracle/samsung/` as the golden reference. See [fixtures/README.md](fixtures/README.md).

## Repository

`SPEC.md` design · `PROGRESS.md` state · `docs/` how it works · `docs/adr/` decisions ·
`src/` modules · `include/m2022/` public headers · `tests/` unit and hardware · `scripts/` tools

## License

MIT. Encoders are clean-room; SpliX, jbigkit and the vendor driver are reference and test
oracles only ([ADR-010](docs/adr/0010-licensing-clean-room.md)).
