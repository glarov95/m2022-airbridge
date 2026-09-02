# ADR-009 — Unprivileged service

**Status:** accepted, 2026-09-02. To be verified in M1 (libusb access as a non-root user).

## Decision
The daemon runs as a dedicated hidden user, on port 8000, with the web UI bound to
localhost and TLS enabled. Only the installer needs root.

## Reason
It processes untrusted input from the network; least privilege limits the damage of a bug.

## Consequences
If macOS 26 requires root for libusb access to this device, this ADR is amended with the
evidence and root becomes the documented fallback.
