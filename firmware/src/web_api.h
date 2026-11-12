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
void webApiBegin(AsyncWebServer& server);
