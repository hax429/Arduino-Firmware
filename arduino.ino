#include <ArduinoBLE.h>

// Define BLE Service and Characteristic UUIDs
BLEService numberService("19B10000-E8F2-537E-4F6C-D104768A1214"); // Custom UUID
BLEIntCharacteristic numberCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

// Global variables for debugging and connection tracking
unsigned long lastHeartbeat = 0;
unsigned long connectionStartTime = 0;
unsigned long totalConnections = 0;
bool wasConnected = false;
int currentNumber = 0;
unsigned long lastNumberSent = 0;
const unsigned long NUMBER_SEND_INTERVAL = 1000; // 1 second

// Debug helper functions
void printSystemInfo() {
  Serial.println("=== SYSTEM INFO ===");
  int memValue = freeMemory();
  if (memValue != -1) {
    Serial.print("Free memory: ");
    Serial.print(memValue);
    Serial.println(" bytes");
  } else {
    Serial.println("Memory info: Not available on this platform");
  }
  Serial.print("Uptime: ");
  Serial.print(millis() / 1000);
  Serial.println(" seconds");
  Serial.println("==================");
}

void printBLEStatus() {
  Serial.println("=== BLE STATUS ===");
  Serial.println("BLE initialized: YES");
  Serial.println("Local name: ArduinoNumber");
  Serial.print("Device address: ");
  Serial.println(BLE.address());
  Serial.println("Advertising: Should be active");
  Serial.print("Total connections: ");
  Serial.println(totalConnections);
  Serial.println("==================");
}

void printConnectionInfo(BLEDevice& central) {
  Serial.println("=== CONNECTION INFO ===");
  Serial.print("Central address: ");
  Serial.println(central.address());
  Serial.print("Central device name: ");
  Serial.println(central.deviceName().length() > 0 ? central.deviceName() : "Unknown");
  Serial.print("Connection time: ");
  Serial.print((millis() - connectionStartTime) / 1000);
  Serial.println(" seconds");
  Serial.print("RSSI: ");
  Serial.println(central.rssi());
  Serial.println("=======================");
}

// Function to estimate free memory (platform compatible)
int freeMemory() {
#ifdef __AVR__
  // AVR-specific memory calculation
  extern char *__brkval;
  extern char __bss_end;
  char top;
  return __brkval ? &top - __brkval : &top - &__bss_end;
#else
  // For non-AVR platforms, return a placeholder value
  return -1; // Indicates memory info not available
#endif
}

void setup() {
  Serial.begin(9600);
  
  // Wait for serial connection with timeout
  unsigned long serialTimeout = millis() + 5000; // 5 second timeout
  while (!Serial && millis() < serialTimeout) {
    delay(100);
  }
  
  Serial.println("\n=== ARDUINO BLE NUMBER SENDER ===");
  Serial.println("Starting initialization...");
  
  // Print system information
  printSystemInfo();

  // Initialize BLE with detailed error checking
  Serial.println("Initializing BLE...");
  if (!BLE.begin()) {
    Serial.println("ERROR: BLE initialization failed!");
    Serial.println("Possible causes:");
    Serial.println("- Hardware issue");
    Serial.println("- Insufficient power");
    Serial.println("- BLE module not connected properly");
    while (1) {
      delay(1000);
      Serial.println("BLE init failed - system halted");
    }
  }
  Serial.println("✓ BLE initialized successfully");

  // Configure BLE device
  Serial.println("Configuring BLE device...");
  BLE.setLocalName("ArduinoNumber");
  BLE.setAdvertisedService(numberService);
  Serial.println("✓ Device name set: ArduinoNumber");

  // Add characteristic to service with error checking
  Serial.println("Adding characteristic to service...");
  numberService.addCharacteristic(numberCharacteristic);
  BLE.addService(numberService);
  Serial.println("✓ Service and characteristic added");

  // Set initial value for the characteristic
  Serial.println("Setting initial characteristic value...");
  if (numberCharacteristic.writeValue(currentNumber)) {
    Serial.println("✓ Initial value set to: " + String(currentNumber));
  } else {
    Serial.println("⚠ Warning: Failed to set initial value");
  }

  // Start advertising with verification
  Serial.println("Starting BLE advertising...");
  BLE.advertise();
  
  // Verify advertising started
  delay(100); // Small delay to let advertising start
  Serial.println("✓ BLE advertising started successfully");

  // Print complete BLE status
  printBLEStatus();
  
  Serial.println("=== SETUP COMPLETE ===");
  Serial.println("Waiting for connections...");
  Serial.println("You can now connect from your iOS device");
  Serial.println("=======================\n");
  
  lastHeartbeat = millis();
}

void loop() {
  unsigned long currentTime = millis();
  
  // Print heartbeat every 10 seconds when not connected
  if (currentTime - lastHeartbeat > 10000) {
    if (!wasConnected) {
      Serial.println("💓 Heartbeat: Waiting for connection...");
    }
    lastHeartbeat = currentTime;
  }

  // Check for BLE central connection
  BLEDevice central = BLE.central();

  if (central) {
    // New connection detected
    if (!wasConnected) {
      wasConnected = true;
      connectionStartTime = currentTime;
      totalConnections++;
      
      Serial.println("\n🔗 NEW CONNECTION DETECTED!");
      printConnectionInfo(central);
      Serial.println("Starting data transmission...\n");
    }

    // Handle connected state
    while (central.connected()) {
      currentTime = millis();
      
      // Send number at specified interval
      if (currentTime - lastNumberSent >= NUMBER_SEND_INTERVAL) {
        // Increment number
        currentNumber++;
        if (currentNumber > 100) currentNumber = 0; // Reset after 100
        
        // Send the number
        if (numberCharacteristic.writeValue(currentNumber)) {
          Serial.println("📤 Sent number: " + String(currentNumber) + 
                         " (Connection time: " + String((currentTime - connectionStartTime) / 1000) + "s)");
        } else {
          Serial.println("❌ Failed to send number: " + String(currentNumber));
        }
        
        lastNumberSent = currentTime;
      }
      
      // Print connection status every 30 seconds
      if (currentTime - lastHeartbeat > 30000) {
        Serial.println("💓 Connection alive - RSSI: " + String(central.rssi()) + 
                       "dBm, Uptime: " + String((currentTime - connectionStartTime) / 1000) + "s");
        lastHeartbeat = currentTime;
      }
      
      // Small delay to prevent overwhelming the loop
      delay(50);
    }

    // Connection lost
    Serial.println("\n🔌 CONNECTION LOST");
    Serial.println("Disconnected from: " + central.address());
    Serial.print("Connection duration: ");
    Serial.print((millis() - connectionStartTime) / 1000);
    Serial.println(" seconds");
    
    wasConnected = false;
    
    // Restart advertising
    Serial.println("🔄 Restarting advertising...");
    BLE.advertise();
    Serial.println("✓ Advertising restarted successfully");
    
    Serial.println("Ready for new connections...\n");
    lastHeartbeat = millis();
  }
  
  // Small delay for main loop
  delay(100);
}