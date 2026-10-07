#include <M5StickCPlus.h>
#include <M5_RoverC.h>

// ===== BLE =====
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// =================================================
// RoverC Instance
M5_RoverC roverc;
bool roverDetected = false;

// =================================================
// BLE Configuration
#define BLE_DEVICE_NAME   "ANT-BOT_09EA"
#define BLE_SERVICE_UUID  "12345678-1234-1234-1234-1234567890ab"
#define BLE_RX_UUID       "12345678-1234-1234-1234-1234567890ac"
#define BLE_TX_UUID       "12345678-1234-1234-1234-1234567890ad"

// ---------------- Motor Direction ----------------
const int MOTOR_A_DIR = 1;
const int MOTOR_B_DIR = 1;

// ---------------- Safety ----------------
const float MIN_SUPPLY_VOLTAGE = 3.0;

// ---------------- Auto Stop ----------------
const bool AUTO_STOP_ENABLED = true;
const unsigned long AUTO_STOP_TIMEOUT_MS = 3000;

// ---------------- Status Push ----------------
const unsigned long STATUS_PERIOD_MS = 500;
unsigned long lastStatusTime = 0;

// ---------------- Display ----------------
const unsigned long DISPLAY_PERIOD_MS = 200;
unsigned long lastDisplayTime = 0;
int lastBleState = -1; // -1 forces initial full render

// ---------------- State ----------------
bool driverEnabled = false;
bool bleConnected  = false;

unsigned long lastCommandTime = 0;
int cmd_x = 0; // sideway / strafe (-100..100)
int cmd_y = 0; // forward / reverse (-100..100)
int cmd_z = 0; // yaw / steering (-100..100)

// ---------------- BLE ----------------
BLECharacteristic *bleTx;
String rxBLE = "";

// =================================================
// Forward declarations
void parseCommand(String cmd);
void enableDriver();
void disableDriver();
void applyMotors(int x, int y, int z);
void stop_all();
void pushStatus();
void updateDisplay();
bool checkRoverI2C();
bool isPowerSafe();

// =================================================
// BLE callbacks
class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    bleConnected = true;
    Serial.println("[BLE] Client connected");
  }

  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    Serial.println("[BLE] Client disconnected");
    stop_all();
    BLEDevice::getAdvertising()->start();
    Serial.println("[BLE] Advertising restarted");
  }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    std::string val = c->getValue();
    if (val.empty()) return;

    bool hasDelim = false;
    for (char ch : val) {
      if (ch == '\n' || ch == '\r') {
        hasDelim = true;
        break;
      }
    }

    if (hasDelim) {
      for (size_t i = 0; i < val.length(); i++) {
        char ch = val[i];
        if (ch == '\n' || ch == '\r') {
          if (rxBLE.length() > 0) {
            parseCommand(rxBLE);
            rxBLE = "";
          }
        } else {
          rxBLE += ch;
        }
      }
    } else {
      // Direct packet without delimiter (common in BLE gamepad apps)
      parseCommand(String(val.c_str()));
    }
  }
};

// =================================================
void setup() {
  M5.begin();
  M5.Lcd.setRotation(3); // Landscape mode (240x135)
  delay(100);

  Serial.begin(115200);
  Serial.println("Booting ESP32 RoverC BLE Robot...");

  // Init I2C for RoverC (SDA=0, SCL=26)
  Wire.begin(0, 26, 100000UL);
  roverc.begin(&Wire, 0, 26, 0x38);
  roverDetected = checkRoverI2C();
  if (roverDetected) {
    Serial.println("[ROVER] RoverC base detected on I2C (0x38)");
  } else {
    Serial.println("[ROVER] WARNING: RoverC base NOT responding! Check power switch.");
  }
  stop_all();

  // ---- BLE Init ----
  Serial.print("[BLE NAME] ");
  Serial.println(BLE_DEVICE_NAME);

  BLEDevice::init(BLE_DEVICE_NAME);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());

  BLEService *service = server->createService(BLE_SERVICE_UUID);

  BLECharacteristic *rx = service->createCharacteristic(
    BLE_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );

  bleTx = service->createCharacteristic(
    BLE_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );

  bleTx->addDescriptor(new BLE2902());
  rx->setCallbacks(new RxCB());

  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();

  Serial.println("[BLE] Advertising started");
  Serial.println("Ready.");

  updateDisplay();

  lastCommandTime = millis();
  lastStatusTime  = millis();
  lastDisplayTime = millis();
}

