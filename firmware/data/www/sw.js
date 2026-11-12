// Service Worker: mette in cache l'interfaccia (HTML/CSS/JS/icone) così che
// il telefono la scarichi una sola volta; le chiamate /api/ vanno sempre in rete.
// Nota: i browser attivano i Service Worker solo in contesti sicuri (HTTPS o
// localhost). Su http://<ip> l'app funziona comunque: la cache è garantita
// dagli header HTTP (ETag + 304) inviati dall'ESP32.
const CACHE = 'ir-remote-v2';
const ASSETS = ['/', '/style.css', '/app.js', '/manifest.json', '/icon.svg', '/icon-192.png', '/icon-512.png', '/icon-maskable.png'];

self.addEventListener('install', (event) => {
  event.waitUntil(caches.open(CACHE).then((c) => c.addAll(ASSETS)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (event) => {
  const req = event.request;
  const url = new URL(req.url);
  if (req.method !== 'GET' || url.origin !== location.origin || url.pathname.startsWith('/api/')) return;

  // Stale-while-revalidate: risposta immediata dalla cache, aggiornamento in background.
  event.respondWith((async () => {
    const cache = await caches.open(CACHE);
    const cached = await cache.match(req, { ignoreSearch: true });
    const network = fetch(req).then((res) => {
      if (res.ok) cache.put(req, res.clone());
      return res;
    });
    if (cached) {
      event.waitUntil(network.catch(() => {}));
      return cached;
    }
    return network;
  })());
});
