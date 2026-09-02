# ADR-008 — macOS arm64 first, portable core

**Status:** accepted, 2026-09-02.

## Decision
The primary target is Apple Silicon macOS. Apple-specific APIs (Unified Logging, launchd,
IOKit, CoreGraphics) are used only in adapters. Core modules are POSIX C17.

## Reason
The printer lives on a Mac today; a Raspberry Pi/Linux host is a desirable v2 that must not
require a rewrite.

## Consequences
CI builds on macOS; a Linux build is added when the first adapter needs it.
