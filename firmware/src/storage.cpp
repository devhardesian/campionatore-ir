#include "storage.h"

#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_task_wdt.h>

#include <memory>

#include "config.h"
#include "json_scan.h"

RemoteStore store;

// ---------------------------------------------------------------------------
//  Memoria
// ---------------------------------------------------------------------------

void* bigMalloc(size_t size) {
  void* p = nullptr;
  if (psramFound()) p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = malloc(size);
  return p;
}

namespace {

struct DbAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override { return bigMalloc(size); }
  void deallocate(void* ptr) override { free(ptr); }
  void* reallocate(void* ptr, size_t newSize) override {
    if (psramFound()) {
      void* p = heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (p) return p;
    }
    return realloc(ptr, newSize);
  }
};

// Print con buffer: evita una scrittura su flash per ogni carattere del JSON.
class BufferedFileWriter : public Print {
 public:
  explicit BufferedFileWriter(File& f) : file_(f) {}
  size_t write(uint8_t c) override {
    buf_[len_++] = c;
    if (len_ == sizeof(buf_)) flushBuffer();
    return 1;
  }
  size_t write(const uint8_t* data, size_t size) override {
    for (size_t i = 0; i < size; i++) write(data[i]);
    return size;
  }
  void flushBuffer() {
    if (len_ && file_.write(buf_, len_) != len_) error_ = true;
    len_ = 0;
  }
  bool error() const { return error_; }

 private:
  File& file_;
  uint8_t buf_[512];
  size_t len_ = 0;
  bool error_ = false;
};

// Lettura bufferizzata per lo scanner JSON (un byte alla volta sarebbe lento).
class BufferedFileSource {
 public:
  explicit BufferedFileSource(File& f) : file_(f) {}
  int read() {
    if (pos_ >= len_) {
      len_ = file_.read(buf_, sizeof(buf_));
      pos_ = 0;
      if (len_ <= 0) return -1;
    }
    return buf_[pos_++];
  }

 private:
  File& file_;
  uint8_t buf_[512];
  int len_ = 0;
  int pos_ = 0;
};

bool isValidType(const char* t) {
  return t && (!strcmp(t, "tv") || !strcmp(t, "ac") || !strcmp(t, "custom"));
}

String randomId() {
  char b[9];
  snprintf(b, sizeof(b), "%08lx", (unsigned long)esp_random());
  return String(b);
}

bool sameText(const char* a, const char* b) {
  return a && b && String(a).equalsIgnoreCase(b);
}

void feedWatchdog() {
  if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
}

// ---------------------------------------------------------------------------
//  File dei segnali: "IRS2" + uint16 count + uint16 (freq/10) + count × uint16
// ---------------------------------------------------------------------------

const size_t kSigHeader = 8;

String sigDir(const char* root, const char* remoteId) {
  return String(root) + "/" + remoteId;
}

String sigPath(const char* root, const char* remoteId, const char* buttonId) {
  return sigDir(root, remoteId) + "/" + buttonId + ".bin";
}

bool writeSignal(const char* root, const char* remoteId, const char* buttonId,
                 const uint16_t* values, size_t count, uint32_t freq) {
  if (count == 0 || count > IR_MAX_ENTRIES) return false;
  LittleFS.mkdir(root);
  LittleFS.mkdir(sigDir(root, remoteId));
  const String path = sigPath(root, remoteId, buttonId);
  const String tmp = path + ".tmp";

  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  const uint16_t freq10 = (uint16_t)(freq / 10);
  const uint8_t header[kSigHeader] = {'I', 'R', 'S', '2',   (uint8_t)(count & 0xFF),
                                      (uint8_t)(count >> 8), (uint8_t)(freq10 & 0xFF),
                                      (uint8_t)(freq10 >> 8)};
  bool ok = f.write(header, kSigHeader) == kSigHeader;
  if (ok) {
    const size_t bytes = count * sizeof(uint16_t);
    ok = f.write((const uint8_t*)values, bytes) == bytes;  // little endian, come l'ESP32
  }
  f.close();
  if (!ok) {
    LittleFS.remove(tmp);
    return false;
  }
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}

