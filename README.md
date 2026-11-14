# HamSatTune CYD

An ESP32 Cheap Yellow Display (CYD) sketch showing HamSatTune satellite profiles, UTC time, and tracked RX/TX frequencies. It can pair with an IC-705 over Bluetooth for CI-V readback and optional frequency control.

## Open in Arduino IDE

1. Install Arduino IDE and add the Espressif ESP32 boards package using Boards Manager.
2. Open `HamSatTune_CYD/HamSatTune_CYD.ino`.
3. Install **GFX Library for Arduino** by Moon On Our Nation through Library Manager.
   Install **WiFiManager** by tzapu as well.
4. Select **ESP32 Dev Module** (the common ESP32-2432S028 CYD uses the original ESP32, but check your board revision). Select **Huge APP (3MB No OTA/1MB SPIFFS)** under Partition Scheme. The Bluetooth build exceeds the default application partition.
5. Connect the board, select its serial port, then click **Upload**.
6. Open Serial Monitor at **115200 baud** for the same example values.

## Compile without uploading

In Arduino IDE, click **Verify** (the checkmark) to compile the sketch without uploading it to the board. Click **Upload** only when you want to flash the board.

You can also compile from PowerShell or the VS Code terminal. Run this command from the project root (`HamSatTune-CYD`):

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --jobs 0 --build-path .build HamSatTune_CYD
```

The first compile builds the ESP32 core and libraries and can take a few minutes. Run the same command again after edits to reuse the `.build` files and speed up the compile. Keep the same build path and do not add `--clean` for normal rebuilds. The command compiles only; it does not upload to the board.

## Upload from the terminal

After compiling with the command above, connect the CYD and run this from the project root:

```powershell
arduino-cli upload --port <your-port> --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --build-path .build HamSatTune_CYD
```

Replace `<your-port>` with the connected board's serial port (for example `/dev/tty.usbserial-1140` on this Mac). Use the same `.build` path for compiling and uploading.

The display and touch setup targets the common ESP32-2432S028 board with an ILI9341 screen and XPT2046 touch controller. Touch is read using the ESP32's built-in SPI library, so no separate touch library is needed. Other CYD revisions may use different hardware.

The sketch is split into `HamSatTune_CYD.ino` for application flow, `display_ui.cpp/.h` for drawing, `touch_input.cpp/.h` for touch input and calibration, `satellite_data.cpp/.h` for satellite profiles, `tracking.cpp/.h` for orbit tracking, and `radio_ic705.cpp/.h` for Bluetooth CI-V. Keep these files together in the `HamSatTune_CYD` sketch folder.

On first boot, tap the three calibration crosshairs (top-left, top-right, bottom-left). Calibration is saved on the board. Connect a phone to the **HamSatTune-CYD** WiFi access point and open `192.168.4.1` to enter the station's WiFi credentials. The portal closes after two minutes if no connection is made. WiFi supplies UTC through NTP and fresh TLEs from AMSAT; no external clock is needed while WiFi is available. Tap **SATELLITES** to select a profile. Tap **SETUP** to edit the grid locator and callsign using the touch keyboard; **SAVE** stores them in flash. If touch needs recalibration, send `C` in Serial Monitor at 115200 baud.

## IC-705 Bluetooth setup

The CYD and IC-705 communicate wirelessly over Bluetooth Classic SPP. No USB or CI-V cable is needed between the two devices. Start pairing and connection from the IC-705, which searches for the CYD's SPP service.

1. On the IC-705, enable Bluetooth. In **MENU > SET > Bluetooth Set > Data Device Set**, set **Serialport Function** to **CI-V (Echo Back OFF)**. Leave the radio's CI-V address at the default **A4**.
2. On the CYD, open **SETUP > IC-705 RADIO**. The CYD advertises the Bluetooth device name **HamSatTune-CYD** and waits for the radio.
3. On the IC-705, open **Bluetooth Set > Pairing/Connect > Device Search > Search Data Device**. Select **HamSatTune-CYD**. If prompted, confirm the passkey shown on the IC-705; the CYD accepts the pairing request and prints the passkey in Serial Monitor.
4. Wait for the CYD radio page to show **CONNECTED** and for RX/TX frequency readback to appear. It also reports whether the radio is receiving or transmitting. If the IC-705 does not find the CYD, confirm Bluetooth is enabled and **Serialport Function** is set to **CI-V (Echo Back OFF)**, tap **PAIR** on the CYD, then search again.
5. Select the desired satellite profile and make sure the main screen shows **TRACKING**. Return to **SETUP > IC-705 RADIO** and tap **START** to enable frequency control. Confirm the radio's VFO and split state on its display. Tap **STOP** to stop control.

Frequency control is off after each CYD reboot. It also stops on a Bluetooth disconnect, satellite change, invalid tracking data, or a CI-V command timeout. While enabled, the CYD selects VFO A, configures split and RX/TX modes for the chosen profile, and updates Doppler-corrected RX on VFO A and TX on VFO B. It reads the radio's transmit state and pauses frequency writes while transmitting; it never sends PTT. The IC-705's previous split and mode settings are not restored when control stops.

When control starts, the sketch selects VFO A, sets split according to whether the profile has an uplink, sets RX/TX modes, and writes Doppler corrected RX to VFO A and TX to VFO B. It reads the radio's TX state and pauses writes while transmitting. It never sends a PTT command. The radio's prior split and mode settings are not restored when control stops.

## Included

- SGP4 orbit propagation, range-rate Doppler, UTC clock, and AMSAT TLE download with flash cache. TLEs older than seven days are not used.
- HamSatTune RX/TX Doppler behavior, including the CW RX offset and FT4 mid-slot TX prediction. Normal profiles update every second; FT4 updates every 250 ms.
- Normal and inverted linear-transponder frequency mapping.
- A CYD landscape display with calibrated touch controls.
- 20 satellite/mode profiles ported from HamSatTune's `Doppler.sqf`, including modes, offsets, transponder direction, and comments.
- IC-705 Bluetooth Classic SPP pairing, CI-V readback, and opt-in VFO frequency and mode control.

## Not yet implemented

- HamSatTune manual radio tuning and correction of a reverse transponder after manual RX tuning.
- Complete HamSatTune radio behavior, including manual tuning and restoring radio settings after control stops.
- A battery-backed clock for operation after a cold boot without WiFi.

The embedded nominal frequencies come from HamSatTune's `Doppler.sqf`. Confirm the current satellite frequency plan before using a profile on air. The display shows `TRACKING` only when UTC, a grid locator, and a current TLE are available. Otherwise it shows nominal frequencies and a reason such as `NO UTC`, `SET GRID`, `NO TLE`, or `TLE OLD`. The bundled SGP4 code is from Hopperpop's Sgp4-Library (MIT license in `src/sgp4/LICENSE`).

## Safety

The radio feature writes CI-V frequency, mode, VFO, and split settings after you tap **START**; it does not key the transmitter. Confirm the selected satellite profile, frequency plan, antenna, and IC-705 display before transmitting. Hardware pairing and radio readback still require verification on a physical IC-705.
