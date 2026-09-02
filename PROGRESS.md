# Progress

The project's state: what works today, what is next, and how it got here. Read this first in
every session; the design is `SPEC.md`, the working rules `CLAUDE.md`, the front page
`README.md`.

## What works today

**v0.3, M8 in progress.** The printer's language is fully decoded and verified against 42
captured vendor jobs; the raster and halftone pipeline is measured against the vendor's
output (text page: 99.99 % pixel agreement); the 0x11 band encoder produces smaller streams
than the vendor's on every band; the job encoder's page headers are byte-identical for all 14
paper sizes; and the Printer Application takes an Apple Raster job from an iPhone or a Mac,
crops it to the printable area, halftones and encodes it, and writes it to the printer over
USB. Nothing from the vendor runs anywhere in that path. `sudo m2022-airbridge install` turns
it into a launchd service under a hidden user, with `doctor`, `status` and `logs` to watch
it. Next: the reliability soak on the way to v1.0.

## Where we are

- **Milestone:** M8 in progress. The installed service survived the soak so far: printer
  off (a job waits and prints when it comes on), USB unplugged and replugged, two jobs back
  to back, a 20-page job at 70 pages/min with 52 MB peak RSS and the first band on the wire
  after 0.05 s. Found and handled: macOS re-creates the Samsung queue from the installed
  vendor driver whenever the printer appears on USB, so `remove-vendor-driver` exists (backup
  first). Remaining for v1.0: sleep/wake, the reboot check, the decision to remove the vendor
  driver, and a week of daily use.
- **Last update:** 2026-09-02.
- **Hardware:** Samsung SL-M2022 on USB (04e8:3321, serial ZF45B8GF3C01YSD), served by the
  installed daemon. The vendor driver package was removed on 2026-09-02 (backup:
  `/Library/Application Support/M2022AirBridge/backup/vendor-driver.tar.gz`, and the M0
  copy under `artifacts/vendor-driver-backup/`); nothing from Samsung runs on this Mac.
- **Running it today:** it runs by itself. `m2022-airbridge status`, `doctor`, `logs -f`;
  `sudo m2022-airbridge restart`. A dev server on port 8000 conflicts with the daemon: use
  `--port 8001` or `sudo m2022-airbridge stop` first.

## Next up

**M8 — finish the soak (v1.0)** (docs/macos-service.md checklist; SPEC.md M8, 17).

1. With the user: sleep/wake, the reboot check (printer visible and printing with nothing
   done by hand), then a page a day for a week. Tick the checklist with dates.
2. Cancel from the phone mid-job (the Floyd–Steinberg photo page takes a few seconds):
   `rendjob` closes the stream; confirm the printer prints the partial page or nothing and the
   next job is fine. (A second job from the phone while one was printing went through,
   2026-09-02.)
3. A Letter page and a manual-feed job from the phone (media options in the print dialog).
4. When the week is clean: SPEC section 17 v1.0 line, README status v1.0, tag `v1.0`.
5. Then M9 (status details, manual duplex, 1200 dpi, toner level over the vendor's USB
   control request; SPEC questions 4, 5, 8, 9, 10).

## Milestones

| # | Milestone | Delivers | Status | Done | Commits |
|---|---|---|---|---|---|
| M0 | Environment probe and vendor capture | the Mac and the printer probed, vendor output captured for 42 pages, driver backed up | done | 2026-09-02 | 6e96237 |
| M1 | Repository, USB transport, replay, decoder | build system, `probe`, `send`, `decode`; a replayed vendor job prints | done, v0.1 | 2026-09-02 | 1b73582, 670be60 |
| M2 | PAPPL skeleton with capture device | printer visible to iPhone, iPad and Mac; incoming raster captured | done, v0.2 | 2026-09-02 | a8c6b5b + follow-up |
| M3 | Raster and halftone pipeline | raster ingest, tone curve, five halftone methods, presets, `render` | done | 2026-09-02 | 5e18397 |
| M4 | Band codec 0x11 encoder | per-band offset tables, smaller output than the vendor's | done | 2026-09-02 | f22d645 |
| M5 | QPDL encoder and `encode` | job encoder with the vendor's envelope and records; first native page printed | done | 2026-09-02 | 74c2e6a |
| M6 | The Printer Application prints | iPhone and Mac print through the daemon over our USB device scheme | done, v0.3 | 2026-09-02 | 0a92b2d |
| M7 | Service, installer, doctor | launchd service under a hidden user, `install`, `uninstall`, `doctor`, `status`, `logs` | done | 2026-09-02 | b2f9485 |
| M8 | Reliability soak | checklist on real hardware, job limits, throughput measured, `remove-vendor-driver` | in progress, v1.0 | | d20dcef, 864a834 |
| M9 | Status and options | toner level, cover and jam status, manual duplex, 1200 dpi experiment | todo | | |
| M10 | Quality program | calibration from scans, comparison scoring against the vendor | todo, v1.5 | | |
| M11 | Release engineering | signed `.pkg`, Homebrew tap, CHANGELOG, tree-wide format, the documentation as a whole | todo | | |

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

