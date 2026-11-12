#include "wifi_manager.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>
#include <vector>

#include "config.h"
#include "ir_engine.h"
#include "storage.h"

namespace net {
namespace {

// Namespace NVS: "n" = numero di reti, "s0".."s4" / "p0".."p4" = nome e
// password, "ap" = password dell'Access Point (assente = predefinita).
const char* kPrefsNamespace = "wifi";
const uint32_t kScanMaxAgeMs = 20000;   // oltre, l'app riceve una scansione nuova
const uint32_t kScanTimeoutMs = 15000;
const uint32_t kCommandDelayMs = 500;   // lascia partire la risposta HTTP
const size_t kMaxScanResults = 30;

struct Cred {
  String ssid;
  String pass;
};

struct Candidate {
  Cred cred;
  bool user = false;  // chiesto dall'app: se riesce la rete viene salvata
};

struct ScanEntry {
  String ssid;
  int32_t rssi;
  bool secure;
};

enum class Mode : uint8_t { Connecting, Station, AccessPoint };
enum class AttemptState : uint8_t { None, Pending, Connecting, Ok, Failed };

const char* attemptStateName(AttemptState s) {
  switch (s) {
    case AttemptState::Pending:
    case AttemptState::Connecting: return "connecting";
    case AttemptState::Ok: return "ok";
    case AttemptState::Failed: return "failed";
    default: return "none";
  }
}

// ---------------------------------------------------------------------------
//  Stato condiviso con i gestori HTTP: si legge e si scrive solo con il mutex
// ---------------------------------------------------------------------------
SemaphoreHandle_t g_mutex = nullptr;

struct Lock {
  Lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_mutex); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

std::vector<Cred> g_saved;  // reti salvate dall'app, la più recente per prima
String g_apPass = AP_PASSWORD;
bool g_dirty = false;       // configurazione da scrivere nella NVS
Mode g_mode = Mode::Connecting;
String g_staSsid;           // rete attuale o in prova
bool g_apActive = false;
uint32_t g_apOffAt = 0;     // spegnimento programmato dell'AP (0 = nessuno)

struct Attempt {
  uint32_t id = 0;
  AttemptState state = AttemptState::None;
  String ssid;
  String error;
  String ip;
  String fallback;  // rete su cui il dispositivo è tornato dopo un errore
} g_attempt;
uint32_t g_attemptCounter = 0;

struct Pending {
  bool connect = false;
  Candidate cand;
  bool applyAp = false;
  bool scan = false;
  uint32_t notBefore = 0;
} g_pending;

std::vector<ScanEntry> g_scan;
uint32_t g_scanAt = 0;  // 0 = nessuna scansione
bool g_scanning = false;

// Scritti dal gestore eventi Wi-Fi, letti dal loop.
volatile uint32_t g_discCount = 0;
volatile uint8_t g_discReason = 0;

// ---------------------------------------------------------------------------
//  Stato interno del loop (un solo task: nessun mutex)
// ---------------------------------------------------------------------------
std::vector<Candidate> t_queue;  // reti da provare dopo quella in corso
Candidate t_cur;
bool t_trying = false;
uint32_t t_start = 0;
uint32_t t_discBase = 0;
Cred t_station;  // rete attuale, per tornarci se un cambio di rete fallisce
bool t_connected = false;
bool t_lost = false;
uint32_t t_lostSince = 0;
uint32_t t_nextRetry = 0;
bool t_scanRunning = false;
bool t_scanAuto = false;
uint32_t t_scanStart = 0;

// ---------------------------------------------------------------------------
//  Validazione
// ---------------------------------------------------------------------------
bool validSsid(const String& s) { return s.length() >= 1 && s.length() <= 32; }

bool isHex64(const String& s) {
  if (s.length() != 64) return false;
  for (size_t i = 0; i < s.length(); i++)
    if (!isxdigit((unsigned char)s[i])) return false;
  return true;
}

// Rete aperta (vuota), passphrase WPA da 8-63 byte o chiave esadecimale.
bool validWifiPass(const String& s) {
  return s.isEmpty() || (s.length() >= 8 && s.length() <= 63) || isHex64(s);
}

// Password dell'AP: passphrase WPA2 standard, solo ASCII stampabile.
bool validApPass(const String& s) {
  if (s.length() < 8 || s.length() > 63) return false;
  for (size_t i = 0; i < s.length(); i++)
    if (s[i] < 0x20 || s[i] > 0x7e) return false;
  return true;
}

// ---------------------------------------------------------------------------
//  Configurazione (NVS)
// ---------------------------------------------------------------------------
String key(char prefix, size_t i) { return String(prefix) + String((unsigned)i); }

void loadConfig() {
  Preferences p;
  if (!p.begin(kPrefsNamespace, false)) {
    Serial.println("[WiFi] ERRORE: NVS non disponibile, uso le impostazioni del firmware");
    return;
  }
  const size_t n = std::min<size_t>(p.getUChar("n", 0), WIFI_MAX_NETWORKS);
  for (size_t i = 0; i < n; i++) {
    Cred c{p.getString(key('s', i).c_str(), ""), p.getString(key('p', i).c_str(), "")};
    if (validSsid(c.ssid)) g_saved.push_back(c);
  }
  const String ap = p.isKey("ap") ? p.getString("ap", "") : String();
  if (validApPass(ap)) g_apPass = ap;
  p.end();
}

// Scrive la configurazione se è cambiata. Le scritture su flash bloccano la
// cache: non si fanno durante un campionamento IR, che verrebbe falsato.
void persist() {
  std::vector<Cred> nets;
  String ap;
  {
    Lock l;
    if (!g_dirty) return;
    nets = g_saved;
    ap = g_apPass;
    g_dirty = false;
  }
  Preferences p;
  if (!p.begin(kPrefsNamespace, false)) {
    Lock l;
    g_dirty = true;
    return;
  }
  p.putUChar("n", nets.size());
  for (size_t i = 0; i < WIFI_MAX_NETWORKS; i++) {
    const String ks = key('s', i), kp = key('p', i);
    if (i < nets.size()) {
      p.putString(ks.c_str(), nets[i].ssid);
      p.putString(kp.c_str(), nets[i].pass);
    } else {
      if (p.isKey(ks.c_str())) p.remove(ks.c_str());
      if (p.isKey(kp.c_str())) p.remove(kp.c_str());
    }
  }
  if (ap == AP_PASSWORD) {
    if (p.isKey("ap")) p.remove("ap");
  } else {
    p.putString("ap", ap);
  }
  p.end();
  Serial.printf("[WiFi] Impostazioni salvate (%u reti)\n", (unsigned)nets.size());
}

int findSavedLocked(const String& ssid) {
  for (size_t i = 0; i < g_saved.size(); i++)
    if (g_saved[i].ssid == ssid) return (int)i;
  return -1;
}

// Salva (o aggiorna) una rete in cima all'elenco; oltre il limite cade la più vecchia.
void rememberLocked(const Cred& c) {
  const int i = findSavedLocked(c.ssid);
  if (i >= 0) g_saved.erase(g_saved.begin() + i);
  g_saved.insert(g_saved.begin(), c);
  if (g_saved.size() > WIFI_MAX_NETWORKS) g_saved.resize(WIFI_MAX_NETWORKS);
  g_dirty = true;
}

bool firmwareNetworkLocked() {
  return strlen(WIFI_SSID) > 0 && findSavedLocked(WIFI_SSID) < 0;
}

// Reti note: quelle salvate, poi quella del firmware se non è già tra queste.
std::vector<Cred> knownLocked() {
  std::vector<Cred> out = g_saved;
  if (firmwareNetworkLocked()) out.push_back({WIFI_SSID, WIFI_PASSWORD});
  return out;
}

// ---------------------------------------------------------------------------
//  Radio
// ---------------------------------------------------------------------------
void onStaDisconnected(arduino_event_t* ev) {
  const uint8_t reason = ev->event_info.wifi_sta_disconnected.reason;
  if (reason == WIFI_REASON_ASSOC_LEAVE) return;  // disconnessione voluta
  g_discReason = reason;
  g_discCount = g_discCount + 1;
}

const char* reasonMessage(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
      return "Rete non trovata: controlla il nome e la distanza dal router";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_802_1X_AUTH_FAILED:
      return "Password errata";
    case WIFI_REASON_ASSOC_FAIL:
      return "Il router ha rifiutato la connessione";
    default:
      return "Connessione non riuscita";
  }
}

// Nome di rete "campionatore-ir.local". Va riavviato dopo ogni riconnessione:
// il responder mDNS perde l'iscrizione al gruppo multicast quando cade il Wi-Fi.
void startMdns() {
  MDNS.end();
  if (!MDNS.begin(HOSTNAME)) {
    Serial.println("[mDNS] ERRORE: avvio fallito");
    return;
  }
  MDNS.addService("http", "tcp", 80);
}

void startAp() {
  if (g_apActive) return;
  String pass;
  {
    Lock l;
    pass = g_apPass;
  }
  // AP+STA: l'interfaccia Station resta disponibile per scansioni e tentativi.
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(AP_SSID, pass.c_str())) {
    Serial.println("[WiFi] ERRORE: Access Point non avviato");
    return;
  }
  {
    Lock l;
    g_apActive = true;
    g_apOffAt = 0;
  }
  Serial.printf("[WiFi] Access Point \"%s\" (password \"%s\"), Web App: http://%s\n", AP_SSID,
                pass.c_str(), WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  if (!g_apActive) return;
  WiFi.softAPdisconnect(true);  // spegne solo l'AP: la Station resta collegata
  Lock l;
  g_apActive = false;
  g_apOffAt = 0;
  Serial.println("[WiFi] Access Point spento");
}

void restartAp() {
  if (!g_apActive) return;
  String pass;
  {
    Lock l;
    pass = g_apPass;
  }
  WiFi.softAP(AP_SSID, pass.c_str());  // i telefoni collegati vengono disconnessi
  Serial.println("[WiFi] Access Point riavviato con la nuova password");
}

std::vector<ScanEntry> collectScan(int16_t n) {
  std::vector<ScanEntry> out;
  for (int16_t i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;  // rete nascosta
    const int32_t rssi = WiFi.RSSI(i);
    const bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    auto it = std::find_if(out.begin(), out.end(), [&](const ScanEntry& e) { return e.ssid == ssid; });
    if (it == out.end()) out.push_back({ssid, rssi, secure});
    else if (rssi > it->rssi) *it = {ssid, rssi, secure};  // più ripetitori: il migliore
  }
  WiFi.scanDelete();
  std::sort(out.begin(), out.end(), [](const ScanEntry& a, const ScanEntry& b) { return a.rssi > b.rssi; });
  if (out.size() > kMaxScanResults) out.resize(kMaxScanResults);
  return out;
}

// Reti note in ordine di segnale; con includeUnseen anche quelle non viste
// dalla scansione (potrebbero essere nascoste), in fondo.
std::vector<Candidate> orderCandidates(const std::vector<Cred>& known,
                                       const std::vector<ScanEntry>& seen, bool includeUnseen) {
  std::vector<Candidate> out;
  auto listed = [&](const String& ssid) {
    return std::any_of(out.begin(), out.end(), [&](const Candidate& c) { return c.cred.ssid == ssid; });
  };
  for (const ScanEntry& s : seen)
    for (const Cred& k : known)
      if (k.ssid == s.ssid && !listed(k.ssid)) out.push_back({k, false});
  if (includeUnseen)
    for (const Cred& k : known)
      if (!listed(k.ssid)) out.push_back({k, false});
  return out;
}

void enterAp() {
  t_trying = false;
  t_queue.clear();
  t_connected = false;
  t_lost = false;
  startAp();
  {
    Lock l;
    g_mode = Mode::AccessPoint;
    g_staSsid = "";
    g_apOffAt = 0;  // l'AP resta acceso finché non si trova una rete
  }
  t_nextRetry = millis() + WIFI_AP_RETRY_MS;
  startMdns();
}

void beginAttempt(const Candidate& c) {
  t_cur = c;
  t_trying = true;
  t_start = millis();
  {
    Lock l;
    g_mode = Mode::Connecting;
    g_staSsid = c.cred.ssid;
    if (c.user) g_attempt.state = AttemptState::Connecting;
  }
  Serial.printf("[WiFi] Connessione a \"%s\"...\n", c.cred.ssid.c_str());
  WiFi.setAutoReconnect(true);  // il core ripete il tentativo dopo un errore
  if (!WiFi.isConnected()) WiFi.disconnect(false, false);  // ferma tentativi precedenti
  t_discBase = g_discCount;
  WiFi.begin(c.cred.ssid.c_str(), c.cred.pass.isEmpty() ? nullptr : c.cred.pass.c_str());
}

void attemptSucceeded() {
  t_trying = false;
  t_queue.clear();
  t_station = t_cur.cred;
  t_connected = true;
  t_lost = false;
  const String ip = WiFi.localIP().toString();
  {
    Lock l;
    g_mode = Mode::Station;
    g_staSsid = t_cur.cred.ssid;
    if (t_cur.user) {
      rememberLocked(t_cur.cred);
      g_attempt.state = AttemptState::Ok;
      g_attempt.ip = ip;
    }
  }
  Serial.printf("[WiFi] Connesso a \"%s\", Web App: http://%s  oppure  http://%s.local\n",
                t_cur.cred.ssid.c_str(), ip.c_str(), HOSTNAME);

  if (g_apActive) {
    // Chi ha chiesto la connessione dall'AP deve poter leggere il nuovo indirizzo.
    if (t_cur.user || WiFi.softAPgetStationNum() > 0) {
      Lock l;
      g_apOffAt = millis() + WIFI_AP_LINGER_MS;
      Serial.printf("[WiFi] L'Access Point resta attivo per %u s\n", (unsigned)(WIFI_AP_LINGER_MS / 1000));
    } else {
      stopAp();
    }
  }
  startMdns();
}

void attemptFailed(const char* message) {
  Serial.printf("[WiFi] \"%s\": %s\n", t_cur.cred.ssid.c_str(), message);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  t_trying = false;

  Candidate next;
  const bool hasNext = !t_queue.empty();
  if (hasNext) {
    next = t_queue.front();
    t_queue.erase(t_queue.begin());
  }
  if (t_cur.user) {
    Lock l;
    g_attempt.state = AttemptState::Failed;
    g_attempt.error = message;
    g_attempt.fallback = hasNext ? next.cred.ssid : String();
  }
  if (hasNext) return beginAttempt(next);
  if (g_apActive) Serial.println("[WiFi] Resto in modalità Access Point");
  else Serial.println("[WiFi] Nessuna rete raggiungibile, avvio l'Access Point");
  enterAp();
}

void checkAttempt(uint32_t now) {
  const wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED && WiFi.SSID() == t_cur.cred.ssid) return attemptSucceeded();

