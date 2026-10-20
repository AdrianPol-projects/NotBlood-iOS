// NotBlood Chosen web app: loads the game, keeps the player's Blood files, settings and saves in the
// browser (IndexedDB, mounted at /notblood), installs as an app that works offline (sw.js).
'use strict';

var GAME_DIR = '/notblood';
var game = null;          // the Emscripten module once loaded
var started = false;
var installPrompt = null;

function $(id) { return document.getElementById(id); }

// ---- offline / install --------------------------------------------------------------------------

var importing = false;

if ('serviceWorker' in navigator && (location.protocol === 'https:' || location.hostname === 'localhost' || location.hostname === '127.0.0.1')) {
    var hadWorker = !!navigator.serviceWorker.controller;
    navigator.serviceWorker.register('sw.js', { updateViaCache: 'none' }).catch(function (e) { console.warn('service worker:', e); });
    // a new version took over (it checks on every start with internet): use it right away, unless
    // the game is running or files are being copied
    navigator.serviceWorker.addEventListener('controllerchange', function () {
        if (hadWorker && !started && !importing) location.reload();
    });
}

function isStandalone() {
    return navigator.standalone === true || window.matchMedia('(display-mode: standalone)').matches ||
        window.matchMedia('(display-mode: fullscreen)').matches;
}

function isAppleTouch() {
    return /iPad|iPhone|iPod/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
}

window.addEventListener('beforeinstallprompt', function (e) {
    e.preventDefault();
    installPrompt = e;
    showInstall();
});

function showInstall() {
    if (isStandalone())
        return;
    var text;
    if (isAppleTouch())
        text = 'Tap the Share button, then <b>Add to Home Screen</b>. NotBlood then gets its own icon, ' +
            'opens full screen and works without internet.';
    else if (installPrompt)
        text = 'Install NotBlood as an app: its own icon and window, and it works without internet.';
    else
        text = 'In your browser\'s menu choose <b>Install</b> or <b>Add to Home screen</b>: NotBlood then ' +
            'gets its own icon and works without internet. It also works offline in this tab once loaded.';
    $('installText').innerHTML = text;
    $('installButton').hidden = !installPrompt;
    $('install').hidden = false;
}

$('installButton').onclick = function () {
    if (!installPrompt)
        return;
    installPrompt.prompt();
    installPrompt.userChoice.finally(function () { installPrompt = null; $('installButton').hidden = true; });
};

// ---- the game files in the browser ----------------------------------------------------------------

var syncing = false, syncAgain = false, syncTimer = 0;

// writes changed files (settings, saves) to IndexedDB, at most one write at a time
function syncFiles(done) {
    if (!game) return;
    if (syncing) { syncAgain = true; return; }
    syncing = true;
    game.FS.syncfs(false, function (err) {
        syncing = false;
        if (err) console.warn('saving files:', err);
        if (syncAgain) { syncAgain = false; syncFiles(done); }
        else if (done) done();
    });
}

function requestSync() {
    clearTimeout(syncTimer);
    syncTimer = setTimeout(syncFiles, 800);
}

setInterval(function () { if (started) syncFiles(); }, 15000);
document.addEventListener('visibilitychange', function () { if (document.hidden) syncFiles(); });
window.addEventListener('pagehide', function () { syncFiles(); });

function listFiles(dir, prefix, out) {
    var FS = game.FS;
    FS.readdir(dir).forEach(function (name) {
        if (name === '.' || name === '..') return;
        var path = dir + '/' + name, rel = prefix ? prefix + '/' + name : name;
        var st = FS.stat(path);
        if (FS.isDir(st.mode)) listFiles(path, rel, out);
        else out.push({ path: rel, size: st.size });
    });
    return out;
}

function haveGameData() {
    return game.FS.readdir(GAME_DIR).some(function (n) { return n.toLowerCase() === 'blood.rff'; });
}

function refresh() {
    $('loading').hidden = true;
    var files = listFiles(GAME_DIR, '', []);
    var total = files.reduce(function (a, f) { return a + f.size; }, 0);
    var ok = haveGameData();
    $('ready').hidden = !ok;
    $('needFiles').hidden = ok;
    $('play').disabled = !ok;
    $('filesInfo').textContent = files.length + ' files, ' + (total / 1048576).toFixed(0) + ' MB stored in this browser.';
    showInstall();
}

function mkdirs(path) {
    var FS = game.FS, parts = path.split('/'), cur = '';
    for (var i = 0; i < parts.length - 1; i++) {
        cur += (cur ? '/' : '') + parts[i];
        try { FS.mkdir(GAME_DIR + '/' + cur); } catch (e) { /* exists */ }
    }
}