- **2026-09-02 (docs)** — README rewritten as an open-source front page: features and
  planned features, compatibility (printer, host, clients), quick start, command table,
  documentation index, contributing, acknowledgements. The status paragraph and the
  milestone descriptions moved here; `CLAUDE.md` says so. Hardware, by the user: a second
  job from the phone while one was printing went through.
- **2026-09-02 (M8, part 1)** — Limits (20 active jobs, 50 kept, 128 MB images), a "first
  band on the device" log line, `scripts/measure-throughput.sh` (multi-page job through a
  file device: 70 pages/min, 52 MB RSS, 0.05 s to first band), `tests/hardware/soak.sh` (N
  jobs through the daemon), `status` exits 1 for a stopped printer. Hardware: printer off →
  job waited 4 min and printed when switched on (PAPPL streams raster jobs, so the client's
  connection waits too; documented); USB unplug/replug → `status` offline, printing resumed.
  macOS re-created the Samsung queue when the printer reappeared: `remove-vendor-driver`
  command (plan, dry run, tar backup of the driver dir and 34 PPDs verified: 823 entries).
  ADR-011 updated: logging stays in the file. 19 suites pass.
- **2026-09-02 (M7)** — `src/service/`: paths, launchd plist, newsyslog entry, launchctl
  parsing, UID choice, install/uninstall plans built from an inspection of the Mac and
  executed step by step (`--dry-run` prints them), DNS-SD browse; `src/cups/ipp.c`: one
  Get-Printer-Attributes; commands `install`, `uninstall`, `start`, `stop`, `restart`,
  `status`, `logs`, `doctor`; `server --state` (PAPPL state file); `probe --quiet` with exit
  codes for the installer's USB check. Tests: `test_service` (plans for fresh Mac, upgrade,
  keep-vendor, nothing installed, purge), `it_doctor` (doctor against a hand-started server,
  with and without it). Real install on this Mac: user created (uid 309), vendor queue backed
  up and removed, daemon up, `doctor` green; a page from the iPhone printed with nothing
  running by hand. Learned: overwriting a running binary in place
  kills it (code signature), so upgrades stop the service first and the copy is atomic; the
  daemon's brief status polls can collide with a USB check, so the check retries. ADR-011,
  docs/macos-service.md, docs/debugging.md. clang-format installed and applied to the new
  files. 19 suites pass.
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

- Remote: `origin` = https://github.com/glarov95/m2022-airbridge (private, branch `main`,
  created 2026-09-02 with `gh`). Push after each commit the user asks for.

- Build: `export PATH=/opt/homebrew/bin:$PATH; cmake -S . -B build -G Ninja; cmake --build build;
  ctest --test-dir build --output-on-failure -LE hardware`
- Hardware tests print pages: `ctest --test-dir build -L hardware` only with the printer on.
- Run the application for client tests: `./build/src/m2022-airbridge server --capture artifacts/capture --spool build/run/spool --log build/run/server.log`; stop with `pkill -f 'm2022-airbridge server'`.
- Recapturing vendor fixtures needs the Intel vendor filter under Rosetta; do not delete the
  vendor driver before M7, and never regenerate the oracle files casually.
- `scripts/spl-survey.py` and `m2022-airbridge decode` are the tools for studying jobs.
- `clang-format` (Homebrew, 2026-09-02) formats new files with the repo's `.clang-format`; the
  files written before M7 were formatted by hand and a tree-wide `clang-format -i` is an M11
  chore (do it in its own commit).