  const uint32_t failures = g_discCount - t_discBase;
  if (failures >= 3 || (failures > 0 && st == WL_CONNECT_FAILED))
    return attemptFailed(reasonMessage(g_discReason));
  if (now - t_start >= WIFI_CONNECT_TIMEOUT_MS)
    return attemptFailed(failures ? reasonMessage(g_discReason) : "Nessuna risposta dalla rete");
}

void checkStation(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!t_connected) {
      Serial.printf("[WiFi] Online: %s\n", WiFi.localIP().toString().c_str());
      startMdns();
    }
    t_connected = true;
    t_lost = false;
    return;
  }
  if (!t_lost) {
    Serial.println("[WiFi] Connessione persa, riconnessione...");
    t_lost = true;
    t_lostSince = now;
    t_connected = false;
  }
  if (now - t_lostSince >= WIFI_LOST_FALLBACK_MS) {
    Serial.printf("[WiFi] \"%s\" non raggiungibile da %u s\n", t_station.ssid.c_str(),
                  (unsigned)(WIFI_LOST_FALLBACK_MS / 1000));
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    enterAp();
  }
}

void startScan(bool forAuto) {
  if (t_scanRunning) {
    t_scanAuto = t_scanAuto || forAuto;
    return;
  }
  if (WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED) {
    Lock l;
    g_scanning = g_pending.scan;
    return;
  }
  t_scanRunning = true;
  t_scanAuto = forAuto;
  t_scanStart = millis();
  Lock l;
  g_scanning = true;
}

