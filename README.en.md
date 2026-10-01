# ESP32-Scorpion

[Русская версия](README.md)

An emulator of the **Scorpion ZS-256** (a ZX Spectrum-compatible computer with 256 KB of RAM)
on an ESP32-S3 board: VGA monitor output, USB keyboard, AY-3-8912 and beeper sound through an
I2S DAC, and TR-DOS with disk images on an SD card.

## Features

- Scorpion ZS-256: 256 KB RAM, ROM 2.95 (menu, Basic 128/48, shadow monitor via Magic, TR-DOS 5.03)
- VGA 640×480@60, 15 colours (with BRIGHT), FLASH
- USB keyboard (boot protocol) via OTG
- Sound: AY-3-8912 + beeper, ACB stereo, 48 kHz on a PCM5102
- TR-DOS: emulated WD1793 (VG93) controller, drives A and B, `.trd` and `.scl` images
- Disk images on an SD card; TR-DOS writes are saved back to the `.trd` on the card
- Disk selection menu on F9
- Tape: instant loading of `.tap` files (LD-BYTES trap)
- USB disk mode: connected to a PC by its USB port, the board shows up as a flash drive (the SD card or the internal flash)

## What you need

| Part | Note |
|---|---|
| ESP32-S3 DevKitC-1 or a clone, **16 MB flash, 8 MB OPI PSRAM** (N16R8) | tested on YD-ESP32-S3 |
| 74AC244 (or 74HC244) | VGA buffer |
| Resistors 360 Ω ×3, 1.5 kΩ ×3, 68 Ω ×2 | VGA DAC |
| VGA connector (DB15) | |
| PCM5102 module (I2S) | |
| microSD module (SPI, 3.3 V) | FAT32 card |
| USB-OTG adapter, USB-C → USB-A | for the keyboard |
| USB keyboard | must support the boot protocol (almost any does) |

## Wiring

### VGA through a 74AC244

| ESP32-S3 | 74AC244 in → out | Resistor | VGA (DB15) |
|---|---|---|---|
| GPIO4 (R) | 2 → 18 | 360 Ω | 1 (Red) |
| GPIO14 (R+) | 4 → 16 | 1.5 kΩ | 1 (Red) |
| GPIO5 (G) | 6 → 14 | 360 Ω | 2 (Green) |
| GPIO15 (G+) | 8 → 12 | 1.5 kΩ | 2 (Green) |
| GPIO6 (B) | 11 → 9 | 360 Ω | 3 (Blue) |
| GPIO16 (B+) | 13 → 7 | 1.5 kΩ | 3 (Blue) |
| GPIO7 (HSYNC) | 15 → 5 | 68 Ω | 13 |
| GPIO8 (VSYNC) | 17 → 3 | 68 Ω | 14 |

- 74AC244: pins 1 and 19 (OE) and 10 (GND) go to ground; pin 20 (VCC) is 3.3 V **from a separate
  regulator** (fed from 5 V), not from the board's 3.3 V rail — otherwise faint stripes caused by
  the ESP32's load show up on light backgrounds. Put a 100 nF ceramic capacitor right across
  pins 20 and 10, with 10 µF nearby.
- VGA grounds (5, 6, 7, 8, 10) go to the board's GND with short wires.
- All 8 buffer inputs must be connected (to a GPIO or to GND). A floating 74AC input oscillates,
  which breaks vertical sync and causes stripes.
- The R/R+ resistors (and G/G+, B/B+) join at the VGA pin: normal colour ≈ 0.55 V,
  bright ≈ 0.68 V.
- If you don't need BRIGHT, leave out the 1.5 kΩ resistors and tie inputs 4, 8 and 13 to GND.

### Sound: PCM5102

| PCM5102 | ESP32-S3 |
|---|---|
| VIN | 5V |
| GND | GND |
| BCK | GPIO1 |
| LCK | GPIO2 |
| DIN | GPIO9 |
| SCK | GND (the DAC generates its clock from BCK) |

Jumper on the front: SCK = closed (to GND).  
Jumpers on the back of the module: FLT = L, DEMP = L, **XSMT = H** (otherwise the DAC stays muted), FMT = L.

### SD card (SPI)

| Module | ESP32-S3 |
|---|---|
| 3V3 | 3V3 |
| GND | GND |
| CS | GPIO10 |
| MOSI | GPIO11 |
| CLK | GPIO12 |
| MISO | GPIO13 |

### USB

- **COM** port — flashing and Serial.
- **USB** port (native) — keyboard through an OTG adapter. On board clones the **USB-OTG**
  jumper must be soldered (it supplies 5 V to this port).
- **Never connect both ports to a computer at the same time.**

## Building

### 1. Arduino IDE

- Core **esp32 by Espressif 3.3.12** (Boards Manager).
- Tools:
  - Board: **ESP32S3 Dev Module**
  - USB Mode: **USB-OTG (TinyUSB)**
  - USB CDC On Boot: **Disabled**
  - PSRAM: **OPI PSRAM**
  - Flash Size: **16MB (128Mb)**
  - Partition Scheme: **16M Flash (2MB APP/12.5MB FATFS)**
  - USB Firmware MSC On Boot: **Disabled**

