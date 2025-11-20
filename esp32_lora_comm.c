/*
 * SHARC Monitoring System v3.2
 * ESP32-S3 LoRa SX1278 Transceiver
 * Bluetooth LE + USB Serial + Web App Integration
 * 
 * Hardware: ESP32-S3 + LoRa SX1278
 * LoRa Pins: CS=10, RST=8, DIO0=7, SCK=13, MISO=12, MOSI=11
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>

// ===== Bluetooth Configuration =====
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// ===== LoRa Pin Configuration =====
#define LORA_CS   10
#define LORA_RST  8
#define LORA_DIO0 7
#define LORA_SCK  13
#define LORA_MISO 12
#define LORA_MOSI 11

// ===== Node Configuration =====
String NODE_ID = "SHARC_Node_B";  // Change to Node_B for second device

// ===== LoRa Configuration =====
float loraFrequency = 433.0E6;      // 433 MHz
int loraSpreadingFactor = 7;        // SF7 (6-12)
long loraSignalBandwidth = 125E3;   // 125 kHz
int loraCodingRate = 5;             // 4/5
int loraTxPower = 20;               // 20 dBm (max)
int loraChannel = 0;
#define BASE_FREQUENCY 433.0E6

// ===== Statistics & ML Data =====
unsigned long packetCount = 0;
unsigned long lastPacketTime = 0;
unsigned long sentPacketCount = 0;
unsigned long receivedPacketCount = 0;
unsigned long packetSequence = 0;
unsigned long lastRxSequence = 0;
unsigned long missedPackets = 0;
float packetDeliveryRatio = 100.0;

// ===== Auto Send (Optional) =====
bool autoSendEnabled = false;       // Set to true for auto messages
unsigned long lastSendTime = 0;
unsigned long sendInterval = 5000;  // 5 seconds

// ===== Function Prototypes =====
void handleCommand(String data, String source);
void initBluetooth();
void initLoRa();
void sendLoRaPacket(String message);
void receiveLoRaPacket();
void sendToClients(String type, String data, int rssi, float snr, float distance, float pdr);
void sendViaBluetooth(String message);
void sendViaSerial(String message);
void setLoRaFrequency(float freq);
void setLoRaSpreadingFactor(int sf);
void setLoRaBandwidth(float bw);
void setLoRaTxPower(int power);
void setLoRaChannel(int channel);
float calculateDistance(int rssi, float freqMHz);
float calculatePDR();
void printConfiguration();

// ===== BLE Callbacks =====
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("✓ Bluetooth device connected");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("✗ Bluetooth device disconnected");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = String(pCharacteristic->getValue().c_str());
      if (value.length() > 0) {
        Serial.println("\n📲 Bluetooth data received!");
        handleCommand(value, "Bluetooth");
      }
    }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n================================================");
  Serial.println("  SHARC Monitoring System v3.2");
  Serial.println("  ESP32-S3 LoRa Transceiver");
  Serial.println("  Bluetooth + Serial + Web App");
  Serial.println("================================================\n");
  Serial.println("🔍 Debug Mode: ENABLED");
  Serial.println("   Node ID: " + NODE_ID + "\n");
  
  // Initialize Bluetooth
  initBluetooth();
  
  // Initialize LoRa
  initLoRa();
  
  Serial.println("\n================================================");
  Serial.println("  System Ready!");
  Serial.println("================================================");
  Serial.println("Bluetooth: SHARC_LoRa_B");
  Serial.println("Serial: 115200 baud");
  Serial.println("LoRa: Ready to TX/RX");
  Serial.println("================================================\n");
  
  printConfiguration();
}

void loop() {
  // Check for LoRa packets
  receiveLoRaPacket();
  
  // Handle Serial input
  if (Serial.available()) {
    String data = Serial.readStringUntil('\n');
    data.trim();
    if (data.length() > 0) {
      Serial.println("\n⌨ Serial input received!");
      // Check if it's a command (JSON) or a message to send
      if (data.startsWith("{")) {
        handleCommand(data, "Serial");
      } else {
        Serial.printf("   Plain text: %s\n", data.c_str());
        sendLoRaPacket(data);
      }
    }
  }
  
  // Auto-send messages (optional)
  if (autoSendEnabled && (millis() - lastSendTime > sendInterval)) {
    String autoMsg = "Auto_Msg_#" + String(packetSequence + 1);
    sendLoRaPacket(autoMsg);
    lastSendTime = millis();
  }
  
  // Handle Bluetooth reconnection
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("→ Bluetooth: Start advertising");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  // LoRa health check every 30 seconds
  static unsigned long lastLoRaCheck = 0;
  if (millis() - lastLoRaCheck > 30000) {
    lastLoRaCheck = millis();
    if (LoRa.beginPacket() == 0) {
      Serial.println("⚠ WARNING: LoRa module not responding!");
    } else {
      LoRa.endPacket(false);
      Serial.println("✓ LoRa module OK");
    }
  }
  
  delay(10);
}

// ===== Bluetooth Initialization =====
void initBluetooth() {
  Serial.println("Initializing Bluetooth LE...");
  
  BLEDevice::init("SHARC_LoRa_B");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_INDICATE
  );
  
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("✓ Bluetooth initialized!");
  Serial.println("  Device name: SHARC_LoRa");
  Serial.println("  Advertising...");
}

// ===== LoRa Initialization =====
void initLoRa() {
  Serial.println("\nInitializing LoRa SX1278...");
  
  // SPI pins configuration
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);
  
  // LoRa pins
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  
  if (!LoRa.begin(loraFrequency)) {
    Serial.println("✗ LoRa initialization failed!");
    Serial.println("  Check wiring and try again.");
    while (1) {
      delay(1000);
    }
  }
  
  Serial.println("✓ LoRa initialized!");
  
  // Configure LoRa parameters
  LoRa.setSpreadingFactor(loraSpreadingFactor);
  LoRa.setSignalBandwidth(loraSignalBandwidth);
  LoRa.setCodingRate4(loraCodingRate);
  LoRa.setTxPower(loraTxPower, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
}

// ===== Print Current Configuration =====
void printConfiguration() {
  Serial.println("\n┌─────────────────────────────────────┐");
  Serial.println("│      LoRa Configuration             │");
  Serial.println("├─────────────────────────────────────┤");
  Serial.printf("│ Frequency:    %7.2f MHz         │\n", loraFrequency / 1E6);
  Serial.printf("│ TX Power:     %2d dBm              │\n", loraTxPower);
  Serial.printf("│ Spreading:    SF%-2d                │\n", loraSpreadingFactor);
  Serial.printf("│ Bandwidth:    %6.1f kHz          │\n", loraSignalBandwidth / 1E3);
  Serial.printf("│ Coding Rate:  4/%-2d                │\n", loraCodingRate);
  Serial.printf("│ Channel:      %-3d                 │\n", loraChannel);
  Serial.println("│ Sync Word:    0x12                  │");
  Serial.println("│ CRC:          Enabled               │");
  Serial.println("└─────────────────────────────────────┘\n");
}

// ===== Handle Commands from Web/Serial =====
void handleCommand(String data, String source) {
  Serial.println("\n════════════════════════════════════════════");
  Serial.printf("📥 RECEIVED from %s\n", source.c_str());
  Serial.printf("   Raw data: %s\n", data.c_str());
  Serial.println("════════════════════════════════════════════");
  
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, data);
  
  if (error) {
    Serial.printf("✗ JSON parse error: %s\n", error.c_str());
    Serial.println("   This might be a plain text message.\n");
    return;
  }
  
  const char* command = doc["command"];
  Serial.printf("→ Command: %s\n", command);
  
  if (strcmp(command, "setFrequency") == 0) {
    float freq = doc["value"];
    setLoRaFrequency(freq);
  }
  else if (strcmp(command, "setSF") == 0) {
    int sf = doc["value"];
    setLoRaSpreadingFactor(sf);
  }
  else if (strcmp(command, "setChannel") == 0) {
    int channel = doc["value"];
    setLoRaChannel(channel);
  }
  else if (strcmp(command, "setBandwidth") == 0) {
    float bw = doc["value"];
    setLoRaBandwidth(bw);
  }
  else if (strcmp(command, "setTxPower") == 0) {
    int power = doc["value"];
    setLoRaTxPower(power);
  }
  else if (strcmp(command, "sendMessage") == 0) {
    const char* message = doc["value"];
    Serial.printf("📤 Preparing to send: %s\n", message);
    sendLoRaPacket(String(message));
  }
  else {
    Serial.printf("✗ Unknown command: %s\n", command);
  }
  Serial.println();
}

// ===== Send LoRa Packet =====
void sendLoRaPacket(String message) {
  sentPacketCount++;
  packetSequence++;
  
  // Add sequence number for tracking
  String packetWithSeq = "SEQ:" + String(packetSequence) + "|" + message;
  
  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.println("║       📤 LoRa Packet SENDING               ║");
  Serial.println("╠════════════════════════════════════════════╣");
  Serial.printf("║ Packet #%-6lu (TX: %-6lu)           ║\n", sentPacketCount, sentPacketCount);
  Serial.printf("║ Sequence: %-6lu                        ║\n", packetSequence);
  Serial.printf("║ Data: %-36s ║\n", message.c_str());
  Serial.println("╟────────────────────────────────────────────╢");
  Serial.printf("║ Frequency: %.2f MHz                      ║\n", loraFrequency / 1E6);
  Serial.printf("║ TX Power:  %d dBm                          ║\n", loraTxPower);
  Serial.printf("║ SF:        %d                               ║\n", loraSpreadingFactor);
  Serial.printf("║ BW:        %.1f kHz                        ║\n", loraSignalBandwidth / 1E3);
  Serial.println("╚════════════════════════════════════════════╝");
  
  // Send packet
  LoRa.beginPacket();
  LoRa.print(packetWithSeq);
  LoRa.endPacket();
  
  Serial.println("✓ Packet transmitted successfully!\n");
  
  // Notify clients
  StaticJsonDocument<400> doc;
  doc["type"] = "sent";
  doc["data"] = message;
  doc["sequence"] = packetSequence;
  doc["txPower"] = loraTxPower;
  doc["count"] = sentPacketCount;
  doc["frequency"] = loraFrequency / 1E6;
  doc["sf"] = loraSpreadingFactor;
  doc["bw"] = loraSignalBandwidth / 1E3;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("📡 Sending confirmation to clients...");
  Serial.printf("   JSON size: %d bytes\n", jsonString.length());
  
  sendViaBluetooth(jsonString);
  sendViaSerial(jsonString);
}

// ===== Receive LoRa Packet =====
void receiveLoRaPacket() {
  int packetSize = LoRa.parsePacket();
  
  if (packetSize > 0) {
    receivedPacketCount++;
    packetCount++;
    lastPacketTime = millis();
    
    String receivedData = "";
    while (LoRa.available()) {
      receivedData += (char)LoRa.read();
    }
    
    // Parse sequence number if present
    unsigned long rxSeq = 0;
    String actualMessage = receivedData;
    int seqEnd = receivedData.indexOf('|');
    if (receivedData.startsWith("SEQ:") && seqEnd > 0) {
      rxSeq = receivedData.substring(4, seqEnd).toInt();
      actualMessage = receivedData.substring(seqEnd + 1);
      
      // Track missed packets
      if (rxSeq > lastRxSequence + 1) {
        missedPackets += (rxSeq - lastRxSequence - 1);
      }
      lastRxSequence = rxSeq;
    }
    
    // Get signal metrics
    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();
    
    // Calculate distance
    float freqMHz = loraFrequency / 1E6;
    float distance = calculateDistance(rssi, freqMHz);
    
    // Calculate PDR
    float pdr = calculatePDR();
    
    // Display on Serial Monitor
    Serial.println("\n╔════════════════════════════════════════════╗");
    Serial.println("║       📡 LoRa Packet RECEIVED              ║");
    Serial.println("╠════════════════════════════════════════════╣");
    Serial.printf("║ Packet #%-6lu (RX: %-6lu)           ║\n", packetCount, receivedPacketCount);
    if (rxSeq > 0) {
      Serial.printf("║ Sequence: %-6lu  Missed: %-6lu      ║\n", rxSeq, missedPackets);
    }
    Serial.printf("║ Data: %-36s ║\n", actualMessage.c_str());
    Serial.println("╟────────────────────────────────────────────╢");
    Serial.printf("║ RSSI:     %-5d dBm                       ║\n", rssi);
    Serial.printf("║ SNR:      %6.1f dB                        ║\n", snr);
    Serial.printf("║ Distance: ~%-5.0f meters                  ║\n", distance);
    Serial.printf("║ TX Power: %-2d dBm                          ║\n", loraTxPower);
    Serial.printf("║ PDR:      %5.1f%%                          ║\n", pdr);
    Serial.println("╚════════════════════════════════════════════╝\n");
    
    // Send to connected clients
    sendToClients("data", actualMessage, rssi, snr, distance, pdr);
  }
}

// ===== Send to Clients =====
void sendToClients(String type, String data, int rssi, float snr, float distance, float pdr) {
  StaticJsonDocument<512> doc;
  doc["type"] = type;
  doc["data"] = data;
  doc["rssi"] = rssi;
  doc["snr"] = snr;
  doc["distance"] = distance;
  doc["txPower"] = loraTxPower;
  doc["pdr"] = pdr;
  doc["timestamp"] = millis();
  doc["frequency"] = loraFrequency / 1E6;
  doc["sf"] = loraSpreadingFactor;
  doc["bw"] = loraSignalBandwidth / 1E3;
  doc["channel"] = loraChannel;
  doc["sentCount"] = sentPacketCount;
  doc["rxCount"] = receivedPacketCount;
  doc["missed"] = missedPackets;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  sendViaBluetooth(jsonString);
  sendViaSerial(jsonString);
}

// ===== Send via Bluetooth =====
void sendViaBluetooth(String message) {
  if (deviceConnected && pCharacteristic) {
    pCharacteristic->setValue(message.c_str());
    pCharacteristic->notify();
    Serial.println("📲 Data sent via Bluetooth");
  } else {
    Serial.println("⚠ Bluetooth not connected - data not sent");
  }
}

// ===== Send via Serial =====
void sendViaSerial(String message) {
  Serial.println("📡 JSON Data: " + message);
}

// ===== Set LoRa Frequency =====
void setLoRaFrequency(float freq) {
  if (freq >= 410 && freq <= 525) {
    loraFrequency = freq * 1E6;
    LoRa.setFrequency(loraFrequency);
    Serial.printf("✓ Frequency set to: %.2f MHz\n", freq);
    printConfiguration();
  } else {
    Serial.println("✗ Invalid frequency! Must be 410-525 MHz");
  }
}

// ===== Set LoRa Spreading Factor =====
void setLoRaSpreadingFactor(int sf) {
  if (sf >= 6 && sf <= 12) {
    loraSpreadingFactor = sf;
    LoRa.setSpreadingFactor(sf);
    Serial.printf("✓ Spreading Factor set to: SF%d\n", sf);
    printConfiguration();
  } else {
    Serial.println("✗ Invalid SF! Must be 6-12");
  }
}

// ===== Set LoRa Bandwidth =====
void setLoRaBandwidth(float bw) {
  long bandwidth = (long)(bw * 1E3);
  loraSignalBandwidth = bandwidth;
  LoRa.setSignalBandwidth(bandwidth);
  Serial.printf("✓ Bandwidth set to: %.1f kHz\n", bw);
  printConfiguration();
}

// ===== Set LoRa TX Power =====
void setLoRaTxPower(int power) {
  if (power >= 2 && power <= 20) {
    loraTxPower = power;
    LoRa.setTxPower(power, PA_OUTPUT_PA_BOOST_PIN);
    Serial.printf("✓ TX Power set to: %d dBm\n", power);
    printConfiguration();
  } else {
    Serial.println("✗ Invalid TX power! Must be 2-20 dBm");
  }
}

// ===== Set LoRa Channel =====
void setLoRaChannel(int channel) {
  if (channel >= 0 && channel <= 255) {
    loraChannel = channel;
    float channelFrequency = (BASE_FREQUENCY + (channel * loraSignalBandwidth)) / 1E6;
    Serial.printf("✓ Channel set to: %d (%.2f MHz)\n", channel, channelFrequency);
    printConfiguration();
  } else {
    Serial.println("✗ Invalid channel! Must be 0-255");
  }
}

// ===== Calculate Distance =====
float calculateDistance(int rssi, float freqMHz) {
  float pathLoss = loraTxPower - rssi;
  float distance = pow(10.0, (pathLoss - 32.45 - 20 * log10(freqMHz)) / 20.0);
  return distance;
}

// ===== Calculate PDR =====
float calculatePDR() {
  if (receivedPacketCount == 0) return 100.0;
  
  unsigned long expected = receivedPacketCount + missedPackets;
  if (expected == 0) return 100.0;
  
  float pdr = ((float)receivedPacketCount / (float)expected) * 100.0;
  return pdr > 100.0 ? 100.0 : pdr;
}
