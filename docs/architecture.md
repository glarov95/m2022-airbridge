# Architecture

## The problem

The Samsung SL-M2022 is a USB-only laser printer. Its macOS driver is an Intel binary that runs
under Rosetta 2, which Apple removes in macOS 28 (fall 2027), and macOS never advertises the
printer in the form iOS needs, so an iPhone cannot print to it at all.

## How printing works today

```text
document ──(macOS renders)──> 600 dpi gray raster ──> rastertosec (Intel, Rosetta) ──> USB
                                                       halftone, bands, 0x11, PJL/QPDL
```

Everything after "renders" is the vendor's closed filter. We captured its output for 42 test
pages before it disappears; those files are the ground truth for our own implementation
(`fixtures/oracle/samsung/`, explained in `spl-qpdl.md`).

## How the finished product works

```text
iPhone / iPad / Mac / any IPP client
        │  DNS-SD discovery ("Samsung M2022"), IPP job as Apple Raster or PWG Raster
        ▼
m2022-airbridge, one daemon under launchd, unprivileged user
  PAPPL ......... IPP server, Bonjour, spooling, job state, TLS, web page (third-party library)
  raster/ ....... gray lines in, scaling, margins, tone curve            (ours, pure C)
  halftone/ ..... gray to black-or-white dots: threshold, clustered-dot,  (ours, pure C)
                  error diffusion, blue-noise mask
  qpdl/ ......... 128-line bands, 0x11 compression, page and band records, (ours, pure C)
                  PJL envelope
  usb/ .......... bulk writes, device identity, port status               (ours, libusb)
        │  USB
        ▼
Samsung SL-M2022
```

Clients render the document themselves at the 600 dpi we advertise, which is how real AirPrint
printers work (ADR-003), so no PDF engine sits in the trust boundary. PAPPL handles the
protocol work that is already specified and solved (ADR-004). Everything that decides what the
page looks like on paper is ours.

## Module rules

- `raster/`, `halftone/`, `qpdl/` are pure: memory in, memory or callbacks out, no I/O,
  deterministic. That is what makes them testable byte for byte against the vendor's output and
  portable to Linux later (ADR-005, ADR-008).
- Apple-specific code (launchd, Unified Logging, IOKit) lives in adapters under `service/`.
- One process owns the USB device; the installer removes the vendor CUPS queue (ADR-006).

## What exists today (M6)

The diagram above is now the running code. `m2022-airbridge server` is the daemon (still
started by hand; launchd comes in M7): PAPPL receives the job, our callbacks in `src/app/app.c`
run each line through `raster/` (gray, crop to the imageable area, tone), `halftone/` (the
preset chosen from the job's quality and content options) and `qpdl/` (bands, 0x11, records),
and the bytes reach the printer through the `m2022usb://` device scheme over `usb/`. The same
modules serve the command line: `encode` builds a job from a file, `send` writes it, `decode`
explains any job byte by byte, `render` shows what a halftone does.

Not yet: the launchd service and installer (M7), the reliability soak (M8), status details,
manual duplex and 1200 dpi (M9), the quality calibration (M10).

## What this teaches

The shape of every driverless printing stack: discovery (DNS-SD), a job protocol (IPP), a
device-independent raster format (PWG/Apple Raster), a device-specific encoder, and a transport.
Reading the vendor's bytes before writing our own is the reverse-engineering discipline that
keeps the encoder honest.