void checkScan(uint32_t now) {
  if (!t_scanRunning) return;
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING && now - t_scanStart < kScanTimeoutMs) return;

  t_scanRunning = false;
  std::vector<ScanEntry> found;
  if (n >= 0) found = collectScan(n);
  else WiFi.scanDelete();
  std::vector<Cred> known;
  {
    Lock l;
    g_scanning = g_pending.scan;  // un'altra richiesta è già in coda
    if (n >= 0) {
      g_scan = found;
      g_scanAt = now ? now : 1;
    }
    known = knownLocked();
  }

  const bool autoScan = t_scanAuto;
  t_scanAuto = false;
  if (!autoScan || n < 0 || t_trying || g_mode != Mode::AccessPoint) return;
  // Access Point: se una rete nota è tornata visibile ci si ricollega.
  std::vector<Candidate> cands = orderCandidates(known, found, false);
  if (cands.empty()) return;
  t_queue.assign(cands.begin() + 1, cands.end());
  beginAttempt(cands.front());
}

void checkApRetry(uint32_t now) {
  if (t_scanRunning || (int32_t)(now - t_nextRetry) < 0) return;
  t_nextRetry = now + WIFI_AP_RETRY_MS;
  if (WiFi.softAPgetStationNum() > 0) return;  // non disturbare chi usa l'AP
  bool any;
  {
    Lock l;
    any = !knownLocked().empty();
  }
  if (any) startScan(true);
}