// Legge un segnale. Se out è nullo restituisce solo count/freq.
bool readSignal(const char* remoteId, const char* buttonId, uint32_t* out, size_t maxCount,
                size_t& count, uint32_t& freq) {
  File f = LittleFS.open(sigPath(SIG_DIR, remoteId, buttonId), "r");
  if (!f) return false;
  uint8_t header[kSigHeader];
  if (f.read(header, kSigHeader) != (int)kSigHeader || memcmp(header, "IRS2", 4) != 0) {
    f.close();
    return false;
  }
  count = (size_t)header[4] | ((size_t)header[5] << 8);
  freq = ((uint32_t)header[6] | ((uint32_t)header[7] << 8)) * 10;
  if (count == 0 || count > IR_MAX_ENTRIES) {
    f.close();
    return false;
  }
  if (!out) {
    f.close();
    return true;
  }
  if (count > maxCount) count = maxCount;

  uint16_t chunk[128];
  size_t done = 0;
  bool ok = true;
  while (done < count && ok) {
    const size_t want = min(count - done, sizeof(chunk) / sizeof(chunk[0]));
    ok = f.read((uint8_t*)chunk, want * 2) == (int)(want * 2);
    for (size_t i = 0; i < want && ok; i++) out[done + i] = chunk[i];
    done += want;
  }
  f.close();
  return ok;
}

bool removeTree(const String& path) {
  File dir = LittleFS.open(path);
  if (!dir) return true;
  if (!dir.isDirectory()) {
    dir.close();
    return LittleFS.remove(path);
  }
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    const String child = path + "/" + e.name();
    const bool isDir = e.isDirectory();
    e.close();
    if (isDir) removeTree(child);
    else LittleFS.remove(child);
  }
  dir.close();
  return LittleFS.rmdir(path);
}

// ---------------------------------------------------------------------------
//  Importazione in streaming (backup v2 e vecchio database v1)
// ---------------------------------------------------------------------------
//
// Profondità nel JSON (numero di parentesi aperte):
//   v1: remotes[]=2 telecomando=3 buttons[]=4 pulsante=5 raw[]=6
//   v2: remotes[]=2 telecomando=3 buttons[]=4 pulsante=5, signals[]=2 segnale=3 raw[]=4

class ImportSink : public JsonScanSink {
 public:
  ImportSink(JsonDocument& dst, const char* root) : dst_(dst), root_(root) {
    remotes_ = dst_["remotes"].to<JsonArray>();
    values_.reset(new uint16_t[IR_MAX_ENTRIES]);
  }

  void onField(int depth, const char* key, const char* value) override {
    if (depth == 3) {
      if (!strcmp(key, "id")) remoteId_ = value;
      else if (!strcmp(key, "name")) remoteName_ = value;
      else if (!strcmp(key, "type")) remoteType_ = value;
      else if (!strcmp(key, "r")) sigRemote_ = value;    // formato v2
      else if (!strcmp(key, "b")) sigButton_ = value;
      else if (!strcmp(key, "freq")) sigFreq_ = strtoul(value, nullptr, 10);
    } else if (depth == 5) {
      if (!strcmp(key, "id")) buttonId_ = value;
      else if (!strcmp(key, "label")) buttonLabel_ = value;
      else if (!strcmp(key, "freq")) buttonFreq_ = strtoul(value, nullptr, 10);
      else if (!strcmp(key, "len")) buttonLen_ = strtoul(value, nullptr, 10);
    }
  }

  bool onRawBegin(int depth) override {
    count_ = 0;
    overflow_ = false;
    if (depth == 6) {  // v1: impulsi dentro il pulsante
      target_ = {remoteId_, buttonId_};
      targetFreq_ = buttonFreq_;
      return !remoteId_.isEmpty() && isValidId(buttonId_.c_str());
    }
    if (depth == 4) {  // v2: array "signals"
      target_ = {sigRemote_, sigButton_};
      targetFreq_ = sigFreq_;
      return !sigRemote_.isEmpty() && isValidId(sigButton_.c_str());
    }
    return false;
  }

