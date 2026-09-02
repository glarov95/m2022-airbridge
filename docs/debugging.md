# Debugging

Every stage of the pipeline has a tool that shows its input or output, so a wrong page can be
traced to the stage that produced it.

| tool | shows |
|---|---|
| `m2022-airbridge probe [--json]` | the host, the USB printer (device ID, port status), the CUPS queues |
| `m2022-airbridge server --capture DIR` | keeps every client page as `job-NNN-pN.pgm` (what iOS or macOS sent, before our pipeline) |
| `m2022-airbridge server --device file:///tmp/job.spl` | a dry run: the job goes to a file instead of the printer |
| `m2022-airbridge render IN --preset P --out page.pbm` | our halftone of a page, to look at |
| `m2022-airbridge encode IN … --out job.spl` | a complete job from a page, with every option |
| `m2022-airbridge decode JOB.spl [--pbm PREFIX]` | every byte explained: PJL lines, page header, each band's codec statistics and checksum, the pages as PBM |
| `m2022-airbridge send JOB.spl` | writes a job over USB with the port status before and after |
| `m2022-airbridge doctor` | one line per thing a working printer needs, a hint per failure |
| `m2022-airbridge status`, `logs -f` | the service and its log |
| `scripts/spl-survey.py` | a one-line summary per captured job |

## Reading the log

Each job logs its option mapping, its geometry and its outcome:

```text
[Job 2] start: format image/urf, 0 page(s), copies 1, media iso_a4_210x297mm (A4) source main type stationery -> feeder auto PAPERTYPE OFF, quality 4 content auto -> preset normal
[Job 2] page 1: raster 4960x7015 px, 8 bpp cspace 18, ; printable 4750x6808 at 104,104; band width 4864
[Job 2] page 1 done: 6808 lines in, 53 bands so far (1 blank)
[Job 2] end of job: 1 page(s), 53 bands (1 blank omitted), 439845 bytes, 1.07 s
```

`page N: unsupported raster` or `rendered at … dpi` means a client sent something outside what
we advertise; the capture shows exactly what. A device write failure names the USB error.

## Usual suspects

- **The phone sees the printer, the job fails.** `doctor`: is the service running, is the printer
  on USB (`usb printer`), does IPP answer? A second owner of the USB device (the old Samsung
  queue printing at the same time) makes writes fail intermittently (ADR-006).
- **Port 8000 in use.** A dev `server` started by hand and the daemon cannot both listen; stop
  one (`pkill -f 'm2022-airbridge server'` or `sudo m2022-airbridge stop`).
- **Wrong margins or a shifted page.** `server --capture`, then `decode --pbm` of a dry-run job:
  compare the captured PGM with the decoded page; the geometry rule is in
  `docs/ipp-airprint.md`.
- **A page looks different from the vendor's.** `render` the same input with the preset used and
  compare with `decode --pbm` of the vendor fixture; `tests/unit/test_halftone_vendor.c` does
  this with numbers.
