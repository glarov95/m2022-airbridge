# Progress

Read this first in every session. It says where we are, what is next, and what to update when
something is finished. The design is `SPEC.md`; the working rules are `CLAUDE.md`.

## Where we are

- **Milestone:** M6 complete, **v0.3**: the iPhone and the Mac print through the Printer
  Application (2026-09-02), over our own IPP front end, raster pipeline, halftone, band
  codec, job encoder and USB transport. The Mac printer was added through System Settings
  as an AirPrint printer ("Samsung M2022"). Next: M7, the launchd service, installer and
  doctor, so it runs without a terminal.
- **Last update:** 2026-09-02.
- **Hardware:** Samsung SL-M2022 on USB (04e8:3321, serial ZF45B8GF3C01YSD). The vendor CUPS
  queue `Samsung_M2020_Series` is still installed and idle; M7 removes it.
- **Running it today:** `./build/src/m2022-airbridge server --spool build/run/spool --log
  build/run/server.log` (the USB printer is the default device); stop with
  `pkill -f 'm2022-airbridge server'`.

## Next up

**M7 — Service, installer, doctor** (SPEC.md 6.7, 6.8, 6.9; docs/architecture.md; ADR-006).

1. `service/` adapter: a launchd LaunchAgent (or LaunchDaemon with a dedicated user; decide
   in an ADR: the LaunchAgent is simpler and libusb works unprivileged, ADR-009) that runs
   `m2022-airbridge server` at login/boot with `--spool` and `--log` under
   `~/Library/Application Support/M2022 AirBridge/` and logs under `~/Library/Logs/`; restart
   on crash (KeepAlive); state file for PAPPL (`papplSystemSetStateFile`?) so the printer
   keeps its ID and settings across restarts.
2. `install` / `uninstall` commands (or `scripts/install.sh`): copy the binary to
   `/usr/local/bin` or `~/Library/...`, write the plist, `launchctl bootstrap`, verify with
   `probe`; uninstall reverses it. Removing the vendor CUPS queue (`lpadmin -x
   Samsung_M2020_Series`) and offering to remove the vendor driver package are separate,
   explicit steps that ask first (ADR-006; keep `artifacts/vendor-driver-backup/`).
3. `doctor` command: checks the binary, the plist, the running service (launchctl print),
   the USB printer (probe), the DNS-SD advertisement (`dns-sd -B _ipp._tcp,_universal`),
   the IPP endpoint (Get-Printer-Attributes on localhost), and prints one line per check
   with a fix hint.
4. Unified Logging or the PAPPL log file: decide; at least rotate the log.
5. Tests: unit tests for the plist generation and doctor checks that are pure; a manual
   checklist for install/uninstall in docs/macos-service.md (planned doc, index already
   lists it). Hardware: after install, print from the iPhone with no terminal open, then
   reboot and print again.
6. Open from M6: the state file so the printer ID stays stable across restarts.

## Milestones

| # | Milestone | Status | Done | Commits |
|---|---|---|---|---|
| M0 | Environment probe and vendor capture | done | 2026-09-02 | 6e96237 |
| M1 | Repository, USB transport, replay, decoder | done, v0.1 | 2026-09-02 | 1b73582, 670be60 |
| M2 | PAPPL skeleton with capture device | done, v0.2 | 2026-09-02 | a8c6b5b + follow-up |
| M3 | Raster and halftone pipeline | done | 2026-09-02 | 5e18397 |
| M4 | Band codec 0x11 encoder | done | 2026-09-02 | f22d645 |
| M5 | QPDL encoder and `encode`, first native print | done | 2026-09-02 | 74c2e6a |
| M6 | iPhone prints through the Printer Application (v0.3) | done | 2026-09-02 | (this commit) |
| M7 | Service, installer, doctor | next | | |
| M8 | Reliability soak (v1.0) | todo | | |
| M9 | Status, manual duplex, 1200 dpi experiment | todo | | |
| M10 | Quality program (v1.5) | todo | | |
| M11 | Release engineering, documentation site | todo | | |

