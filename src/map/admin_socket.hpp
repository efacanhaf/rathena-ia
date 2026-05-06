// DimensionsRO — admin command bridge over a localhost TCP socket.
// Allows out-of-process tools (e.g. Discord bot via SSH tunnel) to invoke a
// whitelisted set of @reload* atcommands without an in-game GM session.

#ifndef ADMIN_SOCKET_HPP
#define ADMIN_SOCKET_HPP

void do_init_admin_socket();
void do_final_admin_socket();

#endif /* ADMIN_SOCKET_HPP */
