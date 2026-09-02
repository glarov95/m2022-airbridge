# Documentation

How M2022 AirBridge works, module by module, written as the modules are built. Each document
states what was measured, what was decided, and what it teaches.

| Document | Covers | Status |
|---|---|---|
| [architecture.md](architecture.md) | the data path today and in the finished product, module map | current |
| [spl-qpdl.md](spl-qpdl.md) | the printer's language: PJL envelope, records, 0x11 band codec, paper codes, band-width rule | current, verified on 867 bands |
| [usb.md](usb.md) | USB printer class transport, device identity, status, what the printer reports | current |
| [ipp-airprint.md](ipp-airprint.md) | IPP Everywhere and AirPrint attributes, DNS-SD record, what clients send | current, measured on macOS 26 and iOS 26 |
| [raster.md](raster.md) | raster ingest, colour conversion, scaling, margins | planned, M3 |
| [halftone.md](halftone.md) | threshold, ordered, clustered-dot, error diffusion, blue noise; tone curves | planned, M3 |
| [macos-service.md](macos-service.md) | launchd, dedicated user, installer, uninstall, logging | planned, M7 |
| [debugging.md](debugging.md) | tools: probe, decode, capture, survey; how to read a failure | planned, M7 |
| [adr/](adr/) | decision records 0001–0010 | current |
| [archive/](archive/) | the original v1 specification | reference |

Reading list per topic: `SPEC.md` section 10.
