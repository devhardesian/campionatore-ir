#pragma once

// Scanner JSON in streaming, a RAM costante: legge un byte alla volta e
// segnala i campi scalari e gli array "raw" degli impulsi, senza mai costruire
// il documento in memoria.
//
// Serve per i file troppo grandi per la RAM: il backup completo e il vecchio
// /remotes.json (formato v1), che conteneva gli impulsi di tutti i telecomandi.
//
// La profondità è il numero di parentesi aperte (l'oggetto radice è 1):
//   formato v1: remotes[]=2, telecomando=3, buttons[]=4, pulsante=5, raw[]=6
//   formato v2: signals[]=2, segnale=3, raw[]=4
//
// Codice C++ puro, senza dipendenze hardware: testabile sul PC.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct JsonScanSink {
  virtual ~JsonScanSink() = default;
  // Campo scalare (le stringhe arrivano senza virgolette).
  virtual void onField(int depth, const char* key, const char* value) = 0;
  // Array "raw": onRawBegin, poi una onRawValue per durata, poi onRawEnd.
  // Se onRawBegin restituisce false l'array viene saltato.
  virtual bool onRawBegin(int depth) { (void)depth; return false; }
  virtual void onRawValue(uint32_t us) { (void)us; }
  virtual void onRawEnd() {}
  virtual void onObjectEnd(int depth) { (void)depth; }
};

// Source deve esporre int read() -> byte successivo, oppure -1 a fine dati.
template <typename Source>
class JsonScanner {
 public:
  JsonScanner(Source& src, JsonScanSink& sink) : src_(src), sink_(sink) {}

  bool run() {
    int depth = 0;
    bool inRaw = false;
    int rawDepth = 0;
    char key[kMaxText] = "";

    for (;;) {
      int c = next();
      if (c < 0) break;

      if (c == '{' || c == '[') {
        depth++;
        if (depth >= kMaxDepth) return false;
        if (c == '[' && !inRaw && !strcmp(key, "raw")) {
          inRaw = sink_.onRawBegin(depth);
          rawDepth = depth;
        }
        key[0] = '\0';
      } else if (c == '}' || c == ']') {
        if (inRaw && depth == rawDepth) {
          sink_.onRawEnd();
          inRaw = false;
        }
        if (c == '}') sink_.onObjectEnd(depth);
        if (--depth < 0) return false;
        key[0] = '\0';
      } else if (c == ',' || c == ':') {
        continue;
      } else if (c == '"') {
        char text[kMaxText];
        if (!readString(text, sizeof(text))) return false;
        int after = next();
        if (after == ':') {
          strncpy(key, text, sizeof(key) - 1);
          key[sizeof(key) - 1] = '\0';
        } else {
          unget(after);
          sink_.onField(depth, key, text);
          key[0] = '\0';
        }
      } else {
        char token[kMaxNumber];
        readToken(c, token, sizeof(token));
        if (inRaw) {
          sink_.onRawValue((uint32_t)strtoul(token, nullptr, 10));
        } else {
          sink_.onField(depth, key, token);
          key[0] = '\0';
        }
      }
    }
    return depth == 0;
  }

 private:
  int get() {
    if (pushback_ >= 0) {
      const int c = pushback_;
      pushback_ = -1;
      return c;
    }
    return src_.read();
  }
  void unget(int c) { pushback_ = c; }

  int next() {  // prossimo carattere significativo
    int c;
    do {
      c = get();
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    return c;
  }

  bool readString(char* out, size_t size) {
    size_t n = 0;
    bool escape = false;
    for (;;) {
      const int c = get();
      if (c < 0) return false;
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
        continue;
      } else if (c == '"') {
        break;
      }
      if (n + 1 < size) out[n++] = (char)c;
    }
    out[n] = '\0';
    return true;
  }

  void readToken(int first, char* out, size_t size) {
    size_t n = 0;
    out[n++] = (char)first;
    for (;;) {
      const int c = get();
      if (c < 0) break;
      if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        unget(c);
        break;
      }
      if (n + 1 < size) out[n++] = (char)c;
    }
    out[n] = '\0';
  }

  static const int kMaxDepth = 12;
  static const size_t kMaxText = 64;
  static const size_t kMaxNumber = 24;

  Source& src_;
  JsonScanSink& sink_;
  int pushback_ = -1;
};
