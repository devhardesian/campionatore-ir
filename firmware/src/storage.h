#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <vector>

// Database dei telecomandi (formato v2).
//
//   /db.json                        metadati, caricati in RAM all'avvio
//   /sig/<telecomando>/<tasto>.bin  impulsi di un singolo segnale
//
// I metadati sono piccoli (poche decine di byte per tasto) e stanno in RAM;
// gli impulsi restano sulla flash e vengono letti solo al momento dell'invio.
// Nella v1 tutto (impulsi compresi) stava in /remotes.json e in RAM: con oltre
// un centinaio di tasti la memoria non bastava più.
//
// /db.json:
// {"version":2,"remotes":[{"id":"3fa2c1d0","name":"TV Sala","type":"tv",
//                          "buttons":[{"id":"power","label":"Power","freq":38000,"len":67}]}]}
//
// File del segnale: "IRS2" + uint16 count + uint16 (freq/10) + count × uint16 (µs).

enum class DbResult : uint8_t { Ok, NotFound, Exists, Invalid, NoMemory, Busy, IoError };

const char* dbResultMessage(DbResult r);

// Allocatore ArduinoJson che usa la PSRAM quando disponibile.
ArduinoJson::Allocator* dbAllocator();

// Id ammessi per telecomandi e pulsanti: [A-Za-z0-9_-], max MAX_ID_LEN.
bool isValidId(const char* s);
// Tronca una stringa UTF-8 senza spezzare i caratteri multibyte.
String clipUtf8(const String& s, size_t maxBytes);

struct SignalRef {
  String remote;
  String button;
};

class RemoteStore {
 public:
  bool begin();

  DbResult createRemote(const String& name, const String& type, String& newId);
  DbResult deleteRemote(const String& remoteId);
  DbResult deleteButton(const String& remoteId, const String& buttonId);
  bool remoteExists(const char* remoteId);
  bool buttonExists(const char* remoteId, const char* buttonId);

  DbResult setButton(const char* remoteId, const char* buttonId, const char* label,
                     const uint32_t* raw, size_t count, uint32_t freq);
  DbResult getButton(const char* remoteId, const char* buttonId, uint32_t* raw, size_t maxCount,
                     size_t& count, uint32_t& freq);

  // Metadati serializzati (buffer malloc, da liberare con free).
  char* serializeMeta(size_t& len);
  // Solo l'array "remotes", per comporre il backup in streaming.
  char* serializeRemotes(size_t& len);
  // Elenco dei segnali presenti, per scorrerli uno alla volta.
  void collectSignals(std::vector<SignalRef>& out);
  // {"r":"…","b":"…","freq":38000,"raw":[…]} letto dalla flash.
  bool signalJson(const SignalRef& ref, String& out);
  // Un pulsante con i suoi impulsi (anteprima dopo l'apprendimento).
  bool buttonToJson(const char* remoteId, const char* buttonId, JsonObject out);

  // Sostituisce l'intero database con il contenuto di un file di backup
  // (formato v2 o v1). Lavora in streaming: la RAM usata non dipende dal file.
  DbResult importFile(const char* path, size_t& remotes);

  size_t buttonCount();

  // Cambia a ogni modifica: chi tiene in cache un segnale sa quando scade.
  uint32_t revision() const { return revision_.load(); }

  // LittleFS.usedBytes() percorre l'intero filesystem (secondi, con molti
  // file): il valore viene aggiornato dal task IR e qui si legge la cache.
  void refreshFsStats();
  size_t fsUsed() const { return fsUsed_; }
  size_t fsTotal() const { return fsTotal_; }
  size_t fsFree() const { return fsTotal_ > fsUsed_ ? fsTotal_ - fsUsed_ : 0; }
  bool hasLegacy() const { return hasLegacy_; }
  bool removeLegacy();

  bool isDirty() const { return dirty_; }
  bool save();

 private:
  bool load();
  bool migrateLegacy();
  bool lock(uint32_t timeoutMs = 3000);
  void unlock();
  JsonObject findRemote(const char* id);
  void resetDoc();

  JsonDocument doc_{dbAllocator()};
  SemaphoreHandle_t mutex_ = nullptr;
  volatile bool dirty_ = false;
  bool fsOk_ = false;
  bool hasLegacy_ = false;
  uint32_t lastSaveFailMs_ = 0;
  std::atomic<uint32_t> revision_{1};
  volatile size_t fsUsed_ = 0;
  volatile size_t fsTotal_ = 0;
};

extern RemoteStore store;

// Buffer grandi (metadati serializzati, backup): PSRAM se presente.
void* bigMalloc(size_t size);
