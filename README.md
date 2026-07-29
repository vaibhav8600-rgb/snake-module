# Snake Dongle Module 🐍

Snake Dongle is a compact, highly customizable ZMK-powered dongle that features a Snake‑game-style animation and optional sound effects.
Complete documentation [here](https://github.com/joaopedropio/snake-dongle).
Click [here](https://www.youtube.com/watch?v=xdSUZYLVVY0) to watch a demo.

<img src="https://i.imgur.com/5ogG2z9.jpeg"/> 

---

## Custom Splash Screen (No Fork Required)

Display your own 240×240 image on the splash screen — no forking, no extra tools needed.

### How to add your own splash image

1. **Convert your image** using the [LVGL Image Converter](https://lvgl.io/tools/imageconverter):
   - Upload your 240×240 image
   - Select **CF_TRUE_COLOR** (LVGL v8) or **CF_RGB565** (LVGL v9) as the color format
   - Set the image name to **`custom_splash`**
   - Download the `.c` file

   Both LVGL v8 and v9 converter outputs are supported — the header is auto-detected.
   The generated file contains a `const uint8_t custom_splash_map[]` — a Full RGB565 (65K color) pixel array.

2. **Drop the `.c` file** into your ZMK config directory (next to your `.conf` and `.keymap`).

3. **Add 2 lines** to your `.conf` file (e.g. `central_dongle.conf`):
   ```ini
   CONFIG_SPLASH_USE_CMD_BITMAP=y
   CONFIG_CUSTOM_SPLASH_IMAGE="custom_splash.c"
   ```

4. **Build and flash** — done!

### Optional Settings

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_SPLASH_DISPLAY_TIME_MS` | `4000` | How long the splash stays on screen (ms) |

### Important Notes

- Works with both **LVGL v8** and **v9** image converter output (header auto-detected)
- The image **must be 240×240** pixels and **RGB565** format
- The C array **must be named** `custom_splash_map` (set the image name to `custom_splash` in the converter)
- If the generated array has the `static` keyword, **remove it** — the module needs external linkage
- The `.c` file is typically ~350 KB as source but compiles to ~112 KB in flash
- Display rotation is handled automatically by the module

---

## Anti-Idle (Humanized Mouse Jiggler)

The module ships an `&anti_idle` toggle behavior that keeps the host awake by
sending tiny, humanized mouse movements from the dongle — no software needed
on the host.

- **Toggle on/off** with a single key press (same key toggles both ways)
- **Humanized pattern**: bursts of 4–9 random ±1–2 px moves (15–50 ms apart),
  then a random 20–60 s pause — never a fixed pattern
- **Confirmation**: the cursor twitches ~300 ms after toggling on
- **Status indicator**: a green square (bt-status-ok theme color) in the
  top-right of the dongle status screen while active
- Keeps running until toggled off, independent of the active layer

### Usage

1. Declare the behavior in your keymap:
   ```dts
   anti_idle: anti_idle {
       compatible = "zmk,behavior-anti-idle";
       #binding-cells = <0>;
   };
   ```
2. Bind `&anti_idle` on any key.
3. Make sure the central/dongle build has `CONFIG_ZMK_POINTING=y`.

The jiggler runs only on the central (it owns the HID endpoints); peripheral
builds compile the behavior as a no-op stub.
