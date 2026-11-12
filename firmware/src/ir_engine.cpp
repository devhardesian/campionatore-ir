#include "ir_engine.h"

#include <driver/gpio.h>
#include <esp_arduino_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "ir_demod.h"
#include "storage.h"

#if ESP_ARDUINO_VERSION_MAJOR >= 3
#include <esp32-hal-rmt.h>
using RmtSymbol = rmt_data_t;
#else
#include <driver/rmt.h>
using RmtSymbol = rmt_item32_t;
#define IR_RMT_CHANNEL RMT_CHANNEL_0
#endif

namespace ir {
namespace {

constexpr UBaseType_t kTaskPriority = configMAX_PRIORITIES - 5;
constexpr uint32_t kTaskStack = 8192;
constexpr uint32_t kRmtMaxDuration = 32767;  // 15 bit per semi-simbolo RMT
// Periodi di portante accettati per la stima della frequenza (µs).
constexpr uint32_t kCarrierPeriodMin = 1000000 / IR_CARRIER_MAX_HZ - 3;
constexpr uint32_t kCarrierPeriodMax = 1000000 / IR_CARRIER_MIN_HZ + 3;

struct Command {
  enum Type : uint8_t { Learn, Send, Save } type;
  char remote[MAX_ID_LEN + 1];
  char button[MAX_ID_LEN + 1];
  char label[MAX_LABEL_LEN + 1];
  uint32_t gapUs;
};

struct Capture {
  size_t count = 0;
  uint32_t freq = IR_DEFAULT_CARRIER_HZ;
  uint32_t durationUs = 0;
};

TaskHandle_t s_task = nullptr;
QueueHandle_t s_queue = nullptr;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
LearnStatus s_status;
uint32_t s_learnStartMs = 0;
volatile bool s_cancel = false;

volatile bool s_edgeSeen = false;
volatile uint32_t s_edgeUs = 0;

// Buffer condiviso da campionamento e trasmissione (usati solo dal task IR).
uint32_t s_buf[IR_MAX_ENTRIES];
RmtSymbol s_symbols[IR_MAX_ENTRIES / 2 + 64];
uint32_t s_txFreq = 0;

// Ultimo segnale letto dalla flash: rileggerlo costa decine di millisecondi,
// e i tasti tenuti premuti ripetono sempre lo stesso.
struct SignalCache {
  char remote[MAX_ID_LEN + 1] = "";
  char button[MAX_ID_LEN + 1] = "";
  uint32_t revision = 0;
  size_t count = 0;
  uint32_t freq = 0;
  bool matches(const char* r, const char* b) const {
    return count && revision == store.revision() && !strcmp(remote, r) && !strcmp(button, b);
  }
};
SignalCache s_cache;

void copyStr(char* dst, const char* src, size_t size) {
  strncpy(dst, src ? src : "", size - 1);
  dst[size - 1] = '\0';
}

void setState(LearnState st, const char* error = nullptr) {
  portENTER_CRITICAL(&s_mux);
  s_status.state = st;
  if (error) copyStr(s_status.error, error, sizeof(s_status.error));
  portEXIT_CRITICAL(&s_mux);
}

// ---------------------------------------------------------------------------
//  Ricezione
// ---------------------------------------------------------------------------

// Primo fronte di discesa: annota l'istante e sveglia il task IR.
void ARDUINO_ISR_ATTR onRxEdge() {
  if (s_edgeSeen) return;
  s_edgeUs = micros();
  s_edgeSeen = true;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(s_task, &woken);
  if (woken) portYIELD_FROM_ISR();
}

inline bool rxActive() {  // il fotodiodo illuminato porta il pin a LOW
  return gpio_get_level((gpio_num_t)IR_RX_PIN) == 0;
}

// Campiona il pin a livello di microsecondi con micros(): il demodulatore
// misura la durata di ogni alternanza e ricompone i mark della portante.
Capture capture(uint32_t t0, uint32_t endGapUs) {
  const IrDemodConfig cfg{IR_CARRIER_GAP_US, endGapUs,         IR_MIN_MARK_US,
                          IR_MAX_CAPTURE_US, kCarrierPeriodMin, kCarrierPeriodMax};
  IrDemodulator demod(s_buf, IR_MAX_ENTRIES, cfg);
  demod.begin(t0, micros(), rxActive());
  while (demod.feed(micros(), rxActive())) {
  }

  Capture res;
  res.count = demod.finish();
  for (size_t i = 0; i < res.count; i++) res.durationUs += s_buf[i];
  const uint32_t hz = demod.carrierHz();
  if (hz >= IR_CARRIER_MIN_HZ && hz <= IR_CARRIER_MAX_HZ) res.freq = hz;
  return res;
}

uint32_t armRx() {
  s_edgeSeen = false;
  ulTaskNotifyTake(pdTRUE, 0);  // scarta notifiche vecchie
  attachInterrupt(IR_RX_PIN, onRxEdge, FALLING);
  return micros();
}

void printSignal(const Capture& c) {
  Serial.printf("[IR] Acquisiti %u valori, portante %.1f kHz, durata %.1f ms\n[IR] raw: ",
                (unsigned)c.count, c.freq / 1000.0f, c.durationUs / 1000.0f);
  for (size_t i = 0; i < c.count; i++) Serial.printf(i ? ",%lu" : "%lu", (unsigned long)s_buf[i]);
  Serial.println();
}

void doLearn(const Command& cmd) {
  Serial.printf("[IR] Apprendimento %s/%s: in attesa del segnale...\n", cmd.remote, cmd.button);
  bool armed = false;
  uint32_t armUs = 0;

  for (;;) {
    if (s_cancel) {
      setState(LearnState::Cancelled);
      break;
    }
    if (millis() - s_learnStartMs >= IR_LEARN_TIMEOUT_MS) {
      setState(LearnState::Timeout, "Nessun segnale ricevuto");
      break;
    }
    if (!armed) {
      armUs = armRx();
      armed = true;
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) == 0) continue;

    gpio_intr_disable((gpio_num_t)IR_RX_PIN);  // durante la cattura si campiona a polling
    setState(LearnState::Capturing);
    // Se il primo fronte arriva prima che la linea sia stata libera per un
    // "gap" intero, ci siamo agganciati a un pacchetto già iniziato (tasto
    // tenuto premuto): la cattura serve solo a consumarlo, poi si riascolta.
    const bool midPacket = (s_edgeUs - armUs) < cmd.gapUs;
    Capture c = capture(s_edgeUs, cmd.gapUs);
    detachInterrupt(IR_RX_PIN);
    armed = false;

    if (midPacket || c.count < IR_MIN_ENTRIES) {  // pacchetto parziale o rumore
      if (midPacket) Serial.println("[IR] Pacchetto incompleto scartato, riascolto...");
      setState(LearnState::Waiting);
      continue;
    }

    printSignal(c);
    s_cache.count = 0;  // s_buf ora contiene il segnale appena catturato
    DbResult r = store.setButton(cmd.remote, cmd.button, cmd.label, s_buf, c.count, c.freq);
    if (r != DbResult::Ok) {
      setState(LearnState::Error, dbResultMessage(r));
      break;
    }
    store.save();  // salvataggio immediato su flash

    portENTER_CRITICAL(&s_mux);
    s_status.count = c.count;
    s_status.freq = c.freq;
    s_status.durationUs = c.durationUs;
    s_status.state = LearnState::Done;
    portEXIT_CRITICAL(&s_mux);
    break;
  }