## Facts established (details in `docs/`)

- The printer's language is fully decoded and verified against 42 captured jobs, 867 bands:
  `docs/spl-qpdl.md`. Compression is 0x11 (not JBIG), band width = ceil(raster width / 256) × 256,
  offset tables vary per band, "Best" is a real 1200 dpi mode.
- Our 0x11 encoder follows the vendor's raw-prefix rule (leading literals, 64..128 bytes) and
  picks the offset table per band; on all 867 vendor bands it needs 0.76 of the vendor's
  bytes and never more for a band. Ordered screens compress 4× better than the vendor's
  output, blue noise costs 2–2.5×, Floyd–Steinberg 6.8× (740 KB photo page): docs/spl-qpdl.md
  3.3–3.4, SPEC.md risk 12 and question 13.
- Our job encoder writes the vendor's envelope and records: page headers byte-identical for
  all 14 sizes and at 1200 dpi (paper size = points × 300/72 rounded half up), same band
  numbers as the vendor for the black square and the text page. Both printed correctly from
  `encode` + `send` on 2026-09-02: the first native print. A 743 KB Floyd–Steinberg photo
  page (largest vendor job: 521 KB) printed correctly too: page size is bandwidth only.
- The whole path works end to end: iOS sends Apple Raster at the full page size (A4
  4960×7015), we crop to the PPD imageable area at (104,104), halftone with the preset from
  the job options, encode and write over USB through our own PAPPL device scheme; 53 bands,
  440 KB, 1.1 s for a phone page. Copies go into the page records; the printer honours
  the field (three sheets for copies 3).
  docs/ipp-airprint.md, docs/usb.md.
- USB access works unprivileged with libusb; the device ID advertises `URF`: `docs/usb.md`.
- The vendor driver is Intel-only and stops working with macOS 28 (fall 2027). Its package is
  backed up under `artifacts/vendor-driver-backup/` (gitignored: keep a copy elsewhere too).
- Open hardware questions live in SPEC.md section 16 (PJL lines required, copies field, 1200 dpi
  print quality, toner channel, raw URF over USB).

## Session log

- **2026-09-02 (M6)** — `src/app/app.c`: the real raster callbacks (gray → crop to the
  imageable area → tone → halftone → job encoder → device, one line at a time; copies in the
  page records, which the printer honours: three sheets for copies 3; status callback from
  the port status byte).
  `src/app/usbdev.c`: PAPPL device scheme `m2022usb://` over `src/usb/`, now the server's
  default device. `src/app/jobmap.c` + `test_jobmap`: quality/content → preset, media type →
  PAPERTYPE, source → feeder, geometry from the PPD imageable areas (added per size to the
  media table; verified against all 14 vendor rasters). `tests/integration/print-raster.sh`:
  ipptool → server with a file device → the vendor's band structure, no hardware. Learned:
  PAPPL's logger has no `%zu`. Hardware: black square through the server, then the iPhone
  page: perfect (v0.3); the Mac, added as an AirPrint printer, printed a two-page document
  (619 KB, 1.9 s). Copies 3 in the records gave three sheets (question 11). Supplies: the
  vendor's `commandtosec` reports the toner level (46 %) without touching the print pipe;
  it links IOKit and carries `EP0 command` and `@PJL LITESMSTATUS` strings, so the level
  comes over a vendor USB control request (question 10, M9). 17 suites pass.
- **2026-09-02 (M5)** — `src/qpdl/encode.c`: streaming job encoder (begin job / page, write
  line, end page / job) with the vendor's PJL envelope, page header from the media table,
  blank-band omission, per-band 0x11 tables, `m2022_qpdl_paper_dots` (rounding fixed: half
  up, verified on all 14 sizes). `encode` command; `render` and `encode` share
  `src/cli/pipeline.c` (input loading, halftone options). `test_qpdl_encode`: envelope text,
  all media headers against the sweep, 1200 dpi header, band mechanics, errors, whole pages
  against the vendor (black square identical, text 99.99 %). Hardware: native black square
  and text page printed correctly (first native print); the 743 KB Floyd–Steinberg photo
  page printed correctly as well (SPEC question 13 answered). Hardware
  test `hw_native_black_square` added. 15 suites pass.
