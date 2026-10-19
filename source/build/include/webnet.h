// In-memory UDP for the web port (see webnet.cpp)
//
// Browsers can't send UDP, so in the web build ENet's sockets and the relay tunnel's local sockets
// are these: datagrams between ports of this one page, delivered in memory. Every address is
// "this machine"; only the port counts. The relay connection (netrelay.cpp, WebSocket) is what
// actually leaves the page.

#ifndef webnet_h_
#define webnet_h_

#ifdef __EMSCRIPTEN__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int vnet_socket(void);                     // -1 when there are too many
int vnet_bind(int s, uint16_t port);       // 0: any free port; -1 when the port is taken
uint16_t vnet_port(int s);                 // after vnet_bind (a send binds to a free port first)
// queues a datagram for the socket bound to toport (dropped when there is none)
int vnet_sendto(int s, uint16_t toport, void const *data, size_t size);
// next queued datagram: its size, 0 when there is none
int vnet_recvfrom(int s, uint16_t *pFromPort, void *buffer, size_t size);
int vnet_pending(int s);
void vnet_close(int s);

// datagrams for this socket go to the handler instead of the queue (the relay tunnel)
typedef void (*vnet_handler)(int s, uint16_t fromport, void const *data, size_t size, void *user);
void vnet_sethandler(int s, vnet_handler fn, void *user);

#ifdef __cplusplus
}
#endif

#endif // __EMSCRIPTEN__

#endif // webnet_h_
