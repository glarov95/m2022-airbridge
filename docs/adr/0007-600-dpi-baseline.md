# ADR-007 — 600 dpi is the baseline; 1200 dpi is experimental

**Status:** accepted, 2026-09-02.

## Decision
All presets target 600×600 dpi. 1200 dpi is investigated in M9 and kept only if it prints
cleanly on the hardware.

## Reason
The vendor driver renders 600 dpi 8-bit gray and exposes no 1200 dpi raster mode; its
"Best" setting is the same raster with a row-feed tweak. A sibling model in SpliX prints
garbage at 1200 dpi.

## Consequences
Quality work (halftoning, tone curves, edge handling) is done at 600 dpi.

## Update 2026-09-02
The vendor "Best" mode emits a genuine 1200×1200 dpi QPDL job (`fixtures/oracle/samsung/small-text-a4.best.spl`), halftoned by the filter from the same 600 dpi gray raster. The format is therefore known; the decision stands until a hardware test shows it prints cleanly.
