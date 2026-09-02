# M2022 AirBridge — Engineering Specification

**Version:** 2 (supersedes v1 dated 2026-08-31)
**Date:** 2026-09-02
**Status:** Design baseline for implementation
**Target hardware:** Samsung Xpress SL-M2022, USB only. It identifies over USB as **"Samsung M2020 Series"**.
**Host:** Apple Silicon Mac running macOS 26 (Tahoe)
**Clients:** iPhone/iPad via AirPrint, the host Mac itself, and any IPP Everywhere / Mopria client
**Direction:** A self-contained, production-quality **Printer Application**: our own IPP/DNS-SD front end (via PAPPL), our own raster and halftone pipeline, our own band codec and SPL/QPDL encoders, and our own USB transport. No interim bridge. No dependency on the Samsung driver at any stage of the shipped product.
**Secondary goal:** Learning. Every milestone names the concepts it teaches and the reading that supports it.

---

## 0. What changed since v1

1. **Stage A is gone.** v1 proposed a Bonjour bridge on top of the existing macOS CUPS queue. That is a temporary solution and is out of scope. It exists as prior art (`sapireli/AirPrint_Bridge`, MIT) should anyone ever need it.
2. **Milestone 0 is done.** The environment probe was run on 2026-09-02. Results are in section 2 and replace every "assumed" statement from v1.
3. **The real deadline is named.** The vendor driver is Intel-only and runs under Rosetta 2. Apple removes general Rosetta support in macOS 28 (fall 2027). Section 2.3.
4. **1200 dpi is experimental, but the format is now known.** The vendor driver renders a 600 dpi gray raster, and its "Best" mode emits a genuine 1200×1200 dpi job from it (captured). Hardware validation is still required. Sections 2.8 and 6.4.
5. **Licensing is decided:** MIT, clean-room encoders. SpliX, jbigkit and the Samsung driver are test oracles only, never linked or copied. Section 13.
6. **v1 accepts raster inputs only** (Apple Raster, PWG Raster, JPEG, PNG), which is how real AirPrint printers behave. Server-side PDF rendering is a v2 quality feature. ADR-003.
7. **Milestones are re-sequenced** so every milestone ships code that stays in the product.
8. **A learning track is added** (section 10).
9. **Vendor output captured and decoded (2026-09-02, after the probe).** Compression is 0x11, an LZ-style scheme documented by SpliX, not JBIG; band width is the raster width rounded up to 256 px, verified across all 14 paper sizes; the page and band record layouts are confirmed byte by byte. Section 2.8 and `docs/spl-qpdl.md`. The JBIG milestone is replaced by a much smaller band-codec milestone.

---

## 1. Executive summary

Build `m2022-airbridge`, a Printer Application that makes a USB-only Samsung SL-M2022 appear on the network as a modern, driverless AirPrint and IPP Everywhere printer, and that talks to the printer in its native SPL/QPDL language using code we wrote and understand.

The shipped data path is:

```text
iPhone / iPad / Mac / any IPP client
        │  DNS-SD discovery, IPP job (Apple Raster or PWG Raster)
        ▼
m2022-airbridge (PAPPL-based Printer Application, arm64 native)
        │  raster ingest → tone curve → halftone → 1-bit bands
        │  → 0x11 band compression → QPDL framing → PJL wrapper
        ▼
USB transport (libusb, printer class, bulk OUT + status IN)
        ▼
Samsung SL-M2022
```

The vendor driver is used exactly once: to capture reference output while it still runs. After that it is deleted from the machine.

The architecture leans on one mature dependency, PAPPL, for the parts that are already solved and well specified: IPP semantics, DNS-SD advertisement, spooling, job state, TLS, and the web interface. Everything printer-specific is ours.

---

## 2. Verified facts (Milestone 0, run 2026-09-02)

All commands were read-only. Nothing on the Mac was changed.

### 2.1 Host

| Item | Value |
|---|---|
| macOS | 26.6.2 (build 25G83) |
| Architecture | arm64 |
| CUPS | 2.3.4 (Apple fork), listens on `localhost:631` only |
| Printer sharing | off (`_share_printers=0`) |
| Firewall | off |
| Wake on LAN | on (`womp 1`); sleep currently prevented by Amphetamine |
| Bonjour tooling | `/usr/bin/dns-sd`, `ippfind`, `ipptool`, `ippeveprinter` present |

### 2.2 Existing print path

| Item | Value |
|---|---|
| Queue name | `Samsung_M2020_Series` (system default) |
| Device URI | `usb://Samsung/M2020%20Series?serial=ZF45B8GF3C01YSD` |
| PPD | `/etc/cups/ppd/Samsung_M2020_Series.ppd`, `*ModelName "Samsung M2020 Series"` |
| Driver package | Samsung UPD 3.93.00 under `/Library/Printers/Samsung/` |
| Filter chain | PDF → `cgpdftoraster` → `/Library/Printers/Samsung/UPD/Filters/rastertosec` → USB |
| Raster the filter consumes | `application/vnd.cups-raster`, 600×600 dpi, 8-bit, `cupsColorSpace 3` (K), `cupsColorOrder 0` |
| "Best" quality | same 600 dpi raster with `cupsRowFeed 2`; not a 1200 dpi raster |
| Queue options | manual duplex, toner save, skip blank pages, edge enhancement, brightness/contrast, 14 page sizes |
| IPP `printer-resolution-supported` | `600dpi` only |
| Last successful job | 2026-05-08 (job 57), so the Intel filter still works under Rosetta today |
| Filter internals (from strings) | `@PJL ENTER LANGUAGE=QPDL`, banded processing, `eJBIGSupH`, gamma H/L, a `1200x1200dpi` string that the PPD never exposes |
| Filter links against | system `libcups.2` and `libcupsimage.2` only; JBIG is compiled in |

Captured later the same day with the printer switched on (`fixtures/oracle/samsung/usb-device-info.txt`):

| Item | Value |
|---|---|
| USB VID / PID | `0x04E8` / `0x3321`, bcdDevice 1.00, high speed (480 Mbit/s) |
| USB strings | vendor "Samsung Electronics Co., Ltd.", product "M2020 Series", serial `ZF45B8GF3C01YSD` |
| IEEE 1284 device ID | `SERN:ZF45B8GF3C01YSD;MFG:Samsung;CMD:SPL,URF,FWV,PIC,EXT,DCU;MDL:M2020 Series;CLS:PRINTER;CID:SA_SPLV3_BW;MODE:SPL3,R000105;STATUS:MPS;` |
| Notable | `CMD` lists **URF**: the firmware claims to accept Apple Raster directly over USB (shared firmware with the W model). An M9 experiment: send a URF file raw and see whether it prints. `CID:SA_SPLV3_BW` confirms SPL version 3, monochrome. |
| Supplies | CUPS reports `marker-levels = 48` for the queue, so some channel (vendor filter back-channel or backend) reads the toner level; finding it is an M9 task |

Reference SPL output for every fixture page was captured on 2026-09-02 (section 2.8), and the driver package is backed up under `artifacts/vendor-driver-backup/`.

### 2.3 Why the existing path is on a clock

- The Samsung driver package contains 44 Mach-O executables. **None has an arm64 slice.** Every one is x86_64/i386 and runs through Rosetta 2.
- Apple: macOS 27 is the last release with full Rosetta 2 support. macOS 28 (fall 2027) keeps only a subset for older games. Intel-only filters will stop running.
- HP has never shipped an arm64 build of the Samsung macOS drivers.

