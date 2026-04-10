#include "FelicitaScale.h"

// Initialize UUID constants
const NimBLEUUID FelicitaScale::DATA_SERVICE_UUID("FFE0");
const NimBLEUUID FelicitaScale::DATA_CHARACTERISTIC_UUID("FFE1");

// ---------------------------------------------------------------------------------------
// ------------------------------   FelicitaScale
// -------------------------------------
// ---------------------------------------------------------------------------------------

FelicitaScale::FelicitaScale() {}

FelicitaScale::~FelicitaScale() { clientCleanup(); }

void FelicitaScale::init(NimBLEAddress address) {
  this->deviceAddress = address;
  this->isAddressSet = true;
}

bool FelicitaScale::connect() {
  if (!isAddressSet) {
    log("Cannot connect: BLE address not set.\n");
    return false;
  }

  if (isConnected()) {
    log("Already connected.\n");
    return true;
  }

  clientCleanup();
  log("Connecting to BLE client...\n");
  client = NimBLEDevice::createClient(deviceAddress);

  if (!client->connect()) {
    clientCleanup();
    log("Failed to connect to BLE client.\n");
    return false;
  }

  if (!performConnectionHandshake()) {
    clientCleanup();
    return false;
  }

  setWeight(0.f);
  return true;
}

void FelicitaScale::disconnect() { clientCleanup(); }

bool FelicitaScale::isConnected() {
  return client != nullptr && client->isConnected();
}

void FelicitaScale::update() {
  if (markedForReconnection) {
    log("Reconnecting...\n");
    clientCleanup();
    connect();
    markedForReconnection = false;
  } else {
    verifyConnected();
  }
}

bool FelicitaScale::tare() {
  if (!verifyConnected())
    return false;
  log("Tare command sent.\n");
  uint8_t tareCommand[] = {CMD_TARE};
  dataCharacteristic->writeValue(tareCommand, sizeof(tareCommand), true);
  return true;
}

void FelicitaScale::setWeightCallback(WeightCallback cb, bool onlyChanges) {
  this->weightCallback = cb;
  this->weightCallbackOnlyChanges = onlyChanges;
}

void FelicitaScale::setLogCallback(LogCallback cb) { this->logCallback = cb; }

void FelicitaScale::setWeight(float newWeight) {
  float previousWeight = weight;
  weight = newWeight;

  if (weightCallback) {
    if (!weightCallbackOnlyChanges || previousWeight != newWeight) {
      weightCallback(newWeight);
    }
  }
}

void FelicitaScale::log(const char *msgFormat, ...) {
  if (!logCallback)
    return;

  va_list args;
  va_start(args, msgFormat);
  int length = vsnprintf(nullptr, 0, msgFormat, args); // Find length of string
  va_end(args);

  va_start(args, msgFormat);
  std::string formattedMessage(length, '\0');
  vsnprintf(&formattedMessage[0], length + 1, msgFormat, args);
  va_end(args);

  logCallback("[Felicita Scale] " + formattedMessage);
}

void FelicitaScale::clientCleanup() {
  if (client == nullptr)
    return;
  log("Cleaning up BLE client...\n");
  NimBLEDevice::deleteClient(client);
  client = nullptr;
  service = nullptr;
  dataCharacteristic = nullptr;
}

bool FelicitaScale::performConnectionHandshake() {
  log("Performing handshake...\n");

  service = client->getService(DATA_SERVICE_UUID);
  if (!service) {
    log("Service not found.\n");
    return false;
  }

  dataCharacteristic = service->getCharacteristic(DATA_CHARACTERISTIC_UUID);
  if (!dataCharacteristic) {
    log("Characteristic not found.\n");
    return false;
  }

  if (dataCharacteristic->canNotify()) {
    dataCharacteristic->subscribe(
        true, [this](NimBLERemoteCharacteristic *characteristic, uint8_t *data,
                     size_t length, bool isNotify) {
          notifyCallback(characteristic, data, length, isNotify);
        });
  } else {
    log("Notifications not supported.\n");
    return false;
  }

  return true;
}

bool FelicitaScale::verifyConnected() {
  if (markedForReconnection) {
    return false;
  }
  if (!isConnected()) {
    markedForReconnection = true;
    return false;
  }
  return true;
}

void FelicitaScale::notifyCallback(NimBLERemoteCharacteristic *characteristic,
                                   uint8_t *data, size_t length,
                                   bool isNotify) {
  if (length < 18) {
    log("Malformed data received (length < 18).\n");
    return;
  }
  parseStatusUpdate(data, length);
}

void FelicitaScale::parseStatusUpdate(const uint8_t *data, size_t length) {
  float newWeight = static_cast<float>(parseWeight(data)) / 100.0f;
  setWeight(newWeight);
}

int32_t FelicitaScale::parseWeight(const uint8_t *data) {
  bool isNegative = (data[2] == 0x2D);

  if ((data[3] | data[4] | data[5] | data[6] | data[7] | data[8]) < '0' ||
      (data[3] & data[4] & data[5] & data[6] & data[7] & data[8]) > '9') {
    log("Invalid digit in weight data.\n");
    return 0;
  }

  // Direct digit expansion for Felicita payload protocol
  return (isNegative ? -1 : 1) *
         ((data[3] - '0') * 100000 + (data[4] - '0') * 10000 +
          (data[5] - '0') * 1000 + (data[6] - '0') * 100 +
          (data[7] - '0') * 10 + (data[8] - '0'));
}

// ---------------------------------------------------------------------------------------
// -----------------------------   FelicitaScanner
// ------------------------------------
// ---------------------------------------------------------------------------------------

void FelicitaScanner::startScan() {
  if (isRunning)
    return;

  NimBLEDevice::init("");
  NimBLEScan *pScan = NimBLEDevice::getScan();

  // Pass 'false' so we don't store device data in the library, preventing
  // memory leaks
  pScan->setScanCallbacks(this, false);
  pScan->setInterval(500);
  pScan->setWindow(100);
  pScan->setMaxResults(0);
  pScan->setDuplicateFilter(false);
  pScan->setActiveScan(true);

  pScan->start(0); // 0 = continuous scanning
  isRunning = true;
}

void FelicitaScanner::stopScan() {
  if (!isRunning)
    return;
  NimBLEDevice::getScan()->stop();
  NimBLEDevice::getScan()->clearResults();
  isRunning = false;
}

void FelicitaScanner::onResult(const NimBLEAdvertisedDevice *advertisedDevice) {
  if (advertisedDevice->haveName()) {
    std::string name = advertisedDevice->getName();
    if (name.find("FELICITA") == 0) {
      foundAddress = advertisedDevice->getAddress();
      deviceFound = true;
      stopScan(); // Stop immediately once we find the scale
    }
  }
}