// =================================================
void loop() {
  M5.update();

  // ---- Auto-stop watchdog ----
  if (AUTO_STOP_ENABLED && (millis() - lastCommandTime > AUTO_STOP_TIMEOUT_MS)) {
    if (cmd_x != 0 || cmd_y != 0 || cmd_z != 0) {
      Serial.println("[AUTO] Timeout -> stop");
      cmd_x = 0;
      cmd_y = 0;
      cmd_z = 0;
      stop_all();
    }
  }

  // ---- Power & Motor Drive ----
  if (!isPowerSafe()) {
    disableDriver();
  } else {
    enableDriver();
    applyMotors(cmd_x, cmd_y, cmd_z);
  }

  // ---- Screen UI update ----
  if (millis() - lastDisplayTime >= DISPLAY_PERIOD_MS) {
    lastDisplayTime = millis();
    roverDetected = checkRoverI2C();
    updateDisplay();
  }

  // ---- Periodic BLE status push ----
  if (bleConnected && (millis() - lastStatusTime >= STATUS_PERIOD_MS)) {
    lastStatusTime = millis();
    pushStatus();
  }

  delay(10);
}

// =================================================
// Power Check
bool isPowerSafe() {
  float vbus = M5.Axp.GetVBusVoltage();
  float vin  = M5.Axp.GetVinVoltage();
  float bat  = M5.Axp.GetBatVoltage();
  // Safe if powered via USB or RoverC 5V bus, or battery is above threshold
  if (vbus > 3.8 || vin > 3.8) return true;
  return (bat >= MIN_SUPPLY_VOLTAGE);
}

// =================================================
// Screen UI: Shows MAC, Status, Rover Detection, Voltage, Current, Motor Commands
void updateDisplay() {
  uint16_t bgColor = bleConnected ? GREEN : ORANGE;

  if (lastBleState != (int)bleConnected) {
    lastBleState = (int)bleConnected;
    M5.Lcd.fillScreen(bgColor);
  }

  M5.Lcd.setTextColor(BLACK, bgColor);

  // Line 1: BLE Device Name
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(5, 3);
  M5.Lcd.printf("DEV: %s    ", BLE_DEVICE_NAME);

  // Line 2: BLE Connection State
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(5, 20);
  M5.Lcd.printf("BLE: %s    ", bleConnected ? "CONNECTED" : "WAITING...");

  // Line 3: RoverC Hardware Connection
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(5, 42);
  if (roverDetected) {
    M5.Lcd.printf("ROVER: OK         ");
  } else {
    M5.Lcd.printf("ROVER: CHECK PWR! ");
  }

  // Line 4: Live Voltage & Current
  float v = M5.Axp.GetBatVoltage();
  float i = M5.Axp.GetBatCurrent();
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(5, 68);
  M5.Lcd.printf("V:%4.2fV I:%4.0fmA  ", v, i);

  // Line 5: Live Motor Command (X=Side, Y=Fwd, Z=Yaw)
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(5, 96);
  M5.Lcd.printf("X:%-3d Y:%-3d Z:%-3d ", cmd_x, cmd_y, cmd_z);
}

