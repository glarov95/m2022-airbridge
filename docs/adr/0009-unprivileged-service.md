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

## Verified 2026-09-02
`m2022-airbridge probe` and `send`, running as an ordinary (admin, non-root) user on macOS 26.6.2, opened the printer with libusb 1.0.30, claimed interface 0, read the IEEE 1284 device ID and port status, and wrote a job over bulk OUT. No kernel driver holds the device. The decision stands: the daemon runs unprivileged.
