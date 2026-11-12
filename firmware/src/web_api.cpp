#include "web_api.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include <memory>
#include <vector>

#include "config.h"
#include "ir_engine.h"
#include "storage.h"
#include "wifi_manager.h"

namespace {

const char kFallbackPage[] PROGMEM = R"html(<!doctype html>
<html lang="it"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Campionatore IR</title>
<body style="font-family:sans-serif;padding:2em;max-width:36em">
<h1>Campionatore IR</h1>
<p>Il firmware è in esecuzione ma la Web App non è presente nel filesystem.</p>
<p>Caricala con <code>pio run -t uploadfs</code> e ricarica la pagina.</p>
<p>API disponibile: <a href="/api/remotes">/api/remotes</a> · <a href="/api/info">/api/info</a></p>
</body></html>)html";

// Parametro dal corpo (form urlencoded) o dalla query string.
String arg(AsyncWebServerRequest* req, const char* name) {
  if (req->hasParam(name, true)) return req->getParam(name, true)->value();
  if (req->hasParam(name)) return req->getParam(name)->value();
  return String();
}

void sendJson(AsyncWebServerRequest* req, int code, const JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  AsyncWebServerResponse* res = req->beginResponse(code, "application/json", body);
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

void sendOk(AsyncWebServerRequest* req, int code = 200) {
  JsonDocument d;
  d["ok"] = true;
  sendJson(req, code, d);
}

void sendError(AsyncWebServerRequest* req, int code, const char* message) {
  JsonDocument d;
  d["ok"] = false;
  d["error"] = message;
  sendJson(req, code, d);
}

int httpCode(DbResult r) {
  switch (r) {
    case DbResult::Ok: return 200;
    case DbResult::NotFound: return 404;
    case DbResult::Exists: return 409;
    case DbResult::Invalid: return 400;
    case DbResult::NoMemory: return 507;
    case DbResult::Busy: return 503;
    case DbResult::IoError: return 500;
  }
  return 500;
}

// Invia un buffer allocato con malloc e lo libera a trasmissione conclusa.
void sendBuffer(AsyncWebServerRequest* req, char* buf, size_t len) {
  std::shared_ptr<char> data(buf, free);
  AsyncWebServerResponse* res = req->beginResponse(
      "application/json", len, [data, len](uint8_t* out, size_t maxLen, size_t index) -> size_t {
        const size_t n = std::min(maxLen, len - index);
        memcpy(out, data.get() + index, n);
        return n;
      });
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

// ---------------------------------------------------------------------------
//  Backup completo in streaming
// ---------------------------------------------------------------------------
//
// {"version":2,"remotes":[…metadati…],"signals":[{"r":…,"b":…,"freq":…,"raw":[…]},…]}
//
// Il JSON viene prodotto un pezzo alla volta mentre la risposta viene inviata:
// in RAM stanno solo i metadati e il segnale corrente, mai il backup intero.
class ExportStream {
 public:
  bool begin() {
    remotes_.reset(store.serializeRemotes(remotesLen_));
    if (!remotes_) return false;
    store.collectSignals(signals_);
    return true;
  }

  size_t read(uint8_t* out, size_t maxLen) {
    size_t written = 0;
    while (written < maxLen) {
      if (pos_ >= len_ && !advance()) break;
      const size_t n = std::min(maxLen - written, len_ - pos_);
      memcpy(out + written, src_ + pos_, n);
      pos_ += n;
      written += n;
    }
    return written;
  }

 private:
  bool advance() {
    switch (phase_) {
      case 0:
        setText("{\"version\":2,\"remotes\":");
        phase_ = 1;
        return true;
      case 1:
        src_ = remotes_.get();
        len_ = remotesLen_;
        pos_ = 0;
        phase_ = 2;
        return true;
      case 2:
        setText(",\"signals\":[");
        phase_ = 3;
        return true;
      case 3:
        while (index_ < signals_.size()) {
          String body;
          if (!store.signalJson(signals_[index_++], body)) continue;  // segnale sparito
          setText(first_ ? body : "," + body);
          first_ = false;
          return true;
        }
        phase_ = 4;
        return advance();
      case 4:
        setText("]}");
        phase_ = 5;
        return true;
      default:
        return false;
    }
  }

  void setText(const String& s) {
    buffer_ = s;
    src_ = buffer_.c_str();
    len_ = buffer_.length();
    pos_ = 0;
  }

  std::unique_ptr<char, void (*)(void*)> remotes_{nullptr, free};
  size_t remotesLen_ = 0;
  std::vector<SignalRef> signals_;
  String buffer_;
  const char* src_ = nullptr;
  size_t len_ = 0, pos_ = 0, index_ = 0;
  int phase_ = 0;
  bool first_ = true;
};

// Stato dell'importazione (una alla volta): il corpo della richiesta viene
// scritto su flash mentre arriva, poi letto in streaming.
struct ImportUpload {
  File file;
  bool ok = false;
  bool active = false;
};
ImportUpload g_import;

size_t freeFsBytes() { return store.fsFree(); }

// ---------------------------------------------------------------------------
//  Gestori
// ---------------------------------------------------------------------------

void handleGetRemotes(AsyncWebServerRequest* req) {
  if (arg(req, "lite") == "1") {
    size_t len = 0;
    char* buf = store.serializeMeta(len);
    if (!buf) return sendError(req, 503, "Memoria insufficiente");
    return sendBuffer(req, buf, len);
  }

  auto stream = std::make_shared<ExportStream>();
  if (!stream->begin()) return sendError(req, 503, "Memoria insufficiente");
  AsyncWebServerResponse* res = req->beginChunkedResponse(
      "application/json", [stream](uint8_t* out, size_t maxLen, size_t) -> size_t {
        return stream->read(out, maxLen);
      });
  res->addHeader("Cache-Control", "no-store");
  res->addHeader("Content-Disposition", "attachment; filename=\"remotes.json\"");
  req->send(res);
}

void handleCreateRemote(AsyncWebServerRequest* req) {
  String name = arg(req, "name");
  const String type = arg(req, "type");
  String id;
  const DbResult r = store.createRemote(name, type, id);
  if (r != DbResult::Ok) return sendError(req, httpCode(r), dbResultMessage(r));
  ir::requestSave();

  JsonDocument d;
  d["ok"] = true;
  JsonObject remote = d["remote"].to<JsonObject>();
  remote["id"] = id;
  name.trim();
  remote["name"] = clipUtf8(name, MAX_NAME_LEN);
  remote["type"] = type;
  remote["buttons"].to<JsonArray>();
  sendJson(req, 201, d);
}

void handleLearnStart(AsyncWebServerRequest* req) {
  const String remote = arg(req, "remote");
  const String button = arg(req, "button");
  const String label = arg(req, "label");
  const String gap = arg(req, "gap");

  if (!store.remoteExists(remote.c_str())) return sendError(req, 404, "Telecomando non trovato");

  uint32_t gapUs = IR_END_GAP_US;
  if (gap.length()) {
    const long ms = gap.toInt();
    if (ms < 5 || ms * 1000 > IR_END_GAP_MAX_US)
      return sendError(req, 400, "Parametro gap non valido");
    gapUs = ms * 1000;
  }

  switch (ir::startLearn(remote.c_str(), button.c_str(), label.c_str(), gapUs)) {
    case IrResult::Ok: break;
    case IrResult::Invalid: return sendError(req, 400, "Identificativo pulsante non valido");
    case IrResult::Busy: return sendError(req, 409, "Apprendimento già in corso");
    default: return sendError(req, 503, "Motore IR occupato");
  }

  JsonDocument d;
  d["ok"] = true;
  d["state"] = "waiting";
  d["timeout"] = IR_LEARN_TIMEOUT_MS;
  sendJson(req, 202, d);
}

void handleLearnStatus(AsyncWebServerRequest* req) {
  const LearnStatus s = ir::status();
  JsonDocument d;
  d["state"] = ir::stateName(s.state);
  d["remote"] = s.remote;
  d["button"] = s.button;
  d["label"] = s.label;
  d["elapsed"] = s.elapsedMs;
  d["timeout"] = s.timeoutMs;
  if (s.state == LearnState::Done) {
    d["len"] = s.count;
    d["freq"] = s.freq;
    d["duration"] = s.durationUs;
    store.buttonToJson(s.remote, s.button, d["signal"].to<JsonObject>());
  }
  if (s.error[0]) d["error"] = s.error;
  sendJson(req, 200, d);
}

void handleLearnCancel(AsyncWebServerRequest* req) {
  ir::cancelLearn();
  sendOk(req);
}

void handleSend(AsyncWebServerRequest* req) {
  const String remote = arg(req, "remote");
  const String button = arg(req, "button");
  switch (ir::queueSend(remote.c_str(), button.c_str())) {
    case IrResult::Ok: return sendOk(req);
    case IrResult::Busy: return sendError(req, 409, "Apprendimento in corso");
    case IrResult::NotFound: return sendError(req, 404, "Pulsante non registrato");
    default: return sendError(req, 503, "Trasmettitore occupato, riprova");
  }
}

void handleDelete(AsyncWebServerRequest* req) {
  const String remote = arg(req, "remote");
  const String button = arg(req, "button");
  if (remote.isEmpty()) return sendError(req, 400, "Parametro remote mancante");
  const DbResult r =
      button.isEmpty() ? store.deleteRemote(remote) : store.deleteButton(remote, button);
  if (r != DbResult::Ok) return sendError(req, httpCode(r), dbResultMessage(r));
  ir::requestSave();
  sendOk(req);
}

void handleImportBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index,
                      size_t total) {
  if (index == 0) {
    if (g_import.file) g_import.file.close();
    LittleFS.remove(IMPORT_TMP_PATH);
    g_import.active = true;
    g_import.file = LittleFS.open(IMPORT_TMP_PATH, "w");
    g_import.ok = (bool)g_import.file && total < freeFsBytes();
    if (!g_import.ok && g_import.file) g_import.file.close();
  }
  if (g_import.ok && len) g_import.ok = g_import.file.write(data, len) == len;
  if (g_import.file && index + len >= total) g_import.file.close();
}

void handleImport(AsyncWebServerRequest* req) {
  if (!g_import.active) return sendError(req, 400, "Nessun dato ricevuto");
  g_import.active = false;
  if (g_import.file) g_import.file.close();
  if (!g_import.ok) {
    LittleFS.remove(IMPORT_TMP_PATH);
    return sendError(req, 507, "Spazio insufficiente sulla flash");
  }
  if (ir::isLearning()) return sendError(req, 409, "Apprendimento in corso");

  size_t remotes = 0;
  const DbResult r = store.importFile(IMPORT_TMP_PATH, remotes);
  LittleFS.remove(IMPORT_TMP_PATH);
  if (r != DbResult::Ok) {
    return sendError(req, httpCode(r),
                     r == DbResult::Invalid ? "File di backup non valido" : dbResultMessage(r));
  }

  JsonDocument d;
  d["ok"] = true;
  d["remotes"] = remotes;
  d["buttons"] = store.buttonCount();
  sendJson(req, 200, d);
}

// Vecchio database v1: resta sulla flash dopo la migrazione finché non lo si
// elimina, così si può scaricare come copia di sicurezza.
void handleLegacyGet(AsyncWebServerRequest* req) {
  if (!store.hasLegacy() || !LittleFS.exists(LEGACY_DB_PATH))
    return sendError(req, 404, "Nessun database v1 presente");
  AsyncWebServerResponse* res = req->beginResponse(LittleFS, LEGACY_DB_PATH, "application/json");
  res->addHeader("Cache-Control", "no-store");
  res->addHeader("Content-Disposition", "attachment; filename=\"remotes-v1.json\"");
  req->send(res);
}

void handleLegacyDelete(AsyncWebServerRequest* req) {
  if (!store.removeLegacy()) return sendError(req, 500, "Impossibile eliminare il file");
  sendOk(req);
}

void handleInfo(AsyncWebServerRequest* req) {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  o["version"] = FW_VERSION;
  net::infoJson(o);  // mode, ssid, ip, rssi, hostname
  o["heap"] = ESP.getFreeHeap();
  o["psram"] = ESP.getFreePsram();
  o["fsUsed"] = store.fsUsed();
  o["fsTotal"] = store.fsTotal();
  o["buttons"] = store.buttonCount();
  o["legacy"] = store.hasLegacy();
  o["uptime"] = millis() / 1000;
  sendJson(req, 200, d);
}

// ---------------------------------------------------------------------------
//  Wi-Fi
// ---------------------------------------------------------------------------

int httpCode(NetResult r) {
  switch (r) {
    case NetResult::Ok: return 200;
    case NetResult::NotFound: return 404;
    case NetResult::Busy:
    case NetResult::Full: return 409;
    case NetResult::ReadOnly: return 403;
    default: return 400;
  }
}

void handleWifiStatus(AsyncWebServerRequest* req) {
  JsonDocument d;
  net::statusJson(d.to<JsonObject>());
  sendJson(req, 200, d);
}

void handleWifiScan(AsyncWebServerRequest* req) {
  JsonDocument d;
  net::scanJson(d.to<JsonObject>(), arg(req, "refresh") == "1");
  sendJson(req, 200, d);
}

void handleWifiConnect(AsyncWebServerRequest* req) {
  uint32_t attempt = 0;
  const NetResult r = net::connect(arg(req, "ssid"), arg(req, "password"), attempt);
  if (r != NetResult::Ok) return sendError(req, httpCode(r), net::resultMessage(r));
  JsonDocument d;
  d["ok"] = true;
  d["attempt"] = attempt;
  d["timeout"] = WIFI_CONNECT_TIMEOUT_MS;
  sendJson(req, 202, d);
}

void handleWifiSave(AsyncWebServerRequest* req) {
  const NetResult r = net::save(arg(req, "ssid"), arg(req, "password"));
  if (r != NetResult::Ok) return sendError(req, httpCode(r), net::resultMessage(r));
  sendOk(req, 201);
}

void handleWifiForget(AsyncWebServerRequest* req) {
  const NetResult r = net::forget(arg(req, "ssid"));
  if (r != NetResult::Ok) return sendError(req, httpCode(r), net::resultMessage(r));
  sendOk(req);
}

void handleWifiAp(AsyncWebServerRequest* req) {
  bool restart = false;
  const NetResult r = net::setApPassword(arg(req, "password"), restart);
  if (r != NetResult::Ok) return sendError(req, httpCode(r), net::resultMessage(r));
  JsonDocument d;
  d["ok"] = true;
  d["restart"] = restart;
  sendJson(req, 200, d);
}

// Tipi MIME dei file della Web App (stessi valori usati da serveStatic).
const char* contentTypeFor(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".js")) return "text/javascript";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".svg")) return "image/svg+xml";
  return "application/octet-stream";
}

