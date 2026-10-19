//-------------------------------------------------------------------------
/*
This file is part of NotBlood.

NotBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
//-------------------------------------------------------------------------

// Online relay connection: WebSocket client (plain ws://), HMAC-SHA256 server key check and
// the UDP <-> WebSocket tunnel for ENet. See netrelay.h. Self-contained on purpose (only
// sockets and the C++ standard library), so it builds the same on every platform.

#ifdef _WIN32
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# include <winsock2.h>
# include <ws2tcpip.h>
typedef SOCKET sock_t;
# define kBadSocket INVALID_SOCKET
# define sockClose closesocket
static int sockError(void) { return WSAGetLastError(); }
static bool sockWouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY; }
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <sys/select.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <fcntl.h>
# include <unistd.h>
# include <errno.h>
typedef int sock_t;
# define kBadSocket (-1)
# define sockClose close
static int sockError(void) { return errno; }
static bool sockWouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS || e == EINTR; }
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "netrelay.h"

// ---- SHA-256 / HMAC ------------------------------------------------------------------------

namespace {

struct Sha256
{
    uint32_t h[8];
    uint64_t len = 0;
    uint8_t buf[64];
    size_t fill = 0;

    Sha256()
    {
        static const uint32_t init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
        memcpy(h, init, sizeof(h));
    }

    static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const uint8_t *p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[i*4] << 24 | (uint32_t)p[i*4+1] << 16 | (uint32_t)p[i*4+2] << 8 | p[i*4+3];
        for (int i = 16; i < 64; i++)
        {
            uint32_t s0 = ror(w[i-15], 7) ^ ror(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = ror(w[i-2], 17) ^ ror(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++)
        {
            uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const void *data, size_t n)
    {
        const uint8_t *p = (const uint8_t *)data;
        len += n;
        while (n--)
        {
            buf[fill++] = *p++;
            if (fill == 64)
            {
                block(buf);
                fill = 0;
            }
        }
    }

    void final(uint8_t out[32])
    {
        uint64_t bits = len * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        pad = 0;
        while (fill != 56)
            update(&pad, 1);
        for (int i = 7; i >= 0; i--)
        {
            uint8_t b = (uint8_t)(bits >> (i * 8));
            update(&b, 1);
        }
        for (int i = 0; i < 8; i++)
        {
            out[i*4] = (uint8_t)(h[i] >> 24);
            out[i*4+1] = (uint8_t)(h[i] >> 16);
            out[i*4+2] = (uint8_t)(h[i] >> 8);
            out[i*4+3] = (uint8_t)h[i];
        }
    }
};

std::string hmacSha256Hex(const std::string &key, const std::string &msg)
{
    uint8_t k[64] = {}, digest[32];
    if (key.size() > 64)
    {
        Sha256 s;
        s.update(key.data(), key.size());
        s.final(k);
    }
    else
        memcpy(k, key.data(), key.size());
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++)
    {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    Sha256 inner;
    inner.update(ipad, 64);
    inner.update(msg.data(), msg.size());
    inner.final(digest);
    Sha256 outer;
    outer.update(opad, 64);
    outer.update(digest, 32);
    outer.final(digest);
    std::string hex;
    for (uint8_t b : digest)
    {
        char t[3];
        snprintf(t, sizeof(t), "%02x", b);
        hex += t;
    }
    return hex;
}

std::string base64(const uint8_t *p, size_t n)
{
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < n; i += 3)
    {
        uint32_t v = p[i] << 16 | (i + 1 < n ? p[i+1] << 8 : 0) | (i + 2 < n ? p[i+2] : 0);
        out += tab[v >> 18 & 63];
        out += tab[v >> 12 & 63];
        out += i + 1 < n ? tab[v >> 6 & 63] : '=';
        out += i + 2 < n ? tab[v & 63] : '=';
    }
    return out;
}

#ifndef __EMSCRIPTEN__

// ---- sockets -------------------------------------------------------------------------------

void setNonBlocking(sock_t s)
{
#ifdef _WIN32
    u_long one = 1;
    ioctlsocket(s, FIONBIO, &one);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

sock_t udpLocalSocket(int *pPort)
{
    sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kBadSocket)
        return s;
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    socklen_t al = sizeof(a);
    if (bind(s, (sockaddr *)&a, sizeof(a)) != 0 || getsockname(s, (sockaddr *)&a, &al) != 0)
    {
        sockClose(s);
        return kBadSocket;
    }
    setNonBlocking(s);
    if (pPort)
        *pPort = ntohs(a.sin_port);
    return s;
}

// ---- relay state ---------------------------------------------------------------------------

struct Relay
{
    std::mutex lock;
    std::thread thread;
    std::atomic<bool> stop { false };
    std::atomic<int> status { kRelayIdle };
    std::atomic<int> clientPort { 0 };
    std::atomic<int> hostGamePort { 23513 };
    std::atomic<unsigned> traffic { 0 };
    std::string error;
    std::deque<std::string> outText;  // main thread -> server
    std::deque<std::string> inText;   // server -> main thread
    std::string server, key, version;

    // quitting during an online game: a still running std::thread would abort the program
    ~Relay()
    {
        if (thread.joinable())
        {
            stop = true;
            thread.join();
        }
    }
} g_relay;

void setError(const std::string &e)
{
    std::lock_guard<std::mutex> l(g_relay.lock);
    g_relay.error = e;
}

struct WsClient
{
    sock_t s = kBadSocket;
    std::string in, out, frag;
    int fragOp = 0;
    std::mt19937 rng { std::random_device{}() };

    ~WsClient() { if (s != kBadSocket) sockClose(s); }

    void frame(int op, const void *data, size_t n)
    {
        std::string f;
        f += (char)(0x80 | op);
        if (n < 126)
            f += (char)(0x80 | n);
        else if (n < 65536)
        {
            f += (char)(0x80 | 126);
            f += (char)(n >> 8);
            f += (char)n;
        }
        else
        {
            f += (char)(0x80 | 127);
            for (int i = 7; i >= 0; i--)
                f += (char)((uint64_t)n >> (i * 8));
        }
        uint32_t m = rng();
        uint8_t mask[4] = { (uint8_t)m, (uint8_t)(m >> 8), (uint8_t)(m >> 16), (uint8_t)(m >> 24) };
        f.append((const char *)mask, 4);
        size_t base = f.size();
        f.append((const char *)data, n);
        for (size_t i = 0; i < n; i++)
            f[base + i] ^= mask[i & 3];
        out += f;
    }

    // false when the connection is gone
    bool flush(void)
    {
        while (!out.empty())
        {
            int n = send(s, out.data(), (int)std::min<size_t>(out.size(), 65536), 0
#ifdef MSG_NOSIGNAL
                | MSG_NOSIGNAL
#endif
            );
            if (n > 0)
                out.erase(0, n);
            else if (n < 0 && sockWouldBlock(sockError()))
                return true;
            else
                return false;
        }
        return true;
    }

    // reads what is available; false when the connection is gone
    bool receive(void)
    {
        char buf[16384];
        for (;;)
        {
            int n = recv(s, buf, sizeof(buf), 0);
            if (n > 0)
                in.append(buf, n);
            else if (n < 0 && sockWouldBlock(sockError()))
                return true;
            else
                return false;
        }
    }

    // next complete message: 1 text, 2 binary, 8 close; 0 when none yet
    int next(std::string &msg)
    {
        for (;;)
        {
            if (in.size() < 2)
                return 0;
            const uint8_t *p = (const uint8_t *)in.data();
            int op = p[0] & 15;
            bool fin = p[0] & 0x80;
            bool masked = p[1] & 0x80;
            uint64_t len = p[1] & 127;
            size_t hdr = 2;
            if (len == 126)
            {
                if (in.size() < 4)
                    return 0;
                len = p[2] << 8 | p[3];
                hdr = 4;
            }
            else if (len == 127)
            {
                if (in.size() < 10)
                    return 0;
                len = 0;
                for (int i = 0; i < 8; i++)
                    len = len << 8 | p[2 + i];
                hdr = 10;
            }
            size_t maskAt = hdr;
            if (masked)
                hdr += 4;
            if (len > (64u << 20))
                return 8;
            if (in.size() < hdr + len)
                return 0;
            std::string payload = in.substr(hdr, (size_t)len);
            if (masked)
                for (size_t i = 0; i < payload.size(); i++)
                    payload[i] ^= in[maskAt + (i & 3)];
            in.erase(0, hdr + (size_t)len);
            switch (op)
            {
            case 9:  // ping
                frame(10, payload.data(), payload.size());
                continue;
            case 10: // pong
                continue;
            case 8:
                return 8;
            case 0:
                frag += payload;
                if (!fin)
                    continue;
                msg.swap(frag);
                frag.clear();
                return fragOp;
            case 1:
            case 2:
                if (!fin)
                {
                    frag = payload;
                    fragOp = op;
                    continue;
                }
                msg.swap(payload);
                return op;
            default:
                continue;
            }
        }
    }
};

typedef std::chrono::steady_clock Clock;

bool waitSocket(sock_t s, bool bWrite, int nMs)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv = { nMs / 1000, (nMs % 1000) * 1000 };
    return select((int)s + 1, bWrite ? NULL : &fds, bWrite ? &fds : NULL, NULL, &tv) > 0;
}

// "host", "host:port", "ws://host[:port][/path]"
void parseServer(const std::string &url, std::string &host, std::string &port, std::string &path)
{
    std::string u = url;
    if (u.compare(0, 5, "ws://") == 0)
        u = u.substr(5);
    else if (u.compare(0, 7, "http://") == 0)
        u = u.substr(7);
    size_t slash = u.find('/');
    path = slash == std::string::npos ? "/ws" : u.substr(slash);
    if (path == "/")
        path = "/ws";
    u = u.substr(0, slash);
    size_t colon = u.rfind(':');
    if (colon != std::string::npos && u.find(':') == colon)
    {
        host = u.substr(0, colon);
        port = u.substr(colon + 1);
    }
    else
    {
        host = u;
        port = "80";
    }
}

bool connectServer(WsClient &ws, const std::string &url)
{
    std::string host, port, path;
    parseServer(url, host, port, path);
    if (host.empty())
    {
        setError("No server address");
        return false;
    }
    addrinfo hints = {}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
    {
        setError("Can't find server " + host);
        return false;
    }
    bool bConnected = false;
    for (addrinfo *a = res; a && !bConnected && !g_relay.stop; a = a->ai_next)
    {
        sock_t s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == kBadSocket)
            continue;
        setNonBlocking(s);
        int r = connect(s, a->ai_addr, (int)a->ai_addrlen);
        if (r != 0 && !sockWouldBlock(sockError()))
        {
            sockClose(s);
            continue;
        }
        auto t0 = Clock::now();
        while (!g_relay.stop && Clock::now() - t0 < std::chrono::seconds(8))
        {
            if (waitSocket(s, true, 100))
            {
                int err = 0;
                socklen_t el = sizeof(err);
                getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &el);
                if (err == 0)
                    bConnected = true;
                break;
            }
        }
        if (bConnected)
            ws.s = s;
        else
            sockClose(s);
    }
    freeaddrinfo(res);
    if (!bConnected)
    {
        setError("Can't connect to " + host);
        return false;
    }
    int one = 1;
    setsockopt(ws.s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(ws.s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof(one));
#endif

    uint8_t nonce[16];
    for (auto &b : nonce)
        b = (uint8_t)ws.rng();
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + (port != "80" ? ":" + port : "") +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " +
        base64(nonce, sizeof(nonce)) + "\r\nUser-Agent: NotBlood\r\n\r\n";
    ws.out = req;
    auto t0 = Clock::now();
    size_t end;
    while ((end = ws.in.find("\r\n\r\n")) == std::string::npos)
    {
        if (g_relay.stop || Clock::now() - t0 > std::chrono::seconds(10))
        {
            setError("No answer from " + host);
            return false;
        }
        if (!ws.flush() || (waitSocket(ws.s, false, 100) && !ws.receive()))
        {
            setError("Connection to " + host + " failed");
            return false;
        }
    }
    std::string head = ws.in.substr(0, end);
    ws.in.erase(0, end + 4);
    if (head.compare(0, 12, "HTTP/1.1 101") != 0)
    {
        std::string line = head.substr(0, head.find("\r\n"));
        if (head.find(" 429") != std::string::npos)
            setError("Server refused: too many connections or wrong keys");
        else
            setError("Not a NotBlood server (" + line + ")");
        return false;
    }
    return true;
}

// waits for one text message during the login
bool readText(WsClient &ws, std::string &msg, int nSeconds)
{
    auto t0 = Clock::now();
    for (;;)
    {
        int op = ws.next(msg);
        if (op == 1)
            return true;
        if (op == 8)
            return false;
        if (op == 2)
            continue;
        if (g_relay.stop || Clock::now() - t0 > std::chrono::seconds(nSeconds))
            return false;
        if (!ws.flush() || (waitSocket(ws.s, false, 100) && !ws.receive()))
            return false;
    }
}

std::string field(const std::string &s, int n)
{
    size_t a = 0;
    while (n-- > 0)
    {
        a = s.find('\t', a);
        if (a == std::string::npos)
            return "";
        a++;
    }
    size_t b = s.find('\t', a);
    return s.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

void relayThread(void)
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    std::string server, key, version;
    {
        std::lock_guard<std::mutex> l(g_relay.lock);
        server = g_relay.server;
        key = g_relay.key;
        version = g_relay.version;
    }

    WsClient ws;
    std::map<int, sock_t> peers;  // host: remote player id -> local socket towards the ENet server
    sock_t client = kBadSocket;    // joining player: socket the local ENet client talks to
    sockaddr_in clientAddr = {};
    bool bClientKnown = false;
    std::string msg;
    auto lastPing = Clock::now();

    auto finish = [&](const char *pzError) {
        if (pzError && !g_relay.stop)
            setError(pzError);
        g_relay.status = kRelayClosed;
    };

    if (!connectServer(ws, server))
    {
        finish(NULL);
        goto done;
    }
    if (!readText(ws, msg, 10) || field(msg, 0) != "challenge")
    {
        finish("The server didn't answer");
        goto done;
    }
    msg = "auth\t" + hmacSha256Hex(key, field(msg, 1)) + "\t1\t" + version;
    ws.frame(1, msg.data(), msg.size());
    if (!readText(ws, msg, 10))
    {
        finish("The server didn't answer");
        goto done;
    }
    if (field(msg, 0) != "ok")
    {
        finish(field(msg, 0) == "error" ? field(msg, 1).c_str() : "The server refused the connection");
        goto done;
    }
    g_relay.status = kRelayOnline;

    while (!g_relay.stop)
    {
        {
            std::lock_guard<std::mutex> l(g_relay.lock);
            while (!g_relay.outText.empty())
            {
                const std::string &t = g_relay.outText.front();
                if (ws.out.size() < (1u << 20) || t.compare(0, 6, "stats\t") != 0)  // drop stats while the line is slow
                    ws.frame(1, t.data(), t.size());
                g_relay.outText.pop_front();
            }
        }
        if (Clock::now() - lastPing > std::chrono::seconds(20))
        {
            ws.frame(9, "", 0);  // keeps proxies from closing an idle connection
            lastPing = Clock::now();
        }
        if (!ws.flush())
        {
            finish("Lost the connection to the server");
            break;
        }

        fd_set rd, wr;
        FD_ZERO(&rd);
        FD_ZERO(&wr);
        sock_t maxfd = ws.s;
        FD_SET(ws.s, &rd);
        if (!ws.out.empty())
            FD_SET(ws.s, &wr);
        for (auto &p : peers)
        {
            FD_SET(p.second, &rd);
            maxfd = std::max(maxfd, p.second);
        }
        if (client != kBadSocket)
        {
            FD_SET(client, &rd);
            maxfd = std::max(maxfd, client);
        }
        timeval tv = { 0, 10000 };
        if (select((int)maxfd + 1, &rd, &wr, NULL, &tv) < 0)
        {
#ifndef _WIN32
            if (errno == EINTR)
                continue;
#endif
            finish("Network error");
            break;
        }

        uint8_t dgram[4096];
        for (auto &p : peers)
        {
            if (!FD_ISSET(p.second, &rd))
                continue;
            int n;
            while ((n = recv(p.second, (char *)dgram + 1, sizeof(dgram) - 1, 0)) > 0)
            {
                dgram[0] = (uint8_t)p.first;
                ws.frame(2, dgram, n + 1);
                g_relay.traffic++;
            }
        }
        if (client != kBadSocket && FD_ISSET(client, &rd))
        {
            int n;
            sockaddr_in from;
            socklen_t fl = sizeof(from);
            while ((n = recvfrom(client, (char *)dgram, sizeof(dgram), 0, (sockaddr *)&from, &fl)) > 0)
            {
                clientAddr = from;
                bClientKnown = true;
                ws.frame(2, dgram, n);
                g_relay.traffic++;
                fl = sizeof(from);
            }
        }

        if (FD_ISSET(ws.s, &rd) && !ws.receive())
        {
            finish("Lost the connection to the server");
            break;
        }
        int op;
        bool bClosed = false;
        while ((op = ws.next(msg)) != 0)
        {
            if (op == 8)
            {
                bClosed = true;
                break;
            }
            if (op == 2)
            {
                g_relay.traffic++;
                if (client != kBadSocket)
                {
                    if (bClientKnown)
                        sendto(client, msg.data(), (int)msg.size(), 0, (sockaddr *)&clientAddr, sizeof(clientAddr));
                }
                else if (msg.size() > 1)
                {
                    auto it = peers.find((uint8_t)msg[0]);
                    if (it != peers.end())
                    {
                        sockaddr_in to = {};
                        to.sin_family = AF_INET;
                        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                        to.sin_port = htons((uint16_t)g_relay.hostGamePort);
                        sendto(it->second, msg.data() + 1, (int)msg.size() - 1, 0, (sockaddr *)&to, sizeof(to));
                    }
                }
                continue;
            }
            std::string cmd = field(msg, 0);
            if (cmd == "peer_join")
            {
                int id = atoi(field(msg, 1).c_str());
                if (!peers.count(id))
                {
                    sock_t s = udpLocalSocket(NULL);
                    if (s != kBadSocket)
                        peers[id] = s;
                }
            }
            else if (cmd == "peer_leave")
            {
                auto it = peers.find(atoi(field(msg, 1).c_str()));
                if (it != peers.end())
                {
                    sockClose(it->second);
                    peers.erase(it);
                }
            }
            else if (cmd == "joined" && client == kBadSocket)
            {
                int port = 0;
                client = udpLocalSocket(&port);
                g_relay.clientPort = port;
            }
            std::lock_guard<std::mutex> l(g_relay.lock);
            if (g_relay.inText.size() < 256)
                g_relay.inText.push_back(msg);
        }
        if (bClosed)
        {
            finish("The server closed the connection");
            break;
        }
    }
    if (g_relay.stop && ws.s != kBadSocket)
    {
        ws.frame(8, "", 0);
        ws.flush();
    }

done:
    for (auto &p : peers)
        sockClose(p.second);
    if (client != kBadSocket)
        sockClose(client);
    g_relay.clientPort = 0;
    if (g_relay.status != kRelayClosed)
        g_relay.status = kRelayClosed;
    if (ws.s != kBadSocket)
    {
        sockClose(ws.s);
        ws.s = kBadSocket;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

} // namespace

void relayConnect(const char *pzServer, const char *pzKey, const char *pzGameVersion)
{
    relayClose();
    {
        std::lock_guard<std::mutex> l(g_relay.lock);
        g_relay.server = pzServer;
        g_relay.key = pzKey;
        g_relay.version = pzGameVersion;
        g_relay.error.clear();
        g_relay.outText.clear();
        g_relay.inText.clear();
    }
    g_relay.stop = false;
    g_relay.clientPort = 0;
    g_relay.traffic = 0;
    g_relay.status = kRelayConnecting;
    g_relay.thread = std::thread(relayThread);
}

void relayClose(void)
{
    if (g_relay.thread.joinable())
    {
        g_relay.stop = true;
        g_relay.thread.join();
    }
    g_relay.stop = false;
    g_relay.status = kRelayIdle;
    g_relay.clientPort = 0;
}

int relayStatus(void)
{
    return g_relay.status;
}

const char *relayError(void)
{
    static std::string copy;
    std::lock_guard<std::mutex> l(g_relay.lock);
    copy = g_relay.error;
    return copy.c_str();
}

void relaySendText(const char *pzText)
{
    std::lock_guard<std::mutex> l(g_relay.lock);
    if (g_relay.outText.size() < 512)
        g_relay.outText.push_back(pzText);
}

bool relayReadText(char *pBuffer, int nSize)
{
    std::lock_guard<std::mutex> l(g_relay.lock);
    if (g_relay.inText.empty() || nSize <= 0)
        return false;
    snprintf(pBuffer, nSize, "%s", g_relay.inText.front().c_str());
    g_relay.inText.pop_front();
    return true;
}

void relaySetHostGamePort(int nPort)
{
    g_relay.hostGamePort = nPort;
}

int relayClientPort(void)
{
    return g_relay.clientPort;
}

unsigned relayTraffic(void)
{
    return g_relay.traffic;
}

// ---- LAN discovery --------------------------------------------------------------------------

namespace {

struct LanState
{
    bool bInit = false;
    sock_t advertise = kBadSocket;
    std::string info;
    sock_t scan = kBadSocket;
    std::vector<LANGAME> found;
} g_lan;

void lanInit(void)
{
#ifdef _WIN32
    if (!g_lan.bInit)
    {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
#endif
    g_lan.bInit = true;
}

// the source address of a socket connected towards somewhere else: the address of the
// network interface the system would use, nothing is sent
bool localAddressTowards(const char *pzTarget, in_addr *pOut)
{
    sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kBadSocket)
        return false;
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = inet_addr(pzTarget);
    bool bOk = false;
    if (connect(s, (sockaddr *)&to, sizeof(to)) == 0)
    {
        sockaddr_in me = {};
        socklen_t ml = sizeof(me);
        bOk = getsockname(s, (sockaddr *)&me, &ml) == 0 && me.sin_addr.s_addr != htonl(INADDR_ANY) &&
            me.sin_addr.s_addr != htonl(INADDR_LOOPBACK);
        if (bOk)
            *pOut = me.sin_addr;
    }
    sockClose(s);
    return bOk;
}

bool localAddress(in_addr *pOut)
{
    lanInit();
    static const char *const targets[] = { "8.8.8.8", "192.168.1.1", "192.168.0.1", "10.0.0.1", "172.16.0.1" };
    for (const char *t : targets)
        if (localAddressTowards(t, pOut))
            return true;
    return false;
}

} // namespace

bool netLocalAddress(char *pzAddress, int nSize)
{
    in_addr a;
    if (!localAddress(&a))
    {
        snprintf(pzAddress, nSize, "unknown");
        return false;
    }
    uint32_t const n = ntohl(a.s_addr);
    snprintf(pzAddress, nSize, "%u.%u.%u.%u", n >> 24, (n >> 16) & 255, (n >> 8) & 255, n & 255);
    return true;
}

void lanAdvertiseStart(void)
{
    lanAdvertiseStop();
    lanInit();
    sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kBadSocket)
        return;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(kLanDiscoveryPort);
    if (bind(s, (sockaddr *)&a, sizeof(a)) != 0)
    {
        sockClose(s);
        return;
    }
    setNonBlocking(s);
    g_lan.advertise = s;
}

void lanAdvertiseUpdate(const char *pzHost, const char *pzVersion, const char *pzMod, int nPort, int nPlayers, int nMaxPlayers)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "NBLD!\t%s\t%s\t%s\t%d\t%d\t%d", pzVersion, pzMod, pzHost, nPort, nPlayers, nMaxPlayers);
    g_lan.info = buf;
}

void lanAdvertisePoll(void)
{
    if (g_lan.advertise == kBadSocket)
        return;
    char buf[64];
    sockaddr_in from;
    socklen_t fl = sizeof(from);
    int n;
    while ((n = recvfrom(g_lan.advertise, buf, sizeof(buf), 0, (sockaddr *)&from, &fl)) > 0)
    {
        if (n >= 5 && !memcmp(buf, "NBLD?", 5) && !g_lan.info.empty())
            sendto(g_lan.advertise, g_lan.info.data(), (int)g_lan.info.size(), 0, (sockaddr *)&from, fl);
        fl = sizeof(from);
    }
}

void lanAdvertiseStop(void)
{
    if (g_lan.advertise != kBadSocket)
        sockClose(g_lan.advertise);
    g_lan.advertise = kBadSocket;
}

void lanScanStart(void)
{
    lanScanStop();
    lanInit();
    g_lan.found.clear();
    sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kBadSocket)
        return;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof(one));
    setNonBlocking(s);
    g_lan.scan = s;

    auto ask = [s](uint32_t nAddress) {
        sockaddr_in to = {};
        to.sin_family = AF_INET;
        to.sin_port = htons(kLanDiscoveryPort);
        to.sin_addr.s_addr = htonl(nAddress);
        sendto(s, "NBLD?", 5, 0, (sockaddr *)&to, sizeof(to));
    };
    ask(INADDR_BROADCAST);
    ask(INADDR_LOOPBACK);  // a host on this machine
    in_addr me;
    if (localAddress(&me))
    {
        uint32_t const nMe = ntohl(me.s_addr), nNet = nMe & 0xffffff00u;
        ask(nNet | 0xff);
        for (uint32_t i = 1; i < 255; i++)
            ask(nNet | i);
    }
}

int lanScanPoll(LANGAME *pGames, int nMax)
{
    if (g_lan.scan != kBadSocket)
    {
        char buf[512];
        sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n;
        while ((n = recvfrom(g_lan.scan, buf, sizeof(buf) - 1, 0, (sockaddr *)&from, &fl)) > 0)
        {
            fl = sizeof(from);
            buf[n] = 0;
            if (n < 5 || memcmp(buf, "NBLD!", 5))
                continue;
            std::string msg(buf);
            LANGAME g = {};
            in_addr src = from.sin_addr;
            if (src.s_addr == htonl(INADDR_LOOPBACK))
                localAddress(&src);  // the same host answers on every address: show the real one
            uint32_t const nSrc = ntohl(src.s_addr);
            snprintf(g.szAddress, sizeof(g.szAddress), "%u.%u.%u.%u", nSrc >> 24, (nSrc >> 16) & 255, (nSrc >> 8) & 255, nSrc & 255);
            snprintf(g.szVersion, sizeof(g.szVersion), "%s", field(msg, 1).c_str());
            snprintf(g.szMod, sizeof(g.szMod), "%s", field(msg, 2).c_str());
            snprintf(g.szHost, sizeof(g.szHost), "%s", field(msg, 3).c_str());
            g.nPort = atoi(field(msg, 4).c_str());
            g.nPlayers = atoi(field(msg, 5).c_str());
            g.nMaxPlayers = atoi(field(msg, 6).c_str());
            bool bKnown = false;
            for (auto const &o : g_lan.found)
                bKnown |= !strcmp(o.szAddress, g.szAddress) && o.nPort == g.nPort;
            if (!bKnown)
                g_lan.found.push_back(g);
        }
    }
    int nCount = std::min((int)g_lan.found.size(), nMax);
    for (int i = 0; i < nCount; i++)
        pGames[i] = g_lan.found[i];
    return nCount;
}

void lanScanStop(void)
{
    if (g_lan.scan != kBadSocket)
        sockClose(g_lan.scan);
    g_lan.scan = kBadSocket;
}

#else // __EMSCRIPTEN__

// ---- web browsers -----------------------------------------------------------------------------
//
// No threads and no sockets in a page: the relay connection is the browser's own WebSocket, its
// events arrive between frames, and the local "UDP" ends of the tunnel are in-memory sockets
// (webnet.h) that ENet uses too. There are no LAN games in a browser.

} // namespace

#include <emscripten.h>
#include <emscripten/websocket.h>
#include "webnet.h"

namespace {

struct WebRelay
{
    EMSCRIPTEN_WEBSOCKET_T ws = 0;
    int status = kRelayIdle;
    bool bOpened = false, bAuthed = false;
    std::string server, key, version, error;
    std::deque<std::string> outText, inText;
    std::map<int, int> peers;  // host: remote player id -> socket towards the ENet server
    int client = -1;           // joining player: the socket the local ENet client talks to
    uint16_t clientPeer = 0;   // ... and the ENet client's port
    int hostGamePort = 23513;
    unsigned traffic = 0;
} g_web;

EM_JS(char *, web_relayPageServer, (), {
    return stringToNewUTF8(Module.notbloodServer || "");
});

EM_JS(int, web_relayPageIsHttps, (), {
    return location.protocol === "https:" ? 1 : 0;
});

std::string webField(const std::string &s, int n)
{
    size_t a = 0;
    while (n-- > 0)
    {
        a = s.find('\t', a);
        if (a == std::string::npos)
            return "";
        a++;
    }
    size_t b = s.find('\t', a);
    return s.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

// "host", "host:port", "ws://...", "http(s)://..." or nothing (the server the page came from)
std::string webServerUrl(const std::string &s)
{
    std::string u = s;
    if (u.empty())
    {
        char *p = web_relayPageServer();
        u = p;
        free(p);
        return u;
    }
    bool const bHttps = web_relayPageIsHttps();
    if (u.compare(0, 7, "http://") == 0)
        u = "ws://" + u.substr(7);
    else if (u.compare(0, 8, "https://") == 0)
        u = "wss://" + u.substr(8);
    else if (u.find("://") == std::string::npos)
        u = (bHttps ? "wss://" : "ws://") + u;
    if (bHttps && u.compare(0, 5, "ws://") == 0)
        u = "wss://" + u.substr(5);  // a secure page may only open secure connections
    size_t const host = u.find("://") + 3;
    if (u.find('/', host) == std::string::npos)
        u += "/ws";
    return u;
}

void webSendText(const std::string &t)
{
    if (g_web.ws)
        emscripten_websocket_send_utf8_text(g_web.ws, t.c_str());
}

void webFinish(const char *pzError)
{
    if (pzError && g_web.error.empty())
        g_web.error = pzError;
    g_web.status = kRelayClosed;
}

// joining player: datagrams from the local ENet client go to the host
void webClientHandler(int s, uint16_t from, void const *data, size_t size, void *)
{
    (void)s;
    g_web.clientPeer = from;
    if (g_web.ws && g_web.status == kRelayOnline)
    {
        emscripten_websocket_send_binary(g_web.ws, (void *)data, (uint32_t)size);
        g_web.traffic++;
    }
}

// host: datagrams from the ENet server to one remote player
void webPeerHandler(int s, uint16_t from, void const *data, size_t size, void *user)
{
    (void)s;
    (void)from;
    if (!g_web.ws || g_web.status != kRelayOnline || size > 4095)
        return;
    uint8_t dgram[4096];
    dgram[0] = (uint8_t)(intptr_t)user;
    memcpy(dgram + 1, data, size);
    emscripten_websocket_send_binary(g_web.ws, dgram, (uint32_t)size + 1);
    g_web.traffic++;
}

void webCloseSockets(void)
{
    for (auto &p : g_web.peers)
        vnet_close(p.second);
    g_web.peers.clear();
    if (g_web.client >= 0)
        vnet_close(g_web.client);
    g_web.client = -1;
    g_web.clientPeer = 0;
}

EM_BOOL webOnOpen(int, const EmscriptenWebSocketOpenEvent *, void *)
{
    g_web.bOpened = true;
    return EM_TRUE;
}

EM_BOOL webOnError(int, const EmscriptenWebSocketErrorEvent *, void *)
{
    webFinish(g_web.bOpened ? "Lost the connection to the server" : "Couldn't connect to the server");
    return EM_TRUE;
}

EM_BOOL webOnClose(int, const EmscriptenWebSocketCloseEvent *e, void *)
{
    if (!g_web.bOpened)
        webFinish("Couldn't connect to the server");
    else if (!g_web.bAuthed)
        webFinish("The server didn't answer");
    else
        webFinish("The server closed the connection");
    (void)e;
    return EM_TRUE;
}

EM_BOOL webOnMessage(int, const EmscriptenWebSocketMessageEvent *e, void *)
{
    if (!e->isText)
    {
        if (g_web.status != kRelayOnline || !e->numBytes)
            return EM_TRUE;
        g_web.traffic++;
        if (g_web.client >= 0)
        {
            if (g_web.clientPeer)
                vnet_sendto(g_web.client, g_web.clientPeer, e->data, e->numBytes);
        }
        else if (e->numBytes > 1)
        {
            auto it = g_web.peers.find(e->data[0]);
            if (it != g_web.peers.end())
                vnet_sendto(it->second, (uint16_t)g_web.hostGamePort, e->data + 1, e->numBytes - 1);
        }
        return EM_TRUE;
    }

    std::string msg((char const *)e->data, e->numBytes && !e->data[e->numBytes - 1] ? e->numBytes - 1 : e->numBytes);
    std::string const cmd = webField(msg, 0);
    if (!g_web.bAuthed)
    {
        if (cmd == "challenge")
            webSendText("auth\t" + hmacSha256Hex(g_web.key, webField(msg, 1)) + "\t1\t" + g_web.version);
        else if (cmd == "ok")
        {
            g_web.bAuthed = true;
            g_web.status = kRelayOnline;
            while (!g_web.outText.empty())
            {
                webSendText(g_web.outText.front());
                g_web.outText.pop_front();
            }
        }
        else
        {
            webFinish(cmd == "error" ? webField(msg, 1).c_str() : "The server refused the connection");
            if (g_web.ws)
                emscripten_websocket_close(g_web.ws, 1000, "");
        }
        return EM_TRUE;
    }

    if (cmd == "peer_join")
    {
        int const id = atoi(webField(msg, 1).c_str());
        if (!g_web.peers.count(id))
        {
            int const s = vnet_socket();
            if (s >= 0 && vnet_bind(s, 0) == 0)
            {
                vnet_sethandler(s, webPeerHandler, (void *)(intptr_t)id);
                g_web.peers[id] = s;
            }
            else if (s >= 0)
                vnet_close(s);
        }
    }
    else if (cmd == "peer_leave")
    {
        auto it = g_web.peers.find(atoi(webField(msg, 1).c_str()));
        if (it != g_web.peers.end())
        {
            vnet_close(it->second);
            g_web.peers.erase(it);
        }
    }
    else if (cmd == "joined" && g_web.client < 0)
    {
        int const s = vnet_socket();
        if (s >= 0 && vnet_bind(s, 0) == 0)
        {
            vnet_sethandler(s, webClientHandler, nullptr);
            g_web.client = s;
        }
        else if (s >= 0)
            vnet_close(s);
    }
    if (g_web.inText.size() < 256)
        g_web.inText.push_back(msg);
    return EM_TRUE;
}

} // namespace

void relayConnect(const char *pzServer, const char *pzKey, const char *pzGameVersion)
{
    relayClose();
    g_web.server = webServerUrl(pzServer);
    g_web.key = pzKey;
    g_web.version = pzGameVersion;
    g_web.error.clear();
    g_web.outText.clear();
    g_web.inText.clear();
    g_web.traffic = 0;
    g_web.bOpened = g_web.bAuthed = false;
    g_web.status = kRelayConnecting;

    if (g_web.server.empty() || !emscripten_websocket_is_supported())
    {
        webFinish("Enter the server address first");
        return;
    }
    EmscriptenWebSocketCreateAttributes attr;
    emscripten_websocket_init_create_attributes(&attr);
    attr.url = g_web.server.c_str();
    attr.protocols = NULL;
    attr.createOnMainThread = EM_TRUE;
    g_web.ws = emscripten_websocket_new(&attr);
    if (g_web.ws <= 0)
    {
        g_web.ws = 0;
        webFinish("Couldn't connect to the server");
        return;
    }
    emscripten_websocket_set_onopen_callback(g_web.ws, NULL, webOnOpen);
    emscripten_websocket_set_onerror_callback(g_web.ws, NULL, webOnError);
    emscripten_websocket_set_onclose_callback(g_web.ws, NULL, webOnClose);
    emscripten_websocket_set_onmessage_callback(g_web.ws, NULL, webOnMessage);
}

void relayClose(void)
{
    if (g_web.ws)
    {
        emscripten_websocket_close(g_web.ws, 1000, "");
        emscripten_websocket_delete(g_web.ws);
        g_web.ws = 0;
    }
    webCloseSockets();
    g_web.status = kRelayIdle;
}

int relayStatus(void)
{
    return g_web.status;
}

const char *relayError(void)
{
    return g_web.error.c_str();
}

void relaySendText(const char *pzText)
{
    if (g_web.status == kRelayOnline)
        webSendText(pzText);
    else if (g_web.outText.size() < 512)
        g_web.outText.push_back(pzText);
}

bool relayReadText(char *pBuffer, int nSize)
{
    if (g_web.inText.empty() || nSize <= 0)
        return false;
    snprintf(pBuffer, nSize, "%s", g_web.inText.front().c_str());
    g_web.inText.pop_front();
    return true;
}

void relaySetHostGamePort(int nPort)
{
    g_web.hostGamePort = nPort;
}

int relayClientPort(void)
{
    return g_web.client >= 0 ? vnet_port(g_web.client) : 0;
}

unsigned relayTraffic(void)
{
    return g_web.traffic;
}

// ---- no LAN in a browser ----------------------------------------------------------------------

bool netLocalAddress(char *pzAddress, int nSize)
{
    (void)pzAddress;
    (void)nSize;
    return false;
}

void lanAdvertiseStart(void) {}
void lanAdvertiseUpdate(const char *, const char *, const char *, int, int, int) {}
void lanAdvertisePoll(void) {}
void lanAdvertiseStop(void) {}
void lanScanStart(void) {}
int lanScanPoll(LANGAME *pGames, int nMax)
{
    (void)pGames;
    (void)nMax;
    return 0;
}
void lanScanStop(void) {}

#endif // __EMSCRIPTEN__
