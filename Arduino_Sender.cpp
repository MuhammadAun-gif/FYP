#include <SPI.h>
#include <LoRa.h>

int counter = 0;
unsigned long lastSendTime = 0;
const int interval = 1000; // Send exactly every 1000ms

void setup() {
  Serial.begin(9600); 

  // Uno Pin Mapping for Ra-02
  LoRa.setPins(8, 9, 2); // NSS/CS, RESET, DIO0

  if (!LoRa.begin(434E6)) {
    Serial.println("LoRa failed");
    while (1);
  }

  // Same settings as Receiver (ESP32-S3)
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setTxPower(2);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  
  Serial.println("Arduino Sender Ready");
}

void loop() {
  if (millis() - lastSendTime >= interval) {
    lastSendTime = millis();

    Serial.print("Sending packet: ");
    Serial.println(counter);

    LoRa.beginPacket();
    LoRa.print(counter); // Primary feature for PDR
    LoRa.endPacket();

    counter++;
  }
}
