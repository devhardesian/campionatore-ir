// =============================================================================
//  Campionatore IR — firmware ESP32-S3
//
//  Frontend  : PWA (HTML/CSS/JS) in LittleFS, cartella /www
//  Backend   : ESPAsyncWebServer + API REST JSON
//  Storage   : metadati in /db.json (RAM), segnali in /sig su LittleFS
//  Rete      : reti Wi-Fi e password dell'AP nella NVS (wifi_manager)
//  Hardware  : RX fotodiodo su GPIO 18, TX LED IR via 2N2222A su GPIO 17
// =============================================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "config.h"
#include "ir_engine.h"
#include "storage.h"
#include "web_api.h"
#include "wifi_manager.h"

static AsyncWebServer server(80);

void setup() {
  // Il LED IR deve restare spento fin dal primo istante.
  pinMode(IR_TX_PIN, OUTPUT);
  digitalWrite(IR_TX_PIN, LOW);

  Serial.begin(115200);
  delay(300);
  Serial.printf("\n=== Campionatore IR v%s ===\n", FW_VERSION);
  Serial.printf("[SYS] Heap libero: %lu byte, PSRAM: %lu byte\n", (unsigned long)ESP.getFreeHeap(),
                (unsigned long)(psramFound() ? ESP.getFreePsram() : 0));

  store.begin();
  if (!LittleFS.exists(WEB_ROOT "index.html"))
    Serial.println("[FS] ATTENZIONE: Web App assente, esegui 'pio run -t uploadfs'");

  ir::begin();
  net::begin();
  webApiBegin(server);
  server.begin();
  Serial.println("[HTTP] Server avviato");
}

void loop() {
  // IR e web server hanno i loro task; qui gira solo la gestione del Wi-Fi.
  net::loop();
  delay(25);
}