**Consequence:** v1.0 of this project must be in daily use before the host is upgraded to macOS 28, i.e. before roughly September 2027. Staying on macOS 27 longer is possible but is not the plan.

### 2.4 What Apple's CUPS does and does not do

- Apple's `cupsd` advertises shared queues with the `_cups` DNS-SD subtype only (per its `cupsd.conf` man page). iOS requires the `_universal` subtype and a `URF` TXT key. That is why Printer Sharing alone never showed this printer to an iPhone, and why bridge scripts exist.
- Apple's CUPS accepts `image/urf` and `image/pwg-raster` as job input and has its own conversion filters. Irrelevant to the shipped product, but useful for experiments.
- None of this is in the shipped data path. The Printer Application replaces the macOS queue entirely (ADR-006).

### 2.5 SpliX facts about this printer family

SpliX (OpenPrinting, GPL-2.0) is the only open implementation of SPL/QPDL. Release 2.0.2 shipped 2026-07-16; the repository is active.

- `ppd/samsung.drv.in` defines **"M2020 Series"** inside the group commented *"Monochrome printers V. 3 (not fully supported, new algorithms)"*, sharing settings with ML-1915 and ML-2165.
- That group sets: `QPDLVersion "3"`, `DocHeaderValues "<0><0><1>"`, hardware margins 12.5 pt, `BandSize "128"` (from `spl2basic.defs`), base resolution 600 dpi, an extra `1200dpi` resolution choice at group level.
- M2020-specific attributes: `PacketSize "512"` and `SpecialBandWidth "True"`.
- Issue #18 (Feb 2026) explains `SpecialBandWidth`: *"Samsung M2026 does not work unless it receives exactly the right bandwidths. There does not seem to be a general formula for all paper sizes, hence simple lookup."* The reporter reverse-engineered Samsung's Linux driver and found an alignment rule (32- or 64-byte band alignment depending on a model flag).
- SpliX takes the compression algorithm from the CUPS raster header (`cupsCompression`), i.e. from a PPD option, and implements 0x0D, 0x0E, 0x11, 0x13 and 0x15 (JBIG, banded). PR #14 for the sibling ML-186x (same `SpecialBandWidth` group) reports: *"The printer seems to only support banded jbig compression, all other schemes don't work"* and *"They were sold as 1200x1200 DPI models but I'm not able to print at 600x1200 or 1200x1200. I get garbled pixels."*

The fixtures captured the same day settle this (section 2.8): QPDL version 3 page header, compression 0x11 in every band and never JBIG, 600 dpi with a real 1200 dpi mode, and band width = ceil(raster width / 256) × 256.

### 2.6 Prior art (reference only, nothing here is used in the product)

| Project | What it is | Why it matters |
|---|---|---|
| `sapireli/AirPrint_Bridge` (MIT) | bash + `dns-sd` + launchd bridge for Mac-shared queues | Shows the exact TXT keys iOS wants; useful for comparing our DNS-SD output |
| OpenPrinting `ghostscript-printer-app` | PAPPL + pappl-retrofit + SpliX, snaps for amd64/arm64/armhf/riscv64 | Proves PAPPL + SpliX-class drivers work as a Printer Application on Linux; an end-to-end reference for the Linux port |
| OpenPrinting `splix` (GPL-2.0) | CUPS raster → QPDL filter | Primary protocol oracle |
| `jbigkit` (GPL-2.0) | Reference JBIG T.82/T.85 codec | Only relevant if the optional JBIG (0x15) path is ever pursued |
| Samsung UPD 3.93 `rastertosec` | Vendor filter, Intel binary | Second protocol oracle; captured output is the ground truth for what the printer accepts |
| `ippeveprinter` (ships with macOS) | Apple's sample IPP Everywhere printer | Reference for what a correct IPP Everywhere/AirPrint advertisement looks like |

### 2.7 Toolchain on this Mac

Present: Xcode clang 21, Homebrew, `libusb 1.0.30`, CUPS headers in the Xcode SDK (`cups/cups.h` etc.), `ipptool`, `ippfind`, `ippeveprinter`.
Installed 2026-09-02 via Homebrew: `cmake` 4.4, `ninja` 1.13, `pkg-config`, `jpeg-turbo`, `libpng`, `openssl@3`, `jbigkit`.
Homebrew has no `pappl` formula; PAPPL is built from source. Latest release: **v1.4.12 (2026-08-20)**. PAPPL `master` targets libcups 3 / CUPS 2.5 and is not usable against macOS's CUPS 2.3.4, so the project pins the 1.4.x line.

### 2.8 What the captured vendor output shows (2026-09-02)

27 jobs plus a 14-size media sweep were captured by running the vendor filter chain by hand (`scripts/capture-vendor-output.sh`) and surveyed with `scripts/spl-survey.py`. A same-day recapture is byte-identical; across days only the `SERVICEDATE` PJL line changes. Byte-level annotation: `docs/spl-qpdl.md`.

| Fact | Evidence |
|---|---|
| Raster the vendor consumes | CUPS raster v3, 600×600 dpi, 8-bit K; A4 4750×6808 px, Letter 4896×6400 px |
| Job structure | PJL header, `@PJL ENTER LANGUAGE=QPDL`, then records 0x00 (page, 17 bytes), 0x0C (band: 11-byte header + data), 0x01 (end page, 3 bytes), 0x09 (end job) followed by the UEL. Nothing else appears. |
| Page header | as the SpliX SPL2 document describes: res/100, copies, paper code, paper width/height in 1/300 in, feeder, duplex bytes, QPDL version 3, x-res/100 |
| Paper codes | Letter 0, Legal 1, A4 2, Executive 3, Com10 6, Monarch 7, C5 8, DL 9, JB5 0x0B, B5 0x0C, Postcard 0x0D, A5 0x10, Folio 0x18, Oficio 0x1C |
| Bands | 128 lines each; blank bands are omitted; numbering counts from the top of the printable area |
| Band width | ceil(raster width / 256) × 256 on all 14 sizes (A4 4864, Letter 5120, ...). Same as the 32-byte alignment reported in SpliX issue #18. |
| Compression | 0x11 in every band of every job: `0x09ABCDEF` magic, 64-entry pointer table, 128 raw bytes, LZ-style tokens, 32-bit checksum. Never JBIG. |
| "Best" quality | a real 1200×1200 dpi job: `@PJL SET RESOLUTION = 1200`, header res 12, band width 9728, twice the bands, from the same 600 dpi gray raster |
| Toner save, edge enhancement | change the band data only (halftone level); no PJL or header change |
| Media type | `@PJL SET PAPERTYPE = THICK` and similar, carried into the filter through the raster header's MediaType string |
| Manual duplex | `@PJL SET DUPLEX = MANUAL` plus `@PJL SET BINDING = LONGEDGE` or `SHORTEDGE`; page header unchanged. The filter needs `/Library/Caches/com.sec.printer` to exist or it crashes. |
| Manual feed | feeder byte 2 instead of 1 |
| Copies | the filter ignores copies and writes 1 in every page footer; CUPS repeats the raster pages instead. Whether the printer honours the footer field is an M9 hardware question. |
| Skip blank pages | `@PJL SET XIGNOREFF=ON`; a blank page has zero bands either way |

---

## 3. Goals and non-goals

### 3.1 Goals

