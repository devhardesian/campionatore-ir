// =============================================================================
//  Campionatore IR — firmware ESP32-S3
//
//  Frontend  : PWA (HTML/CSS/JS) in LittleFS, cartella /www
//  Backend   : ESPAsyncWebServer + API REST JSON
//  Storage   : /remotes.json su LittleFS, caricato in RAM all'avvio
//  Hardware  : RX fotodiodo su GPIO 18, TX LED IR via 2N2222A su GPIO 17
// =============================================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "config.h"
#include "ir_engine.h"
#include "storage.h"
#include "web_api.h"

static AsyncWebServer server(80);

// Nome di rete "campionatore-ir.local". Va riavviato dopo ogni riconnessione:
// il responder mDNS perde l'iscrizione al gruppo multicast quando cade il Wi-Fi.
static void startMdns() {
  MDNS.end();
  if (!MDNS.begin(HOSTNAME)) {
    Serial.println("[mDNS] ERRORE: avvio fallito");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  Serial.printf("[mDNS] Attivo: http://%s.local\n", HOSTNAME);
}

static void startWiFi() {
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);

  bool connected = false;
  if (strlen(WIFI_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[WiFi] Connessione a \"%s\"", WIFI_SSID);
    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) {
      delay(250);
      Serial.print('.');
    }
    Serial.println();
    connected = WiFi.status() == WL_CONNECTED;
  }

  if (connected) {
    Serial.printf("[WiFi] Connesso, Web App: http://%s  oppure  http://%s.local\n",
                  WiFi.localIP().toString().c_str(), HOSTNAME);
  } else {
    if (strlen(WIFI_SSID) > 0) Serial.println("[WiFi] Connessione fallita, avvio l'Access Point");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("[WiFi] Access Point \"%s\" (password \"%s\"), Web App: http://%s\n", AP_SSID,
                  AP_PASSWORD, WiFi.softAPIP().toString().c_str());
  }

  WiFi.setSleep(false);  // latenza minima per la Web App
  startMdns();
}

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
  startWiFi();
  webApiBegin(server);
  server.begin();
  Serial.println("[HTTP] Server avviato");
}

void loop() {
  // Tutto il lavoro avviene nel task IR e nel task del web server.
  static wl_status_t last = WL_IDLE_STATUS;
  if (WiFi.getMode() == WIFI_STA) {
    wl_status_t now = WiFi.status();
    if (now != last) {
      if (now == WL_CONNECTED) {
        Serial.printf("[WiFi] Online: %s\n", WiFi.localIP().toString().c_str());
        startMdns();
      }
      else if (last == WL_CONNECTED) Serial.println("[WiFi] Connessione persa, riconnessione...");
      last = now;
    }
  }
  delay(1000);
}
