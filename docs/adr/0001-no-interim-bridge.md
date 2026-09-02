# ADR-001 — Build the Printer Application directly; no interim bridge

**Status:** accepted, 2026-09-02. Supersedes v1 ADR-001 (reuse the Samsung driver first).

## Decision
The product is a self-contained Printer Application from the first milestone. There is no
Bonjour bridge on top of the macOS CUPS queue, and no milestone ships code that is later
thrown away.

## Reason
The project owner wants the real driver and the understanding that comes with building it.
The vendor driver is Intel-only and stops working with macOS 28, so a bridge on top of it
has no future. Prior-art bridges (sapireli/AirPrint_Bridge) remain reference material.

## Consequences
The first end-to-end print from an iPhone happens later (M6) than a bridge would allow, but
every milestone before it produces product or test code: USB transport, PAPPL skeleton,
raster/halftone, JBIG, QPDL.