// =================================================
// Command parser (supports "drive x,y,z", "motor x,y,z", "x,y,z" or legacy "motor L,R")
void parseCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  lastCommandTime = millis();

  Serial.print("[RX RAW] ");
  Serial.println(cmd);

  String payload = cmd;
  if (payload.startsWith("drive") || payload.startsWith("motor")) {
    int sp = payload.indexOf(' ');
    int col = payload.indexOf(':');
    int eq = payload.indexOf('=');
    int startIdx = 5;
    if (sp >= 0) startIdx = sp + 1;
    else if (col >= 0) startIdx = col + 1;
    else if (eq >= 0) startIdx = eq + 1;
    payload = payload.substring(startIdx);
    payload.trim();
  }

  payload.replace(' ', ',');

  int firstComma = payload.indexOf(',');
  if (firstComma < 0) return;

  int secondComma = payload.indexOf(',', firstComma + 1);

  if (secondComma >= 0) {
    // 3-axis Mecanum: x (sideway), y (forward), z (yaw)
    int rawX = payload.substring(0, firstComma).toInt();
    int rawY = payload.substring(firstComma + 1, secondComma).toInt();
    int rawZ = payload.substring(secondComma + 1).toInt();

    if (abs(rawX) > 100 || abs(rawY) > 100 || abs(rawZ) > 100) {
      cmd_x = constrain(map(rawX, -255, 255, -100, 100), -100, 100);
      cmd_y = constrain(map(rawY, -255, 255, -100, 100), -100, 100);
      cmd_z = constrain(map(rawZ, -255, 255, -100, 100), -100, 100);
    } else {
      cmd_x = constrain(rawX, -100, 100);
      cmd_y = constrain(rawY, -100, 100);
      cmd_z = constrain(rawZ, -100, 100);
    }

    Serial.print("[CMD] X="); Serial.print(cmd_x);
    Serial.print(" Y="); Serial.print(cmd_y);
    Serial.print(" Z="); Serial.println(cmd_z);
  } else {
    // 2-axis legacy: left, right
    int l = payload.substring(0, firstComma).toInt();
    int r = payload.substring(firstComma + 1).toInt();
    if (abs(l) > 100 || abs(r) > 100) {
      l = map(l, -255, 255, -100, 100);
      r = map(r, -255, 255, -100, 100);
    }
    cmd_x = 0;
    cmd_y = constrain((l + r) / 2, -100, 100);
    cmd_z = constrain((r - l) / 2, -100, 100);

    Serial.print("[CMD 2-CH] L="); Serial.print(l);
    Serial.print(" R="); Serial.println(r);
  }
}

// =================================================
// BLE telemetry push
void pushStatus() {
  float supplyV = M5.Axp.GetBatVoltage();
  float current_mA = M5.Axp.GetBatCurrent();

  char buf[64];
  snprintf(buf, sizeof(buf),
           "status V=%.2f I=%.1f\n",
           supplyV, current_mA);

  bleTx->setValue((uint8_t*)buf, strlen(buf));
  bleTx->notify();

  Serial.print("[BLE TX] ");
  Serial.print(buf);
}

// =================================================
// Check RoverC I2C ACK
bool checkRoverI2C() {
  Wire.beginTransmission(0x38);
  return (Wire.endTransmission() == 0);
}

// =================================================
// RoverC Omnidirectional Mecanum Motor Control
// Wheel Hardware Mapping on RoverC (register 0x00):
// buf[0]: Front Left  (FL)
// buf[1]: Front Right (FR)
// buf[2]: Rear Right  (RR)
// buf[3]: Rear Left   (RL)
//
// True Mecanum Kinematics:
// x: sideway / strafe (-100..100) -> FL+, FR-, RR+, RL-
// y: forward / reverse (-100..100) -> FL+, FR+, RR+, RL+
// z: yaw / steering   (-100..100) -> FL+, FR-, RR-, RL+
void applyMotors(int x, int y, int z) {
  if (abs(x) < 3) x = 0;
  if (abs(y) < 3) y = 0;
  if (abs(z) < 3) z = 0;

  // RoverC Physical Motor Mapping:
  // Right-side motors (FR, RR) are mirrored physically relative to left-side (FL, RL):
  // - Forward (y): Left +, Right -
  // - Yaw (z):     Left +, Right +
  // - Strafe (x):  FL +, FR -, RR +, RL -
  int fl =  y + x + z;
  int fr = -y - x + z;
  int rr = -y + x + z;
  int rl =  y - x + z;

  int8_t buf[4];
  buf[0] = (int8_t)constrain(fl, -100, 100); // Front Left
  buf[1] = (int8_t)constrain(fr, -100, 100); // Front Right
  buf[2] = (int8_t)constrain(rr, -100, 100); // Rear Right
  buf[3] = (int8_t)constrain(rl, -100, 100); // Rear Left

  Wire.beginTransmission(0x38);
  Wire.write(0x00);
  Wire.write((uint8_t*)buf, 4);
  Wire.endTransmission();
}

void stop_all() {
  int8_t buf[4] = {0, 0, 0, 0};
  Wire.beginTransmission(0x38);
  Wire.write(0x00);
  Wire.write((uint8_t*)buf, 4);
  Wire.endTransmission();
}

// =================================================
// Driver control
void enableDriver() {
  if (!driverEnabled) {
    driverEnabled = true;
    Serial.println("[DRV] Enabled");
  }
}

void disableDriver() {
  if (driverEnabled) {
    stop_all();
    driverEnabled = false;
    Serial.println("[DRV] Disabled");
  }
}