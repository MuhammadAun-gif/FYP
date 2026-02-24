#include <SPI.h>
#include <LoRa.h>

#define LORA_SCK   13
#define LORA_MOSI  9
#define LORA_MISO  12
#define LORA_CS    10
#define LORA_RST   8
#define LORA_DIO0  7

// PDR
const int WINDOW_SIZE = 20;
bool pdrHistory[WINDOW_SIZE];
int pdrIndex = 0;

// RSSI and SNR history for variance calculation
float rssiHistory[WINDOW_SIZE];
float snrHistory[WINDOW_SIZE];
int statsIndex = 0;

// Inter-arrival time
unsigned long lastPacketTime = 0;
unsigned long interArrivalTime = 0;
const unsigned long TIMEOUT_THRESHOLD = 1200;

void setup() {
  Serial.begin(115200);
  SPI.begin(13, 12, 9, 10);
  LoRa.setPins(10, 8, 7);

  if (!LoRa.begin(434E6)) { while (1); }

  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  // Initialize histories
  for (int i = 0; i < WINDOW_SIZE; i++) {
    pdrHistory[i] = false;
    rssiHistory[i] = 0;
    snrHistory[i] = 0;
  }
}

float calculatePDR() {
  int success = 0;
  for (int i = 0; i < WINDOW_SIZE; i++) {
    if (pdrHistory[i]) success++;
  }
  return (float)success / WINDOW_SIZE;
}

float calculateVariance(float* history, int size) {
  float mean = 0;
  for (int i = 0; i < size; i++) mean += history[i];
  mean /= size;

  float variance = 0;
  for (int i = 0; i < size; i++) {
    variance += pow(history[i] - mean, 2);
  }
  return variance / size;
}

void logToCSV(unsigned long t, int rssi, int prssi, float snr, float pdr,
              long freqError, unsigned long iat, float rssiVar, float snrVar) {
  Serial.print(t);         Serial.print(",");
  Serial.print(rssi);      Serial.print(",");
  Serial.print(prssi);     Serial.print(",");
  Serial.print(snr);       Serial.print(",");
  Serial.print(pdr);       Serial.print(",");
  Serial.print(freqError); Serial.print(",");
  Serial.print(iat);       Serial.print(",");
  Serial.print(rssiVar);   Serial.print(",");
  Serial.println(snrVar); 
}

void loop() {
  int packetSize = LoRa.parsePacket();
  unsigned long now = millis();

  if (now - lastPacketTime > TIMEOUT_THRESHOLD) {
    // Timeout — mark as lost
    pdrHistory[pdrIndex] = false;
    rssiHistory[statsIndex] = -150; // floor value for lost packet
    snrHistory[statsIndex]  = -35;

    pdrIndex    = (pdrIndex + 1)    % WINDOW_SIZE;
    statsIndex  = (statsIndex + 1)  % WINDOW_SIZE;
    interArrivalTime = now - lastPacketTime;
    lastPacketTime = now;

    logToCSV(now, LoRa.rssi(), -150, -35.0, calculatePDR(),
             0, interArrivalTime,
             calculateVariance(rssiHistory, WINDOW_SIZE),
             calculateVariance(snrHistory, WINDOW_SIZE));
  }

  if (packetSize) {
    float snr    = LoRa.packetSnr();
    int prssi    = LoRa.packetRssi();
    int rssi     = LoRa.rssi();
    long freqErr = LoRa.packetFrequencyError();

    interArrivalTime = now - lastPacketTime;
    lastPacketTime   = now;

    pdrHistory[pdrIndex]   = true;
    rssiHistory[statsIndex] = prssi;
    snrHistory[statsIndex]  = snr;

    pdrIndex   = (pdrIndex + 1)   % WINDOW_SIZE;
    statsIndex = (statsIndex + 1) % WINDOW_SIZE;

    logToCSV(now, rssi, prssi, snr, calculatePDR(),
             freqErr, interArrivalTime,
             calculateVariance(rssiHistory, WINDOW_SIZE),
             calculateVariance(snrHistory, WINDOW_SIZE));

    while (LoRa.available()) { LoRa.read(); }
  }
}
