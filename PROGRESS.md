# Progress

Read this first in every session. It says where we are, what is next, and what to update when
something is finished. The design is `SPEC.md`; the working rules are `CLAUDE.md`.

## Where we are

- **Milestone:** M1 complete, **v0.1** reached (a captured vendor job printed through our USB
  transport). M2 not started.
- **Last update:** 2026-09-02, commit `670be60`.
- **Hardware:** Samsung SL-M2022 on USB (04e8:3321, serial ZF45B8GF3C01YSD). The vendor CUPS
  queue `Samsung_M2020_Series` is still installed and idle; it stays until M7 removes it.

## Next up

**M2 — Printer Application skeleton** (SPEC.md section 9 M2; driver data in 6.1; service in 6.7).

1. Add PAPPL **v1.4.12** as a git submodule at `third_party/pappl` and write
   `scripts/build-pappl.sh` that builds it with its autoconf into `build/pappl/` (dependencies
   already installed via Homebrew: libusb, jpeg-turbo, libpng, openssl@3, pkg-config; system
   libcups 2.3.4 and mDNSResponder). Pin the tag; do not track `master` (needs libcups 3).
2. `src/app/`: a PAPPL system with one printer "Samsung M2022"; `pappl_pr_driver_data_t` per
   SPEC 6.1 (raster types SGRAY_8 | BLACK_1 | SRGB_24, resolutions 600 default and 300, media
   from the vendor PPD with 12.5 pt margins, monochrome only, one-sided); raster callbacks that
   write the incoming raster to a file (the "capture" device, kept as a debug device);
   `printfile` forwarding raw data to `usb/`.
3. `m2022-airbridge server [--port 8000] [--capture DIR]` runs it in the foreground.
4. Acceptance: iPhone, iPad and the Mac list the printer; a job from each lands on disk as
   PWG/Apple raster with the expected geometry; `ipptool -tv ... get-printer-attributes.test`
   passes; `ippfind _ipp._tcp,_universal --txt URF` shows the record. Write which format and
   resolution each client sends into `docs/ipp-airprint.md` and SPEC.md 16 Q6.

Start by reading SPEC.md sections 5, 6.1 and 6.7, then the PAPPL driver callback documentation
(`third_party/pappl/doc/pappl.html` once the submodule exists, or https://www.msweet.org/pappl/).

## Milestones

| # | Milestone | Status | Done | Commits |
|---|---|---|---|---|
| M0 | Environment probe and vendor capture | done | 2026-09-02 | 6e96237 |
| M1 | Repository, USB transport, replay, decoder | done, v0.1 | 2026-09-02 | 1b73582, 670be60 |
| M2 | PAPPL skeleton with capture device | next | | |
| M3 | Raster and halftone pipeline | todo | | |
| M4 | Band codec 0x11 encoder | todo | | |
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
- Recapturing vendor fixtures needs the Intel vendor filter under Rosetta; do not delete the
  vendor driver before M7, and never regenerate the oracle files casually.
- `scripts/spl-survey.py` and `m2022-airbridge decode` are the tools for studying jobs.
