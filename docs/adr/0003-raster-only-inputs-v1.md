# ADR-003 — Raster-only inputs in v1

**Status:** accepted, 2026-09-02.

## Decision
v1 advertises and accepts `image/urf`, `image/pwg-raster`, `image/jpeg` and `image/png`.
`application/pdf` is not advertised.

## Reason
Real AirPrint printers behave this way; clients rasterise PDF at the resolution we advertise.
It keeps a PDF engine out of the trust boundary and out of v1 scope.

## Consequences
Text rendering quality in v1 is the client rasteriser at 600 dpi. Server-side PDF
(CoreGraphics on macOS, MuPDF on Linux) is a v2 quality feature behind an interface.
