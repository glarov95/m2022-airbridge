# M2022 AirBridge

Driverless **AirPrint** and **IPP Everywhere** printing for the USB-only
**Samsung Xpress SL-M2022**. Plug the printer into a Mac, install one small daemon, and every
iPhone, iPad and Mac on the network prints to it from the standard print dialog. Nothing is
installed on the phone, and nothing from the vendor runs on the Mac.

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Platform: macOS on Apple Silicon](https://img.shields.io/badge/platform-macOS%20%7C%20Apple%20Silicon-lightgrey.svg)
![Language: C17](https://img.shields.io/badge/language-C17-informational.svg)

```text
iPhone / iPad / Mac ──IPP + Bonjour──> m2022-airbridge ──USB──> Samsung SL-M2022
                                        PAPPL (IPP, discovery, spooling)
                                        raster → halftone → bands → 0x11 → QPDL/PJL
```

## Features

- **Prints from iPhone, iPad and Mac** over AirPrint and IPP Everywhere: Bonjour discovery,
  TLS, the standard print dialogs, no client software.
- **Native Apple Silicon daemon.** One static binary; no Rosetta, no vendor driver, no CUPS
  filter.
- **The whole print path is ours,** written from the printer's captured bytes: raster ingest,
  tone curve, halftoning, the printer's 0x11 band compression, the SPL/QPDL job encoder and
  the USB transport. Only the IPP front end is a library
  ([PAPPL](https://github.com/michaelrsweet/pappl)).
- **Five halftones** (threshold, ordered, clustered dot, blue noise, Floyd–Steinberg), chosen
  from the job's print quality and content: draft, text, normal, photo.
- **Job options that reach the printer:** 14 paper sizes from A4 to envelopes, media types
  (plain, thick, thin, card, labels, envelope, ...), manual feed, and copies printed by the
  printer itself.
- **Printer status** on the phone and the Mac: offline, out of paper, error.
- **Runs as a service.** `install` sets up a launchd daemon under a hidden unprivileged user
  that starts at boot and survives crashes; `doctor`, `status` and `logs` watch it;
  `uninstall` puts the Mac back the way it was.
- **A toolkit for the printer's language.** `decode` explains any SPL/QPDL job record by
  record, `encode` builds one from an image, `render` shows what a halftone does, `send`
  writes a job over USB, `probe` reports what is connected.
- **Verified against the vendor driver:** 42 captured vendor jobs, 867 bands. Page headers are
  byte-identical for all 14 paper sizes, our band encoder produces a smaller stream than the
  vendor's on every band, and our reproduction of the vendor's halftone agrees with it on
  99.99 % of the pixels of a text page.

### Planned

- Reliability soak on the way to **v1.0** (in progress: sleep/wake, reboot, a week of daily
  use).
- Toner level in the Supply Levels panel, over the vendor's USB control request.
- Cover-open and paper-jam status from the printer's status channel.
- Manual duplex, the printer's two-pass flow.
- 1200 dpi "Best" mode, kept only if it prints cleanly.
- Print quality calibration from scans, scored against the vendor's output (**v1.5**).
- Signed and notarized `.pkg`, Homebrew tap, a configuration file for the installed service.
- Later: a Linux and Raspberry Pi port of the same binary (the core modules are portable C),
  server-side PDF rendering, a menu-bar status app.

Clients render the page themselves at 600 dpi, exactly as they do for any AirPrint printer,
so the daemon never parses a document ([ADR-003](docs/adr/0003-raster-only-inputs-v1.md)).

## Compatibility

| | Tested | Should work | Not supported |
|---|---|---|---|
| **Printer** | Samsung Xpress SL-M2022 (USB `04e8:3321`; it reports itself as "M2020 Series") | the other Xpress M2020 models (M2020, M2020W, M2021, M2021W, M2022W): same vendor driver, same PPD, same page language. Untested; a different USB product id currently needs `server --device`, which the installer does not pass through yet | other Samsung SPL printers: other QPDL versions and band compressions (the ML-186x wants JBIG, for example) |
| **Host** | macOS 26 on Apple Silicon | other recent macOS releases | Intel Macs (the PAPPL build pins arm64; untested), Linux (planned, see above) |
| **Clients** | iOS 26, macOS 26 | iPadOS, and any IPP Everywhere client: Linux via CUPS, Windows, Android via Mopria | |

The printer stays on USB, so the Mac has to be on for others to print. The wireless M2020W
and M2022W may offer network printing of their own; this project covers the USB side only.

## Quick start

You need a Mac with Apple Silicon, the Xcode command line tools, Homebrew, and the printer
connected over USB.

```sh
xcode-select --install
brew install cmake ninja pkg-config libusb jpeg-turbo libpng openssl@3
git clone --recurse-submodules https://github.com/glarov95/m2022-airbridge.git
cd m2022-airbridge
export PATH=/opt/homebrew/bin:$PATH
cmake -S . -B build -G Ninja          # also builds the pinned PAPPL submodule
cmake --build build
ctest --test-dir build -LE hardware   # no printer needed
sudo ./build/src/m2022-airbridge install
./build/src/m2022-airbridge doctor
```

Then choose **Samsung M2022** in any print dialog. `install` prints every step it takes
(`--dry-run` only shows them): it creates a hidden service user, copies the binary to
`/usr/local/bin`, backs up and removes the vendor's CUPS queue so that one process owns the
USB device, writes a launchd job, and adds a driverless queue to the Mac.
`sudo m2022-airbridge uninstall` reverses all of it. If macOS keeps re-creating the Samsung
queue, `sudo m2022-airbridge remove-vendor-driver` backs up and deletes the vendor driver
package. Details: [docs/macos-service.md](docs/macos-service.md).

## How it works

Clients render the page at 600 dpi and send it as Apple Raster or PWG Raster, as they would
to any AirPrint printer. PAPPL handles discovery, IPP and job state. Our callbacks take the
raster line by line: crop it to the printer's imageable area, apply the tone curve, halftone
it with the preset the job asked for, compress 128-line bands with the printer's 0x11 scheme,
wrap them in QPDL records and a PJL envelope, and write the bytes over USB through our own
transport. The same modules serve the command line tools.

Read [docs/architecture.md](docs/architecture.md) for the module map,
[docs/spl-qpdl.md](docs/spl-qpdl.md) for the printer's language as decoded from the vendor's
output, and [SPEC.md](SPEC.md) for the full design.

## Commands

| Command | What it does |
|---|---|
| `server [--port N] [--device URI] ...` | run the Printer Application in the foreground (development; `--device file:///PATH` writes the job to a file) |
| `install [--dry-run]` · `uninstall [--purge]` | set up or remove the launchd service (sudo) |
| `start` · `stop` · `restart` | control the service (sudo) |
| `status` · `logs [-n N] [-f]` · `doctor` | service, printer and queue state · the service log · one line per check |
| `remove-vendor-driver` | back up and delete the Samsung driver package (sudo) |
| `probe [--json]` | the host, USB printers (device id, port status), CUPS queues |
| `encode IN [options]` | build a printer job (`.spl`) from a PGM, PBM or CUPS raster: halftone preset, media, source, type, duplex, copies |
| `send JOB.spl` | write a printer-native job over USB |
| `decode JOB.spl [--pbm PREFIX]` | explain a job record by record and write its pages as PBM |
| `render IN [--preset P] [--out OUT.pbm]` | halftone a PGM or CUPS raster into a PBM |

`m2022-airbridge help` lists them; every command prints its options when called wrongly.

## Documentation

- [docs/README.md](docs/README.md) indexes the module documents, each ending with what it
  teaches: [architecture](docs/architecture.md), [the printer's language](docs/spl-qpdl.md),
  [USB](docs/usb.md), [IPP and AirPrint](docs/ipp-airprint.md), [raster](docs/raster.md),
  [halftone](docs/halftone.md), [the macOS service](docs/macos-service.md),
  [debugging](docs/debugging.md).
- [SPEC.md](SPEC.md): the design, with measured facts, open questions and a learning track.
- [docs/adr/](docs/adr/): the decisions and why (no interim bridge, PAPPL, raster-only input,
  600 dpi baseline, unprivileged service, clean-room licensing, ...).
- [PROGRESS.md](PROGRESS.md): project status, roadmap and session log.

## Project status

**v0.3.** The daemon is installed and in daily use on one Mac; the reliability soak for v1.0
is in progress. What works, what was measured and what comes next: [PROGRESS.md](PROGRESS.md).

## Development

- **Layout:** `src/` modules (`app` PAPPL callbacks, `raster`, `halftone`, `qpdl`, `usb`,
  `service`, `cups`, `cli`, `common`), `include/m2022/` public headers, `tests/` unit,
  integration and hardware, `fixtures/` generated pages and the captured vendor output,
  `scripts/` fixture generation and measurements, `third_party/` the PAPPL submodule.
- **Style:** C17, warnings are errors, sanitizers in Debug builds, `.clang-format`.
- **Tests:** `ctest --test-dir build -LE hardware` runs the unit tests (codec fuzzing, every
  vendor band re-encoded, page headers for every size) and the integration tests (a job
  through the server with `ipptool`). `ctest --test-dir build -L hardware` prints a page and
  needs the printer on.
- **Fixtures:** `scripts/gen-fixtures.py` generates deterministic test pages;
  `scripts/capture-vendor-output.sh` ran them through the vendor filter to produce the golden
  files under `fixtures/oracle/samsung/`. See [fixtures/README.md](fixtures/README.md).
- **Clean room:** the encoders are written from published specifications, the SpliX SPL2
  document and our captured bytes. SpliX, jbigkit and the vendor driver are test oracles
  only, never copied or linked ([ADR-010](docs/adr/0010-licensing-clean-room.md)).

## Contributing

Issues and pull requests are welcome, especially reports from other M2020-series printers.
Keep changes small, ship tests with them, keep the pure modules free of I/O, and respect the
clean-room rule above. Every protocol constant carries a comment citing its evidence.

## Acknowledgements

- [PAPPL](https://github.com/michaelrsweet/pappl) by Michael R Sweet, the IPP Everywhere
  front end.
- The [SpliX](https://github.com/OpenPrinting/splix) project's SPL2 document, the only public
  description of the printer's page language.
- [libusb](https://libusb.info).

## License

MIT, see [LICENSE](LICENSE).
