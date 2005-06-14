/** @file io_socket.c
 *
 * This is actually a layer that adds sockets to whatever underlying io
 * layer you decide to use.
 */

#include "io.h"


struct in_addr;

int io_socket_connect(io_atom *io, struct in_addr addr, int port, io_proc proc, int flags);
int io_socket_accept(io_atom *io, struct in_addr *remoteaddr, int *remoteport);
int io_socket_listen(io_atom *io, io_proc proc, struct in_addr addr, int port);
void io_socket_close(io_atom *io);
int io_socket_read(io_atom *io, char *buf, size_t cnt, size_t *len);