  if (armed) detachInterrupt(IR_RX_PIN);
}

// ---------------------------------------------------------------------------
//  Trasmissione: RMT con portante hardware
// ---------------------------------------------------------------------------

// Impacchetta coppie (livello, durata) nei simboli RMT, spezzando le durate
// più lunghe di 15 bit.
class SymbolPacker {
 public:
  bool push(uint32_t level, uint32_t duration) {
    while (duration > 0) {
      const uint32_t d = duration > kRmtMaxDuration ? kRmtMaxDuration : duration;
      duration -= d;
      if (!half_) {
        if (count_ >= sizeof(s_symbols) / sizeof(s_symbols[0])) return false;
        s_symbols[count_].val = 0;
        s_symbols[count_].level0 = level;
        s_symbols[count_].duration0 = d;
        half_ = true;
      } else {
        s_symbols[count_].level1 = level;
        s_symbols[count_].duration1 = d;
        count_++;
        half_ = false;
      }
    }
    return true;
  }
  size_t finish() {
    if (half_) {  // completa l'ultimo simbolo con un breve LOW
      s_symbols[count_].level1 = 0;
      s_symbols[count_].duration1 = 10;
      count_++;
      half_ = false;
    }
    return count_;
  }

 private:
  size_t count_ = 0;
  bool half_ = false;
};

#if ESP_ARDUINO_VERSION_MAJOR >= 3

bool txSetCarrier(uint32_t hz) {
  if (hz == s_txFreq) return true;
  if (!rmtSetCarrier(IR_TX_PIN, true, false /* portante sul livello alto */, hz, IR_CARRIER_DUTY))
    return false;
  s_txFreq = hz;
  return true;
}

bool txBegin() {
  // Risoluzione 1 MHz: 1 tick = 1 µs
  if (!rmtInit(IR_TX_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 1000000)) return false;
  rmtSetEOT(IR_TX_PIN, 0);  // LED spento a riposo
  return txSetCarrier(IR_DEFAULT_CARRIER_HZ);
}

bool txWrite(size_t count, uint32_t durationUs) {
  return rmtWrite(IR_TX_PIN, s_symbols, count, durationUs / 1000 + 200);
}

#else  // Arduino-ESP32 2.x: driver RMT "legacy" di ESP-IDF 4.4

bool txSetCarrier(uint32_t hz) {
  if (hz == s_txFreq) return true;
  rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)IR_TX_PIN, IR_RMT_CHANNEL);
  cfg.clk_div = 80;  // APB 80 MHz / 80 = 1 µs per tick
  cfg.tx_config.carrier_en = true;
  cfg.tx_config.carrier_freq_hz = hz;
  cfg.tx_config.carrier_duty_percent = (uint8_t)(IR_CARRIER_DUTY * 100);
  cfg.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;
  cfg.tx_config.idle_output_en = true;
  cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  if (s_txFreq) rmt_driver_uninstall(IR_RMT_CHANNEL);
  s_txFreq = 0;
  if (rmt_config(&cfg) != ESP_OK || rmt_driver_install(IR_RMT_CHANNEL, 0, 0) != ESP_OK)
    return false;
  s_txFreq = hz;
  return true;
}

