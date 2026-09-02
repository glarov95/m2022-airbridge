# The macOS service

How M2022 AirBridge runs on a Mac without a terminal: a launchd daemon under a hidden user,
installed and removed by the tool itself. Decision record: `adr/0011-service-form.md`.

## What runs

| piece | value |
|---|---|
| launchd job | `/Library/LaunchDaemons/com.m2022airbridge.daemon.plist`, `RunAtLoad`, `KeepAlive` (restarts after a crash, 5 s throttle) |
| program | `/usr/local/bin/m2022-airbridge server --spool … --log … --state … --port 8000 --name "Samsung M2022"` |
| user | `_m2022airbridge`, hidden, no shell, first free UID in 300–499, own group |
| state | `/Library/Application Support/M2022AirBridge/state.conf` (PAPPL's printer ID and web-interface settings; a saved printer wins over the command line, delete the file to reset) |
| spool | `/var/spool/m2022-airbridge` (0700) |
| log | `/Library/Logs/M2022AirBridge/m2022-airbridge.log`, rotated by newsyslog at 5 MB, 5 kept; launchd's own stdout/stderr in `launchd.log` next to it |
| backups | `/Library/Application Support/M2022AirBridge/backup/` (the vendor queue's PPD, device URI and options) |
| port | 8000, unprivileged; DNS-SD carries the port, clients do not care |

The daemon runs unprivileged (ADR-009): libusb opens the printer as an ordinary user, and the
installer proves it before touching anything else by running `probe` as the service user.

## Install

```sh
cmake --build build
sudo ./build/src/m2022-airbridge install            # add --dry-run to only see the plan
./build/src/m2022-airbridge doctor
```

`install` builds a plan from what it finds on the Mac and prints every command before running
it. On a fresh Mac with the Samsung driver installed the plan is:

1. create the group and the hidden user (`dscl`);
2. create the support, backup, spool and log directories with the right owner;
3. copy this binary to `/usr/local/bin`;
4. run `probe --quiet` as the service user: if that user cannot open the printer, stop here;
5. back up the Samsung queue's PPD, device URI and options, then remove the queue
   (`lpadmin -x`): one process owns the USB device (ADR-006); `--keep-vendor-queue` skips this;
6. write the launchd plist and the newsyslog entry;
7. `launchctl bootstrap system …` and wait until IPP answers on localhost:8000;
8. add a driverless CUPS queue `Samsung_M2022` (`lpadmin -m everywhere`) unless a queue already
   points at the printer (one added through System Settings counts); `--test-page` prints
   `/usr/share/cups/data/testprint` through it.

Options: `--name` and `--port` change what the daemon advertises. Everything is idempotent:
running `install` again re-copies the binary, rewrites the plist and restarts the service,
which is also how an upgrade works.

## Day to day

```sh
m2022-airbridge status            # service, pid, printer on USB, IPP state, queue
m2022-airbridge doctor            # every check with a hint on failure; exit 0 = healthy
m2022-airbridge logs [-n N] [-f]  # the service log
sudo m2022-airbridge restart      # also start, stop
```

The printer appears on iPhones and iPads under Other Printers, and on Macs in the system print
dialog and, once added, in every app. Apps with their own print dialog (Chrome) list only
printers added to the Mac.

## Uninstall

```sh
sudo m2022-airbridge uninstall            # stop, remove the job and the binary, restore the Samsung queue
sudo m2022-airbridge uninstall --purge    # also remove state, logs, backups and the service user
```

The vendor queue is restored from the backup with `lpadmin -P backup.ppd`; that only works
while the Samsung driver package is still installed (its filters are what the PPD names). The
queue the installer created is removed; a queue you added yourself is left alone.

## The vendor driver

macOS adds a queue automatically whenever a USB printer with an installed driver appears, so
as long as the Samsung driver package is on the Mac the old queue comes back after every
power cycle or replug, next to ours (`doctor` warns: "vendor queue still exists"). Nothing
breaks unless someone prints to it, but two queues for one printer is confusing and the old
one is the Intel binary that dies with macOS 28. The cure is to remove the driver package,
a separate and explicit step:

```sh
sudo m2022-airbridge remove-vendor-driver --dry-run   # the plan
sudo m2022-airbridge remove-vendor-driver             # backup, then delete
```

It archives `/Library/Printers/Samsung` and the Samsung PPDs into the backup directory, removes
the vendor queue, deletes the driver files, the PPDs and the driver cache, and forgets the
package receipt. `sudo tar xzf …/backup/vendor-driver.tar.gz -C /` puts the driver back.

## Checklist for a new machine (M8 soak)

- [x] `install` on a Mac with the vendor driver: plan runs through, `doctor` clean (2026-09-02)
- [x] `install` again as the upgrade path (2026-09-02; the running service is stopped first)
- [x] print from an iPhone with no terminal open (2026-09-02)
- [x] two jobs back to back through the daemon (`tests/hardware/soak.sh`, 2026-09-02)
- [x] 20-page job through the pipeline: 70 pages/min, 52 MB peak RSS, first band after
      0.05 s (`scripts/measure-throughput.sh`, file device, 2026-09-02)
- [x] printer off: a submitted job waits (PAPPL retries the device every 5 s, the client's
      connection stays open), `status` shows "stopped, paused" and no printer on USB; on again:
      it printed by itself after 4 minutes of waiting (2026-09-02). Caveat: macOS re-created
      the Samsung queue the moment the printer re-appeared, because the vendor driver package
      is still installed (see "The vendor driver" below)
- [ ] sleep and wake: printing works after wake (mDNSResponder re-registers)
- [x] USB unplugged and plugged back: `status` said "no SL-M2022 on USB" and "offline"; after
      the replug a page from the phone printed, no restart (2026-09-02)
- [ ] reboot: printer visible and printing without any manual step
- [x] `remove-vendor-driver`: backup (823 files), queue, driver files, PPDs, cache and receipt
      gone; `doctor` clean afterwards (2026-09-02)
- [ ] `uninstall` restores the vendor queue from the backup (needs the driver put back first
      now); `install` again

## What this teaches

launchd is the whole process model on macOS: a plist says who runs what, when, and what
happens when it dies. The interesting decisions are the user (least privilege, and proving the
USB access it needs before relying on it), where state and logs live so the system's own tools
rotate and find them, and making installation a printed, idempotent plan instead of a script
that surprises people.
