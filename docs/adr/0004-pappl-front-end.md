# ADR-004 — PAPPL 1.4.x for IPP, DNS-SD, spooling, TLS and web UI

**Status:** accepted, 2026-09-02.

## Decision
Use PAPPL v1.4.12, pinned as a git submodule and built by `scripts/build-pappl.sh`. Do not
track PAPPL `master`.

## Reason
IPP semantics, discovery, spooling and raster decoding are solved, specified, and tested
there; PAPPL is developed on Apple Silicon. `master` targets libcups 3 / CUPS 2.5, which
macOS 26 does not ship.

## Consequences
We implement `pappl_pr_driver_data_t` callbacks and nothing network-facing ourselves.
Migration to PAPPL 2.x is a later ADR.
