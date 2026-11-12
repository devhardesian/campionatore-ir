#pragma once

// =============================================================================
//  Campionatore IR — configurazione
// =============================================================================

#define FW_VERSION "1.2.0"

// ---- Rete Wi-Fi --------------------------------------------------------------
// Rete predefinita del firmware (facoltativa). Altre reti si aggiungono dalla
// Web App (⋮ → Wi-Fi) e restano nella NVS: sopravvivono a "uploadfs" e agli
// aggiornamenti del firmware. Una rete salvata dall'app con lo stesso nome ha
// la precedenza su questa (es. dopo un cambio di password del router).
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define WIFI_CONNECT_TIMEOUT_MS 20000  // tempo massimo per collegarsi a una rete
#define WIFI_MAX_NETWORKS 5            // reti salvabili dall'app
#define WIFI_LOST_FALLBACK_MS 120000   // rete persa da 2 min -> Access Point
#define WIFI_AP_RETRY_MS 60000         // in AP: ogni minuto cerca le reti note
#define WIFI_AP_LINGER_MS 120000       // AP ancora acceso dopo una connessione dall'app

// Access Point di ripiego, attivo quando nessuna rete nota è raggiungibile.
// Indirizzo della Web App in modalità AP: http://192.168.4.1
// AP_PASSWORD è la password predefinita: si cambia dalla Web App.
#define AP_SSID "Campionatore-IR"
#define AP_PASSWORD "irremote123"  // 8-63 caratteri

// Tasto BOOT della scheda: tenuto premuto per 5 s ripristina la password
// predefinita dell'Access Point e riavvia (-1 per disattivare).
#define RESET_BUTTON_PIN 0
#define RESET_BUTTON_HOLD_MS 5000

// Nome mDNS: http://campionatore-ir.local
#define HOSTNAME "campionatore-ir"

// ---- Pin hardware --------------------------------------------------------------
#define IR_RX_PIN 18  // Catodo del fotodiodo + pull-up 10 kΩ verso 3.3V
#define IR_TX_PIN 17  // Resistenza -> base del transistor NPN che pilota il LED IR

// ---- Ricezione (campionamento RAW) ----------------------------------------------
// Il fotodiodo "nudo" restituisce il segnale ancora modulato: ogni impulso del
// telecomando arriva come un treno di brevi impulsi LOW alla frequenza della
// portante. Il firmware ricompone l'inviluppo: impulsi LOW distanti meno di
// IR_CARRIER_GAP_US appartengono allo stesso "mark".
#define IR_CARRIER_GAP_US 150      // silenzio massimo tra due cicli di portante
#define IR_END_GAP_US 15000        // silenzio che chiude il pacchetto (15 ms)
#define IR_END_GAP_MAX_US 65000    // limite per il parametro "gap" di /api/learn
#define IR_MIN_MARK_US 50          // mark più corti sono considerati disturbi
#define IR_MIN_ENTRIES 8           // pacchetti più corti sono scartati come rumore
#define IR_MAX_ENTRIES 1024        // durate massime per singolo segnale
#define IR_MAX_CAPTURE_US 3000000  // durata massima di una cattura (3 s)
#define IR_LEARN_TIMEOUT_MS 15000  // attesa massima della pressione del tasto

// ---- Trasmissione ----------------------------------------------------------------
#define IR_DEFAULT_CARRIER_HZ 38000
#define IR_CARRIER_MIN_HZ 30000  // portanti misurate fuori range -> 38 kHz
#define IR_CARRIER_MAX_HZ 60000
#define IR_CARRIER_DUTY 0.33f
#define IR_TX_GAP_MS 40           // pausa minima tra due trasmissioni consecutive
#define IR_SEND_QUEUE_LEN 4

// ---- Storage ----------------------------------------------------------------------
#define DB_PATH "/db.json"                  // metadati (RAM + flash)
#define DB_TMP_PATH "/db.json.tmp"
#define DB_BAD_PATH "/db.bad.json"
#define SIG_DIR "/sig"                      // /sig/<telecomando>/<tasto>.bin
#define SIG_NEW_DIR "/sig.new"              // area di lavoro per l'importazione
#define LEGACY_DB_PATH "/remotes.json"      // database v1 (impulsi inclusi)
#define IMPORT_TMP_PATH "/import.tmp"
#define WEB_ROOT "/www/"

#define MAX_NAME_LEN 32
#define MAX_LABEL_LEN 40
#define MAX_ID_LEN 40
