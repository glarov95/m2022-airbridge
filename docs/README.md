# Documentation

How M2022 AirBridge works, module by module, written as the modules are built. Each document
states what was measured, what was decided, and what it teaches.

| Document | Covers | Status |
|---|---|---|
| [architecture.md](architecture.md) | the data path today and in the finished product, module map | current |
| [spl-qpdl.md](spl-qpdl.md) | the printer's language: PJL envelope, records, the 0x11 band codec (decoder and encoder), paper codes, band-width rule | current, verified on 867 bands; encoder measured against the vendor |
| [usb.md](usb.md) | USB printer class transport, device identity, status, what the printer reports | current |
| [ipp-airprint.md](ipp-airprint.md) | IPP Everywhere and AirPrint attributes, DNS-SD record, what clients send | current, measured on macOS 26 and iOS 26 |
| [raster.md](raster.md) | raster ingest, colour conversion, tone curve, scaling, margins | current |
| [halftone.md](halftone.md) | threshold, ordered, clustered-dot, error diffusion, blue noise; presets; measured against the vendor | current |
| [macos-service.md](macos-service.md) | launchd job, hidden service user, install and uninstall plans, day-to-day commands, checklist | current |
| [debugging.md](debugging.md) | every tool and what it shows; reading the job log; usual suspects | current |
| [adr/](adr/) | decision records 0001–0011 | current |
| [archive/](archive/) | the original v1 specification | reference |

Reading list per topic: `SPEC.md` section 10.
