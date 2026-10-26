// Offline cache of the NotBlood web app. A new build has a new cache name, so the next visit with
// internet gets it and later visits work offline. The player's files are in IndexedDB, not here.
'use strict';

var CACHE = 'notblood-__VERSION__-__BUILD__';
var FILES = [
    './', 'index.html', 'app.js', 'style.css', 'manifest.webmanifest',
    'notblood.js', 'notblood.wasm', 'notblood.data',
    'icon-180.png', 'icon-192.png', 'icon-512.png', 'fonts/pirataone.woff2'
];

self.addEventListener('install', function (e) {
    // past the browser's own cache: Cloudflare lets browsers keep .js and .css for hours, and a new
    // notblood.wasm with an old notblood.js doesn't run
    var fresh = FILES.map(function (f) { return new Request(f, { cache: 'reload' }); });
    e.waitUntil(caches.open(CACHE).then(function (c) { return c.addAll(fresh); }).then(function () { return self.skipWaiting(); }));
});

self.addEventListener('activate', function (e) {
    e.waitUntil(caches.keys().then(function (keys) {
        return Promise.all(keys.filter(function (k) { return k.indexOf('notblood-') === 0 && k !== CACHE; })
            .map(function (k) { return caches.delete(k); }));
    }).then(function () { return self.clients.claim(); }));
});

self.addEventListener('fetch', function (e) {
    var req = e.request;
    if (req.method !== 'GET' || new URL(req.url).origin !== location.origin)
        return;
    e.respondWith(caches.open(CACHE).then(function (c) {
        return c.match(req, { ignoreSearch: true }).then(function (hit) {
            return hit || fetch(req);
        });
    }));
});