// Connessione chiesta dall'app: interrompe un eventuale tentativo automatico.
// Se si era collegati a un'altra rete e il tentativo fallisce, si torna lì.
void userConnect(const Candidate& c) {
  const bool wasStation = !t_trying && t_station.ssid.length() && (t_connected || t_lost);
  if (t_trying) WiFi.disconnect(false, false);
  t_trying = false;
  t_queue.clear();
  if (wasStation && (t_station.ssid != c.cred.ssid || t_station.pass != c.cred.pass))
    t_queue.push_back({t_station, false});
  beginAttempt(c);
}

void processPending(uint32_t now) {
  bool doConnect = false, doAp = false, doScan = false;
  Candidate cand;
  {
    Lock l;
    if ((int32_t)(now - g_pending.notBefore) < 0) return;
    if (g_pending.connect && !t_scanRunning) {
      doConnect = true;
      cand = g_pending.cand;
      g_pending.connect = false;
      g_pending.cand = Candidate();
    }
    doAp = g_pending.applyAp;
    g_pending.applyAp = false;
    if (g_pending.scan && !t_trying && !doConnect) {
      doScan = true;
      g_pending.scan = false;
    }
  }
  if (doAp) restartAp();
  if (doConnect) userConnect(cand);
  if (doScan) startScan(false);
}