- **2026-09-02 (M4)** — `m2022_codec11_encode` (greedy longest match over the 64 table
  distances, vendor raw-prefix rule, little-endian header, payload checksum),
  `m2022_codec11_encode_bound`, `m2022_codec11_choose_table` (3-byte hash of recent positions
  plus the byte-columns to the left as candidates; the columns were essential: without them
  the checkerboard band was 5× the vendor's), `m2022_codec11_default_table`. Tests:
  `test_codec11` (header layout, raw prefix, bounds, table choice), `test_codec11_fuzz`
  (1 500 round trips, 6 000 damaged payloads), `test_qpdl_fixtures` re-encodes every vendor
  band (0.764 of the vendor's bytes, worst band 1.00), `test_codec11_pipeline` (our presets
  through the codec, sizes against the vendor). Survey of the vendor's encoder habits written
  into docs/spl-qpdl.md 3.3. 14 suites pass.
- **2026-09-02 (later)** — M2: PAPPL v1.4.12 submodule + `scripts/build-pappl.sh` (arm64 static;
  configure defaults to universal, pinned via CFLAGS/LDFLAGS); `src/app/` with driver data per
  SPEC 6.1 and capture callbacks; `server` command; media table shared with the encoder
  (`src/app/media.c`, unit-tested against libcups). Measured: macOS sends Apple Raster sGray 8-bit;
  resolution follows print quality when several are advertised (now 600 only); advertising srgb_8
  makes macOS send print-color-mode=color (now gray only); PAPPL streams raster jobs (no spool
  file); page indices are 0-based. `docs/ipp-airprint.md` written. iPhone test: job received
  over TLS as Apple Raster sGray 8-bit 600 dpi; Mac print dialog (Bonjour entry) likewise. M2
  done (v0.2). The temporary `M2022test` CUPS queue was removed.
- **2026-09-02 (M3)** — `raster/` (formats → sGray, sRGB transfer, tone LUT with dot-gain
  compensation, fit/upscale, CUPS raster header) and `halftone/` (threshold, Bayer 4/8, generated
  45° clustered-dot screen, void-and-cluster blue-noise mask, Floyd–Steinberg serpentine; presets
  draft/normal/text/photo/vendor); `render` command; file helpers (gzip-aware) and PNM parsing.
  Tests: tone, raster, halftone, and the vendor comparison (text 99.99 % agreement, ramp ink
  12.3 % vs 10.7 %). docs/raster.md, docs/halftone.md.

- **2026-09-02** — Spec v2 written from a read-only probe. Fixture pages generated; vendor output
  captured for 9 pages × 2 sizes, 8 option variants and a 14-size media sweep; driver package
  backed up. Repository scaffold. USB transport; replaying `black-square-a4.spl` printed
  (v0.1). QPDL decoder; all fixtures decode with verified checksums; decoded pages checked
  visually. Commits 6e96237, 1b73582, 670be60. Machine changes: Homebrew installed cmake,
  ninja, pkg-config, jpeg-turbo, libpng, openssl@3, jbigkit; `/Library/Caches/com.sec.printer`
  created so the vendor filter survives the manual-duplex variant.

## Environment notes

- Build: `export PATH=/opt/homebrew/bin:$PATH; cmake -S . -B build -G Ninja; cmake --build build;
  ctest --test-dir build --output-on-failure -LE hardware`
- Hardware tests print pages: `ctest --test-dir build -L hardware` only with the printer on.
- Run the application for client tests: `./build/src/m2022-airbridge server --capture artifacts/capture --spool build/run/spool --log build/run/server.log`; stop with `pkill -f 'm2022-airbridge server'`.
- Recapturing vendor fixtures needs the Intel vendor filter under Rosetta; do not delete the
  vendor driver before M7, and never regenerate the oracle files casually.
- `scripts/spl-survey.py` and `m2022-airbridge decode` are the tools for studying jobs.