1. Print from iPhone/iPad with nothing installed on the device.
2. Print from the host Mac and any other IPP Everywhere client without the Samsung driver.
3. Native arm64, no Rosetta, no vendor code.
4. Survive reboot, sleep/wake, printer power cycles, USB replug, and network changes without manual intervention.
5. Output quality at least equal to the vendor driver, measured on a defined test sheet.
6. Every module understood well enough to explain, test, and extend. The project doubles as a course in IPP, DNS-SD, raster formats, halftoning, band compression, PJL/QPDL, and USB.

### 3.2 Non-goals

- No interim bridge on top of the macOS CUPS queue.
- No throwaway prototypes: code written for a milestone stays in the product or in the test suite.
- No iOS app, no cloud printing, no Internet exposure, no firmware modification.
- No colour, no scanning, no automatic duplex (the hardware has none).
- No support for other Samsung models in v1, but nothing in the encoder may hard-code M2022-only assumptions where a table entry would do.

---

## 4. Product definition

From an iPhone on the same LAN: Share → Print → pick "Samsung M2022" → print. From the Mac: the printer appears automatically in the print dialog as an AirPrint printer; no driver selection.

Behaviour the user sees:

- Discoverable within a few seconds of the service starting or the network changing.
- Job states reported honestly: printing, printer off, out of paper, jam, cover open, where the printer exposes them.
- If the printer is off or unplugged, jobs queue and print when it returns, or fail with a clear reason after a configurable timeout.
- Manual duplex works the way the vendor driver did: print odd pages, prompt, reinsert, print even pages.
- Print quality presets: Draft, Normal, Text, Photo (section 7).
- A local web page (PAPPL) for status, job list, test page, and logs. Local host only by default.
- `m2022-airbridge doctor` explains any failure in plain language.

---

## 5. Architecture

```text
┌────────────────────────────────────────────────────────────────┐
│ m2022-airbridge (one process, LaunchDaemon, dedicated user)    │
│                                                                │
│  PAPPL system                                                  │
│   ├─ IPP server (IPP Everywhere + AirPrint attributes)         │
│   ├─ DNS-SD via mDNSResponder (_ipp._tcp, _universal, _ipps)   │
│   ├─ spooler, job state, TLS, web UI, logging                  │
│   └─ raster decoding (Apple Raster, PWG Raster, JPEG, PNG)     │
│                     │ 8-bit lines, per page                    │
│  ┌──────────────────▼──────────────────┐                       │
│  │ raster/   ingest, scale, tone curve │  pure C, no I/O       │
│  │ halftone/ threshold, ordered,       │  deterministic        │
│  │           error diffusion, masks    │  unit-tested          │
│  └──────────────────┬──────────────────┘                       │
│                     │ 1-bit page bitmap                        │
│  ┌──────────────────▼──────────────────┐                       │
│  │ qpdl/     band splitting, 0x11      │  clean-room           │
│  │           band codec, headers,      │  byte-exact tests     │
│  │           PJL wrapper, job framing  │  vs captured fixtures │
│  └──────────────────┬──────────────────┘                       │
│                     │ printer-native bytes                     │
│  ┌──────────────────▼──────────────────┐                       │
│  │ usb/      libusb printer-class      │  bulk OUT, status IN, │
│  │           transport, device ID,     │  replug recovery      │
│  │           port status, soft reset   │                       │
│  └──────────────────┬──────────────────┘                       │
│  cli/  service/  doctor/  config/                              │
└─────────────────────┼──────────────────────────────────────────┘
                      │ USB 2.0
              ┌───────▼────────┐
              │ Samsung M2022  │
              └────────────────┘
```

Rules:

- **One owner of the USB device.** The Printer Application owns it. The installer removes the macOS Samsung queue (after backing up its PPD) so Apple's `usb` backend never competes for the interface. Uninstall restores it.
- **Core modules are pure.** `raster/`, `halftone/`, `qpdl/` take memory in and call a write callback out. They know nothing about IPP, files, USB, PAPPL, or macOS. This is what makes them testable byte-for-byte and portable to Linux later.
- **Apple-specific code lives behind interfaces.** Unified Logging, launchd, IOKit details, CoreGraphics (future PDF path) are adapters, not core.
- **Everything is deterministic.** Same input raster and settings produce the same bytes. Golden-file tests depend on it.

---

## 6. Component specifications

### 6.1 IPP and DNS-SD front end (PAPPL)

PAPPL 1.4.x provides the system, printer, and job objects. We provide a `pappl_pr_driver_data_t` and callbacks.

Driver data we declare (PAPPL derives `urf-supported`, `pwg-raster-document-*`, `media-col-database`, and the DNS-SD TXT record from it, so no hand-written TXT records):

- `raster_types`: `PAPPL_PWG_RASTER_TYPE_SGRAY_8 | PAPPL_PWG_RASTER_TYPE_BLACK_1`. Not sRGB: advertising `srgb_8` makes macOS's driverless PPD default to RGB and send `print-color-mode=color`, which a mono printer must refuse (measured 2026-09-02, `docs/ipp-airprint.md`).
- `resolutions`: 600×600 only. With 300 also advertised, macOS renders normal-quality jobs at 300 dpi (measured 2026-09-02). Draft is a halftone/toner-save preset, not a lower input resolution. 1200×1200 only when section 9 M9 proves it.
- `color_supported`: `auto` and `monochrome` (clients send `auto`); default `monochrome`.
- `sides`: one-sided; manual duplex exposed as a vendor option, not as `two-sided-*`.
- `media`: the 14 sizes from the vendor PPD, with the 12.5 pt hardware margins, `media-source` = auto/manual, `media-type` = the vendor list.
- `print-quality`: draft/normal/high mapped to presets (section 7).
- `copies`: handled in the encoder (QPDL page copies) so PAPPL sends the page once.
- `identify-printer`: not supported by the hardware; PAPPL will report so.
- Formats advertised: `image/urf`, `image/pwg-raster`, `image/jpeg`, `image/png`. Not `application/pdf` in v1 (ADR-003).

Callbacks we implement: `rstartjob`, `rstartpage`, `rwriteline`, `rendpage`, `rendjob` (raster path), `printfile` (raw QPDL replay for `send` and tests), `status` (USB port status → `printer-state-reasons`), `testpage`.

Acceptance is measured with tools that ship with macOS:

```text
ippfind _ipp._tcp,_universal --txt URF       # discovery + TXT
ipptool -tv ipp://host:8000/ipp/print get-printer-attributes.test
ipptool -tv -f page.pwg ipp://host:8000/ipp/print print-job.test
```

and with a real iPhone, iPad, and the host Mac.

### 6.2 Raster ingest (`raster/`)

Input: lines from PAPPL, with the page header (width, height, bits per pixel, colour space, resolution).

- Normalise to 8-bit gray. sRGB → luminance (Rec. 709 coefficients, linearised), 1-bit → 0/255.
- Resolution: if the client sent 300 dpi and the engine mode is 600 dpi, upscale 2× with a sharp kernel; never downscale silently. Record the client resolution in the job log; it is a quality signal.
- Crop or pad to the printable area for the selected media, honouring the 12.5 pt margins.
- Page assembly: the encoder needs whole bands, so buffer at least one band height; a full page at 600 dpi A4 is 4960×7016 px ≈ 33 MB at 8-bit, acceptable but we stream per band where possible.

### 6.3 Tone and halftone (`halftone/`)

Output: 1-bit bitmap, 1 = toner.

