#pragma once

class AsyncWebServer;

// Registra gli endpoint REST e i file statici della Web App.
//
//   GET    /api/remotes        struttura completa (con impulsi) — ?lite=1 senza impulsi
//   POST   /api/remotes        crea un telecomando            name, type (tv|ac|custom)
//   POST   /api/learn          avvia l'apprendimento          remote, button, label, [gap ms]
//   GET    /api/learn          stato dell'apprendimento
//   DELETE /api/learn          annulla l'apprendimento
//   POST   /api/send           trasmette un pulsante          remote, button
//   DELETE /api/delete         elimina telecomando/pulsante   remote, [button]
//   POST   /api/import         ripristina un backup JSON (corpo = contenuto di remotes.json)
//   GET    /api/info           stato del dispositivo
//   GET    /api/wifi           stato della rete, reti salvate, ultimo tentativo
//   GET    /api/wifi/scan      reti visibili                  [refresh=1]
//   POST   /api/wifi/connect   collega e, se riesce, salva    ssid, [password]
//   POST   /api/wifi/networks  salva senza collegarsi         ssid, [password]
//   DELETE /api/wifi/networks  dimentica una rete             ssid
//   POST   /api/wifi/ap        password dell'Access Point     password
void webApiBegin(AsyncWebServer& server);
