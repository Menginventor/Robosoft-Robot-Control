# Robosoft-Robot-Control

Web Bluetooth (BLE) robot controller for **M5StickC / M5StickC Plus** mounted on the **M5Stack RoverC** base.

## 🌐 Live Demo
Access the live web controller here:  
👉 **[https://menginventor.github.io/Robosoft-Robot-Control/](https://menginventor.github.io/Robosoft-Robot-Control/)**

*(Requires a Web Bluetooth-supported browser such as Google Chrome on Android, macOS, or Windows)*

---

## 🚀 Features
- **Web Bluetooth (BLE) Control**: Connect directly from your browser with no native app installation needed.
- **Dual Virtual Joysticks**: Left joystick controls robot yaw (rotation in place), and right joystick controls throttle & steering with multi-touch support.
- **Telemetry Display**: Live battery voltage ($V$) and current ($mA$) readings streamed over BLE notifications.
- **Safety Auto-Stop**: Watchdog timer stops the motors automatically if BLE connection or commands drop.
- **Camera Background View**: Optional FPV-style camera feed backdrop while driving.
- **Configurable Controls**: Adjustable max speed limits, invert steering, invert yaw, and invert throttle.
- **Onboard Screen Status**: Displays live connection status, RoverC base detection, battery readings, and motor speeds on the M5StickC Plus LCD.

---

## 🛠️ Hardware Requirements
- **Microcontroller**: M5Stack M5StickC / M5StickC Plus
- **Chassis**: M5Stack RoverC (Omnidirectional Mecanum Base)
- **Communication**: Bluetooth Low Energy (BLE)

---

## 📡 BLE Specifications
- **Device Name**: `ANT-BOT_09EA`
- **Service UUID**: `12345678-1234-1234-1234-1234567890ab`
- **RX UUID (Write)**: `12345678-1234-1234-1234-1234567890ac`
- **TX UUID (Notify)**: `12345678-1234-1234-1234-1234567890ad`

---

## 💻 Getting Started

### 1. Firmware (PlatformIO)
1. Open this repository in VS Code with PlatformIO extension installed.
2. Connect your M5StickC via USB-C.
3. Build and upload:
   ```bash
   pio run --target upload
   ```

### 2. Web Controller
1. Open the [Live Demo](https://menginventor.github.io/Robosoft-Robot-Control/) or host `index.html` locally using any static web server:
   ```bash
   npx serve .
   ```
2. Open the menu (☰) in the top-left corner and click **Connect to BLE**.
3. Select **`ANT-BOT_09EA`** from the browser popup.
4. Drive with the on-screen joystick!
