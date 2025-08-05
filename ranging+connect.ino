#include <ArduinoBLE.h>
#include <StellaUWB.h>

// Enhanced multi-device support with power optimization
#define MAX_CONNECTED_DEVICES 8
#define HEARTBEAT_INTERVAL 15000  // 15 seconds for better power saving
#define CONNECTION_TIMEOUT 60000  // 60 seconds timeout for power efficiency
#define KEEPALIVE_INTERVAL 30000   // 30 seconds keepalive ping for power saving
#define ADVERTISE_CHECK_INTERVAL 10000  // Check advertising every 10 seconds

// Device tracking structure
struct ConnectedDevice {
  BLEDevice device;
  String address;
  bool isActive;
  unsigned long lastActivity;
  bool hasActiveSession;
  uint8_t deviceId;
};

// Global variables for multi-device support
ConnectedDevice connectedDevices[MAX_CONNECTED_DEVICES];
uint8_t deviceCount = 0;
bool uwbInitialized = false;
unsigned long lastHeartbeat = 0;
unsigned long lastKeepalive = 0;
unsigned long lastAdvertiseCheck = 0;

// Device ID counter for unique identification
uint8_t nextDeviceId = 1;

// CRITICAL: Track advertising state explicitly
bool isAdvertising = false;
bool shouldBeAdvertising = true;

/**
 * @brief Find device in connected devices array
 * @param address Device BLE address
 * @return Index of device or -1 if not found
 */