  void onRawValue(uint32_t us) override {
    if (us == 0) us = 1;
    if (count_ >= IR_MAX_ENTRIES) {
      overflow_ = true;
      return;
    }
    values_[count_++] = us > 65535 ? 65535 : (uint16_t)us;
  }

  void onRawEnd() override {
    feedWatchdog();
    if (count_ % 2 == 0 && count_ > 0) count_--;  // deve finire con un mark
    if (overflow_ || count_ < IR_MIN_ENTRIES) {
      skipped_++;
      return;
    }
    uint32_t freq = targetFreq_;
    // Le portanti oltre 45 kHz misurate dalla v1 sono artefatti: erano calcolate
    // come media e bastavano pochi fronti spuri per falsarle.
    if (freq < IR_CARRIER_MIN_HZ || freq > 45000) freq = IR_DEFAULT_CARRIER_HZ;
    if (!writeSignal(root_, target_.remote.c_str(), target_.button.c_str(), values_.get(), count_,
                     freq)) {
      failed_ = true;
      return;
    }
    JsonObject b = ensureButton(target_.remote, target_.button);
    if (b.isNull()) {
      failed_ = true;
      return;
    }
    b["freq"] = freq;
    b["len"] = count_;
    written_++;
  }

  void onObjectEnd(int depth) override {
    if (depth == 5) {  // fine di un pulsante: nel formato v2 i metadati
      // arrivano prima dei segnali, quindi etichetta e portante si registrano qui
      if (!remoteId_.isEmpty() && isValidId(buttonId_.c_str())) {
        JsonObject b = ensureButton(remoteId_, buttonId_);
        if (b.isNull()) failed_ = true;
        else {
          if (b["freq"].isNull()) b["freq"] = buttonFreq_;
          if (b["len"].isNull() && buttonLen_) b["len"] = buttonLen_;
        }
      }
      buttonId_ = buttonLabel_ = "";
      buttonFreq_ = IR_DEFAULT_CARRIER_HZ;
      buttonLen_ = 0;
    } else if (depth == 3) {  // fine di un telecomando (o di un segnale v2)
      if (!remoteId_.isEmpty() && !remoteName_.isEmpty()) ensureRemote(remoteId_);
      remoteId_ = remoteName_ = remoteType_ = "";
      sigRemote_ = sigButton_ = "";
      sigFreq_ = IR_DEFAULT_CARRIER_HZ;
    }
  }

  bool failed() const { return failed_; }
  size_t written() const { return written_; }
  size_t skipped() const { return skipped_; }

 private:
  JsonObject ensureRemote(const String& id) {
    for (JsonObject r : remotes_) {
      if (id == (r["id"] | "")) return r;
    }
    JsonObject r = remotes_.add<JsonObject>();
    if (r.isNull()) return r;
    r["id"] = id;
    String name = clipUtf8(remoteName_, MAX_NAME_LEN);
    r["name"] = name.isEmpty() ? id : name;
    r["type"] = isValidType(remoteType_.c_str()) ? remoteType_ : "custom";
    r["buttons"].to<JsonArray>();
    return r;
  }

  JsonObject ensureButton(const String& remote, const String& button) {
    JsonObject r = ensureRemote(remote);
    if (r.isNull()) return JsonObject();
    JsonArray buttons = r["buttons"];
    for (JsonObject b : buttons) {
      if (button == (b["id"] | "")) return b;
    }
    JsonObject b = buttons.add<JsonObject>();
    if (b.isNull()) return b;
    b["id"] = button;
    String label = clipUtf8(buttonLabel_, MAX_LABEL_LEN);
    b["label"] = label.isEmpty() ? button : label;
    return b;
  }

