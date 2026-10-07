# Bluetooth Low Energy (BLE) Communication Protocol

Technical specification for the BLE communication between the **Robosoft Robot (M5StickC Plus + RoverC)** and controller clients (Web Bluetooth, mobile apps, or Python scripts).

---

## 1. Overview & GATT Profile

The robot acts as a **BLE Peripheral (GATT Server)**, and the controller acts as a **BLE Central (GATT Client)**.

### UUID Specifications

| Service / Characteristic | UUID | Properties | Description |
| :--- | :--- | :--- | :--- |
| **Robosoft Service** | `12345678-1234-1234-1234-1234567890ab` | Primary Service | Root service for robot control |
| **RX Characteristic** | `12345678-1234-1234-1234-1234567890ac` | `WRITE`, `WRITE_NO_RESPONSE` | Client writes motor drive commands |
| **TX Characteristic** | `12345678-1234-1234-1234-1234567890ad` | `NOTIFY` | Robot streams telemetry packets |
| **Client Config (CCCD)** | `00002902-0000-1000-8000-00805f9b34fb` | `READ`, `WRITE` | Standard descriptor to enable notifications |

---

## 2. Device Discovery & Advertising

### Device Name Format
- **Format**: `ANT-BOT_<XXXX>`
- **Example**: `ANT-BOT_09EA`, `ANT-BOT_4B2C`
- **Details**: The 4-character suffix `<XXXX>` is dynamically derived from the last 2 bytes of the ESP32's Bluetooth MAC address (`mac[4]` and `mac[5]`). This ensures every robot has a unique identifier when multiple robots are operating in the same area.
- The active device name is displayed on the top line of the M5StickC Plus LCD upon boot.

### Web Bluetooth Discovery Filter
Clients should filter by prefix:
```javascript
const device = await navigator.bluetooth.requestDevice({
  filters: [{ namePrefix: "ANT-BOT" }],
  optionalServices: ["12345678-1234-1234-1234-1234567890ab"]
});
```

---

## 3. Controller to Robot (RX - Command Protocol)

Commands are sent as UTF-8 encoded text packets written to the **RX Characteristic** (`...90ac`).

### 3.1 Packet Framing
- **Delimiter**: `\n` (newline) or `\r\n`.
- **Packet Streaming Frequency**: Recommended **25 Hz to 30 Hz** (~35 ms intervals).
- **Write Type**: `Write Without Response` (WriteNR) is strongly recommended for minimal latency and high frame rates.

### 3.2 Command Formats

#### 3-Axis Mecanum Drive (Standard)
Controls omnidirectional motion (sideway strafe, forward/backward, yaw rotation):

```text
drive <x>,<y>,<z>\n
```
*Also accepts: `motor <x>,<y>,<z>\n` or `<x>,<y>,<z>\n`.*

| Parameter | Range | Direction | Description |
| :---: | :---: | :--- | :--- |
| **`x`** | `-100` .. `+100` | `-`: Strafe Left, `+`: Strafe Right | Sideway / lateral speed |
| **`y`** | `-100` .. `+100` | `-`: Reverse, `+`: Forward | Forward / reverse speed |
| **`z`** | `-100` .. `+100` | `-`: Spin CCW (Left), `+`: Spin CW (Right) | Yaw / rotational steering speed |

> **Auto-Scaling Note**: If values outside `[-100, 100]` are passed (e.g., standard `[-255, 255]`), firmware automatically rescales them to `[-100, 100]`.

#### Examples:
- **Full Forward**: `drive 0,100,0\n`
- **Full Reverse**: `drive 0,-100,0\n`
- **Strafe Right**: `drive 100,0,0\n`
- **Strafe Left**: `drive -100,0,0\n`
- **Rotate In-Place Clockwise**: `drive 0,0,80\n`
- **Forward-Right Diagonal with Turn**: `drive 50,70,30\n`
- **Stop**: `drive 0,0,0\n`

#### 2-Channel Differential Drive (Legacy Fallback)
```text
motor <left>,<right>\n
```
Firmware converts differential left/right inputs into forward (`y = (l + r)/2`) and yaw (`z = (r - l)/2`).

---

## 4. Robot to Controller (TX - Telemetry Protocol)