The sketch catches wrong USB Mode, CDC On Boot and PSRAM settings at compile time (`#error`).

> **If uploading fails with "MD5 of file does not match data in flash"**, set
> **Erase All Flash Before Sketch Upload: Enabled**. This happens on clones with a
> Spansion/Infineon S25FL127S/128S flash chip (JEDEC ID `01 2018`, check with `esptool flash-id`):
> its 4 KB erase only works at the very start and end of memory. The emulator handles this
> (the image partition is written in 64 KB blocks), but a regular upload from the IDE does not.
> Erase All also wipes the images in flash — upload them again afterwards.

### 2. ROM (required)

The ROM is not included: it contains third-party code (Scorpion firmware, Basic 48/128, TR-DOS).
You need a **Scorpion ZS-256 version 2.95** ROM file, 64 KB (tested: CRC32 `0C6C1EF6`).
Where to get it (the same file, `scorp295-0C6C1EF7.rom`; the CRC in the name has a typo, the actual
CRC32 is `0C6C1EF6` — this is the right one):
- [speccy4ever: Scorpion ROMs](https://speccy4ever.speccy.org/_SC.htm), row "ZS-256 V.2.95" — [direct link](https://speccy4ever.speccy.org/rom/scorp295-0C6C1EF7.rom);
- [the Scorpion ZS-256 Turbo+ project](https://github.com/romychs/Scorpion256TPlus/blob/main/ROM/scorp295-0C6C1EF7.rom) on GitHub.

```sh
python3 tools/rom2h.py scorp295-0C6C1EF7.rom > ESP32-Scorpion/rom_scorpion.h
```

### 3. Tape (optional)

```sh
python3 tools/tap2h.py game.tap > ESP32-Scorpion/tape.h
```

Without `tape.h` the tape deck is empty: `LOAD ""` waits as if no cassette were inserted (Esc = BREAK).

### 4. Upload

Open `ESP32-Scorpion/ESP32-Scorpion.ino` and upload through the COM port.

## Disk images

Where the emulator looks for images (`.trd`, `.scl`, in the root directory), in this order:

1. **SD card** (FAT32). Changes made in TR-DOS are saved back to the `.trd`
   (only the changed tracks, about 0.5 s after the last write). `.scl` is read-only.
2. **Internal flash** (~12 MB) — if there is no card.
3. **`disks.h`** built into the firmware: `python3 tools/disk2h.py disk.scl > ESP32-Scorpion/disks.h`.

**Copying files without a card reader — USB disk mode.** Connect **only the USB port** to a PC
(with a regular cable, no OTG adapter): after ~1.5 s the board becomes a flash drive and the
screen shows "USB DISK MODE". If an SD card is inserted, the PC sees **the card** (just like a
card reader); otherwise it sees **the internal flash**. Copy the files, eject, unplug.
Format the internal flash from the PC before first use: `sudo mkfs.vfat -I -S 4096 -n ZXDISKS /dev/sdX` (Linux).

Only files in the root directory are listed, so you can keep an archive in a subfolder.
At startup the first two images in alphabetical order are inserted into A and B.

## Keys

| Key | Action |
|---|---|
| Shift | CAPS SHIFT |
| Ctrl, Alt | SYMBOL SHIFT |
| Backspace | DELETE (CAPS+0) |
| Arrows | CAPS+5..8 |
| Esc | BREAK |
| CapsLock | CAPS LOCK (CAPS+2) |
| Tab | extended mode (CAPS+SYM) |
| **F9** | disk menu: ↑/↓ (hold to scroll), PgUp/PgDn, Home/End; A or Enter → drive A, B → drive B, `<empty>` ejects, Esc — back |
| **F10** or Ctrl+Alt+Del | reset |
| **F11** | Magic (shadow monitor) |
| **F12** | rewind the tape + `LOAD ""` (from 48 BASIC) |

## TR-DOS in brief

- "128 TR-DOS" in the Scorpion menu runs `boot` from drive A (or shows the `A>` prompt).
- To get to the command prompt without `boot`: choose "48 BASIC", then `RANDOMIZE USR 15616`.
- Switch drives: `*"B:"`. Back to BASIC: `RETURN`.

## Limitations

- Frame timing is the 48K one (69888 T-states), with no contended memory delays.
- WD1793: Read Track is not implemented (TR-DOS doesn't use it).
- VGA timing is slightly non-standard (763 pixels per line) — the monitor may need "Auto adjust".

## Credits

- Z80 emulator — [z80emu](https://github.com/anotherlin/z80emu) by Lin Ke-Fong (the author's free
  licence: "do whatever you want with it"; the z80emu* files keep their own headers).
- Built with the help of [Claude](https://claude.ai) Opus 5.5 (Anthropic).

## Licence

Copyright (C) 2026 Mike Deblin. [GNU GPL version 3](LICENSE).
Modified versions you distribute must also be open under the GPL.
ROMs, games and disk images are not part of the project — they belong to their respective rights holders.
