# EYEMECH ε3.2 control code adapted for ESP32-S3 with PCA9685 Full Web Dashboard Control
Based on Will Cogley's Eye Mechanism

<div align="center">
  <a href="https://youtube.com/shorts/8luMGPw0Tpo">
    <img src="https://img.youtube.com/vi/8luMGPw0Tpo/hqdefault.jpg" alt="EYEMECH ε3.2 demo — click to watch" width="360">
  </a>
  <br>
  <em>▶ Click to watch the demo</em>
</div>

## Overview

EYEMECH ε3.2 turns Will Cogley's animatronic eye mechanism into a self-contained wireless prop. An **ESP32-S3** drives six servos through a **PCA9685** board and hosts its own **offline Wi-Fi dashboard** — no app to install, no internet, and no extra wiring beyond power, the servos, and the I2C link.

Open the dashboard on your phone and you get a live on-screen twin of the eyes, autonomous lifelike behaviour, a choreography editor, and a guided calibration wizard. Everything is controlled from the web page — there are no joysticks, switches, or potentiometers to wire up.

![EyeMech Control dashboard](TestingScreenshots/e2e-dashboard-full.png)

## Features

- **Self-hosted dashboard** — the device is its own Wi-Fi hotspot and web server. Connect and control it from any phone, tablet, or laptop, fully offline.
- **Live digital twin** — two on-screen SVG eyes mirror the real servos in real time, with a telemetry strip (mode, mood, per-eye aperture, blink, pan/tilt).
- **Personality Engine** — an autonomous "Alive" mode with moods (Calm, Alert, Curious, Sleepy, Skittish), natural blinks, spontaneous glances, and ambient gestures.
- **Choreography editor** — build and play your own keyframe sequences, or trigger ten built-in animations.
- **Guided calibration wizard** — a 14-step flow walks you through setting safe limits for every servo, saved permanently on the device.
- **Phone-friendly** — the layout reflows cleanly for small touch screens.

## What You Need

- ESP32-S3 dev board
- PCA9685 16-channel PWM servo driver
- 6 servos — 1 pan (left/right), 1 tilt (up/down), and 4 eyelids
- A 5–6V power supply for the servos
- Will Cogley's 3D-printed [animatronic eye mechanism](https://makerworld.com/es/models/1184807-animatronic-eye-mechanism-e3-2)

> Earlier builds used a physical joystick, switches, and a trim pot. Those are **gone** — the web dashboard is now the only control surface.

## Wiring

The PCA9685 talks to the ESP32-S3 over I2C, and the six servos plug into the PCA9685's channels.

```
ESP32-S3                 PCA9685
-----------------        -----------------
GND         ------->     GND
5V          ------->     VCC
GPIO4 (SDA) ------->     SDA
GPIO5 (SCL) ------->     SCL

PCA9685 Channel Map
-----------------
Channel 0   ------->     Left/Right pan servo
Channel 1   ------->     Up/Down tilt servo
Channel 2   ------->     Top-Left eyelid
Channel 3   ------->     Bottom-Left eyelid
Channel 4   ------->     Top-Right eyelid
Channel 5   ------->     Bottom-Right eyelid
```

Give the servos their own 5–6V supply with a common ground to the ESP32 — don't try to power them from the board's 3.3V rail.

## Getting Started

### 1. Flash the firmware

The firmware is an Arduino sketch in the `EyeMech/` folder. The easiest path is [`arduino-cli`](https://arduino.github.io/arduino-cli/) with the ESP32 core installed:

```
arduino-cli compile --upload -p <YOUR_PORT> --fqbn esp32:esp32:esp32s3 ./EyeMech
```

One external library is required: **WebSockets by Links2004** (install it via the Library Manager). You can also open `EyeMech/EyeMech.ino` in the Arduino IDE and upload from there.

### 2. Connect to the eyes

Once flashed, the ESP32-S3 creates its own Wi-Fi network:

1. **Find the network:** look for the Wi-Fi SSID `EyeMech-Controller`.
2. **Join it:** the password is `eyemech123`.
3. **Open the dashboard:** go to `http://192.168.4.1` (or `http://eyemech.local`) in any browser.

### 3. Calibrate (first time only)

Tap **Calibrate** and follow the 14-step wizard once to teach each servo its safe range. Your settings are saved on the device and survive power cycles.

## Using the Dashboard

Switch modes from the buttons under the digital twin.

**Sleep** — the resting state. The eyes drift slowly closed and "breathe." Any tap wakes them. The eyes also drop back to sleep on their own after 5 minutes of no activity.

![Sleep view](TestingScreenshots/e2e-view-sleep.png)

**Alive** — the star of the show. The Personality Engine takes over: the eyes glance around, blink naturally, and shift mood on their own. Tap a **mood** (Calm, Alert, Curious, Sleepy, Skittish) to steer the feel, or fire a one-shot **expression** (wink, squint, wide, skeptical).

![Alive view](TestingScreenshots/e2e-view-alive.png)

**Manual** — you take the wheel. Drag the virtual joystick to aim the gaze, and use the **Blink** button on demand. The **Record** button captures your moves into a choreography.

![Manual view](TestingScreenshots/e2e-view-manual.png)

**Choreography** — build a sequence of keyframes, then play, loop, or stop it. Ten built-in animations are ready to go, and your own creations save in the browser.

![Choreography view](TestingScreenshots/e2e-view-choreo.png)

**Calibrate** — the guided wizard for setting each servo's centre, travel, and safe limits. The eyes hold still while you work, and you can save, revert, or reset.

![Calibration wizard](TestingScreenshots/e2e-cal-step01.png)

## How It Works

The firmware runs a simple state machine — Sleep, Alive, Manual, Calibration, and Perform — and serves a single web page straight from the chip's flash. The dashboard talks to the device over a small HTTP API and a WebSocket that streams the live eye state, which is what keeps the on-screen twin in sync with the real servos. Because the ESP32-S3 is its own access point and web server, the whole thing works with no internet connection at all.

Want the full technical details — the HTTP API, architecture, and code layout? See **[README_DEV.md](README_DEV.md)**.

## Acknowledgments

The physical eye mechanism is **Will Cogley's** design — full credit for the hardware goes to his fantastic work:

- [Instructables Guide](https://www.instructables.com/Animatronic-Eye-Mechanism/)
- [Patreon](https://www.patreon.com/c/Will_Cogley/posts)
- [Documentation](https://willcogley.notion.site/EyeMech-3-2-1af24779b64d80b19edfdd795d4b90e5)
- [3D Models](https://makerworld.com/es/models/1184807-animatronic-eye-mechanism-e3-2)

This project is an independent ESP32-S3 + web-dashboard control layer for that hardware.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Feel free to open an issue or submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request
