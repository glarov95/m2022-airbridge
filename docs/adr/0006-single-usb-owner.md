# ADR-006 — Single USB owner

**Status:** accepted, 2026-09-02.

## Decision
The Printer Application owns the USB device. The installer backs up and removes the Samsung
CUPS queue; uninstall restores it.

## Reason
Two processes claiming the printer interface produce intermittent failures that are hard to
diagnose. The vendor queue has no future on Apple Silicon anyway.

## Consequences
`install` and `uninstall` are the only code that touches CUPS configuration. During
development the vendor queue may stay until M7, as long as it is idle.