  JsonDocument& dst_;
  JsonArray remotes_;
  const char* root_;
  std::unique_ptr<uint16_t[]> values_;
  size_t count_ = 0;
  bool overflow_ = false;
  bool failed_ = false;
  size_t written_ = 0, skipped_ = 0;
  String remoteId_, remoteName_, remoteType_;
  String buttonId_, buttonLabel_;
  uint32_t buttonFreq_ = IR_DEFAULT_CARRIER_HZ;
  size_t buttonLen_ = 0;
  String sigRemote_, sigButton_;
  uint32_t sigFreq_ = IR_DEFAULT_CARRIER_HZ;
  SignalRef target_;
  uint32_t targetFreq_ = IR_DEFAULT_CARRIER_HZ;
};

// Ricostruisce metadati puliti: scarta le voci non valide, rigenera gli id
// duplicati, rende univoci i nomi.
size_t sanitizeMeta(JsonVariantConst src, JsonDocument& dst) {
  dst.clear();
  dst["version"] = 2;
  JsonArray outRemotes = dst["remotes"].to<JsonArray>();

  for (JsonObjectConst r : src["remotes"].as<JsonArrayConst>()) {
    String name = r["name"] | "";
    name.trim();
    if (name.isEmpty()) continue;
    name = clipUtf8(name, MAX_NAME_LEN);

    String unique = name;
    for (int n = 2;; n++) {
      bool clash = false;
      for (JsonObjectConst o : outRemotes) clash |= sameText(o["name"], unique.c_str());
      if (!clash) break;
      unique = name + " (" + n + ")";
    }

    String id = r["id"] | "";
    for (;;) {
      bool clash = !isValidId(id.c_str());
      for (JsonObjectConst o : outRemotes) clash |= (id == (o["id"] | ""));
      if (!clash) break;
      id = randomId();
    }

    const char* type = r["type"] | "custom";
    JsonObject out = outRemotes.add<JsonObject>();
    out["id"] = id;
    out["name"] = unique;
    out["type"] = isValidType(type) ? type : "custom";
    JsonArray outButtons = out["buttons"].to<JsonArray>();

    JsonArrayConst buttons = r["buttons"];
    for (JsonObjectConst b : buttons) {
      const char* bid = b["id"] | "";
      if (!isValidId(bid)) continue;
      bool dup = false;
      for (JsonObjectConst o : outButtons) dup |= !strcmp(o["id"] | "", bid);
      if (dup) continue;
      const size_t len = b["len"] | 0;
      if (len < IR_MIN_ENTRIES || len > IR_MAX_ENTRIES) continue;
      uint32_t freq = b["freq"] | (uint32_t)IR_DEFAULT_CARRIER_HZ;
      if (freq < IR_CARRIER_MIN_HZ || freq > IR_CARRIER_MAX_HZ) freq = IR_DEFAULT_CARRIER_HZ;

      JsonObject ob = outButtons.add<JsonObject>();
      ob["id"] = bid;
      ob["label"] = clipUtf8(b["label"] | bid, MAX_LABEL_LEN);
      ob["freq"] = freq;
      ob["len"] = len;
    }
  }
  return outRemotes.size();
}

}  // namespace

bool isValidId(const char* s) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n == 0 || n > MAX_ID_LEN) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!isalnum((unsigned char)c) && c != '_' && c != '-') return false;
  }
  return true;
}

String clipUtf8(const String& s, size_t maxBytes) {
  if (s.length() <= maxBytes) return s;
  size_t n = maxBytes;
  while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;  // byte di continuazione
  return s.substring(0, n);
}

ArduinoJson::Allocator* dbAllocator() {
  static DbAllocator allocator;
  return &allocator;
}

const char* dbResultMessage(DbResult r) {
  switch (r) {
    case DbResult::Ok: return "ok";
    case DbResult::NotFound: return "Elemento non trovato";
    case DbResult::Exists: return "Esiste già un telecomando con questo nome";
    case DbResult::Invalid: return "Dati non validi";
    case DbResult::NoMemory: return "Memoria insufficiente";
    case DbResult::Busy: return "Database occupato, riprova";
    case DbResult::IoError: return "Errore di scrittura su flash";
  }
  return "Errore";
}

// ---------------------------------------------------------------------------
//  Avvio e persistenza
// ---------------------------------------------------------------------------