// serveStatic risponde solo a GET: a HEAD vanno le stesse intestazioni
// (tipo, lunghezza, cache) senza corpo. Prima "HEAD /" riceveva la pagina
// di ripiego "Web App non presente".
void handleStaticHead(AsyncWebServerRequest* req) {
  const String& url = req->url();
  if (url.indexOf("..") >= 0) return req->send(404);
  String path = String(WEB_ROOT) + url.substring(1);
  if (path.endsWith("/")) path += "index.html";
  File f = LittleFS.exists(path) ? LittleFS.open(path, "r") : File();
  if (!f || f.isDirectory()) return req->send(404);
  AsyncWebServerResponse* res = req->beginResponse(200, contentTypeFor(path), "");
  res->setContentLength(f.size());
  res->addHeader("Cache-Control", "no-cache");
  f.close();
  req->send(res);
}

}  // namespace

void webApiBegin(AsyncWebServer& server) {
  server.on("/api/remotes", HTTP_GET, handleGetRemotes);
  server.on("/api/remotes", HTTP_POST, handleCreateRemote);
  server.on("/api/learn", HTTP_POST, handleLearnStart);
  server.on("/api/learn", HTTP_GET, handleLearnStatus);
  server.on("/api/learn", HTTP_DELETE, handleLearnCancel);
  server.on("/api/send", HTTP_POST, handleSend);
  server.on("/api/delete", HTTP_DELETE, handleDelete);
  server.on("/api/import", HTTP_POST, handleImport, nullptr, handleImportBody);
  server.on("/api/legacy", HTTP_GET, handleLegacyGet);
  server.on("/api/legacy", HTTP_DELETE, handleLegacyDelete);
  server.on("/api/info", HTTP_GET, handleInfo);
  // "/api/wifi" corrisponde anche a "/api/wifi/…": i percorsi più specifici
  // vanno registrati prima.
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi/connect", HTTP_POST, handleWifiConnect);
  server.on("/api/wifi/networks", HTTP_POST, handleWifiSave);
  server.on("/api/wifi/networks", HTTP_DELETE, handleWifiForget);
  server.on("/api/wifi/ap", HTTP_POST, handleWifiAp);
  server.on("/api/wifi", HTTP_GET, handleWifiStatus);

  // Web App: il browser rivalida ogni file tramite ETag (risposta 304 se
  // invariato), quindi il codice viene scaricato una sola volta.
  server.serveStatic("/", LittleFS, WEB_ROOT).setDefaultFile("index.html").setCacheControl("no-cache");

  server.onNotFound([](AsyncWebServerRequest* req) {
    if (req->url().startsWith("/api/")) return sendError(req, 404, "Endpoint non trovato");
    if (req->method() == AsyncWebRequestMethod::HTTP_HEAD) return handleStaticHead(req);
    if (req->url() == "/" && !LittleFS.exists(WEB_ROOT "index.html"))
      return req->send(200, "text/html", kFallbackPage);
    req->send(404, "text/plain", "Not found");
  });
}