The robot streams telemetry notifications from the **TX Characteristic** (`...90ad`) at **2 Hz** (every 500 ms) as long as BLE is connected.

### 4.1 Telemetry Packet Format

```text
status V=<voltage> I=<current>\n
```

### 4.2 Fields
| Field | Format | Unit | Description |
| :--- | :--- | :---: | :--- |
| **`V`** | Decimal float (e.g., `4.12`) | Volts ($V$) | Current battery/bus supply voltage measured by AXP192 |
| **`I`** | Decimal float (e.g., `125.4`) | Milliamps ($mA$) | Battery discharge or charge current |

#### Example:
```text
status V=4.08 I=132.5\n
```

### 4.3 Parsing in Client (JavaScript Regex)
```javascript
function handleStatus(msg) {
  const v = msg.match(/V=([-\d.]+)/);
  const i = msg.match(/I=([-\d.]+)/);
  if (v && i) {
    const voltage = parseFloat(v[1]);
    const current = parseFloat(i[1]);
    console.log(`Battery: ${voltage} V, Current: ${current} mA`);
  }
}
```

---

## 5. Safety & Watchdogs

1. **Auto-Stop Watchdog (`AUTO_STOP_TIMEOUT_MS = 3000`)**:
   - If no valid command is received within **3000 ms**, the firmware automatically sets `x=0, y=0, z=0` and stops all motors.
   - This prevents runaway robots in the event of packet loss, browser freeze, or out-of-range disconnection.

2. **GATT Disconnection Hook**:
   - When the client disconnects (`onDisconnect`), all 4 motors are immediately set to 0.
   - Advertising is automatically restarted so the robot is instantly discoverable for reconnects.

3. **Power Cutoff Protection**:
   - If battery voltage falls below **3.0 V** (and external 5V / USB is not connected), the motor driver is disabled to prevent deep discharge of the LiPo cell.

---

## 6. Integration Examples

### JavaScript (Web Bluetooth)
```javascript
const SERVICE = "12345678-1234-1234-1234-1234567890ab";
const RX_UUID = "12345678-1234-1234-1234-1234567890ac";
const TX_UUID = "12345678-1234-1234-1234-1234567890ad";

// 1. Connect
const device = await navigator.bluetooth.requestDevice({
  filters: [{ namePrefix: "ANT-BOT" }],
  optionalServices: [SERVICE]
});
const server = await device.gatt.connect();
const service = await server.getPrimaryService(SERVICE);
const rxChar = await service.getCharacteristic(RX_UUID);
const txChar = await service.getCharacteristic(TX_UUID);

// 2. Subscribe to Telemetry
await txChar.startNotifications();
txChar.addEventListener("characteristicvaluechanged", (e) => {
  const text = new TextDecoder().decode(e.target.value);
  console.log("Telemetry:", text);
});

// 3. Send Drive Command (30ms periodic loop)
function sendDrive(x, y, z) {
  const packet = new TextEncoder().encode(`drive ${x},${y},${z}\n`);
  if (rxChar.writeValueWithoutResponse) {
    rxChar.writeValueWithoutResponse(packet);
  } else {
    rxChar.writeValue(packet);
  }
}
```

### Python (`bleak`)
```python
import asyncio
from bleak import BleakClient, BleakScanner

SERVICE_UUID = "12345678-1234-1234-1234-1234567890ab"
RX_UUID      = "12345678-1234-1234-1234-1234567890ac"
TX_UUID      = "12345678-1234-1234-1234-1234567890ad"

def on_telemetry(sender, data: bytearray):
    print(f"Robot: {data.decode().strip()}")

async def run():
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: d.name and d.name.startswith("ANT-BOT")
    )
    if not device:
        print("Robot not found")
        return

    async with BleakClient(device) as client:
        print(f"Connected to {device.name}")
        await client.start_notify(TX_UUID, on_telemetry)

        # Drive forward for 2 seconds
        for _ in range(40):
            await client.write_gatt_char(RX_UUID, b"drive 0,60,0\n", response=False)
            await asyncio.sleep(0.05)

        # Stop
        await client.write_gatt_char(RX_UUID, b"drive 0,0,0\n", response=False)

asyncio.run(run())
```
