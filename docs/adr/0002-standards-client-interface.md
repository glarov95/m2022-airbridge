# ADR-002 — Standards-shaped client interface

**Status:** accepted, 2026-09-02.

## Decision
Clients see an IPP Everywhere printer with the AirPrint attributes, discovered over DNS-SD.

## Reason
iOS, iPadOS, macOS, Windows and Linux all print to such a printer without client software.

## Consequences
We conform to PWG 5100.14 and the Apple Bonjour Printing Specification; we do not invent a
protocol or an app.
