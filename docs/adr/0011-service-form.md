# ADR-011 — The service on macOS: a LaunchDaemon, a hidden user, a printed plan

**Status:** accepted, 2026-09-02 (M7).

## Decision

- The daemon is a launchd **LaunchDaemon** (`com.m2022airbridge.daemon`), started at boot,
  kept alive, running as the hidden system user `_m2022airbridge` created by the installer.
- Installation, removal and service control are subcommands of the one binary (`install`,
  `uninstall`, `start`, `stop`, `restart`, `status`, `logs`, `doctor`). `install` and
  `uninstall` build a plan from what they find, print every step, and `--dry-run` prints
  without executing. The plans are data and unit-tested.
- Logs go to a file under `/Library/Logs/M2022AirBridge`, rotated by newsyslog; PAPPL's
  state file under `/Library/Application Support/M2022AirBridge` keeps the printer's identity
  and web-interface settings. Unified Logging is not wired yet (`log stream` shows nothing).
- The Mac gets a driverless CUPS queue (`lpadmin -m everywhere`) unless one already points at
  the printer; the Samsung queue is backed up and removed (ADR-006).

## Reasons

A LaunchDaemon prints for everyone on the Mac and before anyone logs in, which is what a
printer does; a LaunchAgent would tie the printer to one user's session. libusb works for an
unprivileged user (ADR-009), and the installer proves it for the new user before it removes
anything. A printed plan is easier to trust and to test than a shell script: the same code
runs the steps, shows them, and is checked by `tests/unit/test_service.c` for the situations the
installer meets. A subcommand keeps the whole product in one binary, like the vendor's UX.

## Consequences

- `install` needs sudo; nothing else does except `start`, `stop`, `restart` and `uninstall`.
- The `.pkg` in M11 wraps the same plan; no separate installer logic.
- Restoring the vendor queue on uninstall works only while the Samsung driver is installed.
- Unified Logging and a config file (SPEC.md 6.10) are open; the plist's arguments carry the
  settings for now.

## Update 2026-09-02 (M8): logging stays in the file

PAPPL logs to one destination: a file, stderr, or `syslog(3)` (which lands in Unified
Logging on macOS). The file wins: `doctor` reads it for the last error, `logs -f` tails it,
newsyslog rotates it, and it survives without any `log show` privileges or predicates. A
second destination would need a log callback PAPPL 1.4 does not have. `log stream` therefore
shows nothing for this daemon; that is documented rather than fixed.