bool RemoteStore::begin() {
  mutex_ = xSemaphoreCreateMutex();
  resetDoc();
  fsOk_ = LittleFS.begin(true);
  if (!fsOk_) {
    Serial.println("[DB] ERRORE: LittleFS non disponibile, i dati non verranno salvati");
    return false;
  }
  return load();
}

void RemoteStore::resetDoc() {
  doc_.clear();
  doc_["version"] = 2;
  doc_["remotes"].to<JsonArray>();
}

bool RemoteStore::load() {
  hasLegacy_ = LittleFS.exists(LEGACY_DB_PATH);

  const char* path = nullptr;
  bool fromTmp = false;
  if (LittleFS.exists(DB_PATH)) {
    path = DB_PATH;
  } else if (LittleFS.exists(DB_TMP_PATH)) {  // salvataggio interrotto
    path = DB_TMP_PATH;
    fromTmp = true;
  }

  if (!path) {
    if (hasLegacy_) return migrateLegacy();
    Serial.println("[DB] Nessun database, ne creo uno nuovo");
    dirty_ = true;
  revision_++;
    return true;
  }

  File f = LittleFS.open(path, "r");
  if (!f) return false;
  JsonDocument tmp(dbAllocator());
  DeserializationError err = deserializeJson(tmp, f);
  f.close();

  if (err) {
    Serial.printf("[DB] %s corrotto (%s): salvato in %s, riparto da zero\n", path, err.c_str(),
                  DB_BAD_PATH);
    LittleFS.remove(DB_BAD_PATH);
    LittleFS.rename(path, DB_BAD_PATH);
    resetDoc();
    dirty_ = true;
  revision_++;
    return false;
  }

  const size_t n = sanitizeMeta(tmp, doc_);
  if (doc_.overflowed()) {
    Serial.println("[DB] Memoria insufficiente per caricare i metadati");
    resetDoc();
    return false;
  }
  if (fromTmp) dirty_ = true;
  Serial.printf("[DB] Caricati %u telecomandi, %u tasti\n", (unsigned)n, (unsigned)buttonCount());
  return true;
}

bool RemoteStore::migrateLegacy() {
  Serial.println("[DB] Trovato database v1 (impulsi in RAM): migrazione al formato v2...");
  const uint32_t t0 = millis();
  size_t remotes = 0;
  const DbResult r = importFile(LEGACY_DB_PATH, remotes);
  if (r != DbResult::Ok) {
    Serial.printf("[DB] ERRORE migrazione: %s\n", dbResultMessage(r));
    resetDoc();
    return false;
  }
  Serial.printf("[DB] Migrazione completata in %.1f s: %u telecomandi, %u tasti.\n",
                (millis() - t0) / 1000.0f, (unsigned)remotes, (unsigned)buttonCount());
  Serial.printf("[DB] Il vecchio %s resta come copia (scaricabile da /api/legacy)\n",
                LEGACY_DB_PATH);
  return true;
}

bool RemoteStore::removeLegacy() {
  if (!hasLegacy_) return true;
  const bool ok = LittleFS.remove(LEGACY_DB_PATH);
  if (ok) hasLegacy_ = false;
  return ok;
}

bool RemoteStore::save() {
  if (!dirty_ || !fsOk_) return true;
  if (lastSaveFailMs_ && millis() - lastSaveFailMs_ < 5000) return false;
  if (!lock()) return false;

  dirty_ = false;  // una modifica concorrente lo rimetterà a true
  bool ok = false;
  File f = LittleFS.open(DB_TMP_PATH, "w");
  if (f) {
    BufferedFileWriter w(f);
    const size_t written = serializeJson(doc_, w);
    w.flushBuffer();
    ok = written > 0 && !w.error();
    f.close();
  }
  unlock();

  if (ok && !LittleFS.rename(DB_TMP_PATH, DB_PATH)) {
    LittleFS.remove(DB_PATH);
    ok = LittleFS.rename(DB_TMP_PATH, DB_PATH);
  }

  if (ok) {
    lastSaveFailMs_ = 0;
  } else {
    dirty_ = true;
  revision_++;
    lastSaveFailMs_ = millis() | 1;
    Serial.println("[DB] ERRORE: salvataggio dei metadati fallito");
  }
  return ok;
}

