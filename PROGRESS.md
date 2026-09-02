# Progress

Read this first in every session. It says where we are, what is next, and what to update when
something is finished. The design is `SPEC.md`; the working rules are `CLAUDE.md`.

## Where we are

- **Milestone:** M3 complete. Raster ingest, tone curve and five halftone methods with presets
  exist as pure modules, tested against the vendor's own output (Text preset: 99.99 % pixel
  agreement on the text page). Next: M4, the 0x11 band codec encoder.
- **Last update:** 2026-09-02.
- **Hardware:** Samsung SL-M2022 on USB (04e8:3321, serial ZF45B8GF3C01YSD). The vendor CUPS
  queue `Samsung_M2020_Series` is still installed and idle; it stays until M7 removes it.

## Next up

**M4 — Band codec 0x11 encoder** (SPEC.md 6.5; docs/spl-qpdl.md section 3; decoder in
`src/qpdl/codec11.c`).

1. `m2022_codec11_encode(const uint8_t *band, size_t len, const uint16_t table[64], uint8_t *out,
   size_t out_cap, size_t *out_len)`: write the little-endian header (magic, raw length, table),
   copy the raw prefix, then greedy longest-match tokens over the 64 offsets (match length 3..514,
   literal runs 1..128), then the big-endian checksum of the payload. Pure, no allocation.
2. Offset-table strategy: start with the vendor's text-page table (docs/spl-qpdl.md); add a
   helper that picks a table from a band's statistics later if compression ratio needs it.
   Any table is valid for the printer (verified: the vendor changes it per band).
3. `m2022_qpdl_rows_to_band()` already exists for the column-major layout; the encoder takes the
   column-major band.
4. Tests: encode→decode round trip exact on every vendor band (decode vendor → rows → band →
   encode → decode), on random data and on edge cases (all white, all black, width not a
   multiple of 8 is impossible here since band widths are multiples of 256); compressed size
   within ~1.5× of the vendor's for the same band; fuzz the decoder with encoder output.
5. `m2022-airbridge encode-band` is not needed; `encode` (M5) will use it. Update
   docs/spl-qpdl.md with the encoder's ratio versus the vendor.

Start by reading docs/spl-qpdl.md section 3 and `src/qpdl/codec11.c` (the decoder is the spec).

## Milestones

| # | Milestone | Status | Done | Commits |
|---|---|---|---|---|
| M0 | Environment probe and vendor capture | done | 2026-09-02 | 6e96237 |
| M1 | Repository, USB transport, replay, decoder | done, v0.1 | 2026-09-02 | 1b73582, 670be60 |
| M2 | PAPPL skeleton with capture device | done, v0.2 | 2026-09-02 | a8c6b5b + follow-up |
| M3 | Raster and halftone pipeline | done | 2026-09-02 | (this commit) |
| M4 | Band codec 0x11 encoder | next | | |
| M5 | QPDL encoder and `encode` | todo | | |
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
- USB access works unprivileged with libusb; the device ID advertises `URF`: `docs/usb.md`.
- The vendor driver is Intel-only and stops working with macOS 28 (fall 2027). Its package is
  backed up under `artifacts/vendor-driver-backup/` (gitignored: keep a copy elsewhere too).
- Open hardware questions live in SPEC.md section 16 (PJL lines required, copies field, 1200 dpi
  print quality, toner channel, raw URF over USB).

## Session log

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
