#pragma once

#include <Arduino.h>

#include "config.h"

// Motore IR: un task dedicato (core 1, alta priorità) esegue in sequenza
// campionamenti, trasmissioni e salvataggi del database. Le funzioni pubbliche
// sono non bloccanti e possono essere chiamate dai gestori HTTP.

enum class LearnState : uint8_t { Idle, Waiting, Capturing, Done, Timeout, Cancelled, Error };

struct LearnStatus {
  LearnState state = LearnState::Idle;
  char remote[MAX_ID_LEN + 1] = "";
  char button[MAX_ID_LEN + 1] = "";
  char label[MAX_LABEL_LEN + 1] = "";
  uint32_t elapsedMs = 0;
  uint32_t timeoutMs = 0;
  uint16_t count = 0;       // durate acquisite
  uint32_t freq = 0;        // portante misurata (Hz)
  uint32_t durationUs = 0;  // durata totale del segnale
  char error[64] = "";
};

enum class IrResult : uint8_t { Ok, Busy, QueueFull, NotFound, Invalid };

namespace ir {

bool begin();

// Avvia l'apprendimento di un pulsante. gapUs = silenzio che chiude il pacchetto.
IrResult startLearn(const char* remoteId, const char* buttonId, const char* label, uint32_t gapUs);
void cancelLearn();
bool isLearning();
LearnStatus status();
const char* stateName(LearnState s);

// Accoda la trasmissione di un pulsante memorizzato.
IrResult queueSend(const char* remoteId, const char* buttonId);

// Chiede al task IR di salvare subito il database su flash.
void requestSave();

}  // namespace ir