// files we never keep: Blood's demo recordings (they crash bot matches, see BOTS.md), programs and
// CD images of the store versions
var SKIP_EXT = /\.(dem|exe|dll|com|bat|gog|ins|iso|cue|bin|msi)$/i;
var MAX_FILE = 150 * 1048576;

// Blood Extra Crispy (a mod in the MODS menu): its art, sounds and Gory Kill animations, in their own
// folder. Its other files (maps, BLOOD.INI, scripts) are for the remaster only, and none of them may
// replace Blood's own.
var BEC_DIR = 'mods/BEC_1.2';
var BEC_NEEDED = /^(extrac\.rff|sounds\.rff|surface\.dat|tiles0\d\d\.art)$/i;

async function importExtraCrispy(entries, becRoot) {
    var mine = entries.filter(function (e) {
        var rel = e.path.slice(becRoot.length);
        return e.path.indexOf(becRoot) === 0 && rel.indexOf('/') < 0 && BEC_NEEDED.test(rel);
    });
    for (var i = 0; i < mine.length; i++) {
        var name = mine[i].path.slice(becRoot.length);
        $('importText').textContent = 'Copying Extra Crispy\'s ' + name + ' (' + (i + 1) + '/' + mine.length + ')';
        var data = await mine[i].read();
        mkdirs(BEC_DIR + '/' + name);
        game.FS.writeFile(GAME_DIR + '/' + BEC_DIR + '/' + name, data);
        await new Promise(function (r) { setTimeout(r, 0); });
    }
    await new Promise(function (r) { syncFiles(r); });
    $('importText').textContent = 'Blood Extra Crispy: ' + mine.length + ' files copied. ' +
        'Choose it in the game: MODS > BLOOD EXTRA CRISPY.';
    if (navigator.storage && navigator.storage.persist) navigator.storage.persist();
    refresh();
}

// entries: [{ path: 'BLOOD/BLOOD.RFF', size, read: () => Promise<Uint8Array> }]
async function importEntries(entries) {
    entries.forEach(function (e) { e.path = e.path.replace(/\\/g, '/'); });
    var bec = entries.find(function (e) { return /(^|\/)extrac\.rff$/i.test(e.path); });
    if (bec)
        return importExtraCrispy(entries, bec.path.slice(0, bec.path.length - 10));

    // the folder that holds BLOOD.RFF is the game folder; a backup zip goes in as it is
    var root = null;
    entries.forEach(function (e) {
        var p = e.path;
        if (/(^|\/)blood\.rff$/i.test(p)) {
            var d = p.slice(0, p.length - 9);
            if (root === null || d.length < root.length) root = d;
        }
    });
    if (root === null) {
        // a backup, or extra files (music, movies, mods): strip the picked folder's own name
        var first = entries.length && entries[0].path.indexOf('/') > 0 ? entries[0].path.split('/')[0] + '/' : '';
        var all = first && entries.every(function (e) { return e.path.indexOf(first) === 0; });
        var backup = entries.some(function (e) { return /(^|\/)notblood\.cfg$/i.test(e.path); });
        root = all && !backup ? first : '';
    }

    var done = 0, skipped = [], written = 0;
    for (var i = 0; i < entries.length; i++) {
        var e = entries[i];
        if (e.path.indexOf(root) !== 0 || e.path.endsWith('/')) continue;
        var rel = e.path.slice(root.length);
        if (!rel) continue;
        if (SKIP_EXT.test(rel) || e.size > MAX_FILE) { skipped.push(rel); continue; }
        $('importText').textContent = 'Copying ' + rel + ' (' + (i + 1) + '/' + entries.length + ')';
        var data = await e.read();
        mkdirs(rel);
        game.FS.writeFile(GAME_DIR + '/' + rel, data);
        written++;
        if (++done % 8 === 0) await new Promise(function (r) { setTimeout(r, 0); });
    }
    await new Promise(function (r) { syncFiles(r); });
    var msg = written + ' files copied.';
    if (skipped.length)
        msg += ' Skipped ' + skipped.length + ' (demo recordings, programs, CD images): ' + skipped.slice(0, 4).join(', ') + (skipped.length > 4 ? ', ...' : '');
    $('importText').textContent = msg;
    if (navigator.storage && navigator.storage.persist) navigator.storage.persist();
    refresh();
}

function fileEntries(fileList) {
    return Array.prototype.map.call(fileList, function (f) {
        var rel = f.webkitRelativePath || f.name;
        return { path: rel, size: f.size, file: f, read: function () { return f.arrayBuffer().then(function (b) { return new Uint8Array(b); }); } };
    });
}