bool RemoteStore::lock(uint32_t timeoutMs) {
  return mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void RemoteStore::unlock() { xSemaphoreGive(mutex_); }

// ---------------------------------------------------------------------------
//  Operazioni
// ---------------------------------------------------------------------------

JsonObject RemoteStore::findRemote(const char* id) {
  for (JsonObject r : doc_["remotes"].as<JsonArray>()) {
    if (!strcmp(r["id"] | "", id)) return r;
  }
  return JsonObject();
}

static int findButtonIndex(JsonArray buttons, const char* id) {
  int i = 0;
  for (JsonObject b : buttons) {
    if (!strcmp(b["id"] | "", id)) return i;
    i++;
  }
  return -1;
}

DbResult RemoteStore::createRemote(const String& nameIn, const String& type, String& newId) {
  String name = nameIn;
  name.trim();
  name = clipUtf8(name, MAX_NAME_LEN);
  if (name.isEmpty() || !isValidType(type.c_str())) return DbResult::Invalid;
  if (!lock()) return DbResult::Busy;

  JsonArray remotes = doc_["remotes"];
  for (JsonObject r : remotes) {
    if (sameText(r["name"], name.c_str())) {
      unlock();
      return DbResult::Exists;
    }
  }
  do newId = randomId();
  while (!findRemote(newId.c_str()).isNull());

  JsonObject r = remotes.add<JsonObject>();
  const bool ok = !r.isNull() && r["id"].set(newId) && r["name"].set(name) && r["type"].set(type) &&
                  !r["buttons"].to<JsonArray>().isNull();
  if (!ok) {
    if (!r.isNull()) remotes.remove(remotes.size() - 1);
    unlock();
    return DbResult::NoMemory;
  }
  dirty_ = true;
  revision_++;
  unlock();
  return DbResult::Ok;
}

DbResult RemoteStore::deleteRemote(const String& remoteId) {
  if (!lock()) return DbResult::Busy;
  JsonArray remotes = doc_["remotes"];
  int i = 0;
  for (JsonObject r : remotes) {
    if (remoteId == (r["id"] | "")) {
      remotes.remove(i);
      dirty_ = true;
  revision_++;
      unlock();
      removeTree(sigDir(SIG_DIR, remoteId.c_str()));
      return DbResult::Ok;
    }
    i++;
  }
  unlock();
  return DbResult::NotFound;
}

DbResult RemoteStore::deleteButton(const String& remoteId, const String& buttonId) {
  if (!lock()) return DbResult::Busy;
  DbResult res = DbResult::NotFound;
  JsonObject r = findRemote(remoteId.c_str());
  if (!r.isNull()) {
    JsonArray buttons = r["buttons"];
    const int idx = findButtonIndex(buttons, buttonId.c_str());
    if (idx >= 0) {
      buttons.remove(idx);
      dirty_ = true;
  revision_++;
      res = DbResult::Ok;
    }
  }
  unlock();
  if (res == DbResult::Ok) LittleFS.remove(sigPath(SIG_DIR, remoteId.c_str(), buttonId.c_str()));
  return res;
}

bool RemoteStore::remoteExists(const char* remoteId) {
  if (!lock()) return false;
  const bool found = !findRemote(remoteId).isNull();
  unlock();
  return found;
}

bool RemoteStore::buttonExists(const char* remoteId, const char* buttonId) {
  if (!lock()) return false;
  JsonObject r = findRemote(remoteId);
  const bool found = !r.isNull() && findButtonIndex(r["buttons"], buttonId) >= 0;
  unlock();
  return found;
}

DbResult RemoteStore::setButton(const char* remoteId, const char* buttonId, const char* label,
                                const uint32_t* raw, size_t count, uint32_t freq) {
  if (!isValidId(buttonId) || count == 0 || count > IR_MAX_ENTRIES) return DbResult::Invalid;
  if (!fsOk_) return DbResult::IoError;
  if (!remoteExists(remoteId)) return DbResult::NotFound;

  // Prima il segnale su flash: se fallisce i metadati restano coerenti.
  std::unique_ptr<uint16_t[]> values(new (std::nothrow) uint16_t[count]);
  if (!values) return DbResult::NoMemory;
  for (size_t i = 0; i < count; i++) values[i] = raw[i] > 65535 ? 65535 : (uint16_t)raw[i];
  if (!writeSignal(SIG_DIR, remoteId, buttonId, values.get(), count, freq)) return DbResult::IoError;

  if (!lock()) return DbResult::Busy;
  JsonObject r = findRemote(remoteId);
  if (r.isNull()) {
    unlock();
    return DbResult::NotFound;
  }
  JsonArray buttons = r["buttons"];
  if (buttons.isNull()) buttons = r["buttons"].to<JsonArray>();
  int idx = findButtonIndex(buttons, buttonId);
  JsonObject b;
  if (idx >= 0) {
    b = buttons[idx];
  } else {
    b = buttons.add<JsonObject>();
    if (!b.isNull()) idx = (int)buttons.size() - 1;
  }

  bool ok = !b.isNull() && b["id"].set(buttonId) && b["freq"].set(freq) && b["len"].set(count);
  if (ok && label && *label) ok = b["label"].set(clipUtf8(label, MAX_LABEL_LEN));
  else if (ok && b["label"].isNull()) ok = b["label"].set(buttonId);

  if (!ok) {
    if (idx >= 0) buttons.remove(idx);
    unlock();
    LittleFS.remove(sigPath(SIG_DIR, remoteId, buttonId));
    return DbResult::NoMemory;
  }
  dirty_ = true;
  revision_++;
  unlock();
  return DbResult::Ok;
}

DbResult RemoteStore::getButton(const char* remoteId, const char* buttonId, uint32_t* raw,
                                size_t maxCount, size_t& count, uint32_t& freq) {
  if (!buttonExists(remoteId, buttonId)) return DbResult::NotFound;
  if (!readSignal(remoteId, buttonId, raw, maxCount, count, freq)) return DbResult::IoError;
  return count ? DbResult::Ok : DbResult::Invalid;
}

bool RemoteStore::buttonToJson(const char* remoteId, const char* buttonId, JsonObject out) {
  if (!lock()) return false;
  bool found = false;
  JsonObject r = findRemote(remoteId);
  if (!r.isNull()) {
    JsonArray buttons = r["buttons"];
    const int idx = findButtonIndex(buttons, buttonId);
    if (idx >= 0) found = out.set(buttons[idx].as<JsonObject>());
  }
  unlock();
  if (!found) return false;

  std::unique_ptr<uint32_t[]> raw(new (std::nothrow) uint32_t[IR_MAX_ENTRIES]);
  size_t count = 0;
  uint32_t freq = 0;
  if (raw && readSignal(remoteId, buttonId, raw.get(), IR_MAX_ENTRIES, count, freq)) {
    JsonArray arr = out["raw"].to<JsonArray>();
    for (size_t i = 0; i < count; i++) arr.add(raw[i]);
  }
  return true;
}

void RemoteStore::refreshFsStats() {
  if (!fsOk_) return;
  fsTotal_ = LittleFS.totalBytes();
  fsUsed_ = LittleFS.usedBytes();
}

size_t RemoteStore::buttonCount() {
  if (!lock()) return 0;
  size_t n = 0;
  for (JsonObject r : doc_["remotes"].as<JsonArray>()) n += r["buttons"].size();
  unlock();
  return n;
}

char* RemoteStore::serializeMeta(size_t& len) {
  if (!lock()) return nullptr;
  len = measureJson(doc_);
  char* buf = (char*)bigMalloc(len + 1);
  if (buf) serializeJson(doc_, buf, len + 1);
  unlock();
  return buf;
}

char* RemoteStore::serializeRemotes(size_t& len) {
  if (!lock()) return nullptr;
  JsonArray remotes = doc_["remotes"];
  len = measureJson(remotes);
  char* buf = (char*)bigMalloc(len + 1);
  if (buf) serializeJson(remotes, buf, len + 1);
  unlock();
  return buf;
}

void RemoteStore::collectSignals(std::vector<SignalRef>& out) {
  out.clear();
  if (!lock()) return;
  for (JsonObject r : doc_["remotes"].as<JsonArray>()) {
    const char* rid = r["id"] | "";
    JsonArray buttons = r["buttons"];
    for (JsonObject b : buttons) out.push_back({rid, b["id"] | ""});
  }
  unlock();
}

bool RemoteStore::signalJson(const SignalRef& ref, String& out) {
  std::unique_ptr<uint32_t[]> raw(new (std::nothrow) uint32_t[IR_MAX_ENTRIES]);
  size_t count = 0;
  uint32_t freq = 0;
  if (!raw || !readSignal(ref.remote.c_str(), ref.button.c_str(), raw.get(), IR_MAX_ENTRIES, count,
                          freq))
    return false;

  out = "";
  out.reserve(count * 7 + 64);
  out += "{\"r\":\"";
  out += ref.remote;
  out += "\",\"b\":\"";
  out += ref.button;
  out += "\",\"freq\":";
  out += freq;
  out += ",\"raw\":[";
  for (size_t i = 0; i < count; i++) {
    if (i) out += ',';
    out += raw[i];
  }
  out += "]}";
  return true;
}

DbResult RemoteStore::importFile(const char* path, size_t& remotes) {
  if (!fsOk_) return DbResult::IoError;
  File f = LittleFS.open(path, "r");
  if (!f) return DbResult::Invalid;

  removeTree(SIG_NEW_DIR);
  JsonDocument dst(dbAllocator());
  dst["version"] = 2;
  dst["remotes"].to<JsonArray>();

  std::unique_ptr<ImportSink> sink(new (std::nothrow) ImportSink(dst, SIG_NEW_DIR));
  if (!sink) {
    f.close();
    return DbResult::NoMemory;
  }
  BufferedFileSource src(f);
  JsonScanner<BufferedFileSource> scanner(src, *sink);
  const bool scanned = scanner.run();
  const bool failed = sink->failed();
  const size_t written = sink->written(), skipped = sink->skipped();
  sink.reset();
  f.close();

  if (!scanned || failed || dst.overflowed() || written == 0) {
    removeTree(SIG_NEW_DIR);
    Serial.printf("[DB] Importazione fallita (scan=%d errori=%d segnali=%u)\n", scanned, failed,
                  (unsigned)written);
    if (failed) return DbResult::IoError;
    return dst.overflowed() ? DbResult::NoMemory : DbResult::Invalid;
  }

  // I metadati devono corrispondere ai file scritti: via i tasti senza segnale.
  size_t pruned = 0;
  for (JsonObject r : dst["remotes"].as<JsonArray>()) {
    const char* rid = r["id"] | "";
    JsonArray buttons = r["buttons"];
    for (int i = (int)buttons.size() - 1; i >= 0; i--) {
      const char* bid = buttons[i]["id"] | "";
      if (!LittleFS.exists(sigPath(SIG_NEW_DIR, rid, bid))) {
        buttons.remove(i);
        pruned++;
      }
    }
  }

  if (!lock()) {
    removeTree(SIG_NEW_DIR);
    return DbResult::Busy;
  }
  removeTree(SIG_DIR);
  const bool moved = LittleFS.rename(SIG_NEW_DIR, SIG_DIR);
  doc_ = std::move(dst);
  remotes = doc_["remotes"].size();
  dirty_ = true;
  revision_++;
  unlock();
  if (!moved) {
    Serial.println("[DB] ERRORE: impossibile spostare i segnali importati");
    return DbResult::IoError;
  }
  save();
  refreshFsStats();
  Serial.printf("[DB] Importati %u telecomandi, %u segnali (%u scartati, %u senza dati)\n",
                (unsigned)remotes, (unsigned)written, (unsigned)skipped, (unsigned)pruned);
  return DbResult::Ok;
}
