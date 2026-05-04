// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef AI_CHRIF_HPP
#define AI_CHRIF_HPP

#include <common/cbasetypes.hpp>

extern int32 char_fd;
extern int32 aichrif_state;

void do_init_aichrif(void);
void do_final_aichrif(void);

/// Tries to (re)connect to char-server. Called periodically.
int32 aichrif_check_connect(void);

/// Parser for char-server fd.
int32 aichrif_parse(int32 fd);

/// Sends the AI peer handshake (0x2b30) once the TCP connection is up.
int32 aichrif_send_hello(int32 fd);

#endif /* AI_CHRIF_HPP */
