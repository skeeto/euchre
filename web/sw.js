// Service worker: cache-first, so the game loads offline and installs.
// The build replaces @CACHE_VERSION@ with a hash of the wasm, so each
// release uses a fresh cache and drops the old one.
const CACHE = 'euchre-@CACHE_VERSION@';
const ASSETS = [
  './',
  'index.html',
  'index.js',
  'index.wasm',
  'manifest.webmanifest',
  'icon-192.png',
  'icon-512.png',
  'icon-maskable-512.png',
];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(ASSETS)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k)))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (e) => {
  if (e.request.method !== 'GET') return;
  e.respondWith(
    caches.match(e.request, {ignoreSearch: true}).then((hit) => hit || fetch(e.request))
  );
});
