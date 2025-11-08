#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --------- Global state ----------
int tileMode = 0;            // 0 = idle, 1 = game, etc.
bool deviceConnected = false;

// Use Nordic UART Service (NUS) UUIDs (common pattern)
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // write from phone
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // notify to phone

BLECharacteristic *pTxCharacteristic = nullptr;

// ---------- Callbacks for connect / disconnect ----------
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    deviceConnected = true;
    Serial.println("BLE device connected");
  }

  void onDisconnect(BLEServer *pServer) override {
    deviceConnected = false;
    Serial.println("BLE device disconnected");

    // Restart advertising so phone can reconnect
    pServer->getAdvertising()->start();
    Serial.println("Advertising restarted");
  }
};

// ---------- Callbacks for RX (data from phone) ----------
class MyRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() == 0) return;

    Serial.print("Received via BLE: ");
    Serial.println(rxValue.c_str());

    // Example: first char '0' or '1' sets tileMode
    char c = rxValue[0];
    if (c == '0' || c == '1') {
      tileMode = c - '0';
      Serial.print("tileMode updated to: ");
      Serial.println(tileMode);

      // (Optional) send confirmation back to phone
      if (pTxCharacteristic && deviceConnected) {
        String msg = "Mode set to ";
        msg += tileMode;
        pTxCharacteristic->setValue(msg.c_str());
        pTxCharacteristic->notify();  // phone should subscribe to notifications
      }
    } else {
      // Handle other commands here if you want
      Serial.println("Unknown command (expected '0' or '1')");
    }
  }
};

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("Starting BLE...");

  BLEDevice::init("ESP32_TILE_BLE");

  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // TX characteristic (ESP32 → phone)
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  // RX characteristic (phone → ESP32)
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->setCallbacks(new MyRxCallbacks());

  // Start service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // helps with iOS
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("BLE UART service started, advertising as ESP32_TILE_BLE");
}

// ---------- Loop ----------
void loop() {
  // For now, just periodically print tileMode so you see it's alive
  static unsigned long lastPrint = 0;
  unsigned long now = millis();

  if (now - lastPrint > 3000) {
    lastPrint = now;
    Serial.print("Current tileMode = ");
    Serial.println(tileMode);
  }

  // Later:
  // if (tileMode == 0) showIdlePattern();
  // else if (tileMode == 1) showGamePattern();
}
