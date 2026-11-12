#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Gestione della rete Wi-Fi.
//
// Reti note = reti salvate dall'app (NVS, la più recente per prima) + la rete
// del firmware (config.h). All'avvio il dispositivo si collega alla rete nota
// con il segnale migliore; se nessuna risponde attiva l'Access Point e, finché
// nessun telefono vi è collegato, ogni minuto cerca di nuovo le reti note.
// Se la rete cade per più di WIFI_LOST_FALLBACK_MS torna in Access Point.
//
// Le funzioni pubbliche si possono chiamare dai gestori HTTP: le operazioni
// sulla radio (connessioni, scansioni, riavvio dell'AP) vengono eseguite da
// net::loop() nel task principale, dopo aver lasciato partire la risposta.

enum class NetResult : uint8_t {
  Ok,
  InvalidSsid,
  InvalidPassword,
  InvalidApPassword,
  NotFound,
  Busy,
  ReadOnly,  // rete definita in config.h
  Full,      // già WIFI_MAX_NETWORKS reti salvate
};

namespace net {

void begin();
void loop();  // da chiamare spesso (ogni ~50 ms) dal loop() di Arduino

// Stato completo per GET /api/wifi.
void statusJson(JsonObject out);
// Stato sintetico per GET /api/info.
void infoJson(JsonObject out);

// Ultima scansione; se è vecchia (o refresh) ne avvia una nuova.
void scanJson(JsonObject out, bool refresh);

// Prova a collegarsi a una rete: se riesce la salva. Restituisce l'id del
// tentativo, da confrontare con "attempt.id" di statusJson().
NetResult connect(const String& ssid, const String& password, uint32_t& attemptId);
// Salva una rete senza collegarsi (verrà usata quando è raggiungibile).
NetResult save(const String& ssid, const String& password);
NetResult forget(const String& ssid);

// Password dell'Access Point (8-63 caratteri ASCII stampabili). Se l'AP è
// acceso viene riavviato (apRestart = true): i telefoni collegati cadono.
NetResult setApPassword(const String& password, bool& apRestart);

const char* resultMessage(NetResult r);

}  // namespace net