bool txBegin() { return txSetCarrier(IR_DEFAULT_CARRIER_HZ); }

bool txWrite(size_t count, uint32_t) {
  return rmt_write_items(IR_RMT_CHANNEL, s_symbols, count, true) == ESP_OK;
}

#endif

bool transmit(const uint32_t* raw, size_t count, uint32_t freq) {
  SymbolPacker packer;
  uint32_t total = 0;
  for (size_t i = 0; i < count; i++) {
    if (!packer.push(i % 2 == 0 ? 1 : 0, raw[i])) return false;  // pari = mark, dispari = space
    total += raw[i];
  }
  const size_t symbols = packer.finish();
  if (!txSetCarrier(freq) && !txSetCarrier(IR_DEFAULT_CARRIER_HZ)) return false;
  return txWrite(symbols, total);
}

void doSend(const Command& cmd) {
  size_t count = 0;
  uint32_t freq = IR_DEFAULT_CARRIER_HZ;
  const uint32_t t0 = micros();
  if (s_cache.matches(cmd.remote, cmd.button)) {
    count = s_cache.count;
    freq = s_cache.freq;
  } else {
    const DbResult r = store.getButton(cmd.remote, cmd.button, s_buf, IR_MAX_ENTRIES, count, freq);
    if (r != DbResult::Ok) {
      s_cache.count = 0;
      Serial.printf("[IR] Invio %s/%s: %s\n", cmd.remote, cmd.button, dbResultMessage(r));
      return;
    }
    copyStr(s_cache.remote, cmd.remote, sizeof(s_cache.remote));
    copyStr(s_cache.button, cmd.button, sizeof(s_cache.button));
    s_cache.revision = store.revision();
    s_cache.count = count;
    s_cache.freq = freq;
  }
  const uint32_t readUs = micros() - t0;
  const bool ok = transmit(s_buf, count, freq);
  Serial.printf("[IR] Invio %s/%s: %u valori @ %.1f kHz, letti in %.1f ms, %s\n", cmd.remote,
                cmd.button, (unsigned)count, freq / 1000.0f, readUs / 1000.0f,
                ok ? "OK" : "ERRORE");
  vTaskDelay(pdMS_TO_TICKS(IR_TX_GAP_MS));
}

// ---------------------------------------------------------------------------
//  Task IR
// ---------------------------------------------------------------------------