async function importPicked(fileList) {
    if (!game || !fileList.length) return;
    importing = true;
    try {
        var entries = [];
        var list = fileEntries(fileList);
        for (var i = 0; i < list.length; i++) {
            if (/\.zip$/i.test(list[i].path)) {
                $('importText').textContent = 'Opening ' + list[i].path + '...';
                entries = entries.concat(await zipEntries(list[i].file));
            } else entries.push(list[i]);
        }
        await importEntries(entries);
    } catch (err) {
        console.error(err);
        $('importText').textContent = 'Copying failed: ' + err.message;
    }
    importing = false;
}

$('pickFiles').onchange = function () { importPicked(this.files); this.value = ''; };
$('pickFolder').onchange = function () { importPicked(this.files); this.value = ''; };
if (isAppleTouch()) $('pickFolderLabel').hidden = true;  // Safari on iPad can't pick folders

$('removeAll').onclick = function () {
    if (!game || !confirm('Remove all Blood files, settings and saves from this browser?')) return;
    var FS = game.FS;
    (function rm(dir) {
        FS.readdir(dir).forEach(function (n) {
            if (n === '.' || n === '..') return;
            var p = dir + '/' + n;
            if (FS.isDir(FS.stat(p).mode)) { rm(p); FS.rmdir(p); } else FS.unlink(p);
        });
    })(GAME_DIR);
    syncFiles(refresh);
};

// the backup: everything that isn't game data (settings, saves, co-op saves, touch layout, log)
var DATA_EXT = /\.(rff|art|dat|ini|smk|wav|ogg|flac|mp3|map|dem|kvx|zip|grp|pk3|ar_|def|voc|mid|seq|qav|sfx|raw)$/i;
$('backup').onclick = function () {
    if (!game) return;
    syncFiles();
    var files = listFiles(GAME_DIR, '', []).filter(function (f) {
        return !DATA_EXT.test(f.path) && !/^(movie|addons|mods|autoload)\//i.test(f.path);
    });
    var zip = makeZip(files.map(function (f) { return { path: f.path, data: game.FS.readFile(GAME_DIR + '/' + f.path) }; }));
    var a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([zip], { type: 'application/zip' }));
    a.download = 'notblood-saves-' + new Date().toISOString().slice(0, 10) + '.zip';
    document.body.appendChild(a);
    a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
};

// ---- zip files (reading stored/deflated entries, writing stored ones) ------------------------------