- Tone curve: gamma and dot-gain compensation, per preset, applied through a 256-entry LUT.
- Halftone algorithms, all implemented, selectable per preset and per region:
  - fixed threshold (text, line art);
  - ordered dither with Bayer matrices (Draft);
  - clustered-dot ordered dither (the classic laser screen; isolated single 600 dpi dots print unreliably on toner engines);
  - Floyd–Steinberg error diffusion with serpentine scan (Ostromoukhov's variable-coefficient variant deferred to M10, if scans show a benefit);
  - blue-noise mask generated by void-and-cluster (generated at build time, checked in).
- Optional edge-aware mode: detect text-like regions (high local contrast, bimodal histogram) and threshold them while dithering the rest. Off by default until M10 proves it.
- Every algorithm has a unit test against a hand-checked 8×8 reference and a determinism test.

### 6.4 QPDL encoder (`qpdl/`)

Public interface (unchanged in spirit from v1):

```c
typedef struct {
    int width_px, height_px;      /* 1-bit page bitmap */
    int x_dpi, y_dpi;
    const uint8_t *bits;          /* packed, 1 = black */
    size_t bytes_per_row;
} m2022_page_t;

typedef struct {
    m2022_media_t   media;        /* size + source + type */
    m2022_quality_t quality;
    bool            toner_save;
    int             copies;
    bool            manual_duplex_second_pass;
} m2022_job_options_t;

int m2022_encode_job(const m2022_job_options_t *opt,
                     const m2022_page_t *pages, size_t n_pages,
                     m2022_write_cb write, void *ctx);
```

Responsibilities (layouts confirmed in section 2.8 and `docs/spl-qpdl.md`):

- PJL envelope: `<ESC>%-12345X`, `@PJL DEFAULT SERVICEDATE=yyyymmdd`, comment lines, `@PJL SET XIGNOREFF`, `RESOLUTION`, `BITSPERPIXEL = 1` (600 dpi only), `PAPERTYPE`, `DUPLEX` (+ `BINDING` for manual duplex), `@PJL ENTER LANGUAGE=QPDL`; trailer `0x09` + `<ESC>%-12345X`. Which lines the printer actually requires is an M6/M9 hardware question; until then we emit what the vendor emits.
- Page record (0x00, 17 bytes): resolution/100, copies, paper code, paper width and height in 1/300 in (1/150 in at 1200 dpi, as the vendor does), feeder code, duplex bytes, QPDL version 3, x-resolution/100.
- Band records (0x0C): band number, band width, 128-line height, compression 0x11, data length, then the 0x11 payload (section 6.5). Blank bands are omitted, as the vendor does.
- Band geometry: band width = ceil(raster width / 256) × 256; the page is padded with white to that width. The rule is verified against the vendor output for all 14 paper sizes and asserted by tests.
- Band data is column-major within a band (byte k of line 0, byte k of line 1, ... for the 128 lines, then byte k+1) and bit-inverted (1 = white), per the SpliX document; verified by decoding vendor bands.
- End-page record (0x01) with the copies field; whether the printer honours it is tested in M9, with page repetition as the fallback.
- `PacketSize 512` from SpliX has no visible counterpart in the byte stream; investigated on hardware.
- Copies and manual duplex are encoder-level options.
- The encoder never allocates per line; it writes through the callback.

### 6.5 Band compression 0x11 (`qpdl/codec11.c`)

The only compression the vendor uses on this printer. Documented in the SpliX SPL2 document (`specs-en/bandes.tex`) and visible in every captured band:

- Payload header: `0x09ABCDEF` (32-bit; the printer autodetects endianness from it), raw-data length (32-bit, at most 128), a table of 64 16-bit offsets, then that many raw bytes copied verbatim.
- Stream: LZ-style tokens. A match token has bit 7 set in its first byte: length = (b1 & 0x7F) + ((b2 & 0xC0) << 1) + 3, table index = b2 & 0x3F; the match copies from `position − table[index]`. A literal token's first byte is the literal count minus one (at most 64 bytes), followed by the bytes.
- A 32-bit big-endian checksum of every byte between the band header and the checksum closes the payload; its 4 bytes are included in the band record's length.
- The vendor's offset table (multiples of 128 for horizontal neighbours, 1 and diagonals for vertical ones) is recorded from the fixtures; whether the printer accepts other tables is an M9 question.

Deliverables: decoder (used by `decode` and by tests to explain every vendor band), encoder with a greedy longest-match search over the table, the column-major transform, the checksum. Tests: every vendor band decodes and verifies; decoded bands assembled into a page match the page content; encode→decode round trips exactly on fixtures and random data; fuzzing of the decoder.

JBIG (0x15) is not needed for this printer and is out of scope unless a hardware experiment shows a benefit.

### 6.6 USB transport (`usb/`)

- libusb 1.0 through PAPPL's device layer (`usb://` scheme) for open/claim/bulk write; our own status reads on top.
- Printer class requests: `GET_DEVICE_ID` (0), `GET_PORT_STATUS` (1), `SOFT_RESET` (2). Port status bits (paper empty, selected, no error) feed `printer-state-reasons`.
- Identity by VID/PID/serial, never by enumeration order. Replug is detected by libusb hotplug or a poll and the printer object reconnects without a restart.
- Timeouts and partial-write handling are explicit; the writer never blocks the IPP thread.
- Back-channel: PJL `@PJL INFO STATUS` / `USTATUS` over bulk IN is investigated in M9; toner level only if the printer answers.
- `m2022-airbridge send file.qpdl` writes a captured job straight to the device. This is a shipped diagnostic command and the first hardware test in the project.

### 6.7 Service and lifecycle (macOS)

- LaunchDaemon `com.m2022airbridge.daemon`, `KeepAlive`, running as a dedicated hidden user `_m2022airbridge` created by the installer. Verified in M1: libusb opens and claims the printer as a normal user on macOS 26; no kernel driver holds it.
- Spool and state under `/var/spool/m2022-airbridge` and `/Library/Application Support/M2022AirBridge`.
- Port 8000 (PAPPL default) so no privileged port is needed. DNS-SD SRV records carry the port; clients do not care.
- Sleep/wake: mDNSResponder re-registers automatically and integrates with the Bonjour Sleep Proxy, so an Apple TV or HomePod on the LAN keeps the printer visible and wakes the Mac for a job. The service itself must survive wake without a restart; M8 tests it.
- Logging: PAPPL's log to a file plus an `os_log` adapter so `log stream --predicate 'subsystem == "com.m2022airbridge"'` works.

### 6.8 Installer and uninstaller

Installer steps, each idempotent and each printing what it did:

1. Verify the USB printer is present and matches VID/PID.
2. Back up `/etc/cups/ppd/Samsung_M2020_Series.ppd` and the queue's `lpoptions` to the support directory.
3. Remove the Samsung CUPS queue (`lpadmin -x`).
4. Create the service user, directories, and LaunchDaemon; load it.
5. Wait for DNS-SD registration; run `doctor`; print a test page if `--test-page`.
6. The Mac's own print dialog discovers the printer via Bonjour. The installer also creates an explicit driverless queue so scripts and `lp` work: `lpadmin -p M2022 -E -v ipp://localhost:8000/ipp/print -m everywhere`.

Uninstall reverses every step and restores the Samsung queue from the backup. Distributed as a signed and notarized `.pkg` (M11) with a Homebrew tap as a second channel.

### 6.9 CLI

```text
m2022-airbridge probe [--json]      environment and printer facts (section 2, automated)
m2022-airbridge server              run the Printer Application in the foreground
m2022-airbridge doctor              diagnose discovery, USB, service, and queue state
m2022-airbridge install|uninstall|start|stop|restart|status|logs
m2022-airbridge send FILE           write raw QPDL to the printer
m2022-airbridge capture FILE        capture the raster PAPPL would hand the driver
m2022-airbridge render FILE.pwg --preset text --out page.pbm
m2022-airbridge encode FILE.pbm --media A4 --out job.qpdl
m2022-airbridge decode FILE.qpdl    dump records, band geometry, 0x11 payload headers; bands to PBM
m2022-airbridge testpage
```

`decode` is as important as `encode`: it is how captured vendor jobs are studied and how regressions are explained.

### 6.10 Configuration

`/Library/Application Support/M2022AirBridge/config.json`:

```json
{
  "printer": { "name": "Samsung M2022", "location": "Study", "usb": { "vid": "0x04e8", "pid": "TBD", "serial": "ZF45B8GF3C01YSD" } },
  "network": { "port": 8000, "bind": "any", "tls": true, "web_ui": "localhost" },
  "defaults": { "preset": "normal", "media": "iso_a4_210x297mm", "toner_save": false },
  "limits": { "max_job_bytes": 268435456, "max_queued_jobs": 20, "offline_timeout_s": 3600 }
}
```

No secrets. Schema validated on load with a test for every rejected shape.

### 6.11 Security model

Home-LAN appliance, defence in depth anyway:

1. Listens on LAN interfaces only; no UPnP, no port forwarding, no Internet exposure. Documented firewall rule for port 8000.
2. Web UI on localhost by default; enabling it on the LAN requires the PAPPL admin password.
3. Runs unprivileged. Only the installer needs `sudo`.
4. Input is untrusted: PAPPL parses raster and image formats; our modules receive lines and lengths and bounds-check everything. Fuzz targets for the halftone, band splitter, and band codec (they must never crash on any raster) and for the config parser.
5. Job size and queue depth limits from config.
6. Temporary files created with restrictive permissions and removed on completion or failure.
7. Job names and client addresses sanitised in logs.
8. TLS enabled (self-signed by PAPPL); `_ipps._tcp` advertised alongside `_ipp._tcp`.

---

## 7. Quality program

### 7.1 Presets

| Preset | Resolution | Halftone | Tone | Toner save |
|---|---|---|---|---|
| Draft | 300 dpi upscaled or 600 | Bayer ordered | light | on |
| Normal | 600 | blue-noise mask | calibrated | off |
| Text | 600 | threshold + edge-aware | high contrast | off |
| Photo | 600 | Ostromoukhov error diffusion or clustered-dot | calibrated with dot gain | off |

`print-quality` draft/normal/high map to Draft/Normal/Text; Photo is selected by the `print-content-optimize` attribute (`photo`) or the vendor option. A High preset at 1200 dpi, mirroring the vendor's Best mode, is added once M9 validates 1200 dpi on hardware.

### 7.2 Test sheet

One A4 page, generated by the fixture tool, containing: 4–12 pt serif and sans text, reversed white-on-black 6 pt text, 1 px and 2 px lines horizontal/vertical/diagonal, a QR code, a Code 128 barcode, a 16-step gray ramp, a continuous gray wedge, a photograph, a fine checkerboard.

### 7.3 Method

Print the sheet through (a) the vendor driver while it still exists, (b) SpliX on a Linux box or VM if available, (c) each of our presets. Scan at 1200 dpi. Score: legibility of small text, line continuity, barcode/QR decodability with a scanner app, ramp monotonicity and step count, photo artefacts. Keep the scans in `artifacts/quality/` with the settings used.

### 7.4 Performance targets (600 dpi A4 text page, M1-class Mac)

| Metric | Target |
|---|---|
| Time from job receipt to first byte on USB | < 1.5 s |
| Raster + halftone + compression + framing throughput | > 10 pages/min, i.e. never the bottleneck behind the 20 ppm engine |
| Peak RSS of the daemon during a 50-page job | < 200 MB |
| Job size, text page, 0x11 | comparable to vendor output (fixtures give the number) |

---

## 8. Test strategy

### 8.1 Unit tests (CTest, minimal in-repo assert harness, no framework)

- `halftone/`: each algorithm on 8×8 and 64×64 references; determinism; monotonic ramp behaviour.
- `raster/`: colour conversion, scaling, cropping, margin maths.
- `qpdl/codec11`: decode every vendor band (checksum, geometry); encode→decode round trip on every fixture band and on random data; token edge cases (runs of 64 literals, matches of 3 and of 514 bytes, table-index bounds).
- `qpdl/`: record layouts, band width table for every media size, PJL text, copies, packet chunking, end-of-job.
- `config/`: schema acceptance and rejection.
- `usb/`: protocol state machine with a fake device.

### 8.2 Golden fixtures (`fixtures/`)

Pages: blank, black square, horizontal lines, vertical lines, checkerboard, gray ramp, small text, unicode text, photo, the test sheet. Each in A4 and Letter, 600 dpi, as PWG raster (what PAPPL hands us) and as the 1-bit bitmap after Normal preset.

Oracles captured for each page on 2026-09-02 (27 jobs plus the 14-size media sweep, 14 MB):

```text
fixtures/oracle/samsung/<page>-<media>.spl     vendor output
fixtures/oracle/splix/<page>-<media>.qpdl      SpliX output (built from source, run as a separate process)
```

Capture method for the vendor driver, both variants kept as scripts:

1. Filter chain by hand: `cupsfilter -p <ppd> -m application/vnd.cups-raster page.pdf > page.ras`, then run `rastertosec` with CUPS filter arguments and `PPD=` set, capturing stdout.
2. A temporary CUPS queue with a `file:///path/out.spl` device URI (requires `FileDevice Yes` in `cups-files.conf`), which captures exactly what `cupsd` would send, PJL included. Removed after capture.

Tests compare our output to the vendor output structurally (record sequence, band geometry, header fields, PJL lines that matter) and byte-exactly where the fixtures show the vendor is deterministic.

### 8.3 Integration tests (no hardware)

- Start the server on a random port with the `file://` device; discover it with `ippfind`; validate attributes with `ipptool` and the IPP Everywhere self-certification tests from the PWG; submit PWG raster, Apple raster, JPEG, PNG; assert job state transitions and the captured QPDL matches the golden file.
- Run the same against `ippeveprinter` to confirm the test harness itself is sound.

### 8.4 Hardware tests (`ctest -L hardware`)

Replay a captured vendor job; print each fixture page; unplug during a job; replug; power-cycle; sleep/wake the Mac; 100 sequential one-page jobs; 20 multi-page jobs; cancel mid-job; malformed job followed by a good one.

### 8.5 Fuzzing

libFuzzer targets for the halftone entry points, band splitter, 0x11 codec, QPDL decoder, and config parser, run in CI for a fixed budget.

### 8.6 CI

GitHub Actions on `macos-latest` (arm64): build, unit, integration, fuzz smoke. Hardware tests run locally only and record results into `artifacts/`.

---

## 9. Milestones

Each milestone ends with code that stays in the product or the test suite. Each lists what it teaches. Live status, the exact next task and the session log are in `PROGRESS.md`; the per-session working rules, including what to update when a task finishes, are in `CLAUDE.md`.

### M0 — Environment probe and vendor capture. Done 2026-09-02, one remainder

Done: probe (section 2), fixture pages, vendor oracle capture for every page, option variant and paper size (section 2.8), PPD/options/IPP attributes preserved, driver package backed up, first structural decode (`scripts/spl-survey.py`, `docs/spl-qpdl.md`).

The VID/PID and 1284 device ID were recorded the same day with the printer on. M0 is complete.

Teaches: how CUPS filter chains work, what the vendor driver actually emits, PJL.

### M1 — Repository, build, USB transport, replay

- CMake + Ninja project, C17, warnings as errors, sanitizers in debug, CTest wired.
- `usb/` module with libusb: enumerate, match by VID/PID/serial, claim, bulk write, port status, device ID, soft reset.
- `m2022-airbridge probe --json` (replaces the shell script; uses IOKit/libusb and libcups).
- `m2022-airbridge send FILE` replays a captured vendor job.

Acceptance: a captured vendor job replayed through our transport prints correctly. That proves the transport, the fixture, and USB ownership on macOS 26 in one step.

Teaches: USB printer class, libusb on macOS, IOKit matching, IEEE 1284 device IDs.

### M2 — Printer Application skeleton

- PAPPL 1.4.12 pinned and built by the project's build script.
- Driver data per 6.1; callbacks that write the incoming raster to disk (`capture` device) and, for `printfile`, forward to `usb/`.
- Service can run in the foreground on port 8000.

Acceptance: iPhone, iPad, and the Mac list the printer; a job from each lands as a PWG/Apple raster file on disk with the resolution and colour space we asked for; `ipptool` attribute tests pass; `ippfind` shows `_universal` and a correct `URF` key. Record which format and resolution each client actually sends.

**Done 2026-09-02.** iPhone and Mac (Bonjour entry in the print dialog, and Chrome's IPP Everywhere entry) both list the printer and their jobs arrive as Apple Raster sGray 8-bit at 600 dpi; `ipptool` passes; `dns-sd` shows the `_universal` sub-type and the URF key. Findings in `docs/ipp-airprint.md`. This is **v0.2**.

Teaches: IPP model and encoding, DNS-SD/AirPrint TXT semantics, PWG and Apple raster formats, how PAPPL structures a Printer Application.

### M3 — Raster and halftone pipeline

- `raster/` and `halftone/` per 6.2 and 6.3, with `render` CLI and PBM/PNG debug output.
- Unit tests and references.

Acceptance: every fixture renders deterministically under every preset; the test sheet looks right on screen at 600 dpi.

**Done 2026-09-02.** `raster/` and `halftone/` with 12 passing suites; `render` at 70–115 Mpx/s (Floyd–Steinberg 25 Mpx/s). Measured against the vendor on its own input raster: Text preset agrees with the vendor's text bitmap on 99.99 % of pixels (recall 1.000); the black square matches exactly; on the gray ramp our Normal preset lays down 12.3 % ink where the vendor's screen lays 10.7 %, the calibration gap for M10. `docs/raster.md`, `docs/halftone.md`.

Teaches: colour science basics, gamma and dot gain, ordered dithering, error diffusion, blue-noise masks and void-and-cluster.

### M4 — Band codec 0x11

Per 6.5: decoder first, then encoder, plus the column-major band transform and the checksum.

Acceptance: every vendor band in the fixtures decodes and its checksum verifies; decoded bands assembled into a page bitmap match the page content (PBM inspection, and pixel agreement with our own halftone of the same raster up to the expected halftone differences); encode→decode round trip is exact on every fixture band and on random data; fuzz target clean.

Teaches: LZ77-family compression, offset tables, literal and match coding, checksums, and why a column-major layout compresses laser bands well.

### M5 — QPDL encoder and decoder

Per 6.4, plus `decode`. The band-width rule and paper codes are already derived (2.8); every encoder table entry cites its fixture.

Acceptance: `decode` explains every byte of a vendor fixture; our `encode` of the same page matches the vendor structurally, and byte-exactly for the deterministic parts; SpliX output decodes with our decoder.

Teaches: proprietary printer languages, PJL, reverse engineering from fixtures with a reference implementation.

### M6 — First native print

Wire M3–M5 behind the PAPPL raster callbacks; replace the `capture` device with `usb/`.

Acceptance: blank page, black square, lines, text, and the test sheet print correctly from the iPhone and from the Mac with no vendor code on the machine. This is **v0.3**.

### M7 — Service, installer, doctor

Per 6.7–6.9. Samsung queue removed and backed up; LaunchDaemon; dedicated user; `doctor`; uninstall with rollback.

Acceptance: fresh install on this Mac from the `.pkg`; printer visible after reboot without any manual step; uninstall restores the vendor queue.

Teaches: launchd, macOS users and permissions, package building, Unified Logging.

### M8 — Reliability

Hardware test list 8.4 in full. Fix what breaks. Soak: 100 one-page jobs, 20 multi-page jobs, a week of daily use.

Acceptance: all hardware tests pass; no duplicate DNS-SD instances after sleep/wake or network change; no wedged jobs. This is **v1.0**: the vendor driver is deleted from the Mac.

### M9 — Status and options

Manual duplex flow; PJL status back-channel investigation; paper-out, cover-open, jam reporting to `printer-state-reasons`; toner level if available; 1200 dpi experiment on hardware, kept only if it prints cleanly.

### M10 — Quality

Section 7 in full: presets, calibration from scans, edge-aware mode, comparison scoring against the vendor scans. This is **v1.5** when our Normal and Text presets score at or above the vendor driver.

### M11 — Release engineering

Signed and notarized `.pkg`, Homebrew tap, CHANGELOG, IPP Everywhere self-certification run recorded, and the documentation finished as a presentable whole: `README.md` for newcomers, `docs/` explaining every module (index in `docs/README.md`), each with a "What this teaches" section. Documentation is written as modules are built, not at the end. **v2.0** candidates after this: server-side PDF via CoreGraphics (macOS) or MuPDF (Linux), Linux/Raspberry Pi port of the same binary, menu-bar status app.

---

## 10. Learning track

Reading and reference per area. Primary sources first.

**IPP.** RFC 8010 (encoding), RFC 8011 (model and semantics), PWG 5100.14 IPP Everywhere v1.1, PWG 5100.13 (job and printer extensions), IPP Everywhere self-certification tools. Exercise: read `ipptool`'s `get-printer-attributes.test` and explain every attribute our printer reports.

**Discovery.** RFC 6762 (mDNS), RFC 6763 (DNS-SD), Apple "Bonjour Printing Specification" v1.2.1 (defines every TXT key AirPrint reads). Exercise: diff our TXT record against `ippeveprinter` and against `AirPrint_Bridge`'s.

**Raster formats.** PWG 5102.4 (PWG Raster), CUPS raster (`cups/raster.h`, CUPS "Raster API" docs), Apple Raster/URF as implemented in CUPS `cupsRasterReadHeader` and cups-filters `rastertopdf`. Exercise: write `decode` support for both headers before relying on PAPPL's.

**Halftoning.** Ulichney, *Digital Halftoning* (MIT Press, 1987); Floyd & Steinberg (1976); Ulichney, "The void-and-cluster method for dither array generation" (1993); Ostromoukhov, "A simple and efficient error-diffusion algorithm" (SIGGRAPH 2001). Exercise: reproduce the paper figures with our code.

**Band compression.** The SpliX SPL2 document (`doc` branch, `specs-en/bandes.tex`) describes the 0x11 scheme; Ziv and Lempel (1977) for the family it belongs to. Exercise: hand-decode one vendor band with pencil and paper, then check with `decode`.

**JBIG (optional).** ITU-T T.82/T.85 and jbigkit, only if the 0x15 path is ever pursued.

**SPL/QPDL and PJL.** SpliX specification page (`openprinting.github.io/splix/specs.html`), SpliX source as a reading exercise, HP *PJL Technical Reference Manual*. Exercise: annotate one captured vendor job byte by byte in `docs/spl-qpdl.md`.

**USB.** USB *Device Class Definition for Printing Devices* 1.1, IEEE 1284 device ID string format, libusb API docs, Apple IOKit USB fundamentals. Exercise: dump the descriptor tree and explain each field.

**PAPPL.** The PAPPL programming manual (`msweet.org/pappl`), the `ps-printer-app` and `pappl-retrofit` sources as worked examples.

**macOS platform.** launchd.plist(5), Unified Logging (`os_log`), `pkgbuild`/`productbuild`, code signing and notarization guides.

Each milestone's `docs/` entry ends with a short "what I learned" section written by the developer, not generated.

---

## 11. Technology choices

- **Language:** C17. Clean, portable, what PAPPL and libcups expect. C++ is not needed.
- **Build:** CMake ≥ 3.25 with Ninja; `-Wall -Wextra -Werror`, ASan/UBSan in debug, `clang-format` and `clang-tidy` in CI.
- **PAPPL:** v1.4.12 pinned as a git submodule, built by `scripts/build-pappl.sh` with its autoconf, installed into the build tree. Do not track `master` (needs libcups 3 / CUPS 2.5).
- **Runtime deps (Homebrew):** `libusb`, `jpeg-turbo`, `libpng`, `openssl@3`, `pkg-config`; system: libcups 2.3.4 with Xcode SDK headers, mDNSResponder, zlib.
- **Test-only deps:** SpliX built from source into `third_party/oracles/` and executed as a separate process, `ipptool`/`ippfind`/`ippeveprinter` from macOS, Python 3 for fixture generation scripts.
- **Setup:** `brew install cmake ninja pkg-config jpeg-turbo libpng openssl@3` (libusb already present; `jbigkit` only for the optional JBIG path).
- **Targets:** arm64 macOS primary; x86_64 macOS kept building (universal binary is one CMake flag); Linux arm64 as a v2 target, which is why core modules stay POSIX-only.

---

## 12. Repository structure

```text
m2022-airbridge/
├── README.md                    presentable entry point
├── SPEC.md                      this document
├── PROGRESS.md                  live status, next task, session log
├── CLAUDE.md                    working agreement for coding sessions
├── LICENSE                      MIT
├── CMakeLists.txt
├── cmake/
├── third_party/
│   ├── pappl/                   submodule, v1.4.12
│   └── oracles/                 SpliX checkout and build script; never linked
├── src/
│   ├── cli/                     command parsing, all subcommands
│   ├── app/                     PAPPL system + driver callbacks
│   ├── raster/                  ingest, scale, tone
│   ├── halftone/                algorithms and masks
│   ├── qpdl/                    encoder, decoder, band codec 0x11, tables, PJL
│   ├── usb/                     libusb transport, status
│   ├── service/                 launchd, paths, user, logging adapter
│   ├── doctor/
│   └── config/
├── include/m2022/               public headers of the pure modules
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── hardware/
│   └── fuzz/
├── fixtures/
│   ├── pages/                   PDF sources and generated PWG/PBM
│   └── oracle/{samsung,splix}/
├── scripts/
│   ├── build-pappl.sh
│   ├── capture-vendor-output.sh
│   ├── gen-fixtures.py
│   └── gen-bluenoise.py
├── packaging/macos/{launchd,pkg,homebrew}/
├── docs/
│   ├── architecture.md, ipp-airprint.md, raster.md, halftone.md,
│   ├── spl-qpdl.md, usb.md, macos-service.md, debugging.md
│   └── adr/
└── artifacts/                   probe output, scans, soak logs (gitignored except .gitkeep)
```

---

## 13. Licensing and clean-room rules

**Project license: MIT.**

Why MIT rather than Apache-2.0: if a JBIG path is ever needed and we decide to link jbigkit (GPL-2.0), the project can be relicensed to GPL-2.0 without friction; Apache-2.0 is not GPL-2.0-compatible. Why not GPL from the start: the encoders are the educational core and we want the freedom to publish them permissively.

Rules:

1. No SpliX or jbigkit source is copied, translated, or paraphrased into `src/`. They are built in `third_party/oracles/` and run as separate processes or linked only into test binaries.
2. The Samsung driver is never redistributed. Captured output fixtures are ours to keep; the vendor filter binary is not committed.
3. Protocol knowledge comes from: published specs (T.82, T.85, PJL, USB printer class, PWG, IETF), the SpliX specification page, and our own analysis of captured bytes. Cite the source of every table entry in a comment.
4. `docs/adr/0004-licensing.md` records this and any later change.

---

## 14. Decision records

- **ADR-001 — Build the Printer Application directly; no interim bridge.** Supersedes v1 ADR-001. Every milestone ships product code. Prior-art bridges are reference material only.
- **ADR-002 — Standards-shaped client interface.** IPP Everywhere plus AirPrint attributes over DNS-SD. No client software.
- **ADR-003 — Raster-only inputs in v1.** Apple Raster, PWG Raster, JPEG, PNG. Clients rasterise PDF themselves at the 600 dpi we advertise, which is how real AirPrint printers work and removes a PDF engine from the trust boundary. Server-side PDF (CoreGraphics on macOS, MuPDF on Linux) is a v2 quality feature behind an interface.
- **ADR-004 — PAPPL 1.4.x for IPP, DNS-SD, spooling, TLS, and web UI.** Pinned. Migration to PAPPL 2.x when macOS's CUPS or a bundled libcups 3 allows it.
- **ADR-005 — Pure encoder modules.** `raster/`, `halftone/` and `qpdl/` are I/O-free, deterministic C modules that take memory in and call a write callback out. They know nothing about IPP, files, USB, PAPPL or macOS.
- **ADR-006 — Single USB owner.** The Printer Application owns the device; the installer removes and backs up the Samsung CUPS queue, uninstall restores it.
- **ADR-007 — 600 dpi is the baseline.** 1200 dpi is an experiment in M9 with hardware evidence required to keep it.
- **ADR-008 — macOS arm64 first, portable core.** Apple APIs only in adapters. Linux/Raspberry Pi is a v2 target of the same code.
- **ADR-009 — Unprivileged service.** Dedicated user, port 8000, localhost web UI, TLS on.
- **ADR-010 — Clean-room, MIT.** Section 13.

---

## 15. Risks

| # | Risk | Likelihood | Mitigation |
|---|---|---|---|
| 1 | libusb cannot claim the printer on macOS 26, or a system component holds it | low–medium | M1 tests this first, before any encoder work; IOKit-direct transport is the fallback and the same interface |
| 2 | The engine rejects anything but exact band widths (SpliX #18) | resolved on paper | rule ceil(width/256)×256 verified on all 14 sizes in vendor output; hardware test per size in M8 |
| 3 | 0x11 codec details: offset-table choice, match strategy, checksum scope | low | documented by SpliX and visible in 27 captured jobs; the decoder validates every vendor band before the encoder is written |
| 4 | Apple Raster from iOS arrives at 300 dpi regardless of our advertisement | medium | M2 measures it; upscale path exists; try `RS600`-only advertisement if needed |
| 5 | QPDL v3 details differ from the SpliX spec page for this model | medium | fixtures are ground truth; spec page is a starting point |
| 6 | Printer status is one-way and we cannot report paper-out or jams | medium | port status bits are always available; PJL back-channel investigated in M9; UI copy is honest about what is known |
| 7 | PAPPL 1.4 API churn or macOS build breakage | low | pinned submodule; build script; CI on macOS |
| 8 | Vendor driver stops working before fixtures are captured | low but fatal | capture fixtures first, this week, and back up the driver package |
| 9 | macOS 28 arrives before v1.0 | schedule | v1.0 target is spring 2027; hold the host on macOS 27 if needed |
| 10 | 1200 dpi prints badly on this engine | medium | the vendor emits it in Best mode, so the format is known; the M9 hardware test decides |
| 11 | Scope creep from the learning goal | medium | learning exercises live in `docs/`, not in `src/`; milestones gate features |

---

## 16. Open questions (to be answered by M0-remainder, M1, M2)

1. Answered 2026-09-02: VID 0x04E8, PID 0x3321; device ID in 2.2.
2. Answered 2026-09-02: no JBIG; 0x11 in every band (2.8).
3. Answered 2026-09-02: ceil(raster width / 256) × 256 (2.8).
4. Does `PacketSize 512` reflect USB packetisation the printer needs, or a driver artefact? Nothing in the byte stream is 512-aligned; the bulk OUT endpoint's wMaxPacketSize is 512, which is probably what SpliX means.
5. Which of the observed PJL `SET` lines (`XIGNOREFF`, `RESOLUTION`, `BITSPERPIXEL`, `PAPERTYPE`, `DUPLEX`, `BINDING`) the printer requires versus ignores.
6. macOS 26 answered 2026-09-02: Apple Raster (`image/urf`), sGray 8-bit; resolution follows print quality when several are advertised (300 for normal, 600 for high), so we advertise `RS600` only. iOS 26 answered the same day: Apple Raster, sGray 8-bit, 600 dpi, over a TLS-upgraded connection. iPadOS not tested separately (same stack).
7. Answered 2026-09-02: yes. `probe` and `send` claim interface 0 as a normal user (ADR-009 verified).
8. Does the printer answer PJL `INFO STATUS` / `USTATUS` on bulk IN?
9. Does any 1200 dpi mode print cleanly?
10. Toner level: CUPS shows `marker-levels = 48`, so yes; through which channel?
11. Does the printer honour the page-footer copies field, or must pages be repeated as CUPS does?
12. Answered 2026-09-02: the vendor changes the offset table per band, and the black-square job with 3 different tables printed; any table works.

---

## 17. Definition of success

| Version | Criterion |
|---|---|
| v0.1 | A captured vendor job prints through our USB transport (`send`). |
| v0.2 | iPhone, iPad, and Mac discover the Printer Application and their jobs are captured as raster with the expected parameters. |
| v0.3 | The test sheet prints from the iPhone with no vendor code on the machine. |
| v1.0 | Installed service; all hardware and reliability tests pass; the vendor driver is deleted from the Mac; a week of daily use without intervention. |
| v1.5 | Normal and Text presets score at or above the vendor driver on the test sheet scans. |
| v2.0 | Server-side PDF rendering, Linux/Raspberry Pi build of the same binary, and any presets that measurably beat the vendor driver. |

---

## 18. First tasks for the coding agent

Work in this order. Stop and report after each, with real command output.

1. **M0 remainder.** Done 2026-09-02, including VID/PID and the device ID.
2. **M1 scaffold.** Done 2026-09-02 (CMake + Ninja, C17, warnings as errors, CTest, `clang-format`, README, ADRs 0001–0010).
3. **M1 USB transport.** Done 2026-09-02: `usb/` with libusb (enumerate, match by VID/PID/serial, claim, bulk write/read, device ID, port status, soft reset), pure IEEE 1284 and status helpers with unit tests, `probe [--json]` through libusb and libcups, `send`. Hardware acceptance: `send fixtures/oracle/samsung/black-square-a4.spl` printed the black square (confirmed 2026-09-02). This is **v0.1**.
4. **M1 decode groundwork.** Done 2026-09-02: `qpdl/` module (records, walker, 0x11 decoder, band layout) and `decode FILE [--pbm PREFIX]`; all 42 captured jobs and 867 bands decode with verified checksums; the black-square geometry and the decoded pages check out visually.

Suggested prompt:

> Read `SPEC.md` completely. We are building a production Printer Application, not a bridge. Implement only the M0 remainder and M1 as listed in section 18. Do not touch the halftone or QPDL encoders yet. Use libusb and libcups APIs, not shell parsing, in product code. Every module gets unit tests in the same change. Do not modify CUPS configuration or the Samsung queue; the installer in M7 will. When a step needs the physical printer, say exactly what to do and what output to paste back. End with the list of section 16 questions you were able to answer, with evidence.

---

## 19. References

Standards and primary documentation

1. IETF RFC 8010, RFC 8011 — Internet Printing Protocol/1.1
2. PWG 5100.14 — IPP Everywhere v1.1; PWG 5102.4 — PWG Raster Format
3. IETF RFC 6762 (mDNS), RFC 6763 (DNS-SD); Apple Bonjour Printing Specification v1.2.1
4. ITU-T T.82 (JBIG1), ITU-T T.85 (JBIG fax profile)
5. USB-IF Device Class Definition for Printing Devices 1.1; IEEE 1284
6. HP PJL Technical Reference Manual
7. PAPPL — https://www.msweet.org/pappl/ and https://github.com/michaelrsweet/pappl (v1.4.12, 2026-08-20)
8. CUPS Raster API and `cups/raster.h`

Reference implementations and prior art

9. OpenPrinting SpliX — https://github.com/OpenPrinting/splix (2.0.2, 2026-07-16); spec page https://openprinting.github.io/splix/specs.html; issue #18 (band width), PR #14 (ML-186x JBIG/1200 dpi findings)
10. jbigkit — Markus Kuhn, https://www.cl.cam.ac.uk/~mgk25/jbigkit/
11. OpenPrinting ghostscript-printer-app — https://github.com/OpenPrinting/ghostscript-printer-app
12. OpenPrinting pappl-retrofit — https://github.com/OpenPrinting/pappl-retrofit
13. sapireli/AirPrint_Bridge — https://github.com/sapireli/AirPrint_Bridge
14. cups-filters `rastertopdf` (Apple Raster reader) — https://github.com/OpenPrinting/cups-filters

Platform

15. Apple — Using Intel-based apps on a Mac with Apple silicon — https://support.apple.com/en-us/102527
16. MacRumors, 2026-06-10 — macOS 27 is the last release with full Rosetta 2 support
17. Apple launchd, Unified Logging, notarization documentation

Halftoning

18. R. Ulichney, *Digital Halftoning*, MIT Press, 1987
19. R. Floyd, L. Steinberg, "An adaptive algorithm for spatial gray scale", 1976
20. R. Ulichney, "The void-and-cluster method for dither array generation", 1993
21. V. Ostromoukhov, "A simple and efficient error-diffusion algorithm", SIGGRAPH 2001

Hardware

22. Samsung SL-M2022 specifications — https://www.samsung.com/sec/support/model/SL-M2022/
23. HP support, Samsung Xpress SL-M2022 — https://support.hp.com/bg-en/product/details/samsung-xpress-sl-m2022-laser-printer-series/model/17157281
