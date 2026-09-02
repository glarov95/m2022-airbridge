# ADR-005 — Pure encoder modules

**Status:** accepted, 2026-09-02.

## Decision
`raster/`, `halftone/` and `qpdl/` are I/O-free, deterministic C modules that take
memory in and call a write callback out. They know nothing about IPP, files, USB, PAPPL or
macOS.

## Reason
Byte-for-byte tests against captured vendor output require determinism and isolation, and
the modules must port to Linux unchanged.

## Consequences
No `printf`, no file access, no allocation per line inside these modules; adapters live in
`app/`, `cli/` and `service/`.
