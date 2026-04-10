#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <functional>
#include <string>

class FelicitaScale {
public:
  using WeightCallback = std::function<void(float)>;
  using LogCallback = std::function<void(std::string)>;

  FelicitaScale();
  ~FelicitaScale();

  // Initialize with the BLE address found by the scanner
  void init(NimBLEAddress address);

  bool connect();
  void disconnect();
  bool isConnected();
  void update();
  bool tare();

  float getWeight() const { return weight; }
  void setWeightCallback(WeightCallback cb, bool onlyChanges = false);
  void setLogCallback(LogCallback cb);

private:
  NimBLEAddress deviceAddress;
  bool isAddressSet = false;
  NimBLEClient *client = nullptr;
  NimBLERemoteService *service = nullptr;
  NimBLERemoteCharacteristic *dataCharacteristic = nullptr;

  float weight = 0.f;
  bool markedForReconnection = false;

  WeightCallback weightCallback = nullptr;
  bool weightCallbackOnlyChanges = false;
  LogCallback logCallback = nullptr;

  void setWeight(float newWeight);
  void log(const char *msgFormat, ...);
  void clientCleanup();

  bool performConnectionHandshake();
  void notifyCallback(NimBLERemoteCharacteristic *characteristic, uint8_t *data,
                      size_t length, bool isNotify);
  void parseStatusUpdate(const uint8_t *data, size_t length);
  int32_t parseWeight(const uint8_t *data);
  bool verifyConnected();

  // Constants specific to Felicita Scales
  static const NimBLEUUID DATA_SERVICE_UUID;
  static const NimBLEUUID DATA_CHARACTERISTIC_UUID;
  static constexpr uint8_t CMD_TARE = 0x54;
};

// A simplified scanner specifically for finding the Felicita scale
class FelicitaScanner : public NimBLEScanCallbacks {
public:
  void startScan();
  void stopScan();
  bool isScanRunning() const { return isRunning; }

  bool hasFoundDevice() const { return deviceFound; }
  NimBLEAddress getFoundAddress() const { return foundAddress; }
  void reset() { deviceFound = false; }

private:
  bool isRunning = false;
  bool deviceFound = false;
  NimBLEAddress foundAddress;

  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override;
};