// Tasto BOOT tenuto premuto: torna la password predefinita dell'AP.
void checkResetButton(uint32_t now) {
#if RESET_BUTTON_PIN >= 0
  static bool pressed = false;
  static uint32_t since = 0;
  if (digitalRead(RESET_BUTTON_PIN) != LOW) {
    pressed = false;
    return;
  }
  if (!pressed) {
    pressed = true;
    since = now;
    return;
  }
  if (now - since < RESET_BUTTON_HOLD_MS) return;

  Serial.println("[WiFi] Tasto BOOT: ripristino la password predefinita dell'Access Point e riavvio");
  {
    Lock l;
    g_apPass = AP_PASSWORD;
    g_dirty = true;
  }
  persist();
  if (store.isDirty()) {
    ir::requestSave();
    for (int i = 0; i < 40 && store.isDirty(); i++) delay(50);
  }
  delay(200);
  ESP.restart();
#else
  (void)now;
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
//  API pubblica
// ---------------------------------------------------------------------------

void begin() {
  g_mutex = xSemaphoreCreateMutex();
  loadConfig();
#if RESET_BUTTON_PIN >= 0
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
#endif

  WiFi.persistent(false);  // le credenziali le gestiamo noi nella NVS
  WiFi.setHostname(HOSTNAME);
  WiFi.onEvent(onStaDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // latenza minima per la Web App

  std::vector<Cred> known;
  {
    Lock l;
    known = knownLocked();
  }
  if (known.empty()) {
    Serial.println("[WiFi] Nessuna rete configurata");
    enterAp();
    return;
  }

  Serial.printf("[WiFi] Reti note: %u, ricerca in corso...\n", (unsigned)known.size());
  const int16_t n = WiFi.scanNetworks();
  std::vector<ScanEntry> seen;
  if (n > 0) seen = collectScan(n);
  else WiFi.scanDelete();
  {
    Lock l;
    g_scan = seen;
    g_scanAt = millis();
  }
  std::vector<Candidate> cands = orderCandidates(known, seen, true);
  t_queue.assign(cands.begin() + 1, cands.end());
  beginAttempt(cands.front());
}

void loop() {
  const uint32_t now = millis();
  checkResetButton(now);
  processPending(now);
  checkScan(now);

  if (t_trying) checkAttempt(now);
  else if (g_mode == Mode::Station) checkStation(now);
  else if (g_mode == Mode::AccessPoint) checkApRetry(now);

  if (g_apActive && g_apOffAt && g_mode == Mode::Station && (int32_t)(now - g_apOffAt) >= 0) stopAp();
  if (g_dirty && !ir::isLearning()) persist();
}

void statusJson(JsonObject out) {
  Lock l;
  const bool sta = g_mode == Mode::Station;
  out["mode"] = sta ? "sta" : g_mode == Mode::AccessPoint ? "ap" : "connecting";
  if (g_staSsid.length()) out["ssid"] = g_staSsid;
  if (sta) {
    out["ip"] = WiFi.localIP().toString();
    out["rssi"] = WiFi.RSSI();
  }
  out["hostname"] = HOSTNAME;

  JsonObject ap = out["ap"].to<JsonObject>();
  ap["active"] = g_apActive;
  ap["ssid"] = AP_SSID;
  ap["defaultPassword"] = g_apPass == AP_PASSWORD;
  ap["resetButton"] = RESET_BUTTON_PIN >= 0;
  if (g_apActive) {
    ap["ip"] = WiFi.softAPIP().toString();
    ap["clients"] = WiFi.softAPgetStationNum();
    if (g_apOffAt) ap["offIn"] = (int32_t)(g_apOffAt - millis()) > 0 ? (g_apOffAt - millis()) / 1000 : 0;
  }

  JsonArray nets = out["networks"].to<JsonArray>();
  for (const Cred& c : g_saved) {
    JsonObject o = nets.add<JsonObject>();
    o["ssid"] = c.ssid;
    o["open"] = c.pass.isEmpty();
  }
  if (firmwareNetworkLocked()) {
    JsonObject o = nets.add<JsonObject>();
    o["ssid"] = WIFI_SSID;
    o["open"] = strlen(WIFI_PASSWORD) == 0;
    o["firmware"] = true;
  }
  out["maxNetworks"] = WIFI_MAX_NETWORKS;

  if (g_attempt.id) {
    JsonObject a = out["attempt"].to<JsonObject>();
    a["id"] = g_attempt.id;
    a["ssid"] = g_attempt.ssid;
    a["state"] = attemptStateName(g_attempt.state);
    if (g_attempt.error.length()) a["error"] = g_attempt.error;
    if (g_attempt.ip.length()) a["ip"] = g_attempt.ip;
    if (g_attempt.fallback.length()) a["fallback"] = g_attempt.fallback;
  }
}

void infoJson(JsonObject out) {
  Lock l;
  const bool sta = g_mode == Mode::Station;
  out["mode"] = sta ? "sta" : "ap";
  out["ssid"] = sta ? g_staSsid : String(AP_SSID);
  out["ip"] = sta ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  if (sta) out["rssi"] = WiFi.RSSI();
  out["hostname"] = HOSTNAME;
}

void scanJson(JsonObject out, bool refresh) {
  Lock l;
  const uint32_t now = millis();
  const bool stale = !g_scanAt || now - g_scanAt > kScanMaxAgeMs;
  if ((refresh || stale) && !g_scanning) {
    g_pending.scan = true;
    g_scanning = true;
  }
  out["scanning"] = g_scanning;
  if (g_scanAt) out["age"] = (now - g_scanAt) / 1000;
  JsonArray arr = out["networks"].to<JsonArray>();
  const std::vector<Cred> known = knownLocked();
  for (const ScanEntry& e : g_scan) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = e.ssid;
    o["rssi"] = e.rssi;
    o["secure"] = e.secure;
    if (std::any_of(known.begin(), known.end(), [&](const Cred& c) { return c.ssid == e.ssid; }))
      o["saved"] = true;
  }
}

NetResult connect(const String& ssid, const String& password, uint32_t& attemptId) {
  if (!validSsid(ssid)) return NetResult::InvalidSsid;
  if (!validWifiPass(password)) return NetResult::InvalidPassword;
  Lock l;
  if (g_pending.connect || g_attempt.state == AttemptState::Pending ||
      g_attempt.state == AttemptState::Connecting)
    return NetResult::Busy;
  if (findSavedLocked(ssid) < 0 && g_saved.size() >= WIFI_MAX_NETWORKS) return NetResult::Full;

  g_attempt = Attempt();
  g_attempt.id = ++g_attemptCounter;
  g_attempt.state = AttemptState::Pending;
  g_attempt.ssid = ssid;
  g_pending.connect = true;
  g_pending.cand = {{ssid, password}, true};
  g_pending.notBefore = millis() + kCommandDelayMs;
  attemptId = g_attempt.id;
  return NetResult::Ok;
}

NetResult save(const String& ssid, const String& password) {
  if (!validSsid(ssid)) return NetResult::InvalidSsid;
  if (!validWifiPass(password)) return NetResult::InvalidPassword;
  Lock l;
  if (findSavedLocked(ssid) < 0 && g_saved.size() >= WIFI_MAX_NETWORKS) return NetResult::Full;
  rememberLocked({ssid, password});
  return NetResult::Ok;
}

NetResult forget(const String& ssid) {
  Lock l;
  const int i = findSavedLocked(ssid);
  if (i < 0) return ssid == WIFI_SSID && strlen(WIFI_SSID) ? NetResult::ReadOnly : NetResult::NotFound;
  g_saved.erase(g_saved.begin() + i);
  g_dirty = true;
  return NetResult::Ok;
}

NetResult setApPassword(const String& password, bool& apRestart) {
  if (!validApPass(password)) return NetResult::InvalidApPassword;
  Lock l;
  g_apPass = password;
  g_dirty = true;
  apRestart = g_apActive;
  if (g_apActive) {
    g_pending.applyAp = true;
    g_pending.notBefore = millis() + kCommandDelayMs;
  }
  return NetResult::Ok;
}

const char* resultMessage(NetResult r) {
  switch (r) {
    case NetResult::Ok: return "OK";
    case NetResult::InvalidSsid: return "Nome della rete non valido (1-32 caratteri)";
    case NetResult::InvalidPassword: return "La password Wi-Fi deve avere da 8 a 63 caratteri";
    case NetResult::InvalidApPassword:
      return "La password deve avere da 8 a 63 caratteri, senza lettere accentate né emoji";
    case NetResult::NotFound: return "Rete non salvata";
    case NetResult::Busy: return "Connessione già in corso";
    case NetResult::ReadOnly: return "Rete definita nel firmware (config.h): non si può eliminare dall'app";
    case NetResult::Full: return "Hai già salvato il numero massimo di reti: eliminane una";
  }
  return "Errore";
}

}  // namespace net
