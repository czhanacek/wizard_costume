#include <esp_now.h>
#include <WiFi.h>
#include <Arduino.h>
#define ESPNOW_CHANNEL 1  // Must match receiver's pinned channel

typedef struct {
  int effect_id;
} SpellPacket;

SpellPacket spell;
uint8_t broadcastAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

void setup() {
  Serial.begin(115200);
  Serial.println("ESP-NOW Staff Ready (press space bar + Enter to cast spell)");

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  // Start hidden SoftAP to pin radio to ESPNOW_CHANNEL
  WiFi.softAP("wr-sync", "", ESPNOW_CHANNEL, 1 /* hidden */);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  spell.effect_id = 0;
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c <= '9') {
      spell.effect_id = c - '0';
      esp_now_send(broadcastAddress, (uint8_t *)&spell, sizeof(spell));
      Serial.printf("Cast spell %d\n", spell.effect_id);
    }
  }
}