async function zipEntries(file) {
    var buf = new Uint8Array(await file.arrayBuffer());
    var dv = new DataView(buf.buffer);
    var eocd = -1;
    for (var i = buf.length - 22; i >= Math.max(0, buf.length - 65557); i--)
        if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
    if (eocd < 0) throw new Error(file.name + ' is not a zip file');
    var count = dv.getUint16(eocd + 10, true), pos = dv.getUint32(eocd + 16, true), out = [];
    var dec = new TextDecoder();
    for (var n = 0; n < count; n++) {
        if (dv.getUint32(pos, true) !== 0x02014b50) break;
        var method = dv.getUint16(pos + 10, true), csize = dv.getUint32(pos + 20, true), usize = dv.getUint32(pos + 24, true);
        var nlen = dv.getUint16(pos + 28, true), xlen = dv.getUint16(pos + 30, true), clen = dv.getUint16(pos + 32, true);
        var local = dv.getUint32(pos + 42, true);
        var name = dec.decode(buf.subarray(pos + 46, pos + 46 + nlen));
        pos += 46 + nlen + xlen + clen;
        (function (name, method, csize, usize, local) {
            out.push({
                path: name, size: usize,
                read: async function () {
                    var start = local + 30 + dv.getUint16(local + 26, true) + dv.getUint16(local + 28, true);
                    var raw = buf.subarray(start, start + csize);
                    if (method === 0) return raw.slice();
                    if (method !== 8) throw new Error(name + ': unsupported zip compression');
                    var stream = new Blob([raw]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
                    return new Uint8Array(await new Response(stream).arrayBuffer());
                }
            });
        })(name, method, csize, usize, local);
    }
    return out;
}

var CRC_TABLE = (function () {
    var t = new Uint32Array(256);
    for (var n = 0; n < 256; n++) {
        var c = n;
        for (var k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        t[n] = c >>> 0;
    }
    return t;
})();

function crc32(d) {
    var c = 0xffffffff;
    for (var i = 0; i < d.length; i++) c = CRC_TABLE[(c ^ d[i]) & 255] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

function makeZip(files) {
    var enc = new TextEncoder(), parts = [], central = [], offset = 0;
    files.forEach(function (f) {
        var name = enc.encode(f.path), crc = crc32(f.data);
        var h = new DataView(new ArrayBuffer(30));
        h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint32(14, crc, true);
        h.setUint32(18, f.data.length, true); h.setUint32(22, f.data.length, true); h.setUint16(26, name.length, true);
        parts.push(new Uint8Array(h.buffer), name, f.data);
        var c = new DataView(new ArrayBuffer(46));
        c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint32(16, crc, true);
        c.setUint32(20, f.data.length, true); c.setUint32(24, f.data.length, true); c.setUint16(28, name.length, true);
        c.setUint32(42, offset, true);
        central.push(new Uint8Array(c.buffer), name);
        offset += 30 + name.length + f.data.length;
    });
    var size = central.reduce(function (a, p) { return a + p.length; }, 0);
    var e = new DataView(new ArrayBuffer(22));
    e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true);
    e.setUint32(12, size, true); e.setUint32(16, offset, true);
    return new Blob(parts.concat(central, [new Uint8Array(e.buffer)]));
}

// ---- starting the game ----------------------------------------------------------------------------

function setLoad(text, pct) {
    $('loadText').textContent = text;
    if (pct !== undefined) $('loadBar').value = pct;
}

function notbloodLoaded() {
    NotBlood({
        canvas: $('canvas'),
        // the same build's files, never ones a browser or Cloudflare kept from an older one
        locateFile: function (path, dir) { return dir + path + '?v=__BUILD__'; },
        print: function (s) { console.log(s); },
        printErr: function (s) { console.warn(s); },
        setStatus: function (s) {
            var m = /\((\d+(?:\.\d+)?)\/(\d+)\)/.exec(s || '');
            if (m) setLoad('Loading the game... ' + Math.round(m[1] / 1048576) + ' / ' + Math.round(m[2] / 1048576) + ' MB', 100 * m[1] / m[2]);
        },
        notbloodSync: requestSync,
        onExit: function () { syncFiles(function () { location.reload(); }); },
        onAbort: function (what) {
            syncFiles();
            alert('NotBlood stopped because of an error:\n' + what + '\n\nThe page will reload.');
            location.reload();
        }
    }).then(function (mod) {
        game = mod;
        var FS = game.FS;
        try { FS.mkdir(GAME_DIR); } catch (e) { /* exists */ }
        FS.mount(game.IDBFS, {}, GAME_DIR);
        setLoad('Reading your files...', 100);
        FS.syncfs(true, function (err) {
            if (err) console.warn('reading files:', err);
            refresh();
        });
    }).catch(function (err) {
        setLoad('The game could not start in this browser: ' + err);
    });
}

// Safari only lets a page play sound that started during a tap: the game's audio (SDL) would start
// later and stay silent, so the tap on Play makes it and SDL uses it. Sound that stopped (another
// app, a call, the iPad locked) comes back with the next tap or key.
function unlockAudio() {
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC || (game.SDL2 && game.SDL2.audioContext)) return;
    var ctx;
    try { ctx = new AC({ latencyHint: 'playback' }); } catch (e) { try { ctx = new AC(); } catch (e2) { return; } }
    game.SDL2 = game.SDL2 || {};
    game.SDL2.audioContext = ctx;
    function wake() {
        if (ctx.state !== 'running') ctx.resume().catch(function () {});
    }
    // a moment of silence played inside the tap is what unlocks it on older iPads
    try {
        var src = ctx.createBufferSource();
        src.buffer = ctx.createBuffer(1, 1, ctx.sampleRate);
        src.connect(ctx.destination);
        src.start(0);
    } catch (e) { /* not needed elsewhere */ }
    wake();
    ['touchend', 'mousedown', 'keydown'].forEach(function (t) { document.addEventListener(t, wake, true); });
    document.addEventListener('visibilitychange', function () { if (!document.hidden) wake(); });
}

$('play').onclick = function () {
    if (!game || started || !haveGameData()) return;
    started = true;
    unlockAudio();
    document.body.classList.add('playing');
    var canvas = $('canvas');
    canvas.focus();
    if (!isStandalone() && !isAppleTouch() && document.documentElement.requestFullscreen)
        document.documentElement.requestFullscreen().then(function () {
            // Esc is Blood's menu key: in full screen it shouldn't leave full screen (Chrome, Edge)
            if (navigator.keyboard && navigator.keyboard.lock) navigator.keyboard.lock(['Escape']).catch(function () {});
        }).catch(function () {});
    // no page scrolling or zooming while playing (touch controls)
    document.addEventListener('touchmove', function (e) { e.preventDefault(); }, { passive: false });
    // the online menu's empty SERVER means this page's own server
    game.notbloodServer = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws';
    // window.notbloodArgs: command line options, for testing
    setTimeout(function () { game.callMain(window.notbloodArgs || []); }, 50);
};
