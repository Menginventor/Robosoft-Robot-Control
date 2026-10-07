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
int cmd_left  = 0;
int cmd_right = 0;

// ---------------- BLE ----------------
BLECharacteristic *bleTx;
String rxBLE = "";

// =================================================
// Forward declarations
void parseCommand(String cmd);
void enableDriver();
void disableDriver();
void applyMotors(int left_speed, int right_speed);
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
    if (cmd_left != 0 || cmd_right != 0) {
      Serial.println("[AUTO] Timeout -> stop");
      cmd_left = 0;
      cmd_right = 0;
    }
  }

  // ---- Power & Motor Drive ----
  if (!isPowerSafe()) {
    disableDriver();
  } else {
    enableDriver();
    applyMotors(cmd_left, cmd_right);
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

  // Line 5: Live Motor Command
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(5, 96);
  M5.Lcd.printf("L:%-4d R:%-4d ", cmd_left, cmd_right);
}

// =================================================
// Command parser (supports "motor 100,100", "motor:100,100", "100,100", "motor 100 100")
void parseCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  lastCommandTime = millis();

  Serial.print("[RX RAW] ");
  Serial.println(cmd);

  String payload = cmd;
  if (payload.startsWith("motor")) {
    payload = payload.substring(5);
    payload.trim();
    if (payload.startsWith(":") || payload.startsWith("=")) {
      payload = payload.substring(1);
      payload.trim();
    }
  }

  int sep = payload.indexOf(',');
  if (sep < 0) {
    sep = payload.indexOf(' ');
  }

  if (sep >= 0) {
    cmd_left  = constrain(payload.substring(0, sep).toInt(),  -255, 255);
    cmd_right = constrain(payload.substring(sep + 1).toInt(), -255, 255);
  } else {
    int val = payload.toInt();
    cmd_left  = constrain(val, -255, 255);
    cmd_right = constrain(val, -255, 255);
  }

  Serial.print("[CMD PARSED] L=");
  Serial.print(cmd_left);
  Serial.print(" R=");
  Serial.println(cmd_right);
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
// RoverC Motor Control
// RoverC 4-Wheel Mapping:
// 0: Front Left,  1: Front Right
// 2: Rear Right,  3: Rear Left
void applyMotors(int left_speed, int right_speed) {
  // Map speed from -255..255 (or -100..100) to RoverC pulse -100..100
  int l = constrain(left_speed * MOTOR_A_DIR, -255, 255);
  int r = constrain(right_speed * MOTOR_B_DIR, -255, 255);

  int8_t l_pulse = (int8_t)map(l, -255, 255, -100, 100);
  int8_t r_pulse = (int8_t)map(r, -255, 255, -100, 100);

  // Deadband
  if (abs(left_speed) < 5)  l_pulse = 0;
  if (abs(right_speed) < 5) r_pulse = 0;

  int8_t buf[4];
  buf[0] = l_pulse; // Front Left
  buf[1] = r_pulse; // Front Right
  buf[2] = r_pulse; // Rear Right
  buf[3] = l_pulse; // Rear Left

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