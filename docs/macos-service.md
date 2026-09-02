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

## Checklist for a new machine (M8 soak)

- [ ] `install` on a Mac with the vendor driver: plan runs through, `doctor` clean
- [ ] print from an iPhone with no terminal open
- [ ] reboot: printer visible and printing without any manual step
- [ ] sleep and wake: printing works after wake (mDNSResponder re-registers)
- [ ] printer off: `status` shows offline; on again: printing resumes without a restart
- [ ] `uninstall` restores the vendor queue; `install` again

## What this teaches

launchd is the whole process model on macOS: a plist says who runs what, when, and what
happens when it dies. The interesting decisions are the user (least privilege, and proving the
USB access it needs before relying on it), where state and logs live so the system's own tools
rotate and find them, and making installation a printed, idempotent plan instead of a script
that surprises people.
