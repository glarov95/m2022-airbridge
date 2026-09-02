# M2022 AirBridge

Turns a USB-only **Samsung Xpress SL-M2022** into a modern, driverless **AirPrint / IPP
Everywhere** printer, with our own raster pipeline, halftoning, SPL/QPDL encoder and USB
transport. Native Apple Silicon. No vendor driver, no Rosetta, nothing to install on the phone.

Why: the Samsung driver is an Intel-only binary that stops working when macOS 28 removes
Rosetta 2, and macOS never exposes this printer to iPhones. Also because building a printer
driver from the bytes up is a good way to learn how printing actually works.

## Status

**M6 complete, v0.3: the iPhone and the Mac print.** The printer's language is fully decoded and
verified against 42 captured vendor jobs; our raster and halftone pipeline is measured against
the vendor's output (text: 99.99 % pixel agreement); our 0x11 band encoder produces smaller
streams than the vendor's on every band; our job encoder's page headers are byte-identical for
all 14 paper sizes; and the Printer Application now takes an Apple Raster job from an iPhone,
crops it to the printable area, halftones and encodes it, and writes it to the printer over
USB. Nothing from the vendor runs anywhere in that path. Next: M7 makes it a service that
starts with the Mac. See [PROGRESS.md](PROGRESS.md).

| Milestone | What it delivers |
|---|---|
| M0–M1 ✅ | probe, vendor fixtures, USB transport, `decode` |
| M2 ✅ | printer visible to iPhone/iPad/Mac; incoming raster captured |
| M3 ✅ | raster ingest, tone curve, five halftone methods, presets |
| M4 ✅ | 0x11 band codec encoder with per-band offset tables, smaller output than the vendor's |
| M5 ✅ | job encoder and `encode` command; first native page printed |
| M6 ✅ | iPhone and Mac print through the Printer Application (v0.3) |
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
git submodule update --init            # PAPPL v1.4.12
export PATH=/opt/homebrew/bin:$PATH
cmake -S . -B build -G Ninja        # builds the pinned PAPPL submodule on first configure
cmake --build build
ctest --test-dir build --output-on-failure -LE hardware
```

Hardware tests need the printer switched on and print a page: `ctest --test-dir build -L hardware`.

## Commands so far

```sh
./build/src/m2022-airbridge probe            # host, USB printers (device id, status), CUPS queues
./build/src/m2022-airbridge encode fixtures/oracle/samsung/small-text-a4.ras.gz --preset text --out job.spl
                                             # a complete printer job from a PGM, PBM or CUPS raster
./build/src/m2022-airbridge probe --json
./build/src/m2022-airbridge send JOB.spl     # write a printer-native job over USB
./build/src/m2022-airbridge decode JOB.spl --pbm out   # explain a job; pages to out-p1.pbm ...
./build/src/m2022-airbridge server                     # run the Printer Application on the USB printer (port 8000)
./build/src/m2022-airbridge server --device file:///tmp/job.spl   # dry run: the job goes to a file
./build/src/m2022-airbridge render IN.pgm --preset photo --out page.pbm   # halftone a page
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
