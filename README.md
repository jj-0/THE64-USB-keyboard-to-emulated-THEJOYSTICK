## keyboard2thejoystick

Translates USB keyboard input into virtual THEJOYSTICK events via Linux uinput. This creates a virtual joystick device that THEC64 firmware recognizes natively, allowing you to play games with a keyboard as if a THEJOYSTICK were connected.

### Usage

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

### Interactive mapping (--guimap)

The `--guimap` option launches a framebuffer GUI (same style as the gamepad mapper) that walks through each of the 16 inputs, lets you press the desired key, and saves the result as an executable shell script you can run directly.

Created using Claude Code
