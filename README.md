# POV

A persistence-of-vision display built on an ESP8266. A column of 12 LEDs is
spun by a motor; a hall sensor pulses once per revolution, and the firmware
uses that period to work out the current angular position and clock the right
12-bit column out to the LED driver. Animations are selected from a small web
page served by the ESP itself.

## Layout

| Path                    | Contents                                                        |
| ----------------------- | --------------------------------------------------------------- |
| `src/pov/`              | Arduino sketch (`pov.ino`) and the generated bitmap headers       |
| `support/scripts/`      | Python converters that turn images into C arrays                  |
| `support/images/`       | Source artwork the headers were generated from                    |
| `pcb/`                  | KiCad project and the fabrication Gerbers                         |
| `3dprint/`              | STLs for the shaft and motor adapter                              |

The sketch folder must stay named `pov` to match `pov.ino` — that is an
Arduino requirement, not a preference.

## Hardware

| Signal      | GPIO | Notes                                       |
| ----------- | ---- | ------------------------------------------- |
| `TLC_CLK`   | 14   | Serial clock to the LED driver              |
| `TLC_DIN`   | 13   | Serial data, LSB is D1 (outermost LED)      |
| `TLC_LATCH` | 12   | Latch pulse after each 12-bit column        |
| `TLC_EN`    | 5    | Driver output enable, active low            |
| `HE_SENS`   | 4    | Hall sensor, one falling edge per revolution |
| `BL_ADC`    | A0   | Battery sense through a 100k/10k divider    |

The battery readout maps 3.45 V to empty and 4.2 V to full.

The divider ratio matters more than it looks: the ESP8266's ADC reference is
**1.0 V**, not 3.3 V. With 100k on the high side and 10k to ground, a full
4.2 V cell presents 0.38 V at the pin, safely inside that range, and
`conversionFactor` in `pov.ino` is the matching `10/(10+100)`. Change one and
you must change the other.

## Build and flash

Select an ESP8266 board in the Arduino IDE and open `src/pov/pov.ino`, or from
the command line:

```sh
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 src/pov
arduino-cli upload  --fqbn esp8266:esp8266:nodemcuv2 -p /dev/ttyUSB0 src/pov
```

The build uses roughly 29% of flash and 49% of RAM, so there is room to add
animations.

## Wi-Fi and the web page

Edit the credentials at the top of `pov.ino` before flashing:

```c
const char WIFI_SSID[] = "my-hotspot";
const char WIFI_PASS[] = "my-password";
```

The ESP joins as a station and retries every 5 seconds in the background; it
never blocks the display loop waiting for a connection. Once connected it
prints its IP to serial at 115200 baud and registers as `pov.local` over mDNS.

The page is a single dropdown plus a submit button, under 1 kB so it arrives
in one segment. Pick **Auto** to cycle through the animations, or pick one by
name to hold it. Selecting **Text** or **Scroll text** reveals a text box whose
contents are drawn on the display.

Note that `server.handleClient()` runs inside the display loop, so loading the
page will briefly stall slice output and you may see a flicker for a frame or
two. This is expected.

Text is folded to upper case and anything outside `0x20`–`0x5A` becomes a
space, because that is all the 5x7 font in `alpha.h` covers. Input is capped at
32 characters.

## Animations

| Animation      | Slices  | Dwell | Notes                                        |
| -------------- | ------- | ----- | -------------------------------------------- |
| Sine           | 360     | 6 s   | Travelling sine with a six-sample trail       |
| Eyes           | 120     | 9 s   | Pupils track a pattern and blink at random    |
| Smiley         | 360     | 9 s   | Six-frame bitmap, ping-pongs                  |
| Battery        | 120     | 3 s   | Bar graph plus label                          |
| Kick Foot Ball | 100/60  | ~11 s | One shot, always runs to completion           |
| Sparkle        | 360     | 7 s   | Random pixels with decaying lifetimes         |
| Sweep group    | 360     | 32 s  | Four maths patterns, 8 s each                 |
| Text           | fitted  | 6 s   | Centred user text                             |
| Scroll text    | fitted  | 8 s   | Scrolling user text                           |

"Slices" is how many angular steps the circle is divided into for that
animation; the text animations size it to the length of the message so a long
string wraps the full circle instead of overwriting its own start.

Auto mode also plays a few intro and outro cards that are not offered in the
dropdown.

**Sweep group** cycles four generated patterns, 8 seconds each: three rotating
spiral arms, a radar sweep whose tail thins towards the tip, two rings
expanding outward together, and a moiré built from two counter-rotating
triangle waves at 5 and 8 cycles per revolution. All four are integer-only and
add no flash or RAM beyond their code, since nothing is stored as a bitmap.

**Kick Foot Ball** runs as three phases: the figure walks in for 2.5 s at 100
slices, the kick sequence plays at 60 slices, and the "OUCH" pose is held for
2.5 s back at 100 slices. Each phase keeps the slice count its sprites were
drawn for, so the figure stays the size it was meant to be. The whole sequence
sets `animationInProgress`, so neither the dwell timer nor the auto cycle can
cut it short.

## Adding an animation

Write a `begin_` function that draws the first frame and resets any state, and
a `tick_` function that advances it (use `tick_none` for a static image), then
add a row to the table in `pov.ino`:

```c
{"Name", begin_name, tick_name, SLICE_NUM, 5000, ANIM_AUTO | ANIM_PICK},
```

The flags control where it shows up, and they are independent of each other:

- `ANIM_AUTO` puts it in the auto cycle.
- `ANIM_PICK` offers it in the web dropdown.
- `ANIM_TEXT` marks it as using the web text box.

So `ANIM_AUTO` alone gives an animation that auto mode plays but nobody can
pick by hand, which is how the intro and outro cards work.

`select_anim()` clears the shared state and applies the slice count before
calling `begin`, so animations do not need to undo each other's work. A
long-running animation that must not be cut short by its dwell timer should set
`animationInProgress` while it runs and `animDone` when it finishes, the way
Kick Foot Ball does.

## Support scripts

Both converters print a C array to stdout for pasting into a header. They
resolve images relative to `support/images/`, so they can be run from anywhere:

```sh
python3 support/scripts/ImgToBmpCircular.py support/images/sml1.jpg
python3 support/scripts/ImgToBmpRectangular.py
```

`ImgToBmpCircular.py` samples a square image in polar coordinates into 360
slices of 12 rows, masking out the inner 35% where there are no LEDs; this is
what generated `smiley.h`. `ImgToBmpRectangular.py` packs small 10x12 pixel-art
frames into the sprite arrays at the bottom of `alpha.h`. They need `numpy`,
`pillow` and `matplotlib`.
