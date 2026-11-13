# corvus-rtl

RTL2832U dongles (RTL-SDR Blog V3/V4, NooElec NESDR, FlightAware Pro Stick and the rest of the R820T, R828D,
E4000, FC0012/13 and FC2580 family) for [Corvus Sweep](https://github.com/auggiesoft/corvus), as files it
downloads when one is plugged in. Corvus Sweep itself stays one MIT-licensed program with nothing beside it;
what the dongles need lives here, under the licences of the code it's built from.

> **Unofficial.** Not affiliated with, endorsed by or supported by the RTL-SDR Blog, Osmocom, Realtek,
> NooElec, FlightAware or the libwdi project.

## What a release holds

| File | What it is | Licence |
|---|---|---|
| `corvus-rtl-<v>-windows-x64.dll` | librtlsdr (the RTL-SDR Blog's fork, V1.4.0) with libusb 1.0.29 built in: one DLL, importing only `KERNEL32` and `msvcrt` | GPL-2.0-or-later |
| `libcorvus-rtl-<v>-linux-x86_64.so` | the same librtlsdr for Linux, against the system's `libusb-1.0.so.0`; detaches the DVB-T kernel driver when it opens a dongle | GPL-2.0-or-later |
| `corvus-rtl-driver-<v>-windows-x64.exe` | Windows: gives the dongles Windows' own WinUSB driver (what Zadig does, without its window) | LGPL-3.0-or-later |
| `SHA256SUMS` | checksums | |

Corvus Sweep pins the version and the SHA-256 of each file it uses: a new release here changes nothing for an
existing Corvus Sweep until one is built to use it.

## The Windows driver

Windows has no driver that lets a program read an RTL2832U dongle, and the dongle can't ask for one (its USB
descriptors are in ROM). `corvus-rtl-driver.exe` (Windows 8 or later; it asks for administrator rights):

1. finds the dongles plugged in, and leaves any already on WinUSB alone (`--force` reinstalls);
2. writes a driver package: an INF that points every dongle librtlsdr knows (42 USB IDs, the whole device and
   its first interface, as Windows sees these composite devices) at Windows' WinUSB. No driver files of its own;
3. makes a catalog for it and signs that with a certificate created for the purpose, which goes into the
   machine's *Root* and *TrustedPublisher* stores with its private key deleted, so it can sign nothing else
   (libwdi's `pki.c`, as Zadig does);
4. stages the package in the driver store and installs it on each dongle plugged in.

`--result FILE` writes one line per step, ending `OK`, `NODEVICE` (nothing plugged in; the package is staged
for later) or `ERROR`. Exit status 0, 2 and 1 to match. To undo: Device Manager → the dongle → *Uninstall
device* with *Delete the driver software*, and remove the "Corvus RTL WinUSB driver" certificate from
`certlm.msc` (Trusted Root Certification Authorities and Trusted Publishers).

## Linux

The `.so` needs `libusb-1.0` (every desktop distribution has it) and permission to open the dongle: a udev rule
such as the one in `rtl-sdr/rtl-sdr.rules` (copy it to `/etc/udev/rules.d/`, then replug), or your
distribution's `rtl-sdr` package, which installs it. The DVB-T kernel driver is detached when the dongle is
opened; blacklisting `dvb_usb_rtl28xxu` keeps it away entirely.

## Building

`build/build.sh [version]` builds everything in Docker (Fedora's MinGW-w64 for Windows, with libusb built
statically from its release tarball, checked against its published SHA-256; Ubuntu 24.04 for Linux) into
`out/`, with `SHA256SUMS`.

## Layout

| Path | |
|---|---|
| `rtl-sdr/` | [rtlsdrblog/rtl-sdr-blog](https://github.com/rtlsdrblog/rtl-sdr-blog) V1.4.0 (aed0ea1), unmodified |
| `libwdi/` | [pbatard/libwdi](https://github.com/pbatard/libwdi) v1.5.1 (9b23b82), unmodified; only `pki.c` and `logging.c` are built |
| `driver/` | the WinUSB installer: `corvus-rtl-driver.c`, `wdi_glue.c` (what `pki.c` takes from the rest of libwdi), its manifest |
| `build/` | `build.sh` and the two Dockerfiles |

## Licences

The librtlsdr libraries are GPL-2.0-or-later (`LICENSE-GPL-2.0.txt`); libusb inside the Windows DLL is
LGPL-2.1-or-later. The driver installer is LGPL-3.0-or-later (`LICENSE-LGPL-3.0.txt`, which builds on
`LICENSE-GPL-3.0.txt`), like the libwdi code it contains. The source of every file in a release is this
repository at that release's tag.
