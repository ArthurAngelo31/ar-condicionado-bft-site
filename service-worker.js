const CACHE = 'bft-v13';
const ASSETS = [
  './', './index.html', './manifest.json',
  './css/style.css',
  './js/config.js', './js/theme.js', './js/api.js', './js/icons.js',
  './js/modal.js', './js/home.js', './js/sala.js', './js/admin.js',
  './js/scheduler.js', './js/app.js',
  './img/logo.png',
  './icons/favicon.png', './icons/icon-192.png', './icons/icon-512.png'
];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => Promise.allSettled(ASSETS.map(u => c.add(u)))));
  self.skipWaiting();
});

self.addEventListener('activate', e => {
  e.waitUntil(caches.keys().then(ks => Promise.all(ks.filter(k => k !== CACHE).map(k => caches.delete(k)))));
  self.clients.claim();
});

self.addEventListener('fetch', e => {
  const r = e.request;
  if (r.method !== 'GET') return;
  const u = new URL(r.url);
  if (u.origin !== location.origin) return;

  if (r.mode === 'navigate' || r.headers.get('accept')?.includes('text/html')) {
    e.respondWith(
      fetch(r).then(resp => {
        const clone = resp.clone();
        caches.open(CACHE).then(c => c.put(r, clone));
        return resp;
      }).catch(() => caches.match(r).then(c => c || caches.match('./index.html')))
    );
    return;
  }

  e.respondWith(
    caches.match(r).then(cached => cached || fetch(r).then(resp => {
      const clone = resp.clone();
      caches.open(CACHE).then(c => c.put(r, clone));
      return resp;
    }))
  );
});
