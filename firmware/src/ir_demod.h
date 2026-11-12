#pragma once

// Demodulatore software del segnale IR grezzo. Codice C++ puro, senza
// dipendenze hardware: viene alimentato campione per campione (istante in µs,
// stato del ricevitore) e ricostruisce la sequenza mark/space.
//
// Il fotodiodo "nudo" restituisce la portante non demodulata: durante un mark
// il pin oscilla alla frequenza della portante (es. 38 kHz). Gli impulsi attivi
// separati da meno di carrierGapUs vengono uniti nello stesso mark; i periodi
// tra fronti consecutivi servono a stimare la frequenza della portante.
// Funziona anche con un ricevitore demodulato (TSOP): in quel caso ogni mark è
// un unico impulso e la frequenza non è misurabile.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct IrDemodConfig {
  uint32_t carrierGapUs;  // silenzio massimo tra due cicli di portante
  uint32_t endGapUs;      // silenzio che chiude il pacchetto
  uint32_t minMarkUs;     // mark più corti sono disturbi
  uint32_t maxCaptureUs;  // durata massima della cattura
  uint32_t periodMinUs;   // periodi di portante accettati per la stima
  uint32_t periodMaxUs;
};

class IrDemodulator {
 public:
  IrDemodulator(uint32_t* buf, size_t capacity, const IrDemodConfig& cfg)
      : buf_(buf), cap_(capacity), cfg_(cfg) {}

  // t0 = istante del primo fronte attivo (catturato dall'interrupt),
  // now = inizio del campionamento. Il mark si considera attivo fino a "now":
  // la latenza di risveglio del task non deve troncare il primo impulso.
  void begin(uint32_t t0, uint32_t now, bool activeNow) {
    n_ = 0;
    t0_ = markStart_ = markEnd_ = lastFall_ = t0;
    lastActive_ = now;
    memset(hist_, 0, sizeof(hist_));
    inMark_ = true;
    prev_ = activeNow;
    falseTrigger_ = false;
  }

  // Elabora un campione. Restituisce false quando la cattura è terminata.
  inline bool feed(uint32_t now, bool active) {
    if (active) {
      if (!prev_) {  // fronte attivo = nuovo ciclo di portante
        const uint32_t period = now - lastFall_;
        if (inMark_ && period >= cfg_.periodMinUs && period <= cfg_.periodMaxUs &&
            period >= kHistBase && period < kHistBase + kHistSize) {
          hist_[period - kHistBase]++;
        }
        lastFall_ = now;
      }
      if (!inMark_) {  // inizia un nuovo mark: chiude lo space precedente
        if (n_ + 2 > cap_) return false;
        buf_[n_++] = now - markEnd_;
        markStart_ = now;
        inMark_ = true;
      }
      lastActive_ = now;
    } else if (inMark_) {
      if (now - lastActive_ > cfg_.carrierGapUs) {  // fine del mark
        const uint32_t mark = lastActive_ - markStart_;
        if (mark >= cfg_.minMarkUs) {
          buf_[n_++] = mark;
          markEnd_ = lastActive_;
        } else if (n_ == 0) {
          falseTrigger_ = true;  // disturbo isolato
          return false;
        } else {
          n_--;  // disturbo: scarta il mark e lo space che lo precedeva
        }
        inMark_ = false;
      }
    } else if (now - markEnd_ > cfg_.endGapUs) {
      return false;  // silenzio: fine del pacchetto
    }
    prev_ = active;
    return now - t0_ <= cfg_.maxCaptureUs;
  }

  // Chiude la cattura; restituisce il numero di durate (sempre dispari:
  // mark, space, mark, ..., mark).
  size_t finish() {
    if (falseTrigger_) return n_ = 0;
    if (inMark_) {  // interrotta durante un mark (limite di tempo o di memoria)
      const uint32_t mark = lastActive_ - markStart_;
      if (mark >= cfg_.minMarkUs && n_ < cap_) buf_[n_++] = mark;
      else if (n_ > 0) n_--;
      inMark_ = false;
    }
    if (n_ % 2 == 0 && n_ > 0) n_--;
    return n_;
  }

  // Frequenza della portante in Hz (arrotondata a 100 Hz), 0 se non misurabile.
  //
  // Si usa la moda dell'istogramma dei periodi, non la media: su un fotodiodo
  // un impulso di portante può produrre due fronti vicini, e pochi intervalli
  // spuri falsavano la media (es. 38 kHz misurati come 48 kHz).
  uint32_t carrierHz() const {
    uint32_t total = 0, peak = 0;
    size_t best = 0;
    for (size_t i = 0; i < kHistSize; i++) {
      total += hist_[i];
      if (hist_[i] > peak) {
        peak = hist_[i];
        best = i;
      }
    }
    if (total < 16 || peak == 0) return 0;

    // Media pesata sul picco e sui due vicini: risoluzione sotto il microsecondo.
    uint64_t sum = 0;
    uint32_t n = 0;
    for (size_t i = (best ? best - 1 : 0); i <= best + 1 && i < kHistSize; i++) {
      sum += (uint64_t)(kHistBase + i) * hist_[i];
      n += hist_[i];
    }
    // La misura si accetta solo se è netta: il picco deve raccogliere almeno il
    // 60% degli intervalli e non devono esistere altri gruppi importanti. Un
    // impulso di portante che genera due fronti crea un secondo gruppo a periodo
    // più breve, che farebbe sovrastimare la frequenza: meglio dichiararla non
    // misurabile e lasciare il valore di default.
    if (n * 5 < total * 3) return 0;
    for (size_t c = 0; c < kHistSize; c++) {
      if (c + 3 > best && best + 3 > c) continue;  // il gruppo del picco
      uint32_t other = hist_[c];
      if (c) other += hist_[c - 1];
      if (c + 1 < kHistSize) other += hist_[c + 1];
      if (other * 4 >= total) return 0;  // secondo gruppo importante: ambigua
    }
    const uint32_t hz = (uint32_t)((1000000ULL * n + sum / 2) / sum);
    return (hz + 50) / 100 * 100;
  }

  size_t count() const { return n_; }

 private:
  uint32_t* buf_;
  size_t cap_;
  IrDemodConfig cfg_;
  size_t n_ = 0;
  uint32_t t0_ = 0, markStart_ = 0, lastActive_ = 0, markEnd_ = 0, lastFall_ = 0;
  static const uint32_t kHistBase = 12;  // periodi da 12 a 43 µs (83…23 kHz)
  static const size_t kHistSize = 32;
  uint16_t hist_[kHistSize] = {};
  bool inMark_ = false, prev_ = false, falseTrigger_ = false;
};
