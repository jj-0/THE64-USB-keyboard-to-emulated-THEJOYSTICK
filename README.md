# keyboard2thejoystick

Translates USB keyboard input into virtual THEJOYSTICK events via Linux uinput. This creates a virtual joystick device that THEC64 firmware recognizes natively, allowing you to play games with a keyboard as if a THEJOYSTICK were connected.

The virtual device exactly matches real THEJOYSTICK hardware (name, vendor/product IDs, axis parameters), so the firmware treats it identically to a physical joystick.

## Quick Start (USB drive)

The `USB/` directory contains everything needed to run from a USB drive on THEC64 Mini/Maxi:

1. Copy the `USB/` folder contents to the root of a FAT32-formatted USB drive
2. Insert the drive into THEC64 — `start.sh` runs automatically via the fake firmware update mechanism
3. The script loads the `uinput` kernel module (needed on Mini models), launches `keyboard2thejoystick` in the background, and restarts `the64`

For THEC64 Maxi and later models (Amora/Ares/Snowbird), the uinput module is already built in.

## Usage

```
keyboard2thejoystick [OPTIONS]
```

By default, directions use a QWEASDZXC layout:

```
Q=Up-Left    W=Up      E=Up-Right
A=Left       (S=n/a)   D=Right
Z=Down-Left  X=Down    C=Down-Right
```

Default button keys: Space=Left Fire, Left Alt=Right Fire, [=Left Triangle, ]=Right Triangle, 7-0=Menu 1-4.

### Command-line options

All keys are customizable via command-line options:

```
Direction keys:
  --up KEY         (default: w)       --upleft KEY    (default: q)
  --down KEY       (default: x)       --upright KEY   (default: e)
  --left KEY       (default: a)       --downleft KEY  (default: z)
  --right KEY      (default: d)       --downright KEY (default: c)

Button keys:
  --leftfire KEY   (default: space)   --rightfire KEY (default: lalt)
  --lefttri KEY    (default: bracketleft)
  --righttri KEY   (default: bracketright)
  --menu1 KEY      (default: 7)       --menu2 KEY     (default: 8)
  --menu3 KEY      (default: 9)       --menu4 KEY     (default: 0)

Other:
  --help           Show usage with current configuration
  --guimap         Interactive framebuffer mapping mode
```

Key names can be single characters (`a`, `7`) or names (`space`, `lalt`, `lctrl`, `lshift`, `rshift`, `tab`, `enter`, `esc`, `bracketleft`, `bracketright`, `f1`-`f12`, `up`, `down`, `left`, `right`, etc.).

## Runtime hotkeys

While running, the keyboard is grabbed (exclusive access) to prevent keypresses from reaching other applications. The following hotkeys are always available:

| Hotkey | Action |
|--------|--------|
| **Ctrl+S** | Pause/resume joystick emulation. When paused, the keyboard is ungrabbed and works normally for other applications. Press Ctrl+S again to resume. |
| **Ctrl+R** | Enter the interactive remap GUI (see below). Kills `the64`, opens the framebuffer remapper, then restarts `the64` when done. |
| **Ctrl+C** | Stop and exit. |

## Interactive remap (Ctrl+R)

Pressing Ctrl+R at any time enters a full-screen framebuffer GUI that walks through all 16 inputs (8 directions + 8 buttons), displaying a joystick graphic with the current input highlighted. Press the desired key for each mapping.

After mapping all inputs, a review screen shows the complete configuration with three actions:

- **Apply** — use the new mappings immediately and resume translation
- **Quit** — discard changes and resume with the previous mappings
- **Save** — export the configuration as an executable shell script (`keyboard2thejoystick.sh`) to a directory of your choice via a built-in directory browser, then apply

The review screen and directory browser can be navigated with either the keyboard or a connected joystick.

## Interactive mapping (--guimap)

The `--guimap` option launches the same framebuffer GUI at startup (instead of using the default or CLI-provided mappings), letting you set up all key bindings interactively before translation begins.

## Building

Cross-compile for THEC64 (ARM):

```sh
arm-linux-gnueabihf-gcc -static -O2 -o keyboard2thejoystick keyboard2thejoystick.c
```

The binary must be statically linked since THEC64 has a minimal rootfs.

## How it works

1. Scans `/dev/input/event*` for USB keyboard devices
2. Creates a virtual joystick via `/dev/uinput` with the exact identity of a real THEJOYSTICK
3. Grabs exclusive access to all detected keyboards (EVIOCGRAB)
4. Translates keyboard events to joystick axis/button events in a polling loop
5. Supports diagonal directions via 8-way axis calculation (combining cardinal + diagonal inputs)

Created using [Claude Code](https://claude.ai/code)
