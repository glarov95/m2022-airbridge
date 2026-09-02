# USB transport

Module `src/usb/`, header `include/m2022/usb.h`. Uses libusb 1.0 (Homebrew), no vendor code.

## What the printer is on the bus

Measured 2026-09-02 (`fixtures/oracle/samsung/usb-device-info.txt`, `m2022-airbridge probe`):

| Item | Value |
|---|---|
| VID:PID | 04e8:3321, bcdDevice 1.00, high speed |
| Strings | "Samsung Electronics Co., Ltd.", "M2020 Series", serial ZF45B8GF3C01YSD |
| Interface | 0, class 7 (printer), subclass 1, protocol 2 (bidirectional) |
| Endpoints | bulk OUT 0x02, 512 bytes per packet; bulk IN 0x81 |
| IEEE 1284 device ID | `MFG:Samsung;CMD:SPL,URF,FWV,PIC,EXT,DCU;MDL:M2020 Series;CLS:PRINTER;CID:SA_SPLV3_BW;MODE:SPL3,R000105;STATUS:BUSY;` |
| Port status when ready | 0x18 (selected, no error, paper present) |

The device ID lists `URF`, so the firmware claims to accept Apple Raster directly. Untested;
SPEC.md 16. CUPS prepends `SERN:` from the USB serial when it shows the same string.

## How the transport works

- **Identity, not order.** The device is matched by VID, PID and serial. Enumeration order and
  bus addresses change on replug.
- **Claiming.** libusb opens the device and claims interface 0. On macOS no kernel driver owns
  printer-class devices, so this works as an ordinary user (verified; ADR-009). The vendor CUPS
  queue only holds the device while a job runs.
- **Writing.** Jobs go out in 64 KiB bulk transfers with a timeout; partial progress is reported
  on error. A 6.5 KB job takes under a millisecond to hand over; the printer buffers.
- **Class requests** (USB printer class 1.1, section 4.2): `GET_DEVICE_ID` (0) returns a 2-byte
  big-endian length plus the 1284 string; `GET_PORT_STATUS` (1) returns one byte: bit 5 paper
  empty, bit 4 selected, bit 3 no error; `SOFT_RESET` (2).
- **Status mapping.** Paper empty → `media-empty`, not selected → `offline`, error → `other`.
  Richer states (jam, cover open, toner) are not in the port status byte; SPEC.md M9.

## The PAPPL device scheme (M6, `src/app/usbdev.c`)

PAPPL talks to printers through device URIs. Ours is `m2022usb://04e8:3321[?serial=S]`,
registered with `papplDeviceAddScheme2` and backed by this transport: open claims the printer
interface, write loops over bulk transfers with a 60 s timeout (the printer stalls the pipe
while its buffer is full), read polls the bulk IN endpoint for 250 ms, status turns the port
status byte into `printer-state-reasons`, and the device-ID callback returns the IEEE 1284
string. PAPPL opens the device for each job and, between jobs, briefly for status. The
`file://` scheme that PAPPL ships remains available for dry runs (`server --device
file:///tmp/job.spl`), which is what the integration test uses.

## Supply levels (open, M9)

The vendor driver reports the toner level (46 % on 2026-09-02) through its `commandtosec`
filter, which never touches the print pipe: the binary links IOKit and contains
`UsbDeviceRequest: EP0 command(0x%x, 0x%x, 0x%x)`, `Received Raw Code:` with 8 bytes, and
`@PJL LITESMSTATUS`. So the level is a vendor-specific control request on endpoint 0, not a
class request and not a PJL exchange on the bulk pipes. Once M9 has the request, the level
goes to PAPPL's supplies API and from there to the Mac's Supply Levels panel and the phone.

## Commands

```sh
m2022-airbridge probe [--json]         # enumerate, device id, port status, CUPS queues
m2022-airbridge send JOB.spl           # write a native job; --dry-run only queries
```

Hardware tests: `ctest --test-dir build -L hardware` (prints the black square).

## What this teaches

How a USB class works: descriptors name an interface by class/subclass/protocol, endpoints carry
bulk data, and a handful of class-specific control requests carry identity and status. The same
pattern (descriptor walk, claim, bulk, control) applies to any USB device.
