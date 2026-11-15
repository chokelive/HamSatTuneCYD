# HamSatTune CYD

An ESP32 Cheap Yellow Display (CYD) sketch showing HamSatTune satellite profiles, UTC time, and tracked RX/TX frequencies. It can pair with an IC-705 over Bluetooth for CI-V readback and automatic frequency control.

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

The display and touch setup targets the common ESP32-2432S028 board with an ILI9341 screen and XPT2046 touch controller. The display uses VSPI, and touch uses software SPI on its own pins. No separate touch library is needed. Other CYD revisions may use different hardware.

The sketch is split into `HamSatTune_CYD.ino` for application flow, `display_ui.cpp/.h` for drawing, `touch_input.cpp/.h` for touch input and calibration, `satellite_data.cpp/.h` for satellite profiles, `tracking.cpp/.h` for orbit tracking, and `radio_ic705.cpp/.h` for Bluetooth CI-V. Keep these files together in the `HamSatTune_CYD` sketch folder.

On first boot, tap the three calibration crosshairs (top-left, top-right, bottom-left). Calibration is saved on the board. Connect a phone to the **HamSatTune-CYD** WiFi access point and open `192.168.4.1` to enter the station's WiFi credentials. The portal closes after two minutes if no connection is made. WiFi supplies UTC through NTP and fresh TLEs from AMSAT; no external clock is needed while WiFi is available. Tap **SATELLITES** to select a profile. Tap **SETUP** to edit the grid locator and callsign using the touch keyboard; **SAVE** stores them in flash. If touch needs recalibration, send `C` in Serial Monitor at 115200 baud.

## IC-705 Bluetooth setup

CI-V control uses the IC-705 BLE serial service.

1. Enable Bluetooth on the IC-705 and leave its CI-V address at **A4**. The old HamSatTune-CYD Classic Data Device pairing is no longer used; remove it from the IC-705 pairing list.
2. Open **SET > Bluetooth Set > Pairing Reception** on the IC-705. The CYD scans for the radio and initiates the BLE connection. Wait for `IC-705 BLE CI-V connected` and `IC-705 BLE CI-V ready for tuning` in Serial Monitor at 115200 baud.
3. Select a satellite profile. Automatic tuning starts when the main screen says **TRACKING** and the radio link says **BT CONTROL**.

The IC-705 BLE CI-V sign-in uses the service and characteristic UUIDs and three identification messages documented by [K7MDL2's working example](https://github.com/K7MDL2/IC-705-BLE-Serial-Example/wiki/Technical-Details). The CYD requires the ESP32 Arduino 3.3.11 BLE library. If the radio has no BLE pairing entry, repeat Pairing Reception. Do not use Search Data Device for CI-V in this build.

Frequency control starts by default when the BLE CI-V link is connected, its receive state is known, and tracking is valid. It waits during transmission or when tracking is unavailable, and configures a new profile automatically. A CI-V write error stops control until reconnection or a profile change. It never sends PTT or restores previous split and mode settings.

ISS SSTV satellite profiles remain available for frequency tracking, but audio decoding and the SSTV screen are not part of this BLE control build.
Tap **MAP** on the main screen to see the selected satellite's position, its 90-minute ground track, and its approximate horizon footprint. The map wraps around the globe to keep the saved QTH longitude at the center. The map stays bright inside the satellite's footprint and dims outside it; the yellow line marks the boundary. The cyan marker shows the saved grid location; the green marker shows the satellite. **BACK** returns to the frequency screen. The map updates every five seconds while BLE tuning continues. It uses the map artwork from the original HamSatTune project, stored in firmware, and does not require a microSD card. When UTC, grid, or TLE data is unavailable, the map shows the tracking status instead of a satellite position.
Tap **PASS** on the map to open the next-pass list. It shows one row per satellite with a current TLE, sorted by active pass and then AOS time. Each row shows its countdown, AOS, LOS, and maximum elevation. Tap a row for duration and AOS/LOS azimuths; use **PREV**, **NEXT**, and **BACK** to navigate. Pass times are UTC. The list searches 24 hours ahead from the saved grid location, updates its countdown every second, and recalculates about once a minute while open. Pass prediction uses a separate SGP4 instance so BLE tuning keeps using the selected satellite's live tracking state.
For the **RS-44** and **FO-29** SSB/CW profiles, turn the IC-705 RX dial while control is running to free tune within the satellite's linear transponder. The CYD follows the new RX frequency with Doppler correction and moves the TX frequency in the opposite direction. The main screen has a **CENTER** button to return to the profile frequency; the radio screen shows **FREE TUNE** when offset from center. Free tuning resets when the Bluetooth link drops or the profile changes. FT4 and FM profiles keep their fixed frequencies. The CYD accepts dial positions only while both the nominal downlink and uplink remain inside the [published transponder passbands](https://www.amsat.org/live-linear-satellites-2/).

When control starts, the sketch selects VFO A, sets split according to whether the profile has an uplink, sets RX/TX modes, and writes Doppler corrected RX to VFO A and TX to VFO B. It reads the radio's TX state and pauses writes while transmitting. It never sends a PTT command. The radio's prior split and mode settings are not restored when control stops.

## Included

- SGP4 orbit propagation, range-rate Doppler, UTC clock, and AMSAT TLE download with flash cache. TLEs older than seven days are not used.
- HamSatTune RX/TX Doppler behavior, including the CW RX offset and FT4 mid-slot TX prediction. Normal profiles update every second; FT4 updates every 250 ms.
- Normal and inverted linear-transponder frequency mapping.
- A CYD landscape display with calibrated touch controls.
- 20 satellite/mode profiles ported from HamSatTune's `Doppler.sqf`, including modes, offsets, transponder direction, and comments.
- IC-705 BLE CI-V pairing, readback, and automatic VFO frequency and mode control.

## Not yet implemented

- Complete HamSatTune radio behavior, including restoring radio settings after control stops.
- A battery-backed clock for operation after a cold boot without WiFi.

The embedded nominal frequencies come from HamSatTune's `Doppler.sqf`. Confirm the current satellite frequency plan before using a profile on air. The display shows `TRACKING` only when UTC, a grid locator, and a current TLE are available. Otherwise it shows nominal frequencies and a reason such as `NO UTC`, `SET GRID`, `NO TLE`, or `TLE OLD`. The bundled SGP4 code is from Hopperpop's Sgp4-Library (MIT license in `src/sgp4/LICENSE`).

## Safety

The radio feature writes CI-V frequency, mode, VFO, and split settings automatically when the IC-705 connects and tracking is valid; it does not key the transmitter. Confirm the selected satellite profile, frequency plan, antenna, and IC-705 display before transmitting. Hardware pairing and radio readback still require verification on a physical IC-705.