void workerTask(void*) {
  Command cmd;
  uint32_t lastFsStats = 0;
  for (;;) {
    if (xQueueReceive(s_queue, &cmd, pdMS_TO_TICKS(250)) == pdTRUE) {
      switch (cmd.type) {
        case Command::Learn: doLearn(cmd); break;
        case Command::Send: doSend(cmd); break;
        case Command::Save: break;
      }
    }
    if (store.isDirty()) {
      store.save();
      lastFsStats = 0;  // forza il ricalcolo dello spazio occupato
    }
    // Calcolo lento (percorre il filesystem): qui non blocca le richieste HTTP.
    if (!lastFsStats || millis() - lastFsStats > 60000) {
      store.refreshFsStats();
      lastFsStats = millis() | 1;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
//  API pubblica
// ---------------------------------------------------------------------------

bool begin() {
  pinMode(IR_RX_PIN, INPUT);  // pull-up esterno da 10 kΩ
  pinMode(IR_TX_PIN, OUTPUT);
  digitalWrite(IR_TX_PIN, LOW);

  const bool txOk = txBegin();
  if (!txOk) Serial.println("[IR] ERRORE: inizializzazione RMT fallita");

  s_queue = xQueueCreate(IR_SEND_QUEUE_LEN + 2, sizeof(Command));
  xTaskCreatePinnedToCore(workerTask, "ir", kTaskStack, nullptr, kTaskPriority, &s_task, 1);
  return txOk && s_queue && s_task;
}

bool isLearning() {
  portENTER_CRITICAL(&s_mux);
  const LearnState st = s_status.state;
  portEXIT_CRITICAL(&s_mux);
  return st == LearnState::Waiting || st == LearnState::Capturing;
}

IrResult startLearn(const char* remoteId, const char* buttonId, const char* label, uint32_t gapUs) {
  if (!isValidId(remoteId) || !isValidId(buttonId)) return IrResult::Invalid;

  Command cmd{};
  cmd.type = Command::Learn;
  copyStr(cmd.remote, remoteId, sizeof(cmd.remote));
  copyStr(cmd.button, buttonId, sizeof(cmd.button));
  copyStr(cmd.label, clipUtf8(label, MAX_LABEL_LEN).c_str(), sizeof(cmd.label));
  cmd.gapUs = gapUs;

  bool busy;
  portENTER_CRITICAL(&s_mux);
  busy = s_status.state == LearnState::Waiting || s_status.state == LearnState::Capturing;
  if (!busy) {
    s_status = LearnStatus();
    s_status.state = LearnState::Waiting;
    s_status.timeoutMs = IR_LEARN_TIMEOUT_MS;
    memcpy(s_status.remote, cmd.remote, sizeof(cmd.remote));
    memcpy(s_status.button, cmd.button, sizeof(cmd.button));
    memcpy(s_status.label, cmd.label, sizeof(cmd.label));
    s_learnStartMs = millis();
    s_cancel = false;
  }
  portEXIT_CRITICAL(&s_mux);
  if (busy) return IrResult::Busy;

  if (xQueueSend(s_queue, &cmd, 0) != pdTRUE) {
    setState(LearnState::Error, "Coda comandi piena");
    return IrResult::QueueFull;
  }
  return IrResult::Ok;
}

void cancelLearn() { s_cancel = true; }

LearnStatus status() {
  portENTER_CRITICAL(&s_mux);
  LearnStatus s = s_status;
  const uint32_t start = s_learnStartMs;
  portEXIT_CRITICAL(&s_mux);
  s.elapsedMs = start ? millis() - start : 0;
  return s;
}

const char* stateName(LearnState s) {
  switch (s) {
    case LearnState::Idle: return "idle";
    case LearnState::Waiting: return "waiting";
    case LearnState::Capturing: return "capturing";
    case LearnState::Done: return "done";
    case LearnState::Timeout: return "timeout";
    case LearnState::Cancelled: return "cancelled";
    case LearnState::Error: return "error";
  }
  return "unknown";
}

IrResult queueSend(const char* remoteId, const char* buttonId) {
  if (isLearning()) return IrResult::Busy;
  if (!store.buttonExists(remoteId, buttonId)) return IrResult::NotFound;
  Command cmd{};
  cmd.type = Command::Send;
  copyStr(cmd.remote, remoteId, sizeof(cmd.remote));
  copyStr(cmd.button, buttonId, sizeof(cmd.button));
  return xQueueSend(s_queue, &cmd, 0) == pdTRUE ? IrResult::Ok : IrResult::QueueFull;
}

void requestSave() {
  Command cmd{};
  cmd.type = Command::Save;
  xQueueSend(s_queue, &cmd, 0);  // se la coda è piena il salvataggio avviene comunque a breve
}

}  // namespace ir
