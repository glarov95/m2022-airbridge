# Progress

Read this first in every session. It says where we are, what is next, and what to update when
something is finished. The design is `SPEC.md`; the working rules are `CLAUDE.md`.

## Where we are

- **Milestone:** M4 complete. The 0x11 band codec now has an encoder with a per-band
  offset-table choice: every vendor band re-encodes exactly in 0.76 of the vendor's bytes, and
  our own halftones round-trip through it. Next: M5, the QPDL encoder and the `encode` command.
- **Last update:** 2026-09-02.
- **Hardware:** Samsung SL-M2022 on USB (04e8:3321, serial ZF45B8GF3C01YSD). The vendor CUPS
  queue `Samsung_M2020_Series` is still installed and idle; it stays until M7 removes it.

## Next up

**M5 — QPDL encoder and `encode` command** (SPEC.md 6.4; docs/spl-qpdl.md sections 1 and 2;
record serialisers already exist in `src/qpdl/records.c`, the codec in `src/qpdl/codec11.c`,
media codes in `src/app/media.c`).

1. `src/qpdl/encode.c` behind `include/m2022/qpdl.h`: a job-options struct (media from
   `m2022/media.h`, feeder auto/manual, media type as the PJL `PAPERTYPE` string, copies,
   manual duplex with binding, resolution 600 or 1200, skip blank pages) and a streaming API:
   begin job (PJL envelope exactly as the vendor's in docs/spl-qpdl.md section 1, with the
   `SERVICEDATE` passed in by the caller so tests are deterministic), begin page (0x00 record
   from the media table: paper code, width and height in 1/300 in, halved unit at 1200 dpi),
   add band (rows → `m2022_qpdl_rows_to_band` → `m2022_codec11_choose_table` →
   `m2022_codec11_encode` → 0x0C record; all-white bands omitted), end page (0x01, copies 1),
   end job (0x09 + UEL). Output through a write callback; buffers sized once per page.
2. Band geometry: band width = ceil(raster width / 256) × 256, rows padded with white on the
   right, 128-line bands with the last one padded with white lines. Band numbers count from 0
   at the top of the printable area, including omitted blank bands.
3. `m2022-airbridge encode IN.pgm|IN.pbm|IN.ras[.gz] [--preset|--method ...] [--media A4]
   [--copies N] [--out FILE]`: raster → tone → halftone (M3) → bands → job; share the option
   parsing with `render`. `decode` must explain the result byte by byte.
4. Tests: encode the vendor's input rasters (`fixtures/oracle/samsung/*.ras.gz`) and compare
   with the vendor's `.spl` structurally: same PJL lines except `SERVICEDATE` and the OS
   comment, byte-identical page header, same band numbers and widths, same set of omitted
   bands, every band decoding to exactly our halftone output, the whole job accepted by
   `m2022_qpdl_walk`; the media sweep gives 14 page headers to compare byte for byte with
   `fixtures/oracle/samsung/media/*.spl`. The black square through the Text preset should
   decode to the vendor's identical bitmap.
5. Hardware (only when the user says the printer is on): `send` our own `encode` output of
   the black square, then the text page; that is the first native print and the start of M6.
   Also send the Floyd–Steinberg photo page to answer SPEC.md question 13 (page size limit).

## Milestones

| # | Milestone | Status | Done | Commits |
|---|---|---|---|---|
| M0 | Environment probe and vendor capture | done | 2026-09-02 | 6e96237 |
| M1 | Repository, USB transport, replay, decoder | done, v0.1 | 2026-09-02 | 1b73582, 670be60 |
| M2 | PAPPL skeleton with capture device | done, v0.2 | 2026-09-02 | a8c6b5b + follow-up |
| M3 | Raster and halftone pipeline | done | 2026-09-02 | 5e18397 |
| M4 | Band codec 0x11 encoder | done | 2026-09-02 | (this commit) |
| M5 | QPDL encoder and `encode` | next | | |
| M6 | First native print (v0.3) | todo | | |
| M7 | Service, installer, doctor | todo | | |
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
- USB access works unprivileged with libusb; the device ID advertises `URF`: `docs/usb.md`.
- The vendor driver is Intel-only and stops working with macOS 28 (fall 2027). Its package is
  backed up under `artifacts/vendor-driver-backup/` (gitignored: keep a copy elsewhere too).
- Open hardware questions live in SPEC.md section 16 (PJL lines required, copies field, 1200 dpi
  print quality, toner channel, raw URF over USB).

## Session log

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