int findDeviceIndex(String address) {
  for (int i = 0; i < deviceCount; i++) {
    if (connectedDevices[i].address == address) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief Add new device to connected devices array
 * @param dev BLE device to add
 * @return true if successfully added, false if array is full
 */
bool addDevice(BLEDevice dev) {
  if (deviceCount >= MAX_CONNECTED_DEVICES) {
    return false;
  }
  
  String address = dev.address();
  connectedDevices[deviceCount].device = dev;
  connectedDevices[deviceCount].address = address;
  connectedDevices[deviceCount].isActive = true;
  connectedDevices[deviceCount].lastActivity = millis();
  connectedDevices[deviceCount].hasActiveSession = false;
  connectedDevices[deviceCount].deviceId = nextDeviceId++;
  
  deviceCount++;
  
  // CRITICAL: Force advertising restart after adding a device
  ensureBLEAdvertising();
  
  return true;
}

/**
 * @brief Remove device from connected devices array
 * @param address Device BLE address
 */
void removeDevice(String address) {
  int index = findDeviceIndex(address);
  if (index == -1) {
    return;
  }
  
  // Shift remaining devices to fill the gap
  for (int i = index; i < deviceCount - 1; i++) {
    connectedDevices[i] = connectedDevices[i + 1];
  }
  
  deviceCount--;
  
  // Reset device ID counter if all devices disconnected
  if (deviceCount == 0) {
    nextDeviceId = 1;
  }
  
  // CRITICAL: Ensure advertising continues after device removal
  ensureBLEAdvertising();
}

/**
 * @brief Update device activity timestamp
 * @param address Device BLE address
 */
void updateDeviceActivity(String address) {
  int index = findDeviceIndex(address);
  if (index != -1) {
    connectedDevices[index].lastActivity = millis();
  }
}

/**
 * @brief Check for inactive devices and clean them up
 */
void cleanupInactiveDevices() {
  unsigned long currentTime = millis();
  
  for (int i = deviceCount - 1; i >= 0; i--) {
    if (currentTime - connectedDevices[i].lastActivity > CONNECTION_TIMEOUT) {
      removeDevice(connectedDevices[i].address);
    }
  }
}

/**
 * @brief Get number of active devices
 * @return Number of devices with active sessions
 */
uint8_t getActiveDeviceCount() {
  uint8_t activeCount = 0;
  for (int i = 0; i < deviceCount; i++) {
    if (connectedDevices[i].hasActiveSession) {
      activeCount++;
    }
  }
  return activeCount;
}

/**
 * @brief Send keepalive signal to maintain active connections
 */
void sendKeepalive() {
  // Update activity for all connected devices to prevent timeout
  for (int i = 0; i < deviceCount; i++) {
    if (connectedDevices[i].isActive) {
      connectedDevices[i].lastActivity = millis();
    }
  }
}

/**
 * @brief CRITICAL: Ensure BLE advertising continues for multi-device support
 * This is the key function that enables multiple simultaneous connections
 */
void ensureBLEAdvertising() {
  // Only advertise if we have space for more devices
  if (deviceCount < MAX_CONNECTED_DEVICES && shouldBeAdvertising) {
    
    // Check current advertising state
    bool currentlyAdvertising = isAdvertising;
    
    if (!currentlyAdvertising) {
      // CRITICAL: Restart advertising immediately
      // This is the key to allowing multiple connections
      if (BLE.advertise()) {
        isAdvertising = true;
      } else {
        isAdvertising = false;
      }
    } else {
      // Already advertising - periodic confirmation
      if (!isAdvertising) {
        isAdvertising = true;
      }
    }
    
  } else if (deviceCount >= MAX_CONNECTED_DEVICES) {
    // Stop advertising if we're at max capacity
    if (isAdvertising) {
      BLE.stopAdvertise();
      isAdvertising = false;
    }
  }
}

/**
 * @brief Force restart BLE advertising
 * Call this if advertising seems stuck
 */
void forceRestartAdvertising() {
  // Stop advertising first
  if (isAdvertising) {
    BLE.stopAdvertise();
    delay(50);  // Brief delay - reduced for power saving
  }
  
  // Restart advertising
  if (BLE.advertise()) {
    isAdvertising = true;
  } else {
    isAdvertising = false;
  }
}

/**
 * @brief notification handler for ranging data
 * @param rangingData the received data
 */
void rangingHandler(UWBRangingData &rangingData) {
  //nearby interaction is based on Double-sided Two-way Ranging method
  if(rangingData.measureType()==(uint8_t)uwb::MeasurementType::TWO_WAY) {
    //get the TWR (Two-Way Ranging) measurements
    RangingMeasures twr=rangingData.twoWayRangingMeasure();
    //loop for the number of available measurements
    for(int j=0;j<rangingData.available();j++) {
      //if the measure is valid
      if(twr[j].status==0 && twr[j].distance!=0xFFFF) {
        // Distance data is available but not printed for power saving
        // Can be processed here if needed
      }
    }
  }
}

/**
 * @brief callback invoked when a BLE client connects
 * @param dev the client BLE device
 */
void clientConnected(BLEDevice dev) {
  // Add device to tracking array
  if (!addDevice(dev)) {
    return;
  }
  
  // Initialize UWB stack upon first connection
  if (deviceCount == 1) {
    if (!uwbInitialized) {
      UWB.begin();  // Remove boolean check as this method returns void
      uwbInitialized = true;
      
      // Force start ranging for all connected devices
      for (int i = 0; i < deviceCount; i++) {
        if (connectedDevices[i].isActive) {
          connectedDevices[i].hasActiveSession = true;
        }
      }
    }
  } else {
    // If UWB is already initialized, start session for this device
    if (uwbInitialized) {
      int index = findDeviceIndex(dev.address());
      if (index != -1) {
        connectedDevices[index].hasActiveSession = true;
      }
    }
  }
  
  // CRITICAL: This is the most important part for multi-device support
  // Force advertising to continue after connection
  ensureBLEAdvertising();
}

/**
 * @brief callback for BLE client disconnection
 * @param dev 
 */
void clientDisconnected(BLEDevice dev) {
  // Remove device from tracking array
  removeDevice(dev.address());
  
  // Deinitialize UWB stack if no devices are connected
  if(deviceCount == 0) {
    if (uwbInitialized) {
      UWB.end();
      uwbInitialized = false;
    }
  }
  
  // CRITICAL: Ensure advertising resumes after disconnection
  ensureBLEAdvertising();
}

/**
 * @brief callback for when a UWB session with a client is started
 * @param dev 
 */
void sessionStarted(BLEDevice dev) {
  String address = dev.address();
  
  // Update device session status
  int index = findDeviceIndex(address);
  if (index != -1) {
    connectedDevices[index].hasActiveSession = true;
    connectedDevices[index].lastActivity = millis();
  }
}

/**
 * @brief callback for when a UWB session with a client is terminated
 * @param dev 
 */
void sessionStopped(BLEDevice dev) {
  String address = dev.address();
  
  // Update device session status
  int index = findDeviceIndex(address);
  if (index != -1) {
    connectedDevices[index].hasActiveSession = false;
  }
}

void setup() {
  // Minimal serial initialization for critical errors only
  Serial.begin(115200);

#if defined(ARDUINO_PORTENTA_C33)
  /* Only the Portenta C33 has an RGB LED. */
  pinMode(LEDR, OUTPUT);
  digitalWrite(LEDR, LOW);
#endif

  // Initialize device tracking array
  for (int i = 0; i < MAX_CONNECTED_DEVICES; i++) {
    connectedDevices[i].isActive = false;
    connectedDevices[i].hasActiveSession = false;
    connectedDevices[i].lastActivity = 0;
  }

  //register the callback for ranging data
  UWB.registerRangingCallback(rangingHandler);
  
  //register the callback for client connection/disconnection events
  UWBNearbySessionManager.onConnect(clientConnected);
  UWBNearbySessionManager.onDisconnect(clientDisconnected);

  //register the callbacks for client session start and stop events
  UWBNearbySessionManager.onSessionStart(sessionStarted);
  UWBNearbySessionManager.onSessionStop(sessionStopped);

  // Initialize BLE manually first
  if (!BLE.begin()) {
    while (1);
  }
  
  // Set device name BEFORE UWB initialization
  BLE.setLocalName("Proxi Pilot");
  BLE.setDeviceName("Proxi Pilot");
  
  //init the BLE services and characteristic, advertise with device name
  UWBNearbySessionManager.begin("Proxi Pilot");  // Name ending with "Pilot" for filtering
  
  // Add delay for BLE stack initialization
  delay(500);  // Reduced delay for power saving
  
  // CRITICAL: Ensure advertising is active from the start
  isAdvertising = true;  // Assume advertising starts after UWBNearbySessionManager.begin()
  
  // Try to start advertising manually
  BLE.stopAdvertise();  // Stop any existing advertising
  delay(200);  // Reduced delay
  
  if (BLE.advertise()) {
    isAdvertising = true;
  } else {
    isAdvertising = false;
  }
  
  if (!isAdvertising) {
    forceRestartAdvertising();
  }
}

void loop() {
  // Optimized delay for power saving while maintaining responsiveness
  delay(50);  // 50ms for balanced power consumption and responsiveness
  
  //poll the BLE stack
  UWBNearbySessionManager.poll();
  
  // Clean up inactive devices
  cleanupInactiveDevices();
  
  // CRITICAL: Check advertising status periodically
  // This is the key to maintaining multi-device connectivity
  if (millis() - lastAdvertiseCheck > ADVERTISE_CHECK_INTERVAL) {
    ensureBLEAdvertising();
    lastAdvertiseCheck = millis();
  }
  
  // Send keepalive to maintain active connections (less frequent)
  if (millis() - lastKeepalive > KEEPALIVE_INTERVAL) {
    sendKeepalive();
    lastKeepalive = millis();
  }
  
  // Optional: Add sleep mode when no devices are connected
  if (deviceCount == 0 && !uwbInitialized) {
    delay(100);  // Additional delay when idle for power saving
  }
}