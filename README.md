# HamSatTune CYD v0.1 - Doppler Core Prototype

This is an initial Arduino sketch for the ESP32 Cheap Yellow Display (CYD). It shows a landscape status screen with illustrative Doppler targets; it is not a complete HamSatTune port.

## Open in Arduino IDE

1. Install Arduino IDE and add the Espressif ESP32 boards package using Boards Manager.
2. Open `HamSatTune_CYD/HamSatTune_CYD.ino`.
3. Install **GFX Library for Arduino** by Moon On Our Nation through Library Manager.
4. Select **ESP32 Dev Module** (the common ESP32-2432S028 CYD uses the original ESP32, but check your board revision).
5. Connect the board, select its serial port, then click **Upload**.
6. Open Serial Monitor at **115200 baud** for the same example values.

## Compile without uploading

In Arduino IDE, click **Verify** (the checkmark) to compile the sketch without uploading it to the board. Click **Upload** only when you want to flash the board.

You can also compile from PowerShell or the VS Code terminal. Run this command from the project root (`HamSatTune-CYD`):

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 --jobs 0 --build-path .build HamSatTune_CYD
```

The first compile builds the ESP32 core and libraries and can take a few minutes. Run the same command again after edits to reuse the `.build` files and speed up the compile. Keep the same build path and do not add `--clean` for normal rebuilds. The command compiles only; it does not upload to the board.

## Upload from the terminal

After compiling with the command above, connect the CYD and run this from the project root:

```powershell
arduino-cli upload --port COM14 --fqbn esp32:esp32:esp32 --build-path .build HamSatTune_CYD
```

`COM14` is the port in this project's VS Code configuration. Replace it with the port shown for your board if it changes. Use the same `.build` path for compiling and uploading.

The display and touch setup targets the common ESP32-2432S028 board with an ILI9341 screen and XPT2046 touch controller. Touch is read using the ESP32's built-in SPI library, so no separate touch library is needed. Other CYD revisions may use different hardware.

The sketch is split into `HamSatTune_CYD.ino` for application flow, `display_ui.cpp/.h` for drawing, `touch_input.cpp/.h` for touch input and calibration, `satellite_data.cpp/.h` for satellite profiles, and `doppler.h` for frequency math. Keep these files together in the `HamSatTune_CYD` sketch folder.

On first boot, tap the three calibration crosshairs (top-left, top-right, bottom-left). Calibration is saved on the board. Tap **SATELLITES** to open the satellite/mode list; tap a row to select it or **BACK** to return. Use **PREV** and **NEXT** to move through the list. **DEMO: ON/OFF** toggles simulated Doppler correction. These controls do not operate a radio. If touch needs recalibration, send `C` in Serial Monitor at 115200 baud; if calibration times out, the screen still appears but touch buttons stay inactive until calibration succeeds.

## Included

- Doppler receive and transmit equations with an explicit range-rate sign convention.
- Normal and inverted linear-transponder frequency mapping.
- A CYD landscape display with calibrated touch controls.
- 20 satellite/mode profiles ported from HamSatTune's `Doppler.sqf`, including modes, offsets, transponder direction, and comments.

## Not yet implemented

- SGP4/TLE satellite tracking or observer position and clock management.
- Full parity with HamSatTune's manual tuning and Doppler behavior.
- IC-705 Bluetooth CI-V connection, VFO read/write, and CAT safety interlocks.
- Persistent user settings beyond touch calibration and Wi-Fi TLE updates.

The embedded nominal frequencies come from HamSatTune's `Doppler.sqf`. Confirm the current satellite frequency plan before using a profile on air. **AUTO DEMO** applies a fixed illustrative range rate; it is not live satellite tracking.

## Safety

No CAT commands or transmission are implemented in this prototype. Verify transponder mapping, sign conventions, band limits, and PTT behavior on hardware before enabling automatic TX VFO updates.
