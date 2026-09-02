# ADR-010 — MIT license, clean-room encoders

**Status:** accepted, 2026-09-02.

## Decision
The project is MIT licensed. The band codec and QPDL encoders are written from published
specifications and from analysis of captured bytes. SpliX and jbigkit (GPL-2.0) are built
separately as test oracles and are never linked into shipped binaries or copied into `src/`.
The Samsung driver is never redistributed.

## Reason
The encoders are the educational core and should be publishable permissively. MIT keeps a
later relicensing to GPL-2.0 possible if linking jbigkit ever becomes necessary
(Apache-2.0 would not).

## Consequences
Every protocol table entry carries a comment citing its source. jbigkit may be linked into
test binaries only.
