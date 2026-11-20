# POV

A persistence-of-vision display built on an ESP8266. A column of 12 LEDs is
spun by a motor; a hall sensor pulses once per revolution, and the firmware
uses that period to work out the current angular position and clock the right
12-bit column out to the LED driver. Animations are selected from a small web
page served by the ESP itself.

<img width="400" height="492" alt="SunFlower" src="https://github.com/user-attachments/assets/b7d47eed-a24a-4656-9ab8-0268e6367a50" />


## Layout

| Path                    | Contents                                                        |
| ----------------------- | --------------------------------------------------------------- |
| `src/pov/`              | Arduino sketch (`pov.ino`) and the generated bitmap headers       |
| `support/scripts/`      | Python helpers: image converters and a pattern previewer           |
| `support/images/`       | Source artwork the headers were generated from                    |
| `pcb/`                  | KiCad project and the fabrication Gerbers                         |
| `3dprint/`              | STLs for the shaft and motor adapter                              |

The sketch folder must stay named `pov` to match `pov.ino` — that is an
Arduino requirement, not a preference.

## Hardware

The full schematic is the KiCad project in `pcb/`. An ESP-12F runs everything,
a TLC5926 16-channel constant-current sink (U4) drives the 12 LEDs from OUT0 to
OUT11, a DRV5033 hall switch (U5) marks each revolution, and an AP2112K-3.3
regulator feeds the ESP from the battery.

| Signal      | GPIO | Goes to                                           |
| ----------- | ---- | ------------------------------------------------- |
| `TLC_CLK`   | 14   | TLC5926 CLK, serial clock                         |
| `TLC_DIN`   | 13   | TLC5926 SDI, LSB is D1 (outermost LED)            |
| `TLC_LATCH` | 12   | TLC5926 LE, latch pulse after each 12-bit column  |
| `TLC_EN`    | 5    | TLC5926 OE, output enable, active low             |
| `HE_SENS`   | 4    | DRV5033 output, one falling edge per revolution   |
| `BL_ADC`    | A0   | Battery sense through the R1/R2 100k/10k divider  |

The battery readout maps 3.45 V to empty and 4.2 V to full. The divider keeps a
full cell inside the ESP8266's 1.0 V ADC reference, so `conversionFactor` in
`pov.ino` has to match the resistors fitted.

### LED brightness

**R8**, on the driver's R-EXT pin, sets the current every output sinks, so it is
the component to change if the display is too dim or too bright. At the
TLC5926's power-on defaults:

```
Iout = (1.25 V / R8) x 15
```

R8 is 1.8 kΩ, which works out to about **10.4 mA per LED**, or roughly 125 mA
off the battery with a whole column lit. A smaller resistor is brighter: 820 Ω
gives about 23 mA and 390 Ω about 48 mA.


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
If connecting via pc, you can use the mDNS pov.local or use the ESP Serial monitor to get the IP address. Alternatively, you can use nmap to get the ESP ip.
```
http://pov.local
```
<img width="1064" height="270" alt="WS" src="https://github.com/user-attachments/assets/1b36819d-cafe-4eed-af83-a7efd34d50ef" />

## Adding an animation

Write a `begin_` function that draws the first frame and resets any state, and
a `tick_` function that advances it (use `tick_none` for a static image), then
add a row to the `anims[]` table in `pov.ino`:

```c
{"Rose", begin_rose, tick_rose, SLICE_NUM, 8000, ANIM_AUTO | ANIM_PICK},
```

| Column  | Meaning                                                          |
| ------- | ---------------------------------------------------------------- |
| name    | Label for the web dropdown                                       |
| begin   | Called once when the animation is selected                       |
| tick    | Called on every loop pass to advance it                          |
| slices  | Angular steps this animation is drawn at, 360 maximum            |
| dwellMs | How long auto mode shows it before moving on                     |
| flags   | `ANIM_AUTO` auto cycle, `ANIM_PICK` dropdown, `ANIM_TEXT` text box |

The flags are independent, so `ANIM_AUTO` on its own gives a card that auto
mode plays but nobody can pick by hand, which is how the intro and outro cards
work. `select_anim()` clears the shared state and applies the slice count
before calling `begin`, so animations do not need to undo each other's work.
One that runs longer than its dwell should set `animationInProgress` while it
runs and `animDone` when it finishes, the way Kick Foot Ball does.

## Creating the content

Whatever the animation does, it ends up in one array: `frame[i]` holds the 12
LEDs for slice `i`, bit 0 being the outermost. There are three ways to fill it.

### 1. A maths function

The cheapest option, since it stores no data at all: work out a radius for each
angle and set that bit. This is how `Sine`, `Phyllotaxis` and the sweep group
patterns work.

```c
for (uint16_t i = 0; i < SLICE_NUM; i++)
  frame[i] = 1 << ((i / 10 + step) % 12);   // three rotating spiral arms
```

Precompute anything expensive once at startup, the way `populate_sine_wave()`
does, and keep the per-tick work cheap so slice output stays in step with the
motor. `support/scripts/PatternPreview.py` renders a pattern as an animated GIF
of the spinning disc, which is quicker than reflashing to see how it looks.

### 2. Hand-drawn sprites

For a figure like the walker or the kicker, draw each frame in Krita or any
pixel editor as a **10 px wide by 12 px tall** image, black on white, and
export it as PNG or JPG. One pixel is one LED: width is angular slices, height
is the LED column. Then convert:

```sh
python3 support/scripts/ImgToBmpRectangular.py
```

It packs each column into a `uint16_t`, treating black as lit, and prints a C
array to paste into `alpha.h`. Input paths are hardcoded at the bottom of the
script, so point them at your own files. Frames are stored back to back and
indexed as `walker[frame * 10 + column]`.

### 3. An existing image

For a full-disc picture, let the converter scan it radially:

```sh
python3 support/scripts/ImgToBmpCircular.py support/images/sml1.jpg
```

It thresholds the image to black and white, samples 360 angles by 12 radii,
masks out the inner 35% where there are no LEDs, and prints a
`const uint16_t pov_frame[360]` along with a preview plot. `smiley.h` is six of
these, one per frame.

Both converters print to stdout and need `numpy`, `pillow` and `matplotlib`.
Keep source artwork in `support/images/`, which is where they look by default.

### 4. Youtube Shorts

https://youtube.com/shorts/mBMTX7Lzo30?si=xlOcu3QmO_tnsg1q
