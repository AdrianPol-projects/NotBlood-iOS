// In-memory UDP for the web port: see webnet.h

#ifdef __EMSCRIPTEN__

#include "webnet.h"

#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace {

struct Datagram
{
    uint16_t from;
    std::string data;
};

struct VSocket
{
    bool used = false;
    uint16_t port = 0;
    std::deque<Datagram> queue;
    vnet_handler handler = nullptr;
    void *user = nullptr;
};

enum { kMaxSockets = 64, kMaxQueue = 1024 };

VSocket g_sockets[kMaxSockets];
std::map<uint16_t, int> g_ports;  // bound port -> socket
uint16_t g_nextPort = 40000;

VSocket *get(int s)
{
    return s >= 0 && s < kMaxSockets && g_sockets[s].used ? &g_sockets[s] : nullptr;
}

} // namespace

int vnet_socket(void)
{
    for (int i = 0; i < kMaxSockets; i++)
    {
        if (!g_sockets[i].used)
        {
            g_sockets[i] = VSocket();
            g_sockets[i].used = true;
            return i;
        }
    }
    return -1;
}

int vnet_bind(int s, uint16_t port)
{
    VSocket *v = get(s);
    if (!v)
        return -1;
    if (v->port)
        g_ports.erase(v->port);
    if (!port)
    {
        for (int tries = 0; tries < 20000; tries++)
        {
            uint16_t const p = g_nextPort;
            g_nextPort = g_nextPort >= 59999 ? 40000 : g_nextPort + 1;
            if (!g_ports.count(p))
            {
                port = p;
                break;
            }
        }
        if (!port)
            return -1;
    }
    else if (g_ports.count(port))
        return -1;
    v->port = port;
    g_ports[port] = s;
    return 0;
}

uint16_t vnet_port(int s)
{
    VSocket *v = get(s);
    return v ? v->port : 0;
}

int vnet_sendto(int s, uint16_t toport, void const *data, size_t size)
{
    VSocket *v = get(s);
    if (!v)
        return -1;
    if (!v->port && vnet_bind(s, 0) < 0)
        return -1;
    auto it = g_ports.find(toport);
    if (it == g_ports.end())
        return (int)size;  // nobody listens there: lost, like UDP
    VSocket *to = &g_sockets[it->second];
    if (to->handler)
        to->handler(it->second, v->port, data, size, to->user);
    else if (to->queue.size() < kMaxQueue)
        to->queue.push_back(Datagram { v->port, std::string((char const *)data, size) });
    return (int)size;
}

int vnet_recvfrom(int s, uint16_t *pFromPort, void *buffer, size_t size)
{
    VSocket *v = get(s);
    if (!v || v->queue.empty())
        return 0;
    Datagram d = std::move(v->queue.front());
    v->queue.pop_front();
    if (pFromPort)
        *pFromPort = d.from;
    size_t const n = d.data.size() < size ? d.data.size() : size;
    memcpy(buffer, d.data.data(), n);
    return (int)n;
}

int vnet_pending(int s)
{
    VSocket *v = get(s);
    return v ? (int)v->queue.size() : 0;
}

void vnet_close(int s)
{
    VSocket *v = get(s);
    if (!v)
        return;
    if (v->port)
        g_ports.erase(v->port);
    *v = VSocket();
}

void vnet_sethandler(int s, vnet_handler fn, void *user)
{
    if (VSocket *v = get(s))
    {
        v->handler = fn;
        v->user = user;
    }
}

#endif // __EMSCRIPTEN__
